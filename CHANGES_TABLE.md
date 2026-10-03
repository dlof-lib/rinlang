# CHANGES_TABLE — توسعة مفهوم الجدول (بلا دوال جديدة)

المبدأ: **لا اسم دالة جديد في اللغة.** كل شيء أدناه هو تعليم دوال موجودة أن تفهم `@table`.

## الملفات
- `app/src/main/cpp/rin_table.cpp` (جديد، يُضمَّن في نهاية `rin_interpreter.cpp` مثل `rin_extra_natives*.cpp`): `tableNative` (الخطّاف الموحَّد)، `tableExport`، `tableLoadCsv`، `tableAppendRecords`، `tableOrderRecord`، `tableColumnValues`، `tableCopy/Move/Forget`.
- `rin_interpreter.h` — تصريحات الأعضاء الجديدة.
- `rin_interpreter.cpp` — أعمدة كحقول وكتابة `body`/`columns` في `tableVirtual*`؛ `row` بقاموس يستعمل `tableOrderRecord`؛ `save format=csv|json|md|html|txt`؛ خطّافات `insertDoc updateDoc deleteDoc findDoc queryDocs queryOneDoc docIds allDocs countDocs container.renameField`؛ `destroyContainer` ينظّف حالة الجدول.
- `rin_extra_natives*.cpp` — سطر خطّاف في أول: `container.sum avg min max distinct countBy pluck groupBy sortBy top paginate query search stats toRows toCsv exportCsv toJson fromJson exportToFile importFromFile mergeFrom diff equals checksum contains push pop getOr`؛ و`clone/rename/remove` تنسخ/تنقل/تمسح صفوف الجدول ونمطه (كانت تُفقد).

## تغيير سلوك واحد مقصود
`container.set(t, "columns"|"body", مصفوفة)` كان `E0004` دائماً (للقراءة فقط)؛ صار مسموحاً. بقي `E0004` لغير المصفوفات. عُدّل سطر واحد في `table_bridge.rin` و`table_sql.rin` ليختبر القيمة غير المصفوفة.

## التحقق
`tests/verification/table_{bridge,sql,columns,docs,aggregate,export,lifecycle}` ✓ · `tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 43/44 (الفرق الوحيد `input_forms` قديم: اسم الملف في موقع الخطأ) · `tools/test_table|containers|container_sql` تعمل. بُني على سطح المكتب (g++)؛ لم يُبنَ APK.
