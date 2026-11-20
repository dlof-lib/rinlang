// ============================================================================
//  rin_lock.cpp — main.lock : خزنة الأسرار المشفَّرة الخاصة بـ Rin (مفاتيح API وكلمات السر والتوكنات)
// ----------------------------------------------------------------------------
//  يُضمَّن في نهاية rin_interpreter.cpp (مثل rin_rintwist.cpp). الشرح الكامل: docs/main-lock.md
//
//    main.lock(5);                                   ← تفعيل الخزنة بمستوى قوة 1..10 (يحدّد كلفة اشتقاق المفتاح PBKDF2)
//    main.lock(5, "عبارة المرور");                   ← مع عبارة مرور (وإلا: متغيّر البيئة RIN_LOCK_KEY، وإلا مفتاح جلسة عشوائي)
//    let <<main.lock["API_KEY"]>> = "sk-live-…";     ← ختم سرّ (تشفير AES-256-GCM، nonce عشوائي، AAD = الاسم)
//    let key = <<main.lock["API_KEY"]>>;             ← فتح السرّ (فكّ التشفير عند الطلب فقط)
//    let names = <<main.lock[]>>;                    ← أسماء الأسرار (بلا قيم أبداً)
//    let <<main.lock[]>>;                            ← تقرير الخزنة على الشاشة (أسماء + أطوال + مستوى، بلا قيم)
//
//  تفكّك الصيغ في المحلّل إلى نداءات natives أدناه، فتعمل على المفسّر وRinVM بلا تغيير فيهما.
//  الأسرار لا تُحفَظ في الذاكرة كنص صريح: كل سرّ نصٌّ مشفَّر (nonce‖ciphertext‖tag) ومفتاح الخزنة مشتقّ من عبارة المرور.
//  صدق الحدود: لا تحمي من مهاجم يقرأ ذاكرة العملية وهي تعمل (المفتاح في الذاكرة)، ولا من عبارة مرور ضعيفة؛ فائدتها منع
//  التسرّب بالطباعة/السجلات/الحفظ/الإرسال العرضي ومنع حفظ المفاتيح نصاً في الملفات (save/load مشفَّران ومصدّقان).
// ============================================================================
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rin {
namespace {

struct LockEntry { std::string name; std::string nonce; std::string sealed; };   // sealed = ciphertext‖tag

struct LockVault {
    bool armed = false;
    int level = 5;
    uint32_t iters = 0;
    std::string salt;            // 16 بايت
    std::string pass;            // عبارة المرور (أو مفتاح الجلسة العشوائي) — لاشتقاق المفتاح ثانيةً عند load()
    std::string source;          // "argument" | "env" | "session"
    std::string key;             // 32 بايت مشتقّة
    std::vector<LockEntry> items;
    std::vector<std::string> revealed;   // قيم فُتحت (لـ mask)
};

LockVault& vault() { static LockVault v; return v; }

uint32_t itersForLevel(int level) {
    static const uint32_t t[] = {0, 10000, 25000, 50000, 100000, 200000, 400000, 800000, 1600000, 3200000, 6400000};
    return t[level];
}

std::string hexOf(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string o; o.reserve(s.size() * 2);
    for (unsigned char c : s) { o += d[c >> 4]; o += d[c & 15]; }
    return o;
}

bool unhex(const std::string& h, std::string& out) {
    if (h.size() % 2) return false;
    out.clear();
    auto v = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    for (size_t i = 0; i < h.size(); i += 2) { int a = v(h[i]), b = v(h[i + 1]); if (a < 0 || b < 0) return false; out += (char)(a * 16 + b); }
    return true;
}

std::string randomBytes(size_t n) {
    std::string r(n, '\0');
    rincrypto::osRandomBytes(reinterpret_cast<uint8_t*>(&r[0]), n);
    return r;
}

LockEntry* findItem(LockVault& v, const std::string& name) {
    for (auto& e : v.items) if (e.name == name) return &e;
    return nullptr;
}

bool validName(const std::string& n) {
    if (n.empty() || n.size() > 128) return false;
    for (unsigned char c : n) if (c < 0x20 || c == '\t' || c == '\n' || c == '\r') return false;
    return true;
}

Value newMapValue() { return Value::makeMap(std::make_shared<MapData>()); }
void put(Value& m, const char* k, Value v) { m.map->append(Value::string(k), std::move(v)); }

} // namespace

void Interpreter::registerNativesLock() {
    using Args = std::vector<Value>;

    auto needArmed = [this](const char* fn, int line) -> LockVault& {
        auto& v = vault();
        if (!v.armed)
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "': the vault is locked (not armed yet)",
                                "call `main.lock(level);` first, e.g. `main.lock(5);` or `main.lock(5, \"passphrase\");`");
        return v;
    };
    auto sealInto = [](LockVault& v, const std::string& name, const std::string& value) {
        LockEntry e;
        e.name = name;
        e.nonce = randomBytes(12);
        if (!rincrypto::gcmSeal(v.key, e.nonce, value, name, e.sealed)) return false;
        if (auto* old = findItem(v, name)) *old = std::move(e); else v.items.push_back(std::move(e));
        return true;
    };
    auto openItem = [this](LockVault& v, const LockEntry& e, int line) -> std::string {
        std::string out;
        if (!rincrypto::gcmOpen(v.key, e.nonce, e.sealed, e.name, out))
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock: authentication failed for secret `" + e.name + "`",
                                "wrong passphrase or the stored data was tampered with");
        return out;
    };
    auto derive = [](LockVault& v) { v.key = rincrypto::pbkdf2Sha256(v.pass, v.salt, v.iters, 32); };

    // main.lock(level?, passphrase?) -> status map
    natives["main.lock"] = [this, derive](Args& a, int line) -> Value {
        expectArgsRange("main.lock", a, 0, 2, line);
        auto& v = vault();
        if (v.armed)
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock: the vault is already armed",
                                "call `main.lock.reset();` first if you really want to re-arm it with another level/passphrase");
        int level = 5;
        if (!a.empty()) {
            double d = asNumber(a[0], "main.lock", line);
            if (d != static_cast<int>(d) || d < 1 || d > 10)
                throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock: level must be a whole number from 1 to 10", "level 5 is the default balance; 8+ is slow on purpose");
            level = static_cast<int>(d);
        }
        if (a.size() > 1) {
            v.pass = asString(a[1], "main.lock", line);
            if (v.pass.empty()) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock: the passphrase is empty", "pass a non-empty string or omit it");
            v.source = "argument";
        } else if (const char* env = std::getenv("RIN_LOCK_KEY"); env && *env) {
            v.pass = env; v.source = "env";
        } else {
            v.pass = randomBytes(32); v.source = "session";
        }
        v.level = level;
        v.iters = itersForLevel(level);
        v.salt = randomBytes(16);
        derive(v);
        v.armed = true;
        Value m = newMapValue();
        put(m, "armed", Value::boolean_(true));
        put(m, "level", Value::num(level));
        put(m, "iterations", Value::num(v.iters));
        put(m, "source", Value::string(v.source));
        return m;
    };

    // main.lock.set(name, value) -> true
    natives["main.lock.set"] = [this, needArmed, sealInto](Args& a, int line) -> Value {
        expectArgsRange("main.lock.set", a, 2, 2, line);
        auto& v = needArmed("main.lock.set", line);
        std::string name = asString(a[0], "main.lock.set", line);
        if (!validName(name)) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.set: invalid secret name", "use a non-empty name (max 128 chars) without control characters");
        std::string value = a[1].type == Value::Type::STRING ? a[1].str : a[1].toDisplayString();
        if (!sealInto(v, name, value)) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.set: encryption failed", "internal error");
        v.revealed.push_back(value);
        return Value::boolean_(true);
    };

    // main.lock.get(name, default?) -> string (decrypted on demand)
    natives["main.lock.get"] = [this, needArmed, openItem](Args& a, int line) -> Value {
        expectArgsRange("main.lock.get", a, 1, 2, line);
        auto& v = needArmed("main.lock.get", line);
        std::string name = asString(a[0], "main.lock.get", line);
        auto* e = findItem(v, name);
        if (!e) {
            if (a.size() > 1) return a[1];
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock: no secret named `" + name + "`",
                                "seal it first: `let <<main.lock[\"" + name + "\"]>> = \"…\";` or `main.lock.env(\"" + name + "\");`");
        }
        std::string s = openItem(v, *e, line);
        bool known = false;
        for (auto& r : v.revealed) if (r == s) { known = true; break; }
        if (!known) v.revealed.push_back(s);
        return Value::string(s);
    };

    natives["main.lock.has"] = [this, needArmed](Args& a, int line) -> Value {
        expectArgsRange("main.lock.has", a, 1, 1, line);
        return Value::boolean_(findItem(needArmed("main.lock.has", line), asString(a[0], "main.lock.has", line)) != nullptr);
    };

    // main.lock.names() -> ["A","B"]  (بلا قيم)
    natives["main.lock.names"] = [this, needArmed](Args& a, int line) -> Value {
        expectArgsRange("main.lock.names", a, 0, 0, line);
        auto& v = needArmed("main.lock.names", line);
        auto arr = std::make_shared<ArrayData>();
        for (auto& e : v.items) arr->push_back(Value::string(e.name));
        return Value::makeArray(arr);
    };

    natives["main.lock.remove"] = [this, needArmed](Args& a, int line) -> Value {
        expectArgsRange("main.lock.remove", a, 1, 1, line);
        auto& v = needArmed("main.lock.remove", line);
        std::string name = asString(a[0], "main.lock.remove", line);
        for (size_t i = 0; i < v.items.size(); ++i)
            if (v.items[i].name == name) { v.items.erase(v.items.begin() + i); return Value::boolean_(true); }
        return Value::boolean_(false);
    };

    // main.lock.env(name, envVar?) : يختم متغيّر بيئة كسرّ (المفتاح لا يُكتب في الشيفرة أصلاً)
    natives["main.lock.env"] = [this, needArmed, sealInto](Args& a, int line) -> Value {
        expectArgsRange("main.lock.env", a, 1, 2, line);
        auto& v = needArmed("main.lock.env", line);
        std::string name = asString(a[0], "main.lock.env", line);
        std::string var = a.size() > 1 ? asString(a[1], "main.lock.env", line) : name;
        const char* e = std::getenv(var.c_str());
        if (!e) return Value::boolean_(false);
        sealInto(v, name, e);
        v.revealed.push_back(e);
        return Value::boolean_(true);
    };

    // main.lock.mask(text) : يستبدل كل سرّ فُتح/خُتم في هذه الجلسة بـ ••••  (للسجلات والطباعة)
    natives["main.lock.mask"] = [this](Args& a, int line) -> Value {
        expectArgsRange("main.lock.mask", a, 1, 1, line);
        std::string s = a[0].type == Value::Type::STRING ? a[0].str : a[0].toDisplayString();
        for (auto& r : vault().revealed) {
            if (r.empty()) continue;
            size_t p = 0;
            while ((p = s.find(r, p)) != std::string::npos) { s.replace(p, r.size(), "••••"); p += 4; }
        }
        return Value::string(s);
    };

    // main.lock.seal(text) -> "rinlock1:<nonce>:<ct>"  ·  main.lock.open(token) -> text  (رموز محمولة يمكن حفظها خارجاً)
    natives["main.lock.seal"] = [this, needArmed](Args& a, int line) -> Value {
        expectArgsRange("main.lock.seal", a, 1, 1, line);
        auto& v = needArmed("main.lock.seal", line);
        std::string pt = asString(a[0], "main.lock.seal", line), nonce = randomBytes(12), out;
        if (!rincrypto::gcmSeal(v.key, nonce, pt, "rinlock1", out)) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.seal: encryption failed", "internal error");
        return Value::string("rinlock1:" + hexOf(nonce) + ":" + hexOf(out));
    };
    natives["main.lock.open"] = [this, needArmed](Args& a, int line) -> Value {
        expectArgsRange("main.lock.open", a, 1, 1, line);
        auto& v = needArmed("main.lock.open", line);
        std::string tok = asString(a[0], "main.lock.open", line);
        size_t c1 = tok.find(':'), c2 = tok.find(':', c1 == std::string::npos ? 0 : c1 + 1);
        std::string nonce, ct, out;
        if (tok.compare(0, 9, "rinlock1:") != 0 || c2 == std::string::npos || !unhex(tok.substr(c1 + 1, c2 - c1 - 1), nonce) || !unhex(tok.substr(c2 + 1), ct) ||
            !rincrypto::gcmOpen(v.key, nonce, ct, "rinlock1", out))
            return Value::nil();   // رمز تالف أو مفتاح خاطئ
        v.revealed.push_back(out);
        return Value::string(out);
    };

    // main.lock.store(path) / main.lock.restore(path) : ملف مشفَّر ومصدَّق؛ يتطلّب عبارة مرور (لا مفتاح جلسة)
    natives["main.lock.store"] = [this, needArmed](Args& a, int line) -> Value {
        expectArgsRange("main.lock.store", a, 1, 1, line);
        auto& v = needArmed("main.lock.store", line);
        if (v.source == "session")
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.store: the vault key is a random session key",
                                "arm with a passphrase (`main.lock(5, \"…\")`) or set RIN_LOCK_KEY, otherwise the file could never be opened again");
        std::ofstream f(asString(a[0], "main.lock.store", line), std::ios::binary | std::ios::trunc);
        if (!f) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.store: cannot write the file", "check the path/permissions");
        f << "RINLOCK1\n" << v.level << "\n" << hexOf(v.salt) << "\n";
        // عنصر تحقّق: يفشل فتحه فوراً عند load() بعبارة خاطئة بدل أن يتأخّر الخطأ إلى أول get
        std::string vn = randomBytes(12), vct;
        rincrypto::gcmSeal(v.key, vn, "rinlock-ok", "__check__", vct);
        f << "__check__\t" << hexOf(vn) << "\t" << hexOf(vct) << "\n";
        for (auto& e : v.items) f << hexOf(e.name) << "\t" << hexOf(e.nonce) << "\t" << hexOf(e.sealed) << "\n";
        return Value::num(static_cast<double>(v.items.size()));
    };
    natives["main.lock.restore"] = [this, needArmed, derive](Args& a, int line) -> Value {
        expectArgsRange("main.lock.restore", a, 1, 1, line);
        auto& v = needArmed("main.lock.restore", line);
        if (v.source == "session")
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: the vault key is a random session key",
                                "arm with the same passphrase used when saving: `main.lock(5, \"…\")` or RIN_LOCK_KEY");
        std::ifstream f(asString(a[0], "main.lock.restore", line), std::ios::binary);
        if (!f) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: cannot read the file", "check the path");
        std::string magic, lvl, saltHex, salt;
        std::getline(f, magic); std::getline(f, lvl); std::getline(f, saltHex);
        int level = std::atoi(lvl.c_str());
        if (magic != "RINLOCK1" || level < 1 || level > 10 || !unhex(saltHex, salt) || salt.size() != 16)
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: not a valid .rinlock file", "it must have been written by main.lock.store");
        LockVault tmp = v;                       // لا نمسّ الخزنة الحيّة إلا بعد نجاح كل شيء
        tmp.level = level; tmp.iters = itersForLevel(level); tmp.salt = salt;
        derive(tmp);
        std::vector<LockEntry> loaded;
        std::string row;
        bool checked = false;
        while (std::getline(f, row)) {
            if (row.empty()) continue;
            size_t t1 = row.find('\t'), t2 = row.find('\t', t1 == std::string::npos ? 0 : t1 + 1);
            if (t1 == std::string::npos || t2 == std::string::npos) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: corrupt file", "a row is malformed");
            std::string nm = row.substr(0, t1), non, ct;
            if (!unhex(row.substr(t1 + 1, t2 - t1 - 1), non) || !unhex(row.substr(t2 + 1), ct))
                throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: corrupt file", "a row is not valid hex");
            if (nm == "__check__") {
                std::string out;
                if (!rincrypto::gcmOpen(tmp.key, non, ct, "__check__", out) || out != "rinlock-ok")
                    throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: wrong passphrase (or the file was tampered with)", "arm with the passphrase used at save time");
                checked = true; continue;
            }
            std::string name;
            if (!unhex(nm, name)) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: corrupt file", "a secret name is not valid hex");
            loaded.push_back({name, non, ct});
        }
        if (!checked) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "main.lock.restore: missing integrity check row", "the file is truncated or not from main.lock.store");
        // دمج: الأسرار المحمَّلة تحلّ محل الموجودة بالاسم نفسه؛ الحاليّة تُعاد تشفيرها بمفتاح الملف الجديد
        std::vector<LockEntry> merged = loaded;
        for (auto& e : v.items) {
            if (std::any_of(loaded.begin(), loaded.end(), [&](const LockEntry& x) { return x.name == e.name; })) continue;
            std::string pt;
            if (!rincrypto::gcmOpen(v.key, e.nonce, e.sealed, e.name, pt)) continue;
            LockEntry ne; ne.name = e.name; ne.nonce = randomBytes(12);
            rincrypto::gcmSeal(tmp.key, ne.nonce, pt, e.name, ne.sealed);
            merged.push_back(std::move(ne));
        }
        v.level = tmp.level; v.iters = tmp.iters; v.salt = tmp.salt; v.key = tmp.key; v.items = std::move(merged);
        return Value::num(static_cast<double>(loaded.size()));
    };

    // main.lock.status() -> {armed, level, iterations, source, count}
    natives["main.lock.status"] = [this](Args& a, int line) -> Value {
        expectArgsRange("main.lock.status", a, 0, 0, line);
        auto& v = vault();
        Value m = newMapValue();
        put(m, "armed", Value::boolean_(v.armed));
        put(m, "level", Value::num(v.armed ? v.level : 0));
        put(m, "iterations", Value::num(v.armed ? v.iters : 0));
        put(m, "source", Value::string(v.armed ? v.source : ""));
        put(m, "count", Value::num(static_cast<double>(v.items.size())));
        return m;
    };

    // main.lock.report() : يطبع جرداً بلا قيم (`let <<main.lock[]>>;`)
    natives["main.lock.report"] = [this, needArmed](Args& a, int line) -> Value {
        expectArgsRange("main.lock.report", a, 0, 0, line);
        auto& v = needArmed("main.lock.report", line);
        std::ostringstream o;
        o << "🔒 main.lock — level " << v.level << " (" << v.iters << " PBKDF2 rounds) · key source: " << v.source << " · " << v.items.size() << " secret(s)\n";
        for (auto& e : v.items) o << "   • " << e.name << "  (" << (e.sealed.size() >= 16 ? e.sealed.size() - 16 : 0) << " bytes, AES-256-GCM)\n";
        output << o.str();
        return Value::nil();
    };

    // main.lock.reset() : يمسح المفتاح والأسرار وقيم mask؛ بعدها يمكن إعادة التفعيل
    natives["main.lock.reset"] = [this](Args& a, int line) -> Value {
        expectArgsRange("main.lock.reset", a, 0, 0, line);
        auto& v = vault();
        std::fill(v.key.begin(), v.key.end(), '\0');
        std::fill(v.pass.begin(), v.pass.end(), '\0');
        v = LockVault{};
        return Value::boolean_(true);
    };
}

std::string lockRedact(const std::string& text) {
    auto& v = vault();
    if (!v.armed || v.revealed.empty()) return text;
    std::string s = text;
    for (auto& r : v.revealed) {
        if (r.size() < 4) continue;          // قيم قصيرة جداً تُفسد النص العادي إن أُخفيت
        size_t p = 0;
        while ((p = s.find(r, p)) != std::string::npos) { s.replace(p, r.size(), "••••"); p += 4; }
    }
    return s;
}

} // namespace rin
