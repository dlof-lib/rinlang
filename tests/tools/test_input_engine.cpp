// tests/tools/test_input_engine.cpp
// اختبار وحدة لمحرّك نماذج الإدخال (rin_input.h): المترجم (Program/disassemble) والمنفِّذ (معاملة + جولات المُدقِّق)
// بمضيف وهمي — بلا stdin ولا Interpreter حقيقي. البناء: g++ -std=c++17 -I app/src/main/cpp test_input_engine.cpp <كائنات المحرّك> -lz -ldl
#include "rin_input.h"
#include <cstdio>
#include <deque>
#include <iostream>

namespace rin { RinError diagErr(diag::Code code, int line, std::string message); }   // معرَّفة في rin_interpreter.cpp
using namespace rin;
using namespace rin::inputx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static Value mapOf(std::initializer_list<std::pair<const char*, Value>> kv) {
    auto m = std::make_shared<MapData>();
    for (auto& p : kv) m->push_back({Value::string(p.first), p.second});
    return Value::makeMap(m);
}
static Value* slot(Value& map, const std::string& k) {
    for (auto& kv : *map.map) if (kv.first.str == k) return &kv.second;
    return nullptr;
}

// مضيف وهمي: الأسئلة تُجاب من طابور؛ نص "!cancel" يرمي خطأ إلغاء؛ oop.set يكتب في القاموس مباشرة.
struct Fake {
    std::deque<std::string> answers;
    std::vector<std::string> asked;
    Value verdictNeedsEqual;      // مُدقِّق النموذج: يقبل فقط إن تساوى الحقلان a و b
    int checks = 0;
    Host host() {
        Host h;
        h.call = [this](const char* fn, std::vector<Value> a, int line) -> Value {
            std::string f = fn;
            if (f == "oop.set") {
                Value* s = slot(a[0], a[1].str);
                if (s) *s = a[2]; else a[0].map->push_back({a[1], a[2]});
                return a[2];
            }
            if (f == "oop.get") { Value* s = slot(a[0], a[1].str); return s ? *s : Value::nil(); }
            if (f == "input" || f == "inputNumber" || f == "confirm") {
                asked.push_back(a[0].str);
                if (answers.empty()) throw diagErr(diag::Code::E0035_RuntimeError, line, "eof");
                std::string ans = answers.front(); answers.pop_front();
                if (ans == "!cancel") throw diagErr(diag::Code::E0035_RuntimeError, line, "cancel");
                Value v = f == "inputNumber" ? Value::num(std::stod(ans)) : Value::string(ans);
                Value* s = slot(a[a.size() - 2], a.back().str);     // (label, target, key)
                if (s) *s = v; else a[a.size() - 2].map->push_back({a.back(), v});
                return v;
            }
            throw diagErr(diag::Code::E0035_RuntimeError, line, "fake: unexpected native " + f);
        };
        h.hasContainer = [](const std::string&) { return false; };
        h.isCallable = [](const Value&) { return false; };
        h.invoke = [this](const Value&, std::vector<Value> a, int) -> Value {
            ++checks;
            Value* x = slot(a[0], "a"); Value* y = slot(a[0], "b");
            if (x && y && x->str == y->str) return Value::boolean_(true);
            return Value::string("a != b");
        };
        h.fail = [](int line, const std::string& m, const std::string&) { return diagErr(diag::Code::E0035_RuntimeError, line, m); };
        h.atEof = [] { return false; };
        return h;
    }
};

static std::shared_ptr<FormPlan> flat(const Value& target, const char* prompt, std::initializer_list<std::pair<const char*, FieldKind>> fs) {
    auto p = std::make_shared<FormPlan>();
    p->kind = TargetKind::Map; p->target = target; p->prompt = prompt;
    for (auto& f : fs) { FieldPlan fp; fp.name = f.first; fp.kind = f.second; p->fields.push_back(fp); }
    return p;
}

int main() {
    // ---- 1) المترجم: تعليمات مسطّحة + نطاقات متداخلة + Commit + Check ----
    {
        Value inner = mapOf({{"city", Value::string("R")}});
        Value outer = mapOf({{"name", Value::string("A")}});
        auto root = flat(outer, "U> ", {{"name", FieldKind::Text}});
        FieldPlan nested; nested.name = "home"; nested.kind = FieldKind::NestedObject; nested.commitNested = true;
        nested.nested = flat(inner, "U> home.", {{"city", FieldKind::Text}, {"zip", FieldKind::Number}});
        root->fields.push_back(nested);
        Request rq; rq.root = root; rq.hasCheck = true; rq.check = Value::string("fn"); rq.line = 7;
        Program p = FormCompiler().compile(rq);
        CHECK(p.scopes.size() == 2);
        CHECK(p.askCount() == 3);
        CHECK(p.hasCheck && p.line == 7);
        std::string d = p.disassemble();
        std::cout << d;
        CHECK(d.find("ASK.TEXT") != std::string::npos);
        CHECK(d.find("ASK.NUM") != std::string::npos);
        CHECK(d.find("COMMIT") != std::string::npos);
        CHECK(d.find("home <- s1") != std::string::npos);
        CHECK(d.find("path=home") != std::string::npos);
        CHECK(d.find("check=yes") != std::string::npos);
    }
    // ---- 2) المنفِّذ: نجاح عادي ----
    {
        Fake fk; fk.answers = {"x", "5"};
        Value t = mapOf({{"a", Value::string("old")}, {"n", Value::num(1)}});
        Request rq; rq.root = flat(t, "F> ", {{"a", FieldKind::Text}, {"n", FieldKind::Number}});
        Program p = FormCompiler().compile(rq);
        Host h = fk.host();
        Value r = FormExecutor(h).run(p);
        CHECK(slot(r, "a")->str == "x" && slot(r, "n")->number == 5);
        CHECK(fk.asked.size() == 2 && fk.asked[0] == "F> a: ");
    }
    // ---- 3) المنفِّذ: الإلغاء في المنتصف يعيد الهدف (بما فيه حقل لم يكن موجوداً) ----
    {
        Fake fk; fk.answers = {"new", "!cancel"};
        Value t = mapOf({{"a", Value::string("old")}});
        Request rq; rq.root = flat(t, "F> ", {{"a", FieldKind::Text}, {"extra", FieldKind::Text}, {"z", FieldKind::Text}});
        Program p = FormCompiler().compile(rq);
        Host h = fk.host();
        bool threw = false;
        try { FormExecutor(h).run(p); } catch (const RinError&) { threw = true; }
        CHECK(threw);
        CHECK(t.map->size() == 1 && slot(t, "a")->str == "old");
    }
    // ---- 4) مُدقِّق النموذج: جولة مرفوضة ثم مقبولة، ثم استنفاد الجولات مع تراجع ----
    {
        Fake fk; fk.answers = {"p", "q", "p", "p"};
        Value t = mapOf({{"a", Value::string("")}, {"b", Value::string("")}});
        Request rq; rq.root = flat(t, "S> ", {{"a", FieldKind::Text}, {"b", FieldKind::Text}});
        rq.hasCheck = true; rq.check = Value::string("v");
        Program p = FormCompiler().compile(rq);
        Host h = fk.host();
        Value r = FormExecutor(h).run(p);
        CHECK(fk.checks == 2);
        CHECK(slot(r, "a")->str == "p" && slot(r, "b")->str == "p");
        CHECK(fk.asked.size() == 4 && fk.asked[2].find("[a != b]") == 0);   // سبب الرفض أمام أول سؤال في الجولة التالية

        Fake fk2; for (int i = 0; i < 20; ++i) fk2.answers.push_back(i % 2 ? "x" : "y");
        Value t2 = mapOf({{"a", Value::string("orig")}, {"b", Value::string("orig")}});
        Request rq2; rq2.root = flat(t2, "S> ", {{"a", FieldKind::Text}, {"b", FieldKind::Text}});
        rq2.hasCheck = true; rq2.check = Value::string("v");
        Program p2 = FormCompiler().compile(rq2);
        Host h2 = fk2.host();
        bool threw = false;
        try { FormExecutor(h2).run(p2); } catch (const RinError&) { threw = true; }
        CHECK(threw && fk2.checks == FormExecutor::kMaxRounds);
        CHECK(slot(t2, "a")->str == "orig" && slot(t2, "b")->str == "orig");
    }
    std::printf(failures ? "\ninput_engine: %d FAILED\n" : "\ninput_engine: all passed\n", failures);
    return failures ? 1 : 0;
}
