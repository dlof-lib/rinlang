# Rin 1.0 — المكتبات والحزم وجسر C++ وJSON واختبارات rintest

> ملف المصدر: `app/src/main/cpp/rin_extra_natives4.cpp` (مُضمَّن تلقائياً في `rin_interpreter.cpp`).
> الاختبار الشامل: `rin tests/rintests.rin` → `ALL PASSED`.
>
> **قاعدة مهمة:** دوال Rin تتطلب عدداً مطابقاً تماماً من الوسائط، فمرّر `nil` للوسيط الاختياري
> (مثل `cpp.eval("1+1", nil)`). كلمة `merge` محجوزة لذلك اسمها `json.mergeDeep`.

## `json.*`
| الدالة | الوصف |
|---|---|
| `parse(s)` / `tryParse(s)` / `valid(s)` | تحليل صارم (يرمي) / `{ok,value,error}` / فحص |
| `stringify(v, indent, sortKeys)` / `pretty(v, indent)` / `minify(s)` / `canonical(v)` | تنسيق؛ `canonical` = مفاتيح مرتبة بلا فراغات (للتجزئة) |
| `get(v, path, default)` / `query(v, path)` / `has` | مسارات `a.b[0].c` و`items[*].name` |
| `set(v, path, x)` / `remove(v, path)` | تُرجع نسخة جديدة ولا تعدّل الأصل |
| `mergeDeep(a, b, ...)` / `flatten(v, sep)` / `unflatten(m, sep)` | دمج عميق وتسطيح |
| `diff(a, b)` / `patch(v, ops)` | عمليات RFC 6902 (`add/remove/replace`) |
| `validate(v, schema)` | `{valid, errors:[{path,message}]}`: type/required/properties/items/enum/const/min/max/pattern/uniqueItems/additionalProperties |
| `type(v)` / `equals(a,b)` / `readFile(p)` / `writeFile(p, v, indent)` | مساعدات |

## `semver.*`
`parse valid compare gt lt eq diff sort(list, desc) bump(v, major|minor|patch|prerelease, id) satisfies(v, range) maxSatisfying(list, range) validRange`
— النطاقات: `^ ~ >= <= > < =` و`1.2.x` و`*` و`||` والمسافات.

## `pkg.*` — أدوات بناء الحزم
| الدالة | الوصف |
|---|---|
| `parseManifest(toml)` / `readManifest(path)` / `manifest(map)` | قراءة/كتابة `rin.toml` |
| `validateManifest(m)` | `{valid, errors, warnings}` |
| `depOrder(graph)` | ترتيب تحميل (تبعيات أولاً) + كشف الدورات والمفقود |
| `api(src)` / `apiFile(path)` | استخراج الدوال/الأصناف مع تعليقات `///` |
| `checksum(dir)` | بصمة SHA-256 ثابتة لمجلد الحزمة |
| `scaffold(dir, name, {kind:"lib"\|"app", cpp:true, ...})` | ينشئ مشروع حزمة كاملاً (rin.toml, src, tests, README) |

## مكتبة `packkit` (`@import "packkit";`)
نظام حزم داخل اللغة: `pk_define(name, version, {deps, exports, config, init})`، `pk_use(name)` /
`pk_require(name, range)` (يحمّل التبعيات مرة واحدة ويكشف الدورات ويختار أحدث إصدار متوافق)،
`pk_loadAll`, `pk_order`, `pk_list`, `pk_info`, `pk_setConfig`, إضافات: `pk_on/pk_off/pk_emit`، وأدوات
`pk_memoize` و`pk_deprecated` و`pk_implements` و`pk_defineFromManifest`.

## جسر C++ — `cpp.*`
**مقفل افتراضياً.** يُفعَّل بـ `rin --allow-native file.rin` أو `RIN_ALLOW_NATIVE=1` (قرار من يشغّل البرنامج، لا يستطيع كود مستورَد تفعيله).
لينكس/macOS فقط؛ على أندرويد/ويندوز/WASM تُرجع `{ok:false,error}`.

```rin
let l = cpp.lib("#include \"rin_abi.h\"\nRIN_FN(sumAll){double t=0;for(auto&v:args.arr)t+=v.num;return rin::Json::number(t);}", nil);
print cpp.call(l.handle, "sumAll", [1, 2, 3.5]).value;     // 6.5
print cpp.callNum(l.handle, "hyp", [3, 4], "double").value; // دالة extern "C" رقمية
print cpp.eval("std::sqrt(2.0)", nil).value;
```
`info() header() enabled() compile(src,opts) run(src,{stdin,args,timeout}) eval(expr,opts) lib load has call callNum unload exec(src,fn,args,opts)`
— خيارات: `compiler std opt flags includes libs timeout force`. النتائج تُخزَّن مؤقتاً بحسب بصمة المصدر.

## `rintest` (`@import "rintest";`)
```rin
rt_describe("حساب", fun() {
    rt_test("جمع", fun() { rt_expect(1 + 1).toBe(2); });
    rt_each("جدول", [[1,2,3],[2,2,4]], fun(a, b, c) { rt_expect(a + b).toBe(c); });
});
rt_done();   // ملخص + يفشل الملف عند وجود إخفاق (يعمل مع `rin test`)
```
Matchers: `toBe toEqual toBeTrue toBeFalse toBeNil toBeTruthy toBeFalsy toContain toHaveLength toBeGreaterThan toBeLessThan toBeCloseTo(x,eps) toMatch toHaveKey toBeType toThrow(fragment|nil) toThrowAny`.
العكس: `rt_expectNot(x)`. أخرى: `rt_expectThrows rt_skip rt_beforeEach rt_afterEach rt_bench rt_quiet rt_reset rt_stats`.

## `lang.*` — فحص المصدر
- `lang.check(src)` → `{ok, line, message}`: يمرّر المصدر على نفس Lexer/Parser الحقيقيين **دون تنفيذه**.
- `lang.isBuiltin(name)` → هل الاسم دالة مدمجة في المحرك (تعريف `fun` بنفس اسمها يُرفض بالخطأ E0002، مثل `lerp` و`mean` و`clamp`).

## `wesscode` — مساعد إنشاء المكتبات (`@import "wesscode";`)
يبني مكتبة/حزمة Rin كاملة من الكود، يفحصها بالمحلّل الحقيقي قبل أي كتابة، ثم يكتبها على القرص.

```rin
let lib = wc_new("mathx", "0.1.0", "دوال رياضية");
wc_fn(lib, "twice", ["x"], "return x * 2;", "يضاعف الرقم");
wc_const(lib, "TAU", "6.283185307179586", "ضعف باي");
wc_class(lib, "Box", {v: "0"}, "صندوق");
wc_method(lib, "Box", "get", [], "return self.v;");
wc_dep(lib, "other", "^1.0.0");
wc_test(lib, "twice يضاعف", "twice(21)", 42);
wc_cpp(lib, "fast_sum", "double t=0; for(auto&v:args.arr) t+=v.num; return rin::Json::number(t);");
wc_testNative(lib, "native", "fast_sum([1,2,3.5])", 6.5);   // يُتخطّى تلقائياً بدون --allow-native
print wc_check(lib);                    // {ok, errors, warnings}
wc_write(lib, "out/mathx");             // rin.toml + src/lib.rin + tests + docs/API.md + README + native/*.cpp
wc_writeOg(lib, ".");                   // ./lib/mathx.og.rin ليعمل @import "mathx"
```
| الفئة | الدوال |
|---|---|
| بناء | `wc_new wc_meta wc_dep wc_fn wc_const wc_class wc_method wc_test wc_testNative wc_raw wc_cpp wc_fromSource` |
| توليد | `wc_render wc_renderTests wc_manifest wc_docs wc_cppSource wc_lit` |
| فحص | `wc_check wc_validIdent wc_validFnName wc_validName` (يكشف الكلمات المحجوزة والتعارض مع المدمجة والكود الفاسد) |
| إخراج | `wc_write wc_writeOg` |
| تطوير | `wc_bump wc_stats wc_apiOf` |
| قوالب | `wc_template(kind, name)` مع `math strings collections validate`، و`wc_templates()` |

الاختبار: `rin tests/wesscode_tests.rin` (21 اختباراً).
