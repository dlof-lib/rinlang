// rin_json.h — تحويل حقيقي (وليس تمثيل عرض فقط) بين Value الخاصة بلغة Rin ونص JSON فعلي.
// لماذا هذا ملف مستقل عن toDisplayString() الموجودة أصلاً في rin_interpreter.cpp؟
// toDisplayString تنتج تمثيلاً "يشبه" JSON للطباعة فقط (مفاتيح بلا علامتي تنصيص، undefined لأنواع
// كالدوال...)، وليس JSON صالحاً يمكن إرساله فعلياً لخادوم حقيقي أو تحليله من رد خادوم حقيقي. أي
// اتصال API حقيقي (rin_http.h) يحتاج تحويلاً صحيحاً 100% في الاتجاهين: encode لبناء جسم الطلب،
// decode لتفكيك جسم الرد إلى Value (map/array/رقم/نص/bool/nil) يمكن للبرنامج التعامل معه مباشرة.
#pragma once
#include "rin_interpreter.h"
#include <sstream>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace rin {
namespace json {

inline void encodeEscaped(const std::string& s, std::ostringstream& os) {
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
                else os << (char)c;
        }
    }
    os << '"';
}

// Value -> JSON (يدعم كل أنواع Value؛ الدوال/nil غير القابل للتمثيل تصبح null).
inline void encode(const Value& v, std::ostringstream& os) {
    switch (v.type) {
        case Value::Type::NIL: os << "null"; break;
        case Value::Type::BOOL: os << (v.boolean ? "true" : "false"); break;
        case Value::Type::NUMBER: {
            if (v.number == static_cast<long long>(v.number)) os << static_cast<long long>(v.number);
            else os << v.number;
            break;
        }
        case Value::Type::STRING: encodeEscaped(v.str, os); break;
        case Value::Type::FUNCTION: os << "null"; break;
        case Value::Type::ARRAY: {
            os << "[";
            for (size_t i = 0; i < v.array->size(); i++) { if (i) os << ","; encode((*v.array)[i], os); }
            os << "]";
            break;
        }
        case Value::Type::MAP: {
            os << "{";
            for (size_t i = 0; i < v.map->size(); i++) {
                if (i) os << ",";
                encodeEscaped((*v.map)[i].first.toDisplayString(), os);
                os << ":";
                encode((*v.map)[i].second, os);
            }
            os << "}";
            break;
        }
    }
}

inline std::string encode(const Value& v) {
    std::ostringstream os;
    encode(v, os);
    return os.str();
}

// ---- محلِّل JSON صغير ومكتفٍ ذاتياً (recursive-descent) لتحويل نص JSON حقيقي (رد خادوم فعلي)
// إلى Value قابلة للاستخدام مباشرة داخل برنامج Rin (map/array/رقم/نص/bool/nil). لا يعتمد على أي
// مكتبة خارجية عمداً — نفس فلسفة بقية المفسّر (بلا اعتماديات خارج STL).
class Decoder {
public:
    explicit Decoder(const std::string& s) : src(s), pos(0), len(s.size()) {}

    // يُرجع true عند نجاح التحليل الكامل (بعد تجاهل أي فراغ زائد في النهاية)، false عند أي خلل
    // تركيبي — outErr يحمل رسالة مختصرة، outValue تبقى Value::nil() في هذه الحالة.
    bool parse(Value& outValue, std::string& outErr) {
        try {
            skipWs();
            outValue = parseValue();
            skipWs();
            if (pos != len) throw std::runtime_error("محتوى زائد بعد نهاية JSON صالح");
            return true;
        } catch (std::exception& e) {
            outErr = e.what();
            return false;
        }
    }

private:
    const std::string& src;
    size_t pos, len;

    char peek() { if (pos >= len) throw std::runtime_error("نهاية JSON غير متوقعة"); return src[pos]; }
    char advance() { return src[pos++]; }
    void skipWs() { while (pos < len && (unsigned char)src[pos] <= ' ') pos++; }
    void expect(char c) { if (pos >= len || src[pos] != c) throw std::runtime_error(std::string("متوقَّع '") + c + "'"); pos++; }

    Value parseValue() {
        skipWs();
        if (pos >= len) throw std::runtime_error("قيمة JSON مفقودة");
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Value::string(parseString());
        if (c == 't') { expectLiteral("true"); return Value::boolean_(true); }
        if (c == 'f') { expectLiteral("false"); return Value::boolean_(false); }
        if (c == 'n') { expectLiteral("null"); return Value::nil(); }
        return parseNumber();
    }

    void expectLiteral(const char* lit) {
        size_t n = std::strlen(lit);
        if (pos + n > len || src.compare(pos, n, lit) != 0) throw std::runtime_error(std::string("قيمة JSON غير صالحة (متوقَّع ") + lit + ")");
        pos += n;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos >= len) throw std::runtime_error("سلسلة JSON غير مغلقة");
            char c = advance();
            if (c == '"') break;
            if (c == '\\') {
                if (pos >= len) throw std::runtime_error("هروب JSON غير مكتمل");
                char e = advance();
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (pos + 4 > len) throw std::runtime_error("\\u غير مكتمل");
                        unsigned code = (unsigned)std::stoul(src.substr(pos, 4), nullptr, 16);
                        pos += 4;
                        // ترميز UTF-8 بسيط (يكفي النطاق الأساسي BMP بلا أزواج مفاجئة surrogate pairs).
                        if (code < 0x80) out += (char)code;
                        else if (code < 0x800) {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        } else {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: throw std::runtime_error("هروب JSON غير معروف");
                }
            } else out += c;
        }
        return out;
    }

    Value parseNumber() {
        size_t start = pos;
        if (pos < len && (src[pos] == '-' || src[pos] == '+')) pos++;
        while (pos < len && (std::isdigit((unsigned char)src[pos]) || src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' || src[pos] == '-' || src[pos] == '+')) pos++;
        if (pos == start) throw std::runtime_error("رقم JSON غير صالح");
        try { return Value::num(std::stod(src.substr(start, pos - start))); }
        catch (...) { throw std::runtime_error("رقم JSON غير صالح"); }
    }

    Value parseArray() {
        expect('[');
        auto arr = std::make_shared<ArrayData>();
        skipWs();
        if (pos < len && peek() == ']') { pos++; return Value::makeArray(arr); }
        while (true) {
            arr->push_back(parseValue());
            skipWs();
            if (pos < len && peek() == ',') { pos++; continue; }
            break;
        }
        skipWs();
        expect(']');
        return Value::makeArray(arr);
    }

    Value parseObject() {
        expect('{');
        auto m = std::make_shared<MapData>();
        skipWs();
        if (pos < len && peek() == '}') { pos++; return Value::makeMap(m); }
        while (true) {
            skipWs();
            std::string key = parseString();
            skipWs();
            expect(':');
            Value val = parseValue();
            m->push_back({Value::string(key), val});
            skipWs();
            if (pos < len && peek() == ',') { pos++; continue; }
            break;
        }
        skipWs();
        expect('}');
        return Value::makeMap(m);
    }
};

// يحاول تحليل [text] كـ JSON. عند الفشل يُعيد Value نصية خام (fallback آمن دائماً؛ لا يرمي أبداً) —
// مناسب لأجسام ردود API حقيقية قد لا تكون JSON صالحاً (نص عادي، HTML لصفحة خطأ، إلخ).
inline Value decodeOrRaw(const std::string& text) {
    Value out; std::string err;
    Decoder d(text);
    if (d.parse(out, err)) return out;
    return Value::string(text);
}

} // namespace json
} // namespace rin
