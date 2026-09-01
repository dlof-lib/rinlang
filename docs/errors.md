# الأخطاء (Errors)

Rin يعتمد نظام تشخيص موحّد (diagnostics) يُبلّغ عن كود الخطأ، الرسالة، موقع
المصدر (سطر/عمود)، وسياق مفيد عند توفّره. للتفاصيل المعمارية الكاملة راجع
[`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md).

يظهر هذا النظام عند أخطاء في أي مفهوم من مفاهيم اللغة: متغيّر غير معرَّف
(انظر [`variables.md`](./variables.md))، شرط بصياغة خاطئة (انظر
[`control-flow.md`](./control-flow.md))، دالة غير معروفة أو عدد وسائط خاطئ
(انظر [`functions.md`](./functions.md))، أو مخالفة داخل [حاوية](./containers.md).

## انظر أيضاً

- [`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) — الشرح التقني الكامل.
- [`language-reference.md`](./language-reference.md) — خريطة كل المفاهيم.
