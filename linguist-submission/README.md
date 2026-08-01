<div align="center">

# 🎨 إضافة لغة Rin إلى GitHub Linguist

### تلوين ملفات `.rin` نحوياً تلقائياً على صفحات github.com

<p>
<img alt="Linguist" src="https://img.shields.io/badge/GitHub-Linguist-181717?style=for-the-badge&logo=github&logoColor=white" />
<img alt="Grammar" src="https://img.shields.io/badge/Grammar-TextMate-orange?style=for-the-badge" />
<img alt="Status" src="https://img.shields.io/badge/Status-Verified%20against%20live%20repo-2EA043?style=for-the-badge" />
</p>

</div>

---

> **تحديث (تحقّق فعلي):** النسخة السابقة من هذا الدليل كانت افتراضية جزئياً — فيها معلومتان لم
> تعودا صحيحتين اليوم، وملف الگرامر الذي كانت تشير إليه (`vscode-rinlang/syntaxes/rin.tmLanguage.json`)
> **لم يكن موجوداً فعلياً في المستودع**. تم التحقق من هذا الدليل مباشرة من
> [`CONTRIBUTING.md`](https://github.com/github-linguist/linguist/blob/main/CONTRIBUTING.md)
> و[`languages.yml`](https://github.com/github-linguist/linguist/blob/main/lib/linguist/languages.yml)
> الفعليَّين في مستودع linguist اليوم، وتم **إنشاء ملف الگرامر الحقيقي** في هذا التسليم عند
> `syntaxes/rin.tmLanguage.json` (لم يكن موجوداً من قبل).

تلوين GitHub لأي لغة (الشكل الذي تراه في الملفات على github.com) يعتمد على مشروع
[`github-linguist/linguist`](https://github.com/github-linguist/linguist)، وهو **منفصل تماماً**
عن VS Code. لا يمكن "تفعيله" من داخل مستودعك مباشرة — يتطلب **Pull Request مقبول** في مستودع
linguist نفسه.

## ⚠️ اقرأ هذا أولاً: شرط الاستخدام الفعلي (الأهم في كل العملية)

قبل أي خطوة تقنية، فريق GitHub يفرض شرط **انتشار حقيقي** لأي امتداد ملف جديد — منصوص عليه حرفياً في
["Language extension and filename usage requirements"](https://github.com/github-linguist/linguist/blob/main/CONTRIBUTING.md#language-extension-and-filename-usage-requirements):

- **٢٠٠٠ ملف على الأقل** بامتداد `.rin`، مفهرسة عبر GitHub Code Search خلال آخر سنة، بلا احتساب
  الـ forks — لأن `.rin` امتداد "يُتوقَّع تكراره أكثر من مرة" في كل مستودع (مثل لغة برمجة عادية،
  وليس ملف تهيئة فريد كـ `Makefile`).
- توزيع **معقول عبر مستودعات/مستخدمين مختلفين حقيقيين** — لا يكفي أن يكون كل الاستخدام من
  `dlof-lib/rinlang` نفسه؛ فريق linguist يستبعد صاحب اللغة الأساسي عمداً من الحساب
  (`-user:dlof-lib`) لتفادي احتساب نشاطك الخاص.
- عبارة PR الرسمية تطلب منك إرفاق **رابط بحث GitHub فعلي** يثبت هذا الانتشار.

**الوضع الحالي لـ Rin:** الاستخدام الوحيد المعروف لملفات `.rin` هو داخل هذا المستودع نفسه. هذا
**أقل بكثير جداً** من الحد الأدنى (٢٠٠٠ ملف موزّعة). فريق linguist يغلق أي PR للغات جديدة/هواة لا
تحقق هذا الشرط دون مراجعة تقنية أصلاً (نصّهم الحرفي: *"we do not accept PRs for very new or hobby
languages, and will close any such PRs that attempt to add them"*).

➡️ **الخلاصة الصادقة:** كل الملفات في هذا المجلد الآن *جاهزة وصحيحة تقنياً*، لكن فتح PR بها اليوم
شبه مضمون الرفض السريع بسبب شرط الانتشار — وليس بسبب أي خطأ تقني فيها. الأفضل: انتظر حتى ينمو عدد
المشاريع/المستخدمين الذين يكتبون ملفات `.rin` علناً على GitHub (كل مشروع Rin ينشئه أي شخص آخر
يقرّبك من الحد)، ثم افتح الـPR. وريثما يحدث ذلك، امتداد VS Code (`RinLangVSSDK`، الموجود فعلاً في
`src/`) يمنحك تلوين نحوي كامل الآن بلا انتظار أي موافقة من GitHub.

---

## ما الجاهز فعلياً في هذا التسليم

| الملف | الحالة |
|---|---|
| `syntaxes/rin.tmLanguage.json` | ✅ تم إنشاؤه من الصفر بناءً على كلمات Rin المحجوزة الحقيقية في `app/src/main/cpp/rin_lexer.cpp` (لم يكن موجوداً سابقاً) |
| `languages.yml.patch` | ✅ محدَّث ليطابق قواعد اليوم (بلا `language_id` يدوي) |
| `samples/*.rin` | ✅ ٦ ملفات، ٦٧٩ سطراً إجمالاً، من مكتبات Rin القياسية الفعلية |
| ~~`grammars.yml.patch`~~ | ❌ حُذف — لم يعد يُعدَّل يدوياً، انظر الخطوة ٤ أدناه |

## خطوات التقديم (الرسمية، من CONTRIBUTING.md الحالي)

| الخطوة | ماذا تفعل |
|---|---|
| 1 | تحقّق من شرط الانتشار أعلاه أولاً — لا تكمل قبل تحقيقه |
| 2 | Fork مستودع linguist وتجهيز البيئة (Codespaces هو الأسهل) |
| 3 | أضف مُدخل `Rin` إلى `languages.yml` **بلا** حقل `language_id` |
| 4 | انشر `syntaxes/rin.tmLanguage.json` في مستودع GitHub عام، ثم شغّل `script/add-grammar <رابط المستودع>` داخل نسخة linguist المستنسخة — هذا يضيفه تلقائياً كـ submodule ويحدّث `grammars.yml` بنفسه |
| 5 | انسخ `samples/*.rin` إلى `samples/Rin/` داخل نسخة linguist |
| 6 | شغّل `script/update-ids` لتوليد `language_id` تلقائياً (لا تكتبه يدوياً أبداً) |
| 7 | شغّل الاختبارات محلياً، ثم افتح PR باستخدام قالبهم الرسمي مع رابط بحث GitHub |

### 2) تجهيز البيئة

```bash
git clone --recursive https://github.com/dlof-lib/linguist.git
cd linguist
script/bootstrap   # Ruby + Docker مطلوبان (Docker إلزامي عند إضافة/تحديث الگرامر)
```

### 3) إضافة اللغة إلى `languages.yml`

انسخ محتوى `languages.yml.patch` المرفق هنا حرفياً — لاحظ أنه **لا يحتوي** على `language_id`،
هذا مقصود ومطابق للتعليمات الحالية.

### 4) إضافة الگرامر (طريقة اليوم، لا تعديل يدوي)

```bash
# أولاً: انشر syntaxes/rin.tmLanguage.json من هذا التسليم في مستودع عام،
# مثلاً dlof-lib/rinlang نفسه أو dlof-lib/rin-tmlanguage مستقل.

# ثانياً، من داخل نسخة linguist المستنسخة:
script/add-grammar https://github.com/dlof-lib/rinlang
```

هذا الأمر يحلّل الگرامر تلقائياً، ويرفض إضافته لو كانت رخصته غير مطابقة
[للرخص المسموحة](https://github.com/github/linguist/blob/9b1023ed5d308cb3363a882531dea1e272b59977/vendor/licenses/config.yml#L4-L15)
— تأكد أن ترخيص المستودع (MIT هنا) واضح قبل التشغيل.

### 5) العيّنات

```bash
cp linguist-submission/samples/*.rin <linguist-checkout>/samples/Rin/
```

هذه العيّنات مأخوذة فعلياً من مكتبات Rin القياسية (`lib/*.rin`) — **ليست** أمثلة "hello world"
(وهذه فئة يرفضها فريق linguist صراحة). ٦٧٩ سطراً موزّعة على ٦ ملفات.

### 6) توليد المعرّف

```bash
script/update-ids
```

### 7) الاختبار وفتح PR

```bash
bundle exec rake test
```

افتح PR باستخدام **قالبهم الرسمي فقط** (لن يُراجَع أي PR بدونه)، ويجب أن يتضمن رابط
[بحث GitHub](https://github.com/search?type=code&q=NOT+is%3Afork+path%3A*.rin) يثبت الانتشار
المطلوب في القسم الأول من هذا الملف.

## ملاحظة مهمة

القبول في Linguist **ليس مضموناً وليس فورياً** حتى لو تحقق شرط الانتشار — فريق GitHub يراجع
يدوياً وقد يستغرق أسابيع أو أشهر. بينما ننتظر ذلك، امتداد VS Code (`RinLangVSSDK` في `src/`)
يمنحك تلوين نحوي كامل فوراً بنفس ملف الگرامر تماماً (`syntaxes/rin.tmLanguage.json`)، وGitHub
سيعرض كود `.rin` كنص عادي (بلا تلوين) حتى تُقبل الـPR.
