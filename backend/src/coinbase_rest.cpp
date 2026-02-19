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

// ©¤©¤ authenticated_get ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
std::string CoinbaseRest::authenticated_get(const std::string& path) {
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    std::string timestamp = std::to_string(seconds);
    std::string signature = sign_request(timestamp, "GET", path, "");

    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        std::string url = base_url_ + path;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: crypto-router/1.0");
        headers = curl_slist_append(headers, ("CB-ACCESS-KEY: " + api_key_).c_str());
        headers = curl_slist_append(headers, ("CB-ACCESS-SIGN: " + signature).c_str());
        headers = curl_slist_append(headers, ("CB-ACCESS-TIMESTAMP: " + timestamp).c_str());
        headers = curl_slist_append(headers, ("CB-ACCESS-PASSPHRASE: " + passphrase_).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "[coinbase-rest] CURL error: " << curl_easy_strerror(res) << "\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// ©¤©¤ Internal POST helper ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
static std::string post_order(const std::string& base_url,
    const std::string& api_key,
    const std::string& signature,
    const std::string& timestamp,
    const std::string& passphrase,
    const std::string& body) {
    CURL* curl = curl_easy_init();
    std::string response_str;
    if (curl) {
        std::string url = base_url + "/orders";
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "User-Agent: crypto-router/1.0");
        headers = curl_slist_append(headers, ("CB-ACCESS-KEY: " + api_key).c_str());
        headers = curl_slist_append(headers, ("CB-ACCESS-SIGN: " + signature).c_str());
        headers = curl_slist_append(headers, ("CB-ACCESS-TIMESTAMP: " + timestamp).c_str());
        headers = curl_slist_append(headers, ("CB-ACCESS-PASSPHRASE: " + passphrase).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK)
            std::cout << "[coinbase-rest] Response: " << response_str << "\n";
        else
            std::cerr << "[coinbase-rest] CURL error: " << curl_easy_strerror(res) << "\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response_str;
}

static std::string extract_order_id(const std::string& response) {
    size_t id_pos = response.find("\"id\":\"");
    if (id_pos == std::string::npos) return "";
    id_pos += 6;
    size_t end_pos = response.find("\"", id_pos);
    if (end_pos == std::string::npos) return "";
    return response.substr(id_pos, end_pos - id_pos);
}

// ©¤©¤ buy_market ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
std::string CoinbaseRest::buy_market(double quote_amount,
    const std::string& product_id) {
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    std::string timestamp = std::to_string(seconds);

    std::ostringstream body_stream;
    body_stream << std::fixed << std::setprecision(2);
    body_stream << "{\"type\":\"market\",\"side\":\"buy\","
        << "\"product_id\":\"" << product_id << "\","
        << "\"funds\":\"" << quote_amount << "\"}";
    std::string body = body_stream.str();

    std::string signature = sign_request(timestamp, "POST", "/orders", body);
    std::string response = post_order(base_url_, api_key_, signature,
        timestamp, passphrase_, body);
    return extract_order_id(response);
}

// ©¤©¤ sell_market ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
std::string CoinbaseRest::sell_market(double quote_amount,
    const std::string& product_id) {
    auto now = std::chrono::system_clock::now();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    std::string timestamp = std::to_string(seconds);

    std::ostringstream body_stream;
    body_stream << std::fixed << std::setprecision(2);
    body_stream << "{\"type\":\"market\",\"side\":\"sell\","
        << "\"product_id\":\"" << product_id << "\","
        << "\"funds\":\"" << quote_amount << "\"}";
    std::string body = body_stream.str();

    std::string signature = sign_request(timestamp, "POST", "/orders", body);
    std::string response = post_order(base_url_, api_key_, signature,
        timestamp, passphrase_, body);
    return extract_order_id(response);
}

// ©¤©¤ get_best_bid_ask ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
static double parse_first_price(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return 0.0;
    pos = json.find("[[", pos);
    if (pos == std::string::npos) return 0.0;
    pos += 2;
    size_t q1 = json.find("\"", pos);
    if (q1 == std::string::npos) return 0.0;
    size_t q2 = json.find("\"", q1 + 1);
    if (q2 == std::string::npos) return 0.0;
    return std::stod(json.substr(q1 + 1, q2 - q1 - 1));
}

BidAsk CoinbaseRest::get_best_bid_ask(const std::string& product_id) {
    std::string resp = authenticated_get("/products/" + product_id + "/book?level=1");

    BidAsk ba;
    ba.bid = parse_first_price(resp, "bids");
    ba.ask = parse_first_price(resp, "asks");

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::time_t t = ms / 1000;
    std::tm* tm_info = std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    ba.timestamp = std::string(buf) + "." + std::to_string(ms % 1000) + " UTC";
    return ba;
}

// ©¤©¤ get_order_details ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
static double parse_double_field(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\":\"");
    if (pos != std::string::npos) {
        pos += key.size() + 4;
        size_t end = json.find("\"", pos);
        try { return std::stod(json.substr(pos, end - pos)); }
        catch (...) { return 0.0; }
    }
    pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return 0.0;
    pos += key.size() + 3;
    size_t end = json.find_first_of(",}", pos);
    try { return std::stod(json.substr(pos, end - pos)); }
    catch (...) { return 0.0; }
}

static std::string parse_string_field(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\":\"");
    if (pos == std::string::npos) return "";
    pos += key.size() + 4;
    size_t end = json.find("\"", pos);
    return json.substr(pos, end - pos);
}

OrderDetails CoinbaseRest::get_order_details(const std::string& order_id) {
    std::string resp = authenticated_get("/orders/" + order_id);
    std::cout << "[DEBUG] Order details raw: " << resp << "\n";

    OrderDetails d;
    d.status = parse_string_field(resp, "status");
    d.executed_value = parse_double_field(resp, "executed_value");
    d.filled_size = parse_double_field(resp, "filled_size");
    d.fill_fees = parse_double_field(resp, "fill_fees");
    if (d.filled_size > 0.0)
        d.fill_price = d.executed_value / d.filled_size;
    return d;
}