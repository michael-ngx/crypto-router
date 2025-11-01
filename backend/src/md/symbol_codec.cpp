#include "symbol_codec.hpp"

static std::string canonize(std::string s) { return s; } // TODO: extend later

std::string SymbolCodec::to_venue(const std::string &venue, const std::string &c)
{
    if (venue == "kraken")
    {
        std::string v = c;
        for (auto &ch : v)
            if (ch == '-')
                ch = '/';
        return v;
    }
    else if (venue == "coinbase")
    {
        return c;
    }
    return c;
}

std::string SymbolCodec::to_canonical(const std::string &venue, const std::string &v)
{
    if (venue == "kraken")
    {
        std::string c = v;
        for (auto &ch : c)
            if (ch == '/')
                ch = '-';
        return canonize(c);
    }
    else if (venue == "coinbase")
    {
        return canonize(v);
    }
    return canonize(v);
}