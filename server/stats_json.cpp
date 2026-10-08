#include "stats_json.h"

#include <cstdio>

namespace {

void appendString(std::string& out, const std::string& text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                    out += escaped;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void appendField(std::string& out, const char* name, const std::string& value) {
    out += '"';
    out += name;
    out += "\":";
    appendString(out, value);
}

void appendField(std::string& out, const char* name, std::int64_t value) {
    out += '"';
    out += name;
    out += "\":";
    out += std::to_string(value);
}

}  // namespace

std::string toJson(int online, const std::vector<StatEvent>& events) {
    std::string out = "{\"online\":" + std::to_string(online) + ",\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        const StatEvent& e = events[i];
        if (i > 0) out += ',';
        out += '{';
        appendField(out, "id", e.id);
        out += ',';
        appendField(out, "type", e.type);
        out += ',';
        appendField(out, "at", e.at);
        out += ',';
        appendField(out, "player", e.player);
        out += ',';
        appendField(out, "subject", e.subject);
        out += ',';
        appendField(out, "killerKind", e.killerKind);
        out += ',';
        appendField(out, "count", e.count);
        out += ',';
        appendField(out, "icon", e.icon);
        out += '}';
    }
    out += "]}";
    return out;
}
