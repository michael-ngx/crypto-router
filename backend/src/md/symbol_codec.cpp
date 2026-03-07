#include "symbol_codec.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

std::string to_upper_ascii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string normalize_asset_alias(std::string token)
{
    token = to_upper_ascii(std::move(token));

    // Kraken alias normalization into canonical asset symbols.
    if (token == "XBT") return "BTC";

    return token;
}

std::string canonize(std::string s)
{
    s = to_upper_ascii(std::move(s));
    const std::size_t sep = s.find('-');
    if (sep == std::string::npos) return s;
    if (sep == 0 || sep + 1 >= s.size()) return s;

    std::string base = normalize_asset_alias(s.substr(0, sep));
    std::string quote = normalize_asset_alias(s.substr(sep + 1));
    return base + "-" + quote;
}

} // namespace

std::string SymbolCodec::to_venue(const std::string &venue, const std::string &c)
{
    std::string venue_lc = venue;
    for (auto &ch : venue_lc) ch = tolower(ch);

    if (venue_lc == "kraken")
    {
        std::string v = c;
        for (auto &ch : v)
            if (ch == '-')
                ch = '/';
        return v;
    }
    else if (venue_lc == "coinbase")
    {
        return c;
    }
    return c;
}

std::string SymbolCodec::to_canonical(const std::string &venue, const std::string &v)
{
    std::string venue_lc = venue;
    for (auto &ch : venue_lc) ch = tolower(ch);

    if (venue_lc == "kraken")
    {
        std::string c = v;
        for (auto &ch : c)
            if (ch == '/')
                ch = '-';
        return canonize(c);
    }
    else if (venue_lc == "coinbase")
    {
        return canonize(v);
    }
    return canonize(v);
}

bool SymbolCodec::is_canonical_pair(const std::string& pair)
{
    const std::size_t sep = pair.find('-');
    return sep != std::string::npos && sep > 0 && sep + 1 < pair.size();
}
