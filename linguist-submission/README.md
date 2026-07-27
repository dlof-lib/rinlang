<div align="center">

# 🎨 إضافة لغة Rin إلى GitHub Linguist

### تلوين ملفات `.rin` نحوياً تلقائياً على صفحات github.com

<p>
<img alt="Linguist" src="https://img.shields.io/badge/GitHub-Linguist-181717?style=for-the-badge&logo=github&logoColor=white" />
<img alt="Grammar" src="https://img.shields.io/badge/Grammar-TextMate-orange?style=for-the-badge" />
<img alt="Status" src="https://img.shields.io/badge/Status-Submission%20Guide-informational?style=for-the-badge" />
</p>

</div>

---

تلوين GitHub لأي لغة (الشكل الذي تراه في الملفات على github.com) يعتمد على مشروع
[`github-linguist/linguist`](https://github.com/github-linguist/linguist)، وهو **منفصل تماماً**
عن VS Code. لا يمكن "تفعيله" من داخل مستودعك مباشرة — يتطلب **Pull Request مقبول** في مستودع
linguist نفسه. هذه هي الخطوات الرسمية، خطوة بخطوة:

| الخطوة | ماذا تفعل |
|---|---|
| 1 | تجهيز مستودع عام لملف الگرامر |
| 2 | Fork مستودع linguist وتجهيز البيئة |
| 3 | إضافة الگرامر إلى `grammars.yml` |
| 4 | إضافة اللغة إلى `languages.yml` |
| 5 | إضافة عيّنات كود (`samples/`) |
| 6 | تشغيل الاختبارات محلياً |
| 7 | فتح Pull Request |

## 1) تجهيز مستودع عام (public) لملف الگرامر

Linguist لا يخزّن ملفات الگرامر بنفسه، بل يربط كل `tm_scope` بمستودع Git عام عبر
`grammars.yml`، ثم يسحبه كـ git submodule. لذلك يجب أولاً أن يكون
`syntaxes/rin.tmLanguage.json` (الموجود في هذا التسليم ضمن `vscode-rinlang/`) منشوراً
في مستودع GitHub عام — إمّا في `dlof-lib/rinlang` نفسه (الأسهل) أو في مستودع مستقل
مثل `dlof-lib/rin-tmlanguage`.

## 2) Fork مستودع linguist وتجهيز البيئة

```bash
git clone --recursive https://github.com/dlof-lib/linguist.git
cd linguist
bundle install   # يتطلب Ruby
```

## 3) إضافة الگرامر إلى grammars.yml

أضف السطر الموجود في `grammars.yml.patch` (المرفق هنا) داخل `grammars.yml`، ثم اسحب الگرامر فعلياً:

```bash
script/vendor
```

هذا يضيف submodule جديد تحت `vendor/grammars/` يشير إلى مستودعك.

## 4) إضافة اللغة إلى languages.yml

أضف المُدخل الموجود في `languages.yml.patch` داخل `lib/linguist/languages.yml`، **مع تعديل
`language_id`** إلى رقم فريد أعلى من أكبر رقم موجود حالياً في الملف (لن يقبل الفريق أي رقم مكرر).

## 5) إضافة عينات (samples)

Linguist يستخدم عينات كود حقيقية لتدريب/اختبار مصنّف اللغة الإحصائي. ضع الملفات المرفقة هنا
(`samples/*.rin`) داخل:

```
samples/Rin/
```

هذه العيّنات مأخوذة فعلياً من مكتبات Rin القياسية (`lib/*.rin`) في مستودعك — أكثر من 600 سطر
مجمّعة، وهذا أعلى من الحد الأدنى الذي يشترطه Linguist لقبول لغة جديدة (~200 سطر إجمالاً على
الأقل، موزّعة على عدة ملفات).

## 6) تشغيل الاختبارات محلياً قبل فتح PR

```bash
bundle exec rake test
script/report-linguist-samples   # اختياري: يعرض دقّة تصنيف عيّناتك مقابل عيّنات لغات أخرى
```

## 7) فتح Pull Request

افتح PR إلى `github-linguist/linguist` يتضمن:
- التعديل في `languages.yml`
- التعديل في `grammars.yml` + الـ submodule الجديد
- عينات `samples/Rin/`

اتبع نموذج (template) الـPR في المستودع بدقة — فريق GitHub يراجع لغات جديدة يدوياً وقد يطلب
تعديلات (خاصة على `language_id` ولون `color`). راجع
[`CONTRIBUTING.md`](https://github.com/github-linguist/linguist/blob/main/CONTRIBUTING.md)
الخاص بهم للتفاصيل الكاملة والأحدث.

## ملاحظة مهمة

القبول في Linguist **ليس مضموناً وليس فورياً** — فريق GitHub يراجع يدوياً، وقد يستغرق أسابيع
أو أشهر، وأحياناً يُرفض لأسباب تتعلق بعدد المستخدمين الحاليين للغة. بينما ننتظر ذلك، امتداد
VS Code (`vscode-rinlang/`) يمنحك تلوين نحوي كامل فوراً بنفس ملف الگرامر تماماً، وGitHub سيعرض
كود `.rin` كنص عادي (بلا تلوين) حتى تُقبل الـPR.
