# CHANGES_ENV — دعم ملف `.env`
- جديد: `rin_env.cpp` (محلّل dotenv + `Env.*`)، `docs/env.md`، `tests/verification/env_basic.rin|.expected`.
- `rin_interpreter.h/.cpp`: تصريح `registerNativesEnv` + استدعاؤه + `#include "rin_env.cpp"`.
- لم تُعدَّل ملفات Kotlin ولا `rinc.cpp` (لا تلوين لـ `Env` بعد).
