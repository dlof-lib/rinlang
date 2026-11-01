// cli/linux/src/pkg/toml_lite.cpp
#include "toml_lite.h"
#include <sstream>
#include <cctype>
#include <algorithm>

namespace rinpm::toml {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// يحلل نصاً مقتبساً "..." بدءاً من موضع بعد علامة الاقتباس الأولى. يعيد المحتوى
// المفكوك (escapes بسيطة: \" \\ \n \t) ويحرّك pos ليتجاوز علامة الاقتباس الختامية.
std::string parseQuotedString(const std::string& line, size_t& pos, int lineNo) {
    std::string out;
    while (pos < line.size()) {
        char c = line[pos];
        if (c == '"') { ++pos; return out; }
        if (c == '\\' && pos + 1 < line.size()) {
            char n = line[pos + 1];
            switch (n) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(n); break;
            }
            pos += 2;
            continue;
        }
        out.push_back(c);
        ++pos;
    }
    throw ParseError("unterminated string literal", lineNo);
}

Value parseValue(const std::string& raw, int lineNo) {
    std::string v = trim(raw);
    if (v.empty()) throw ParseError("expected a value after '='", lineNo);
    if (v[0] == '"') {
        size_t pos = 1;
        Value val;
        val.kind = Value::Kind::String;
        val.str = parseQuotedString(v, pos, lineNo);
        return val;
    }
    if (v[0] == '[') {
        // مصفوفة نصوص: ["a", "b", "c"] — قد تمتد أسطراً متعددة في TOML العام، لكن
        // نكتفي هنا بدعم السطر الواحد (كافٍ تماماً لصيغة rin.toml الحالية).
        if (v.back() != ']') throw ParseError("array value must end with ']' on the same line", lineNo);
        Value val;
        val.kind = Value::Kind::StringArray;
        size_t pos = 1;
        while (pos < v.size()) {
            while (pos < v.size() && (v[pos] == ' ' || v[pos] == '\t' || v[pos] == ',')) ++pos;
            if (pos >= v.size() || v[pos] == ']') break;
            if (v[pos] != '"') throw ParseError("array elements must be quoted strings", lineNo);
            ++pos;
            val.arr.push_back(parseQuotedString(v, pos, lineNo));
        }
        return val;
    }
    if (v == "true" || v == "false") {
        Value val; val.kind = Value::Kind::Bool; val.boolean = (v == "true");
        return val;
    }
    // رقم صحيح (لا نحتاج فاصلة عائمة في rin.toml/rin.lock حالياً)
    bool allDigits = !v.empty();
    size_t start = (v[0] == '-') ? 1 : 0;
    for (size_t i = start; i < v.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(v[i]))) { allDigits = false; break; }
    if (allDigits && v.size() > start) {
        Value val; val.kind = Value::Kind::Int; val.integer = std::stoll(v);
        return val;
    }
    throw ParseError("unrecognized value syntax: \"" + v + "\"", lineNo);
}

} // namespace

Document parse(const std::string& text) {
    Document doc;
    std::istringstream in(text);
    std::string rawLine;
    int lineNo = 0;
    std::string currentSection; // "" يعني لا قسم فُتح بعد (جذر غير مدعوم للكتابة المباشرة)
    bool inArrayTable = false;
    std::string currentArrayTable;

    while (std::getline(in, rawLine)) {
        ++lineNo;
        // إزالة تعليق (# ...) خارج النصوص المقتبسة فقط
        std::string line;
        bool inStr = false;
        for (size_t i = 0; i < rawLine.size(); ++i) {
            char c = rawLine[i];
            if (c == '"' && (i == 0 || rawLine[i - 1] != '\\')) inStr = !inStr;
            if (c == '#' && !inStr) break;
            line.push_back(c);
        }
        line = trim(line);
        if (line.empty()) continue;

        if (line.size() >= 4 && line[0] == '[' && line[1] == '[') {
            auto close = line.find("]]");
            if (close == std::string::npos) throw ParseError("expected ']]' to close array-of-tables header", lineNo);
            std::string name = trim(line.substr(2, close - 2));
            if (name.empty()) throw ParseError("array-of-tables name cannot be empty", lineNo);
            if (!doc.arrayTables.count(name)) doc.arrayTableOrder.push_back(name);
            doc.arrayTables[name].push_back(Table{});
            inArrayTable = true;
            currentArrayTable = name;
            currentSection.clear();
            continue;
        }
        if (line[0] == '[') {
            auto close = line.find(']');
            if (close == std::string::npos) throw ParseError("expected ']' to close section header", lineNo);
            std::string name = trim(line.substr(1, close - 1));
            if (name.empty()) throw ParseError("section name cannot be empty", lineNo);
            if (!doc.sections.count(name)) doc.sectionOrder.push_back(name);
            doc.sections[name]; // ينشئ جدولاً فارغاً إن لم يوجد
            currentSection = name;
            inArrayTable = false;
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) {
            throw ParseError("expected 'key = value' but found: \"" + line + "\"", lineNo);
        }
        std::string key = trim(line.substr(0, eq));
        std::string rawVal = line.substr(eq + 1);
        if (key.empty()) throw ParseError("empty key before '='", lineNo);
        Value val = parseValue(rawVal, lineNo);

        if (inArrayTable) {
            if (doc.arrayTables[currentArrayTable].empty()) {
                throw ParseError("key '" + key + "' appears before any '[[" + currentArrayTable + "]]' row", lineNo);
            }
            doc.arrayTables[currentArrayTable].back()[key] = val;
        } else {
            if (currentSection.empty()) {
                throw ParseError("key '" + key + "' must appear under a [section] header", lineNo);
            }
            doc.sections[currentSection][key] = val;
        }
    }
    return doc;
}

std::string getStr(const Table& t, const std::string& key, const std::string& def) {
    auto it = t.find(key);
    if (it == t.end()) return def;
    if (it->second.kind == Value::Kind::String) return it->second.str;
    if (it->second.kind == Value::Kind::Int) return std::to_string(it->second.integer);
    if (it->second.kind == Value::Kind::Bool) return it->second.boolean ? "true" : "false";
    return def;
}

bool hasKey(const Table& t, const std::string& key) { return t.count(key) != 0; }

std::vector<std::string> getStrArray(const Table& t, const std::string& key) {
    auto it = t.find(key);
    if (it == t.end()) return {};
    if (it->second.kind == Value::Kind::StringArray) return it->second.arr;
    return {};
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

void Writer::section(const std::string& name) {
    out_ += "[" + name + "]\n";
}
void Writer::beginArrayTable(const std::string& name) {
    out_ += "[[" + name + "]]\n";
}
void Writer::kv(const std::string& key, const std::string& value) {
    out_ += key + " = " + quote(value) + "\n";
}
void Writer::kvRaw(const std::string& key, const std::string& rawValue) {
    out_ += key + " = " + rawValue + "\n";
}
void Writer::kvArray(const std::string& key, const std::vector<std::string>& values) {
    out_ += key + " = [";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out_ += ", ";
        out_ += quote(values[i]);
    }
    out_ += "]\n";
}
void Writer::blank() { out_ += "\n"; }

} // namespace rinpm::toml
