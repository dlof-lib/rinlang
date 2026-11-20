# تعديلات Parser و Lexer المطلوبة

## 1. Lexer (`rin_lexer.cpp` / `rin_common.h`)

لا حاجة لرموز جديدة إلزامية. الاختصارات `msg!` `msg?` `msg+` `msg#` تُعالج في المحلّل بعد قراءة المعرف `msg` متبوعاً بعلامة.

اختياري: إن رغبت برموز مخصّصة أضف إلى `TokenType`:

```cpp
MSG_BANG,   // msg!
MSG_Q,      // msg?
MSG_PLUS,   // msg+
MSG_HASH,   // msg#
```

وفي `scanToken` بعد قراءة identifier `"msg"` انظر للعلامة التالية.

## 2. Parser — اختصارات `msg` / `tok`

في `primary()` أو بداية التعبير:

```cpp
// بعد قراءة IDENT "msg"
if (previous().lexeme == "msg") {
    std::string level = "info";
    if (match(TokenType::BANG)) level = "error";
    else if (match(TokenType::QUESTION)) level = "warning";
    else if (match(TokenType::PLUS)) level = "success";
    else if (match(TokenType::HASH)) level = "debug";  // أو TokenType::HASH إن وُجد

    ExprPtr body = expression();   // أو primary()
    ExprPtr meta = nullptr;
    ExprPtr code = nullptr;

    if (check(TokenType::LEFT_BRACE)) {
        meta = objectLiteral();    // أو parseMap()
    }
    // دعم: code "E1"
    if (check(TokenType::IDENTIFIER) && peek().lexeme == "code") {
        advance();
        code = primary();
    }

    // ابنِ CallExpr("message", [level, body, meta?, code?])
    auto call = std::make_shared<CallExpr>();
    call->callee = "message";
    call->args.push_back(makeStringLiteral(level));
    call->args.push_back(body);
    if (meta) call->args.push_back(meta);
    if (code) call->args.push_back(code);
    call->line = ...;
    return call;
}
```

نفس الفكرة لـ `tok` → `token(...)`.

لـ `msg ... with t`:

```cpp
// بعد بناء message call، إن جاء "with" ثم تعبير:
if (match_identifier("with")) {
    ExprPtr tok = primary();
    // لفّ النداء أو أضف حقل token عبر نداء مساعد __message_with_token(msg, tok)
}
```

## 3. تسجيل الـ natives في Interpreter

في نهاية `registerNatives()` (بجانب `registerNativesConditions` إلخ):

```cpp
registerNativesMessage(*this);     // rin_message_natives.cpp
registerNativesCapToken(*this);    // rin_cap_token_natives.cpp
registerNativesAudit(*this);       // rin_audit_natives.cpp
```

أضف الملفات إلى `CMakeLists.txt`:

```cmake
rin_message_natives.cpp
rin_cap_token_natives.cpp
rin_audit_natives.cpp
```

## 4. ملاحظات توافق

- `token(...)` لإنشاء توكن صلاحيات **لا يتعارض** مع `tokenList` / `tokenAt` (أسماء مختلفة).
- `msg` و`tok` كلمات سياقية فقط — لا تُضاف إلى قائمة الكلمات المحجوزة.
- افترض وجود `expectArgs` / `expectArgsRange` / `asNumber` / `diagErr` / `RinRuntimeError` كما في بقية الـ natives.
- إن كان `natives` خاصاً داخل Interpreter، اجعل دوال التسجيل `friend` أو مرّر مرجع `natives` صراحةً.
