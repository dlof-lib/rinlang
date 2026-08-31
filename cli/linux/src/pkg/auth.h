// cli/linux/src/pkg/auth.h
// ============================================================================
// RinPM :: Authentication — قسم 16 من المواصفة. Token-based فقط: لا كلمة مرور
// تُخزَّن أبداً على القرص، لا هنا ولا في الذاكرة بعد نهاية عملية login. الرمز
// المميز (token) يُخزَّن في <cache-root>/config/credentials بصلاحيات 0600
// (قراءة/كتابة للمالك فقط)، وهو أفضل ما يوفره نظام ملفات محلي بلا Keychain/DPAPI
// حقيقي متاح من C++ قياسي دون مكتبات خاصة بكل نظام تشغيل.
// ============================================================================
#pragma once
#include <string>
#include <optional>
#include "cache.h"

namespace rinpm::auth {

struct Session {
    std::string username;
    std::string registryUrl; // فارغ = registry:local الافتراضي
    std::string token;
};

// يخزّن الجلسة (يُستدعى بعد أن يحصل `rin pkg login` فعلياً على token من الخادم/المصدر).
void saveSession(const Cache& cache, const Session& s);
// يقرأ الجلسة الحالية إن وُجدت (nullopt إن لم يسجّل المستخدم دخوله من قبل).
std::optional<Session> loadSession(const Cache& cache);
// يحذف بيانات الاعتماد المخزَّنة (`rin pkg logout`).
void clearSession(const Cache& cache);

} // namespace rinpm::auth
