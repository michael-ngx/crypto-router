#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <sstream>
#include <iomanip>

// Basic JSON string escaper
inline std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// Encode a vector<pair<price,size>> as an array of objects:
// [ { "price": ..., "size": ... }, ... ]
inline void json_pair_array(std::ostringstream& os,
                            const std::vector<std::pair<double,double>>& rows) {
    os << "[";
    bool first = true;
    for (const auto& [px, sz] : rows) {
        if (!first) os << ",";
        first = false;
        os << "{"
           << "\"price\":" << std::setprecision(15) << px << ","
           << "\"size\":"  << std::setprecision(15) << sz
           << "}";
    }
    os << "]";
}

// Encode a ladder with venue information.
// Level must have fields: .price (double), .size (double), .venue (std::string)
template <typename Level>
inline void json_ladder_array(std::ostringstream& os,
                              const std::vector<Level>& rows) {
    os << "[";
    bool first = true;
    for (const auto& lvl : rows) {
        if (!first) os << ",";
        first = false;
        os << "{"
           << "\"price\":"  << std::setprecision(15) << lvl.price << ","
           << "\"size\":"   << std::setprecision(15) << lvl.size << ","
           << "\"venue\":\"" << json_escape(lvl.venue) << "\""
           << "}";
    }
    os << "]";
}