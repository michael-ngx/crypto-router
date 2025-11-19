#include "symbol_codec.hpp"

static std::string canonize(std::string s) { return s; } // TODO: extend later

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