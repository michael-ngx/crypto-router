#include "md_normalizer.hpp"
#include "symbol_codec.hpp"
#include <cstdlib>
#include <string>
#include <chrono>

// Search the raw JSON string for a key and return its numeric value
static double get_num(const std::string &s, const char *key)
{
    std::string k = std::string("\"") + key + "\":";
    auto i = s.find(k);
    if (i == std::string::npos)
        return 0.0;
    i += k.size();
    if (i < s.size() && s[i] == '"')
    {
        ++i;
    }
    auto j = s.find_first_of(",}\"", i);
    return std::atof(s.substr(i, j - i).c_str());
}

struct KrakenNormalizer : IMarketNormalizer
{
    bool parse_ticker(const std::string &raw, NormalizedTick &out) override
    {
        if (raw.find("\"channel\":\"ticker\"") == std::string::npos)
            return false;

        // v2 has "symbol":"BTC/USD"
        auto p = raw.find("\"symbol\":\"");
        if (p == std::string::npos)
            return false;
        p += 10;
        auto e = raw.find('"', p);
        if (e == std::string::npos)
            return false;
        std::string sym = raw.substr(p, e - p);

        out.venue = "kraken";
        out.symbol = SymbolCodec::to_canonical(out.venue, sym);

        out.bid = get_num(raw, "bid");
        out.ask = get_num(raw, "ask");
        out.last = get_num(raw, "last");
        if (out.bid == 0 && out.ask == 0 && out.last == 0)
            return false;

        if (out.ts_ns == 0)
        {
            using namespace std::chrono;
            out.ts_ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        }
        return true;
    }
};

// factory
IMarketNormalizer *make_kraken_normalizer() { return new KrakenNormalizer(); }