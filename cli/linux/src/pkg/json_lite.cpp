// cli/linux/src/pkg/json_lite.cpp
#include "json_lite.h"
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdio>

namespace rinpm::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    JsonValue parseDocument() {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        if (pos_ != s_.size()) throw std::runtime_error("trailing content after JSON value");
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    void skipWs() { while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
    char peek() { if (pos_ >= s_.size()) throw std::runtime_error("unexpected end of JSON"); return s_[pos_]; }
    char next() { char c = peek(); ++pos_; return c; }
    void expect(char c) { if (next() != c) throw std::runtime_error(std::string("expected '") + c + "'"); }

    JsonValue parseValue() {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') { JsonValue v; v.type = JsonValue::Type::String; v.str = parseString(); return v; }
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') { expectLiteral("null"); return JsonValue{}; }
        return parseNumber();
    }

    void expectLiteral(const char* lit) {
        for (const char* p = lit; *p; ++p) if (next() != *p) throw std::runtime_error(std::string("invalid literal, expected ") + lit);
    }

    JsonValue parseBool() {
        JsonValue v; v.type = JsonValue::Type::Bool;
        if (peek() == 't') { expectLiteral("true"); v.boolean = true; }
        else { expectLiteral("false"); v.boolean = false; }
        return v;
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < s_.size() && (std::isdigit((unsigned char)s_[pos_]) || s_[pos_] == '.' || s_[pos_] == 'e' ||
                                     s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
        if (pos_ == start) throw std::runtime_error("invalid number");
        JsonValue v; v.type = JsonValue::Type::Number;
        v.number = std::stod(s_.substr(start, pos_ - start));
        return v;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                char e = next();
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'u': {
                        std::string hex = s_.substr(pos_, 4); pos_ += 4;
                        int code = std::stoi(hex, nullptr, 16);
                        if (code < 0x80) out.push_back(static_cast<char>(code));
                        else out.push_back('?'); // تبسيط مقبول: لا نحتاج نطاقات يونيكود كاملة لبروتوكول الميتاداتا
                        break;
                    }
                    default: out.push_back(e);
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue v; v.type = JsonValue::Type::Array;
        skipWs();
        if (peek() == ']') { ++pos_; return v; }
        while (true) {
            v.array.push_back(parseValue());
            skipWs();
            char c = next();
            if (c == ']') break;
            if (c != ',') throw std::runtime_error("expected ',' or ']' in array");
            skipWs();
        }
        return v;
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue v; v.type = JsonValue::Type::Object;
        skipWs();
        if (peek() == '}') { ++pos_; return v; }
        while (true) {
            skipWs();
            std::string key = parseString();
            skipWs();
            expect(':');
            v.object[key] = parseValue();
            skipWs();
            char c = next();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("expected ',' or '}' in object");
        }
        return v;
    }
};

void encodeEscaped(const std::string& s, std::ostringstream& os) {
    os << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); os << buf; }
                else os << static_cast<char>(c);
        }
    }
    os << '"';
}

void encodeValue(const JsonValue& v, std::ostringstream& os) {
    switch (v.type) {
        case JsonValue::Type::Null: os << "null"; break;
        case JsonValue::Type::Bool: os << (v.boolean ? "true" : "false"); break;
        case JsonValue::Type::Number:
            if (v.number == static_cast<long long>(v.number)) os << static_cast<long long>(v.number);
            else os << v.number;
            break;
        case JsonValue::Type::String: encodeEscaped(v.str, os); break;
        case JsonValue::Type::Array: {
            os << "[";
            for (size_t i = 0; i < v.array.size(); ++i) { if (i) os << ","; encodeValue(v.array[i], os); }
            os << "]";
            break;
        }
        case JsonValue::Type::Object: {
            os << "{";
            bool first = true;
            for (auto& [k, val] : v.object) {
                if (!first) os << ","; first = false;
                encodeEscaped(k, os);
                os << ":";
                encodeValue(val, os);
            }
            os << "}";
            break;
        }
    }
}

} // namespace

JsonValue parse(const std::string& text) {
    Parser p(text);
    return p.parseDocument();
}

std::string encode(const JsonValue& v) {
    std::ostringstream os;
    encodeValue(v, os);
    return os.str();
}

} // namespace rinpm::json
