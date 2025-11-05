#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <sstream>
#include <iomanip>

inline std::string json_escape(std::string_view s) {
    std::string out; out.reserve(s.size()+8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

inline void json_pair_array(std::ostringstream& os,
                            const std::vector<std::pair<double,double>>& rows) {
    os << "[";
    bool first=true;
    for (const auto& [px,sz] : rows) {
        if (!first) os << ",";
        first=false;
        os << "[" << std::setprecision(15) << px << "," << std::setprecision(15) << sz << "]";
    }
    os << "]";
}