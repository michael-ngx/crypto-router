#include "coinbase_rest.hpp"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>

// Helper for CURL write callback
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t new_length = size * nmemb;
    s->append((char*)contents, new_length);
    return new_length;
}

CoinbaseRest::CoinbaseRest(std::string api_key, std::string api_secret,
    std::string passphrase, bool sandbox)
    : api_key_(std::move(api_key))
    , api_secret_(std::move(api_secret))
    , passphrase_(std::move(passphrase))
{
    base_url_ = sandbox ?
        "https://api-public.sandbox.exchange.coinbase.com" :
        "https://api.exchange.coinbase.com";
}

std::string CoinbaseRest::base64_encode(const unsigned char* data, size_t len) {
    BIO* bio, * b64;
    BUF_MEM* bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(bio, data, len);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    BIO_set_close(bio, BIO_NOCLOSE);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    BUF_MEM_free(bufferPtr);

    return result;
}

std::string CoinbaseRest::base64_decode(const std::string& str) {
    BIO* bio, * b64;
    int decode_len = str.length();
    std::vector<unsigned char> buffer(decode_len);

    bio = BIO_new_mem_buf(str.data(), -1);
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    int length = BIO_read(bio, buffer.data(), decode_len);
    BIO_free_all(bio);

    return std::string(reinterpret_cast<char*>(buffer.data()), length);
}

std::string CoinbaseRest::sign_request(const std::string& timestamp,
    const std::string& method,
    const std::string& path,
    const std::string& body) {
    // Prehash string: timestamp + method + path + body
    std::string prehash = timestamp + method + path + body;

    // Decode base64 secret
    std::string decoded_secret = base64_decode(api_secret_);

    // HMAC-SHA256
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;

    HMAC(EVP_sha256(),
        decoded_secret.data(),
        decoded_secret.length(),
        reinterpret_cast<const unsigned char*>(prehash.data()),
        prehash.length(),
        digest,
        &digest_len);

    // Base64 encode result
    return base64_encode(digest, digest_len);
}

std::string CoinbaseRest::buy_btc_usd(double usd_amount) {
    // Get current timestamp (seconds since epoch)
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    std::string timestamp = std::to_string(seconds);

    // Build request body
    std::ostringstream body_stream;
    body_stream << std::fixed << std::setprecision(2);
    body_stream << "{\"type\":\"market\",\"side\":\"buy\","
        << "\"product_id\":\"BTC-USD\",\"funds\":\""
        << usd_amount << "\"}";
    std::string body = body_stream.str();

    // Sign request
    std::string method = "POST";
    std::string path = "/orders";
    std::string signature = sign_request(timestamp, method, path, body);

    // Make HTTP request
    CURL* curl = curl_easy_init();
    std::string response_str;
    std::string order_id;

    if (curl) {
        std::string url = base_url_ + path;

        // Setup headers
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "User-Agent: crypto-router/1.0");
        headers = curl_slist_append(headers,
            ("CB-ACCESS-KEY: " + api_key_).c_str());
        headers = curl_slist_append(headers,
            ("CB-ACCESS-SIGN: " + signature).c_str());
        headers = curl_slist_append(headers,
            ("CB-ACCESS-TIMESTAMP: " + timestamp).c_str());
        headers = curl_slist_append(headers,
            ("CB-ACCESS-PASSPHRASE: " + passphrase_).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            std::cout << "[coinbase-rest] Response: " << response_str << "\n";

            // Simple parse for order ID (look for "id":"...")
            size_t id_pos = response_str.find("\"id\":\"");
            if (id_pos != std::string::npos) {
                id_pos += 6; // skip past "id":"
                size_t end_pos = response_str.find("\"", id_pos);
                if (end_pos != std::string::npos) {
                    order_id = response_str.substr(id_pos, end_pos - id_pos);
                }
            }
        }
        else {
            std::cerr << "[coinbase-rest] CURL error: "
                << curl_easy_strerror(res) << "\n";
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return order_id;
}

bool CoinbaseRest::is_order_filled(const std::string& order_id) {
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    std::string timestamp = std::to_string(seconds);

    std::string method = "GET";
    std::string path = "/orders/" + order_id;
    std::string signature = sign_request(timestamp, method, path, "");

    CURL* curl = curl_easy_init();
    std::string response_str;
    bool is_filled = false;

    if (curl) {
        std::string url = base_url_ + path;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: crypto-router/1.0");
        headers = curl_slist_append(headers,
            ("CB-ACCESS-KEY: " + api_key_).c_str());
        headers = curl_slist_append(headers,
            ("CB-ACCESS-SIGN: " + signature).c_str());
        headers = curl_slist_append(headers,
            ("CB-ACCESS-TIMESTAMP: " + timestamp).c_str());
        headers = curl_slist_append(headers,
            ("CB-ACCESS-PASSPHRASE: " + passphrase_).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            // Check if status is "done" or "settled"
            is_filled = (response_str.find("\"status\":\"done\"") != std::string::npos) ||
                (response_str.find("\"status\":\"settled\"") != std::string::npos);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return is_filled;
}