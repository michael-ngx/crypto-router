#include "kraken_rest.hpp"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <algorithm>

// ── CURL write callback ────────────────────────────────────────
static size_t write_callback(void* contents, size_t size, size_t nmemb,
                             std::string* s) {
    s->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

// ── Constructor ────────────────────────────────────────────────
KrakenRest::KrakenRest(std::string api_key, std::string api_secret,
                       bool sandbox)
    : api_key_(std::move(api_key))
    , api_secret_(std::move(api_secret))
{
    base_url_ = sandbox
        ? "https://demo-futures.kraken.com"
        : "https://futures.kraken.com";
}

// ── Symbol conversion ──────────────────────────────────────────
// "BTC-USD" → "PI_XBTUSD"
// "ETH-USD" → "PI_ETHUSD"
// "PI_XBTUSD" passes through unchanged
std::string KrakenRest::to_kraken_symbol(const std::string& pair) const {
    // Already in Kraken Futures format
    if (pair.find('_') != std::string::npos)
        return pair;

    auto dash = pair.find('-');
    std::string base  = (dash != std::string::npos) ? pair.substr(0, dash) : pair;
    std::string quote = (dash != std::string::npos) ? pair.substr(dash + 1) : "USD";

    // Kraken uses XBT, not BTC
    if (base == "BTC") base = "XBT";

    // Uppercase both parts
    for (char& c : base)  c = static_cast<char>(toupper(c));
    for (char& c : quote) c = static_cast<char>(toupper(c));

    return "PI_" + base + quote;
}

// ── Base64 encode ──────────────────────────────────────────────
std::string KrakenRest::base64_encode_str(const unsigned char* data,
                                          size_t len) const {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    BIO_set_close(b64, BIO_NOCLOSE);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    BUF_MEM_free(bptr);
    return result;
}

// ── Base64 decode ──────────────────────────────────────────────
std::vector<unsigned char> KrakenRest::base64_decode_bytes(
        const std::string& str) const {
    std::vector<unsigned char> buf(str.size());
    BIO* mem = BIO_new_mem_buf(str.data(), static_cast<int>(str.size()));
    BIO* b64 = BIO_new(BIO_f_base64());
    mem = BIO_push(b64, mem);
    BIO_set_flags(mem, BIO_FLAGS_BASE64_NO_NL);
    int len = BIO_read(mem, buf.data(), static_cast<int>(str.size()));
    BIO_free_all(mem);
    if (len < 0) len = 0;
    buf.resize(static_cast<size_t>(len));
    return buf;
}

// ── compute_authent ────────────────────────────────────────────
// Authent = base64( HMAC-SHA512( base64_decode(secret),
//                                SHA256(postData + nonce + endpointPath) ) )
std::string KrakenRest::compute_authent(const std::string& post_data,
                                        const std::string& nonce,
                                        const std::string& endpoint_path) const {
    // Step 1: SHA-256( postData + nonce + endpointPath )
    std::string message = post_data + nonce + endpoint_path;

    std::cerr << "[DEBUG] authent input:\n"
        << "  post_data     : " << post_data << "\n"
        << "  nonce         : " << nonce << "\n"
        << "  endpoint_path : " << endpoint_path << "\n"
        << "  full message  : " << message << "\n";

    unsigned char sha256_out[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(message.data()),
           message.size(), sha256_out);

    // Step 2: base64-decode the api secret
    std::vector<unsigned char> secret_bytes = base64_decode_bytes(api_secret_);

    // Step 3: HMAC-SHA512( decoded_secret, sha256_out )
    unsigned char hmac_out[EVP_MAX_MD_SIZE];
    unsigned int  hmac_len = 0;
    HMAC(EVP_sha512(),
         secret_bytes.data(), static_cast<int>(secret_bytes.size()),
         sha256_out, SHA256_DIGEST_LENGTH,
         hmac_out, &hmac_len);

    // Step 4: base64-encode the HMAC result
    return base64_encode_str(hmac_out, hmac_len);
}

// ── public_get ─────────────────────────────────────────────────
std::string KrakenRest::public_get(const std::string& path) const {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        std::string url = base_url_ + path;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: crypto-router/1.0");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "[kraken-rest] CURL error: " << curl_easy_strerror(res) << "\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// ── authenticated_get ──────────────────────────────────────────
std::string KrakenRest::authenticated_get(const std::string& path) const {
    std::string endpoint_path = path;
    auto qmark = path.find('?');
    if (qmark != std::string::npos)
        endpoint_path = path.substr(0, qmark);

    const std::string prefix = "/derivatives";
    std::string auth_path = endpoint_path;
    if (auth_path.rfind(prefix, 0) == 0)
        auth_path = auth_path.substr(prefix.size());

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string nonce = std::to_string(ms);

    std::string authent = compute_authent("", nonce, auth_path);

    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        std::string url = base_url_ + path;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: crypto-router/1.0");
        headers = curl_slist_append(headers, ("APIKey: "  + api_key_).c_str());
        headers = curl_slist_append(headers, ("Nonce: "   + nonce).c_str());
        headers = curl_slist_append(headers, ("Authent: " + authent).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "[kraken-rest] CURL error: " << curl_easy_strerror(res) << "\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// ── authenticated_post ─────────────────────────────────────────
std::string KrakenRest::authenticated_post(const std::string& path,
                                            const std::string& body) const {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string nonce = std::to_string(ms);

    std::string auth_path = path;
    const std::string prefix = "/derivatives";
    if (auth_path.rfind(prefix, 0) == 0)
        auth_path = auth_path.substr(prefix.size());

    std::string authent = compute_authent(body, nonce, auth_path);

    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        std::string url = base_url_ + path;
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
        headers = curl_slist_append(headers, "User-Agent: crypto-router/1.0");
        headers = curl_slist_append(headers, ("APIKey: "  + api_key_).c_str());
        headers = curl_slist_append(headers, ("Nonce: "   + nonce).c_str());
        headers = curl_slist_append(headers, ("Authent: " + authent).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK)
            std::cout << "[kraken-rest] Response: " << response << "\n";
        else
            std::cerr << "[kraken-rest] CURL error: " << curl_easy_strerror(res) << "\n";

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// ── JSON helpers ───────────────────────────────────────────────
// Extract the first string value for a key: "key":"value"
std::string KrakenRest::parse_str(const std::string& json,
                                  const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// Extract the first numeric value for a key: "key":1234.5  OR  "key":"1234.5"
double KrakenRest::parse_dbl(const std::string& json, const std::string& key) {
    // Try quoted form first: "key":"1234.5"
    {
        std::string needle = "\"" + key + "\":\"";
        size_t pos = json.find(needle);
        if (pos != std::string::npos) {
            pos += needle.size();
            size_t end = json.find('"', pos);
            try { return std::stod(json.substr(pos, end - pos)); }
            catch (...) {}
        }
    }
    // Unquoted form: "key":1234.5
    {
        std::string needle = "\"" + key + "\":";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return 0.0;
        pos += needle.size();
        // skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        size_t end = json.find_first_of(",}]", pos);
        try { return std::stod(json.substr(pos, end - pos)); }
        catch (...) { return 0.0; }
    }
}

// ── buy_market ─────────────────────────────────────────────────
// PI_* inverse perpetuals: 1 contract = $1 face value
// → size (contracts) = round(quote_amount)
std::string KrakenRest::buy_market(double quote_amount,
                                   const std::string& symbol) {
    std::string ks   = to_kraken_symbol(symbol);
    long long   size = static_cast<long long>(std::round(quote_amount));
    if (size < 1) {
        std::cerr << "[kraken-rest] buy_market: quote_amount too small (min 1 contract = $1)\n";
        return "";
    }

    // postData must be URL-encoded form string
    std::ostringstream body;
    body << "orderType=mkt"
         << "&symbol=" << ks
         << "&side=buy"
         << "&size="   << size;

    std::string resp = authenticated_post("/derivatives/api/v3/sendorder", body.str());

    // Extract order_id from sendStatus
    // {"result":"success","sendStatus":{"order_id":"...","status":"placed",...}}
    size_t pos = resp.find("\"order_id\":\"");
    if (pos == std::string::npos) {
        std::cerr << "[kraken-rest] buy_market: no order_id in response\n";
        return "";
    }
    pos += 12;
    size_t end = resp.find('"', pos);
    return resp.substr(pos, end - pos);
}

// ── sell_market ────────────────────────────────────────────────
std::string KrakenRest::sell_market(double quote_amount,
                                    const std::string& symbol) {
    std::string ks   = to_kraken_symbol(symbol);
    long long   size = static_cast<long long>(std::round(quote_amount));
    if (size < 1) {
        std::cerr << "[kraken-rest] sell_market: quote_amount too small (min 1 contract = $1)\n";
        return "";
    }

    std::ostringstream body;
    body << "orderType=mkt"
         << "&symbol=" << ks
         << "&side=sell"
         << "&size="   << size;

    std::string resp = authenticated_post("/derivatives/api/v3/sendorder", body.str());

    size_t pos = resp.find("\"order_id\":\"");
    if (pos == std::string::npos) {
        std::cerr << "[kraken-rest] sell_market: no order_id in response\n";
        return "";
    }
    pos += 12;
    size_t end = resp.find('"', pos);
    return resp.substr(pos, end - pos);
}

// ── get_best_bid_ask ───────────────────────────────────────────
// Public endpoint: GET /derivatives/api/v3/orderbook?symbol=PI_XBTUSD
// Response:
// {"result":"success","orderBook":{"bids":[[price,qty],...],
//                                  "asks":[[price,qty],...]},...}
BidAsk KrakenRest::get_best_bid_ask(const std::string& symbol) {
    std::string ks = to_kraken_symbol(symbol);
    std::string resp = public_get("/derivatives/api/v3/orderbook?symbol=" + ks);

    BidAsk ba;

    // Asks: sorted ascending → first entry = best ask
    auto extract_first = [&](const std::string& side_key) -> double {
        std::string needle = "\"" + side_key + "\":[[";
        size_t pos = resp.find(needle);
        if (pos == std::string::npos) return 0.0;
        pos += needle.size();
        size_t end = resp.find_first_of(",]", pos);
        if (end == std::string::npos) return 0.0;
        try { return std::stod(resp.substr(pos, end - pos)); }
        catch (...) { return 0.0; }
        };

    // Bids: sorted ascending → LAST entry = best bid
    auto extract_last = [&](const std::string& side_key) -> double {
        std::string needle = "\"" + side_key + "\":[[";
        size_t start = resp.find(needle);
        if (start == std::string::npos) return 0.0;

        // Find the closing ]] of the bids array
        size_t arr_end = resp.find("]]", start);
        if (arr_end == std::string::npos) return 0.0;

        // Walk backwards from ]] to find the last [price, entry
        size_t bracket = resp.rfind('[', arr_end - 1);
        if (bracket == std::string::npos) return 0.0;
        bracket += 1; // skip the '['
        size_t end = resp.find_first_of(",]", bracket);
        if (end == std::string::npos) return 0.0;
        try { return std::stod(resp.substr(bracket, end - bracket)); }
        catch (...) { return 0.0; }
        };

    ba.bid = extract_last("bids");   // best bid = last entry
    ba.ask = extract_first("asks");  // best ask = first entry

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::time_t t = ms / 1000;
    std::tm* tm_info = std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    ba.timestamp = std::string(buf) + "." + std::to_string(ms % 1000) + " UTC";
    return ba;
}

// ── get_order_details ──────────────────────────────────────────
OrderDetails KrakenRest::get_order_details(const std::string& order_id) {
    OrderDetails d;

    // ── 1. Check fills endpoint ───────────────────────────────
    std::string fills_resp = authenticated_get("/derivatives/api/v3/fills");
    std::cout << "[DEBUG] fills raw: " << fills_resp << "\n";

    // Search for this order_id in the fills array.
    // Fill objects look like:
    // {"fill_id":"...","order_id":"...","price":85000.0,"size":10,
    //  "side":"buy","symbol":"pi_xbtusd","fillTime":"...","fee":-0.0000058,...}
    size_t search_pos = 0;
    double total_contracts = 0.0;
    double total_cost_usd  = 0.0;   // sum(contracts * price) for avg price calc
    double total_fee_usd   = 0.0;
    bool   found_fill      = false;

    while (true) {
        // Find next occurrence of this order_id in fills response
        std::string oid_needle = "\"order_id\":\"" + order_id + "\"";
        size_t match = fills_resp.find(oid_needle, search_pos);
        if (match == std::string::npos) break;

        found_fill = true;

        // Back up to find the start of this fill object '{'
        size_t obj_start = fills_resp.rfind('{', match);
        if (obj_start == std::string::npos) { search_pos = match + 1; continue; }

        // Find end of this fill object '}'
        size_t obj_end = fills_resp.find('}', match);
        if (obj_end == std::string::npos) { search_pos = match + 1; continue; }

        std::string fill_obj = fills_resp.substr(obj_start, obj_end - obj_start + 1);

        double price     = parse_dbl(fill_obj, "price");
        double size_ctrs = parse_dbl(fill_obj, "size");  // contracts ($1 each)
        double fee       = parse_dbl(fill_obj, "fee");   // in BTC (negative = paid)

        if (price > 0.0 && size_ctrs > 0.0) {
            total_contracts += size_ctrs;
            total_cost_usd  += size_ctrs;          // $1/contract face value
            total_fee_usd   += std::abs(fee) * price; // BTC fee → USD
        }

        search_pos = obj_end + 1;
    }

    if (found_fill) {
        d.status         = "filled";
        d.executed_value = total_contracts;  // USD face (1 contract = $1)
        d.fill_fees      = total_fee_usd;
        if (total_contracts > 0.0) {
            double weighted_price_sum = 0.0;
            size_t p2 = 0;
            while (true) {
                std::string oid_needle = "\"order_id\":\"" + order_id + "\"";
                size_t m2 = fills_resp.find(oid_needle, p2);
                if (m2 == std::string::npos) break;
                size_t os = fills_resp.rfind('{', m2);
                size_t oe = fills_resp.find('}', m2);
                if (os == std::string::npos || oe == std::string::npos) { p2 = m2 + 1; continue; }
                std::string fo = fills_resp.substr(os, oe - os + 1);
                double px = parse_dbl(fo, "price");
                double sz = parse_dbl(fo, "size");
                if (px > 0.0 && sz > 0.0) weighted_price_sum += px * sz;
                p2 = oe + 1;
            }
            d.fill_price   = weighted_price_sum / total_contracts;
            d.filled_size  = total_contracts / d.fill_price;  // BTC equivalent
        }
        return d;
    }

    // ── 2. Check openorders ───────────────────────────────────
    std::string open_resp = authenticated_get("/derivatives/api/v3/openorders");
    std::cout << "[DEBUG] openorders raw: " << open_resp << "\n";

    if (open_resp.find(order_id) != std::string::npos) {
        // Find the object containing this order_id
        std::string oid_needle = "\"order_id\":\"" + order_id + "\"";
        size_t m = open_resp.find(oid_needle);
        if (m != std::string::npos) {
            size_t os = open_resp.rfind('{', m);
            size_t oe = open_resp.find('}', m);
            if (os != std::string::npos && oe != std::string::npos) {
                std::string order_obj = open_resp.substr(os, oe - os + 1);
                std::string st = parse_str(order_obj, "status");
                d.status      = st.empty() ? "open" : st;
                d.filled_size = parse_dbl(order_obj, "filledSize");
                // No fill price available while open
            }
        } else {
            d.status = "open";
        }
        return d;
    }

    // ── 3. Not found anywhere ─────────────────────────────────
    d.status = "notFound";
    return d;
}

// ── is_order_filled ────────────────────────────────────────────
bool KrakenRest::is_order_filled(const std::string& order_id) {
    std::string resp = authenticated_get("/derivatives/api/v3/fills");
    std::string needle = "\"order_id\":\"" + order_id + "\"";
    return resp.find(needle) != std::string::npos;
}