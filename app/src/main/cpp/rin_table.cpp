// rin_table.cpp — توسعة مفهوم الجدول (@table / @container.table)
// ============================================================================
// تُضمَّن مباشرة في نهاية rin_interpreter.cpp (نفس أسلوب rin_extra_natives*.cpp) فلا تحتاج أي تعديل في
// CMake/Gradle. المبدأ الحاكم: **لا يُضاف اسم دالة جديد إلى اللغة**. بدلاً من ذلك تُعلَّم الدوال الموجودة
// أصلاً أن تفهم الجدول:
//   * دوال المستندات  : insertDoc updateDoc deleteDoc findDoc queryDocs queryOneDoc docIds allDocs countDocs
//   * دوال الحاويات   : sum avg min max distinct countBy pluck groupBy sortBy top paginate query search stats
//                       toRows toCsv exportCsv toJson fromJson exportToFile importFromFile mergeFrom diff equals
//                       checksum contains push pop getOr renameField clone rename remove
//   * حقول الأعمدة    : getField/setField/hasField/deleteField (في tableVirtual* داخل rin_interpreter.cpp)
//   * التصدير         : save format=csv|json|md|html|txt
// كل دالة من الأولى تستدعي tableNative("<اسمها>", ...) في أول سطر لها؛ فإن لم يكن الوسيط الأول جدولاً
// ترجع false وتكمل الدالة الأصلية سلوكها القديم كما هو بلا أي تغيير.
// ============================================================================

namespace rin {

namespace {

bool tIsArr(const Value& v) { return v.type == Value::Type::ARRAY && v.array; }
Value tArr(ArrayData d) { return Value::makeArray(std::make_shared<ArrayData>(std::move(d))); }
Value tMap(MapData d) { return Value::makeMap(std::make_shared<MapData>(std::move(d))); }
Value tS(const std::string& s) { return Value::string(s); }

// معرّف صف: رقم 1-based كنص ("2") أو كرقم (2).
bool tRowId(const Value& id, size_t& out) {
    if (id.type == Value::Type::NUMBER) {
        if (id.number < 1 || id.number != static_cast<double>(static_cast<size_t>(id.number))) return false;
        out = static_cast<size_t>(id.number);
        return true;
    }
    if (id.type == Value::Type::STRING && !id.str.empty() && id.str.size() < 12) {
        for (char c : id.str) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        out = static_cast<size_t>(std::stoul(id.str));
        return out >= 1;
    }
    return false;
}

std::string tCell(const Value& v) {
    if (v.type == Value::Type::NIL) return "";
    if (v.type == Value::Type::ARRAY || v.type == Value::Type::MAP) return json::encode(v);
    return v.toDisplayString();
}

size_t tU8Len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string tEscHtml(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

// "12" / "-3.5" / "0.25" -> رقم؛ لا يُحوَّل ما يفقد معلومة (007 · 1e5 · +5 · نص).
bool tParseNumber(const std::string& s, double& out) {
    size_t i = 0, n = s.size();
    if (n == 0 || n > 15) return false;
    if (s[i] == '-') ++i;
    size_t intStart = i;
    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    size_t intLen = i - intStart;
    if (intLen == 0) return false;
    if (intLen > 1 && s[intStart] == '0') return false;
    if (i < n) {
        if (s[i] != '.') return false;
        ++i;
        size_t fracStart = i;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i == fracStart || i != n) return false;
    }
    out = std::strtod(s.c_str(), nullptr);
    return true;
}

} // namespace

// ---------------------------------------------------------------- أساسيات
bool Interpreter::tableColumnValues(const std::string& name, const std::string& col, std::vector<Value>& out) const {
    std::vector<std::string> h;
    bool named = tableHeader(name, h);
    size_t ci = std::find(h.begin(), h.end(), col) - h.begin();
    if (ci >= h.size()) return false;
    out.clear();
    auto it = tableRows.find(name);
    if (it == tableRows.end() || !it->second) return true;
    for (size_t r = named ? 1 : 0; r < it->second->size(); ++r) {
        const Value& row = (*it->second)[r];
        bool has = row.type == Value::Type::ARRAY && row.array && ci < row.array->size();
        out.push_back(has ? (*row.array)[ci] : Value::nil());
    }
    return true;
}

Value Interpreter::tableOrderRecord(const std::string& name, const Value& rec, int line) {
    std::vector<std::string> hdr;
    if (!tableHeader(name, hdr) || hdr.empty())
        throw diagErr(diag::Code::E0004_InvalidType, line, "الكتابة بقاموس تحتاج رأساً: اجعل أول صف في الجدول مصفوفة عناوين نصية");
    auto ordered = std::make_shared<ArrayData>(hdr.size(), Value::nil());
    if (rec.type == Value::Type::MAP && rec.map) {
        for (auto& kv : *rec.map) {
            size_t c = std::find(hdr.begin(), hdr.end(), kv.first.toDisplayString()) - hdr.begin();
            if (c >= hdr.size())
                throw diagErr(diag::Code::E0007_InvalidArguments, line, "العمود '" + kv.first.toDisplayString() + "' غير موجود في رأس الجدول");
            (*ordered)[c] = kv.second;
        }
    }
    return Value::makeArray(ordered);
}

size_t Interpreter::tableAppendRecords(const std::string& name, const std::vector<Value>& records, int line) {
    auto rows = tableRowsOf(name);
    std::vector<std::string> hdr;
    bool named = tableHeader(name, hdr);
    if (!named) {
        if (!rows->empty())
            throw diagErr(diag::Code::E0004_InvalidType, line, "الإلحاق بسجلّات يحتاج جدولاً برأس نصي (أو جدولاً فارغاً)");
        rows->push_back(Value::makeArray(std::make_shared<ArrayData>()));
        hdr.clear();
    }
    for (auto& r : records) {
        if (r.type != Value::Type::MAP || !r.map)
            throw diagErr(diag::Code::E0004_InvalidType, line, "كل سجلّ يجب أن يكون قاموساً، وُجد `" + r.typeName() + "`");
        for (auto& kv : *r.map) { // عمود جديد: يُضاف للرأس وتُمدَّد الصفوف السابقة بـ nil
            std::string k = kv.first.toDisplayString();
            if (std::find(hdr.begin(), hdr.end(), k) != hdr.end()) continue;
            hdr.push_back(k);
            (*rows)[0].array->push_back(tS(k));
            for (size_t i = 1; i < rows->size(); ++i)
                if ((*rows)[i].type == Value::Type::ARRAY && (*rows)[i].array)
                    while ((*rows)[i].array->size() < hdr.size()) (*rows)[i].array->push_back(Value::nil());
        }
        rows->push_back(tableOrderRecord(name, r, line));
    }
    return records.size();
}

void Interpreter::tableForget(const std::string& name) { tableRows.erase(name); containerStyles.erase(name); }

void Interpreter::tableMove(const std::string& from, const std::string& to) {
    auto r = tableRows.find(from);
    if (r != tableRows.end()) { auto v = std::move(r->second); tableRows.erase(r); tableRows[to] = std::move(v); }
    auto s = containerStyles.find(from);
    if (s != containerStyles.end()) { auto v = std::move(s->second); containerStyles.erase(s); containerStyles[to] = std::move(v); }
}

void Interpreter::tableCopy(const std::string& from, const std::string& to) {
    auto r = tableRows.find(from);
    if (r != tableRows.end() && r->second) {
        auto dst = std::make_shared<ArrayData>();
        for (auto& row : *r->second) { // نسخة عميقة: لا مشاركة خلايا بين الجدولين
            if (row.type == Value::Type::ARRAY && row.array) dst->push_back(Value::makeArray(std::make_shared<ArrayData>(*row.array)));
            else dst->push_back(row);
        }
        tableRows[to] = dst;
    }
    auto s = containerStyles.find(from);
    if (s != containerStyles.end()) containerStyles[to] = s->second;
}

// ---------------------------------------------------------------- CSV -> جدول
void Interpreter::tableLoadCsv(const std::string& name, const std::string& text) {
    std::vector<std::vector<std::string>> recs;
    std::vector<std::string> row;
    std::string cur;
    bool inQ = false, any = false;
    size_t i = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) i = 3;
    auto endField = [&]() { row.push_back(cur); cur.clear(); any = true; };
    auto endRow = [&]() { if (any || !cur.empty()) { endField(); recs.push_back(row); } row.clear(); any = false; };
    for (; i < text.size(); ++i) {
        char c = text[i];
        if (inQ) {
            if (c == '"') { if (i + 1 < text.size() && text[i + 1] == '"') { cur += '"'; ++i; } else inQ = false; }
            else cur += c;
        } else if (c == '"') { inQ = true; any = true; }
        else if (c == ',') endField();
        else if (c == '\n') endRow();
        else if (c == '\r') { /* يُتجاهل: CRLF */ }
        else cur += c;
    }
    endRow();
    auto rows = std::make_shared<ArrayData>();
    for (size_t r = 0; r < recs.size(); ++r) {
        auto cells = std::make_shared<ArrayData>();
        for (auto& f : recs[r]) {
            if (r == 0) { cells->push_back(tS(f)); continue; } // الصف الأول رأس: يبقى نصاً دائماً
            std::string t = f;
            if (t.size() > 1 && t[0] == '\'' && (t[1] == '=' || t[1] == '+' || t[1] == '-' || t[1] == '@')) t = t.substr(1); // عكس حماية csvCell
            double d;
            if (t.empty()) cells->push_back(Value::nil());
            else if (tParseNumber(t, d)) cells->push_back(Value::num(d));
            else cells->push_back(tS(t));
        }
        rows->push_back(Value::makeArray(cells));
    }
    *tableRowsOf(name) = std::move(*rows); // في المكان: أي مقبض rows سابق يبقى صالحاً
}

// ---------------------------------------------------------------- تصدير نصي
std::string Interpreter::tableExport(const std::string& name, const std::string& fmt, int line) {
    auto rows = tableRowsOf(name);
    std::vector<std::string> hdr;
    bool named = tableHeader(name, hdr);
    size_t width = hdr.size();
    auto cellsOf = [&](size_t r) {
        std::vector<std::string> out(width);
        const Value& row = (*rows)[r];
        if (row.type == Value::Type::ARRAY && row.array)
            for (size_t c = 0; c < width && c < row.array->size(); ++c) out[c] = tCell((*row.array)[c]);
        return out;
    };
    size_t first = named ? 1 : 0;

    if (fmt == "csv") {
        std::string o;
        for (size_t r = 0; r < rows->size(); ++r) {
            const Value& row = (*rows)[r];
            for (size_t c = 0; c < width; ++c) {
                if (c) o += ",";
                if (row.type == Value::Type::ARRAY && row.array && c < row.array->size()) o += extra3::csvCell((*row.array)[c]);
            }
            o += "\n";
        }
        return o;
    }
    if (fmt == "json") { // مفصول بسطر لكل سجلّ؛ بلا رأس نصي = مصفوفة مصفوفات
        std::vector<std::string> items;
        if (named) for (auto& d : tableAsDocs(name)) items.push_back(json::encode(d.second));
        else for (auto& r : *rows) items.push_back(json::encode(r));
        if (items.empty()) return "[]\n";
        std::string o = "[\n";
        for (size_t i = 0; i < items.size(); ++i) o += "  " + items[i] + (i + 1 < items.size() ? ",\n" : "\n");
        return o + "]\n";
    }
    if (fmt == "md") {
        auto esc = [](std::string s) {
            std::string o;
            for (char c : s) { if (c == '|') o += "\\|"; else if (c == '\n' || c == '\r') o += ' '; else o += c; }
            return o;
        };
        std::string o = "|";
        for (auto& h : hdr) o += " " + esc(h) + " |";
        o += "\n|";
        for (size_t c = 0; c < width; ++c) o += " --- |";
        o += "\n";
        for (size_t r = first; r < rows->size(); ++r) {
            o += "|";
            for (auto& x : cellsOf(r)) o += " " + esc(x) + " |";
            o += "\n";
        }
        return o;
    }
    if (fmt == "html") {
        std::string o = "<table class=\"rin-table\"";
        auto st = containerStyles.find(name);
        if (st != containerStyles.end()) o += " data-style=\"" + tEscHtml(st->second) + "\"";
        o += ">\n  <thead><tr>";
        for (auto& h : hdr) o += "<th>" + tEscHtml(h) + "</th>";
        o += "</tr></thead>\n  <tbody>\n";
        for (size_t r = first; r < rows->size(); ++r) {
            o += "    <tr>";
            for (auto& x : cellsOf(r)) o += "<td>" + tEscHtml(x) + "</td>";
            o += "</tr>\n";
        }
        return o + "  </tbody>\n</table>\n";
    }
    if (fmt == "txt") {
        std::vector<std::vector<std::string>> grid;
        grid.push_back(hdr);
        for (size_t r = first; r < rows->size(); ++r) grid.push_back(cellsOf(r));
        std::vector<size_t> w(width, 1);
        for (auto& g : grid) for (size_t c = 0; c < width; ++c) w[c] = std::max(w[c], tU8Len(g[c]));
        auto sep = [&]() { std::string s = "+"; for (size_t c = 0; c < width; ++c) s += std::string(w[c] + 2, '-') + "+"; return s + "\n"; };
        auto line_ = [&](const std::vector<std::string>& g) {
            std::string s = "|";
            for (size_t c = 0; c < width; ++c) s += " " + g[c] + std::string(w[c] - tU8Len(g[c]), ' ') + " |";
            return s + "\n";
        };
        std::string o = sep() + line_(grid[0]) + sep();
        for (size_t r = 1; r < grid.size(); ++r) o += line_(grid[r]);
        return o + sep();
    }
    throw diagErr(diag::Code::E0007_InvalidArguments, line, "صيغة تصدير الجدول '" + fmt + "' غير مدعومة (المتاح: csv · json · md · html · txt)");
}

// ---------------------------------------------------------------- الخطّاف الموحَّد للدوال الموجودة
bool Interpreter::tableNative(const std::string& fn, std::vector<Value>& a, int line, Value& out) {
    // container.search(text, kind) هي الوحيدة التي يقع اسم الحاوية فيها ثانياً
    std::string name;
    if (fn == "container.search") {
        if (a.size() != 2 || a[1].type != Value::Type::STRING) return false;
        name = a[1].str;
    } else {
        if (a.empty() || a[0].type != Value::Type::STRING) return false;
        name = a[0].str;
    }
    if (!isTableContainer(name)) return false;
    const size_t n = a.size();
    auto docs = [&]() { return tableAsDocs(name); };
    auto bodyOff = [&]() { std::vector<std::string> h; return tableHeader(name, h) ? size_t(1) : size_t(0); };
    auto needCol = [&](const Value& c, std::vector<Value>& vals) {
        std::string col = c.toDisplayString();
        if (!tableColumnValues(name, col, vals))
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "العمود '" + col + "' غير موجود في الجدول '" + name + "'");
    };
    auto fieldsList = [&](const Value& f) {
        std::vector<std::string> cols;
        if (!tIsArr(f)) throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' يتوقّع قائمة أعمدة (مصفوفة) أو nil");
        for (auto& c : *f.array) cols.push_back(c.toDisplayString());
        return cols;
    };
    auto withId = [](const std::string& id, const Value& rec) {
        MapData m;
        m.push_back({tS("_id"), tS(id)});
        if (rec.type == Value::Type::MAP && rec.map) for (auto& kv : *rec.map) m.push_back(kv);
        return tMap(std::move(m));
    };
    auto cellOf = [](const Value& rec, const std::string& col) -> const Value* {
        if (rec.type != Value::Type::MAP || !rec.map) return nullptr;
        for (auto& kv : *rec.map) if (kv.first.toDisplayString() == col) return &kv.second;
        return nullptr;
    };

    // ===== دوال المستندات: معرّف الصف = رقمه في الجسم (1-based)، كما في استعلامات RCSQL =====
    if (fn == "allDocs" && n == 1) { ArrayData r; for (auto& d : docs()) r.push_back(d.second); out = tArr(std::move(r)); return true; }
    if (fn == "countDocs" && n == 1) { out = Value::num(static_cast<double>(docs().size())); return true; }
    if (fn == "docIds" && n == 1) { ArrayData r; for (auto& d : docs()) r.push_back(tS(d.first)); out = tArr(std::move(r)); return true; }
    if (fn == "findDoc" && n == 2) {
        size_t id; auto d = docs();
        out = (tRowId(a[1], id) && id <= d.size()) ? d[id - 1].second : Value::nil();
        return true;
    }
    if ((fn == "queryDocs" || fn == "queryOneDoc") && n == 3) {
        std::string field = a[1].toDisplayString();
        ArrayData hits;
        for (auto& d : docs()) {
            const Value* c = cellOf(d.second, field);
            if (c && valuesEqual(*c, a[2])) { if (fn == "queryOneDoc") { out = d.second; return true; } hits.push_back(d.second); }
        }
        out = fn == "queryOneDoc" ? Value::nil() : tArr(std::move(hits));
        return true;
    }
    if (fn == "insertDoc" && n == 3) {
        if (a[2].type != Value::Type::MAP || !a[2].map)
            throw diagErr(diag::Code::E0020_InvalidDocument, line, "'insertDoc' expects a map as the third argument (document fields)");
        Value row = tableOrderRecord(name, a[2], line);
        auto rows = tableRowsOf(name);
        size_t id, off = bodyOff();
        if (tRowId(a[1], id) && off + id - 1 < rows->size()) { (*rows)[off + id - 1] = row; out = Value::boolean_(false); } // استبدال
        else { rows->push_back(row); out = Value::boolean_(true); }                                                       // إلحاق
        return true;
    }
    if (fn == "updateDoc" && n == 3) {
        if (a[2].type != Value::Type::MAP || !a[2].map)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'updateDoc' يتوقّع كائناً/قاموساً (map) كوسيط ثالث للحقول الجديدة");
        Value patch = tableOrderRecord(name, a[2], line); // يتحقّق من الأعمدة (E0007)
        auto rows = tableRowsOf(name);
        size_t id, off = bodyOff();
        if (!tRowId(a[1], id) || off + id - 1 >= rows->size()) { out = Value::boolean_(false); return true; }
        std::vector<std::string> hdr; tableHeader(name, hdr);
        auto cells = (*rows)[off + id - 1].array;
        while (cells->size() < hdr.size()) cells->push_back(Value::nil());
        for (auto& kv : *a[2].map) {
            size_t c = std::find(hdr.begin(), hdr.end(), kv.first.toDisplayString()) - hdr.begin();
            (*cells)[c] = kv.second;
        }
        out = Value::boolean_(true);
        return true;
    }
    if (fn == "deleteDoc" && n == 2) {
        auto rows = tableRowsOf(name);
        size_t id, off = bodyOff();
        if (!tRowId(a[1], id) || off + id - 1 >= rows->size()) { out = Value::boolean_(false); return true; }
        rows->erase(rows->begin() + (off + id - 1));
        out = Value::boolean_(true);
        return true;
    }

    // ===== تجميعات على عمود =====
    if ((fn == "container.sum" || fn == "container.avg" || fn == "container.min" || fn == "container.max") && n == 2) {
        std::vector<Value> v; needCol(a[1], v);
        double sum = 0, mn = 0, mx = 0; size_t k = 0;
        for (auto& x : v) if (x.type == Value::Type::NUMBER) {
            if (!k) mn = mx = x.number; else { mn = std::min(mn, x.number); mx = std::max(mx, x.number); }
            sum += x.number; ++k;
        }
        if (fn == "container.sum") out = Value::num(sum);
        else if (!k) out = Value::nil();
        else out = Value::num(fn == "container.avg" ? sum / k : fn == "container.min" ? mn : mx);
        return true;
    }
    if (fn == "container.pluck" && n == 2) { std::vector<Value> v; needCol(a[1], v); out = tArr(ArrayData(v.begin(), v.end())); return true; }
    if (fn == "container.distinct" && n == 2) {
        std::vector<Value> v; needCol(a[1], v);
        ArrayData r;
        for (auto& x : v) { bool dup = false; for (auto& e : r) if (valuesEqual(e, x)) { dup = true; break; } if (!dup) r.push_back(x); }
        out = tArr(std::move(r));
        return true;
    }
    if (fn == "container.countBy" && n == 2) {
        std::vector<Value> v; needCol(a[1], v);
        MapData r;
        for (auto& x : v) {
            bool found = false;
            for (auto& kv : r) if (valuesEqual(kv.first, x)) { kv.second.number += 1; found = true; break; }
            if (!found) r.push_back({x, Value::num(1)});
        }
        out = tMap(std::move(r));
        return true;
    }
    if (fn == "container.groupBy" && n == 2) { // {قيمة العمود: [معرّفات الصفوف]}
        std::vector<Value> v; needCol(a[1], v);
        MapData r;
        for (size_t i = 0; i < v.size(); ++i) {
            Value key = tS(v[i].toDisplayString());
            bool found = false;
            for (auto& kv : r) if (valuesEqual(kv.first, key)) { kv.second.array->push_back(tS(std::to_string(i + 1))); found = true; break; }
            if (!found) r.push_back({key, tArr(ArrayData{tS(std::to_string(i + 1))})});
        }
        out = tMap(std::move(r));
        return true;
    }
    if (fn == "container.sortBy" && (n == 2 || n == 3)) { // معرّفات الصفوف مرتّبة؛ الخلايا الفارغة أخيراً
        std::vector<Value> v; needCol(a[1], v);
        bool desc = n > 2 && a[2].isTruthy();
        std::vector<size_t> idx, nils;
        for (size_t i = 0; i < v.size(); ++i) (v[i].type == Value::Type::NIL ? nils : idx).push_back(i);
        std::stable_sort(idx.begin(), idx.end(), [&](size_t x, size_t y) { return desc ? extra3::lessVal(v[y], v[x]) : extra3::lessVal(v[x], v[y]); });
        ArrayData r;
        for (size_t i : idx) r.push_back(tS(std::to_string(i + 1)));
        for (size_t i : nils) r.push_back(tS(std::to_string(i + 1)));
        out = tArr(std::move(r));
        return true;
    }
    if (fn == "container.top" && n == 3) { // أعلى n صفوف بقيمة العمود (معرّفات)
        Value desc = Value::boolean_(true);
        std::vector<Value> sa{a[0], a[1], desc};
        Value sorted;
        tableNative("container.sortBy", sa, line, sorted);
        size_t k = static_cast<size_t>(std::max(0.0, a[2].type == Value::Type::NUMBER ? a[2].number : 0.0));
        ArrayData r;
        for (size_t i = 0; i < sorted.array->size() && i < k; ++i) r.push_back((*sorted.array)[i]);
        out = tArr(std::move(r));
        return true;
    }
    if (fn == "container.paginate" && n >= 1 && n <= 3) {
        double page = n > 1 && a[1].type == Value::Type::NUMBER ? a[1].number : 1;
        double size = n > 2 && a[2].type == Value::Type::NUMBER ? a[2].number : 10;
        size = std::max(1.0, std::min(1000.0, std::floor(size)));
        page = std::max(1.0, std::floor(page));
        auto d = docs();
        double total = static_cast<double>(d.size()), pages = std::max(1.0, std::ceil(total / size));
        ArrayData items;
        for (size_t i = static_cast<size_t>((page - 1) * size); i < d.size() && items.size() < static_cast<size_t>(size); ++i)
            items.push_back(withId(d[i].first, d[i].second));
        out = tMap({{tS("items"), tArr(std::move(items))}, {tS("page"), Value::num(page)}, {tS("size"), Value::num(size)},
                    {tS("total"), Value::num(total)}, {tS("pages"), Value::num(pages)}});
        return true;
    }
    if (fn == "container.query" && n >= 1 && n <= 3) {
        const MapData* conds = nullptr; const MapData* opts = nullptr;
        if (n > 1 && a[1].type != Value::Type::NIL) { if (a[1].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'container.query' يتوقّع قاموس شروط"); conds = a[1].map.get(); }
        if (n > 2 && a[2].type != Value::Type::NIL) { if (a[2].type != Value::Type::MAP) throw diagErr(diag::Code::E0004_InvalidType, line, "'container.query' يتوقّع قاموس خيارات"); opts = a[2].map.get(); }
        std::vector<std::pair<std::string, Value>> hits;
        for (auto& d : docs()) {
            bool ok = true;
            if (conds) for (auto& c : *conds) { if (!extra::matchCondition(cellOf(d.second, c.first.toDisplayString()), c.second)) { ok = false; break; } }
            if (ok) hits.push_back(d);
        }
        if (opts) {
            const Value *sb = nullptr, *desc = nullptr, *limit = nullptr;
            for (auto& kv : *opts) {
                std::string k = kv.first.toDisplayString();
                if (k == "sortBy") sb = &kv.second; else if (k == "desc") desc = &kv.second; else if (k == "limit") limit = &kv.second;
            }
            if (sb && sb->type == Value::Type::STRING) {
                std::string col = sb->str; bool dsc = desc && desc->isTruthy();
                std::stable_sort(hits.begin(), hits.end(), [&](const auto& x, const auto& y) {
                    const Value *vx = cellOf(x.second, col), *vy = cellOf(y.second, col);
                    if (!vx || !vy) return vx == nullptr && vy != nullptr;
                    return dsc ? extra3::lessVal(*vy, *vx) : extra3::lessVal(*vx, *vy);
                });
            }
            if (limit && limit->type == Value::Type::NUMBER && limit->number >= 0 && hits.size() > static_cast<size_t>(limit->number))
                hits.resize(static_cast<size_t>(limit->number));
        }
        ArrayData r;
        for (auto& h : hits) r.push_back(tS(h.first));
        out = tArr(std::move(r));
        return true;
    }
    if (fn == "container.search") { // نص غير حسّاس للحالة داخل أي خلية -> معرّفات الصفوف
        std::string q = extra3::lowerAscii(a[0].toDisplayString());
        ArrayData r;
        for (auto& d : docs()) {
            bool hit = false;
            if (d.second.map) for (auto& kv : *d.second.map) if (extra3::lowerAscii(tCell(kv.second)).find(q) != std::string::npos) { hit = true; break; }
            if (hit) r.push_back(tS(d.first));
        }
        out = tArr(std::move(r));
        return true;
    }
    if (fn == "container.stats" && n == 1) { // {count: عدد الصفوف, fields: {عمود: {count, numeric, min, max, sum, avg}}}
        std::vector<std::string> hdr; tableHeader(name, hdr);
        MapData fields;
        for (auto& col : hdr) {
            std::vector<Value> v; tableColumnValues(name, col, v);
            double cnt = 0, sum = 0, mn = 0, mx = 0; bool numeric = true, first = true;
            for (auto& x : v) {
                if (x.type == Value::Type::NIL) continue;
                cnt += 1;
                if (x.type == Value::Type::NUMBER) { if (first) { mn = mx = x.number; first = false; } else { mn = std::min(mn, x.number); mx = std::max(mx, x.number); } sum += x.number; }
                else numeric = false;
            }
            MapData st{{tS("count"), Value::num(cnt)}, {tS("numeric"), Value::boolean_(numeric)}};
            if (numeric && cnt > 0) { st.push_back({tS("min"), Value::num(mn)}); st.push_back({tS("max"), Value::num(mx)}); st.push_back({tS("sum"), Value::num(sum)}); st.push_back({tS("avg"), Value::num(sum / cnt)}); }
            fields.push_back({tS(col), tMap(std::move(st))});
        }
        out = tMap({{tS("count"), Value::num(static_cast<double>(docs().size()))}, {tS("fields"), tMap(std::move(fields))}});
        return true;
    }

    // ===== تحويل وتصدير واستيراد =====
    if (fn == "container.toRows" && (n == 1 || n == 2)) {
        std::vector<std::string> cols;
        bool sub = n == 2 && a[1].type != Value::Type::NIL;
        if (sub) cols = fieldsList(a[1]);
        ArrayData r;
        for (auto& d : docs()) {
            if (!sub) { r.push_back(withId(d.first, d.second)); continue; }
            MapData m{{tS("_id"), tS(d.first)}};
            for (auto& c : cols) { const Value* v = cellOf(d.second, c); m.push_back({tS(c), v ? *v : Value::nil()}); }
            r.push_back(tMap(std::move(m)));
        }
        out = tArr(std::move(r));
        return true;
    }
    auto csvText = [&](const Value* f) {
        if (!f || f->type == Value::Type::NIL) return tableExport(name, "csv", line);
        auto cols = fieldsList(*f);
        std::string o;
        for (size_t i = 0; i < cols.size(); ++i) o += (i ? "," : "") + extra3::csvCell(tS(cols[i]));
        o += "\n";
        for (auto& d : docs()) {
            for (size_t i = 0; i < cols.size(); ++i) { const Value* v = cellOf(d.second, cols[i]); o += (i ? "," : "") + (v ? extra3::csvCell(*v) : std::string()); }
            o += "\n";
        }
        return o;
    };
    if (fn == "container.toCsv" && (n == 1 || n == 2)) { out = tS(csvText(n > 1 ? &a[1] : nullptr)); return true; }
    if (fn == "container.exportCsv" && (n == 2 || n == 3)) {
        writeRealFile(a[1].toDisplayString(), csvText(n > 2 ? &a[2] : nullptr), line, "container.exportCsv");
        out = Value::boolean_(true);
        return true;
    }
    if (fn == "container.toJson" && n == 1) { out = tS(tableExport(name, "json", line)); return true; }
    auto loadRecords = [&](const Value& decoded, bool overwrite) {
        if (!tIsArr(decoded)) return false;
        bool allMaps = !decoded.array->empty(), allArrs = !decoded.array->empty();
        for (auto& e : *decoded.array) { if (e.type != Value::Type::MAP) allMaps = false; if (!tIsArr(e)) allArrs = false; }
        if (decoded.array->empty()) { if (overwrite) tableRowsOf(name)->clear(); return true; }
        if (!allMaps && !allArrs) return false;
        if (overwrite) tableRowsOf(name)->clear();
        if (allMaps) tableAppendRecords(name, *decoded.array, line);
        else for (auto& r : *decoded.array) tableRowsOf(name)->push_back(Value::makeArray(std::make_shared<ArrayData>(*r.array)));
        return true;
    };
    if (fn == "container.fromJson" && (n == 2 || n == 3)) {
        out = Value::boolean_(loadRecords(json::decodeOrRaw(a[1].toDisplayString()), n < 3 || a[2].isTruthy()));
        return true;
    }
    auto extOf = [](const std::string& p) {
        size_t dot = p.rfind('.');
        return dot == std::string::npos ? std::string() : extra3::lowerAscii(p.substr(dot + 1));
    };
    if (fn == "container.exportToFile" && n == 2) {
        std::string path = a[1].toDisplayString(), ext = extOf(path);
        std::string fmt = (ext == "csv" || ext == "md" || ext == "html" || ext == "txt") ? ext : "json";
        writeRealFile(path, tableExport(name, fmt, line), line, "container.exportToFile");
        out = Value::boolean_(true);
        return true;
    }
    if (fn == "container.importFromFile" && (n == 2 || n == 3)) {
        std::string path = a[1].toDisplayString();
        std::ifstream in(resolvePath(path, line), std::ios::binary);
        if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, "'container.importFromFile': تعذّر فتح الملف '" + path + "'");
        std::ostringstream buf; buf << in.rdbuf();
        bool overwrite = n < 3 || a[2].isTruthy();
        if (extOf(path) == "csv") {
            if (overwrite) { tableLoadCsv(name, buf.str()); out = Value::boolean_(true); return true; }
            std::string tmp = name + "\x01csv"; // جدول مؤقت لاستخراج السجلّات ثم إلحاقها
            tableRows[tmp] = std::make_shared<ArrayData>();
            containerKinds[tmp] = ContainerKind::TABLE;
            tableLoadCsv(tmp, buf.str());
            std::vector<Value> recs;
            for (auto& d : tableAsDocs(tmp)) recs.push_back(d.second);
            tableRows.erase(tmp); containerKinds.erase(tmp);
            tableAppendRecords(name, recs, line);
            out = Value::boolean_(true);
            return true;
        }
        out = Value::boolean_(loadRecords(json::decodeOrRaw(buf.str()), overwrite));
        return true;
    }

    // ===== مقارنة ودمج =====
    if (fn == "container.mergeFrom" && (n == 2 || n == 3) && a[1].type == Value::Type::STRING && isTableContainer(a[1].str) && a[1].str != name) {
        std::vector<Value> recs;
        for (auto& d : tableAsDocs(a[1].str)) recs.push_back(d.second);
        out = Value::num(static_cast<double>(tableAppendRecords(name, recs, line)));
        return true;
    }
    if ((fn == "container.diff" || fn == "container.equals") && n == 2 && a[1].type == Value::Type::STRING && isTableContainer(a[1].str)) {
        auto da = docs(), db = tableAsDocs(a[1].str);
        auto keys = [](const std::vector<std::pair<std::string, Value>>& d) { std::vector<std::string> k; for (auto& x : d) k.push_back(json::encode(x.second)); return k; };
        auto ka = keys(da), kb = keys(db);
        if (fn == "container.equals") { out = Value::boolean_(ka == kb); return true; }
        ArrayData added, removed;
        for (size_t i = 0; i < db.size(); ++i) if (std::find(ka.begin(), ka.end(), kb[i]) == ka.end()) added.push_back(db[i].second);
        for (size_t i = 0; i < da.size(); ++i) if (std::find(kb.begin(), kb.end(), ka[i]) == kb.end()) removed.push_back(da[i].second);
        bool same = added.empty() && removed.empty();
        out = tMap({{tS("same"), Value::boolean_(same)}, {tS("added"), tArr(std::move(added))}, {tS("removed"), tArr(std::move(removed))}});
        return true;
    }
    if (fn == "container.checksum" && n == 1) { out = tS(extra2::sha256Hex(json::encode(Value::makeArray(tableRowsOf(name))))); return true; }
    if (fn == "container.contains" && n == 3) {
        std::vector<Value> v;
        if (!tableColumnValues(name, a[1].toDisplayString(), v)) return false; // ليس عموداً: السلوك الأصلي
        bool hit = false;
        for (auto& x : v) if (valuesEqual(x, a[2])) { hit = true; break; }
        out = Value::boolean_(hit);
        return true;
    }

    // ===== حقول: push/pop على rows · getOr · إعادة تسمية عمود =====
    auto ownVar = [&](const std::string& k) { auto e = containers.find(name); return e != containers.end() && e->second && e->second->values.count(k); };
    if (fn == "container.push" && n == 3 && a[1].toDisplayString() == "rows" && !ownVar("rows")) {
        Value row = a[2];
        if (row.type == Value::Type::MAP) row = tableOrderRecord(name, row, line);
        else if (!tIsArr(row)) throw diagErr(diag::Code::E0004_InvalidType, line, "'container.push' على rows يتوقّع صفاً (مصفوفة خلايا أو قاموساً)");
        else row = Value::makeArray(std::make_shared<ArrayData>(*row.array));
        auto rows = tableRowsOf(name);
        rows->push_back(row);
        out = Value::num(static_cast<double>(rows->size()));
        return true;
    }
    if (fn == "container.pop" && n == 2 && a[1].toDisplayString() == "rows" && !ownVar("rows")) {
        auto rows = tableRowsOf(name);
        if (rows->empty()) { out = Value::nil(); return true; }
        out = rows->back();
        rows->pop_back();
        return true;
    }
    if (fn == "container.getOr" && n == 3 && !ownVar(a[1].toDisplayString())) {
        Value v;
        out = tableVirtualGet(name, a[1].toDisplayString(), v) ? v : a[2];
        return true;
    }
    if (fn == "container.renameField" && n == 3 && !ownVar(a[1].toDisplayString())) {
        std::vector<std::string> hdr;
        if (!tableHeader(name, hdr)) return false;
        std::string from = a[1].toDisplayString(), to = a[2].toDisplayString();
        size_t c = std::find(hdr.begin(), hdr.end(), from) - hdr.begin();
        if (c >= hdr.size() || to.empty() || std::find(hdr.begin(), hdr.end(), to) != hdr.end()) { out = Value::boolean_(false); return true; }
        (*(*tableRowsOf(name))[0].array)[c] = tS(to);
        out = Value::boolean_(true);
        return true;
    }
    return false;
}

} // namespace rin
