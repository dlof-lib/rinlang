// أمثلة جاهزة لمحرّر Rin التفاعلي. "expected" هو الناتج الحقيقي المُسجَّل مسبقاً
// (نُفِّذت هذه الأمثلة فعلياً على المحرّك أثناء كتابة الدروس) — يُستخدَم فقط
// في وضع المعاينة، حين لا يكون المحرّك المُجمَّع (WASM) مُحمَّلاً في الصفحة.
window.RIN_EXAMPLES = {
  hello: {
    label: "مرحباً بالعالم",
    code:
      'let name = "Rin";\n' +
      'print "Hello, " + name + "!";\n',
    expected: "Hello, Rin!"
  },
  fib: {
    label: "متتالية فيبوناتشي (تكرار)",
    code:
      "fun fib(n) {\n" +
      "    if (n < 2) { return n; }\n" +
      "    return fib(n - 1) + fib(n - 2);\n" +
      "}\n" +
      "print fib(10);\n",
    expected: "55"
  },
  collections: {
    label: "مصفوفات وقواميس",
    code:
      "let arr = [1, 2, 3];\n" +
      "push(arr, 4);\n" +
      "print pop(arr);\n" +
      "print sort([3, 1, 2]);\n\n" +
      'let m = {name: "Rin", age: 2};\n' +
      'print m["name"];\n' +
      "print keys(m);\n",
    expected: "4\n[1, 2, 3]\nRin\n[name, age]"
  },
  closures: {
    label: "دوال مجهولة وإغلاقات",
    code:
      "let fns = [];\n" +
      "for (let i = 0; i < 3; i = i + 1) {\n" +
      "    fns[len(fns)] = fun() { return i; };\n" +
      "}\n" +
      "print fns[0]();\n" +
      "print fns[1]();\n" +
      "print fns[2]();\n\n" +
      "fun makeAdder(n) { return fun(x) { return x + n; }; }\n" +
      "print makeAdder(3)(4);\n",
    expected: "0\n1\n2\n7"
  },
  forin: {
    label: "التكرار for..in",
    code:
      "for (let x in [10, 20, 30]) { print x; }\n\n" +
      'let m = {"a": 1, "b": 2};\n' +
      'for (let k in m) { print k + "=" + m[k]; }\n\n' +
      'for (let c in "abc") { print c; }\n',
    expected: "10\n20\n30\na=1\nb=2\na\nb\nc"
  },
  container: {
    label: "حاوية @container",
    code:
      "@container=my_data\n" +
      '    text title = "بيانات رين";\n' +
      "    print title;\n\n" +
      "    Section=numbers\n" +
      "        let a = 10;\n" +
      "        let b = 4;\n" +
      "        print Addition(a, b);\n" +
      "    .end/Section\n" +
      ".end/container\n",
    expected: "بيانات رين\n14"
  },
  pipe: {
    label: "أنابيب |>",
    code:
      "@container.pipe=sales_pipeline\n" +
      "    let raw = [1, 2, 3, 4, 5];\n" +
      "    let result = raw |> normalize() |> mean();\n" +
      "    print result;\n" +
      ".end/container.pipe\n",
    expected: "(رقم — متوسط raw بعد normalize(). شغّل المحرّك الحقيقي أعلاه لرؤية القيمة الدقيقة.)"
  },
  nosql: {
    label: "قاعدة بيانات NoSQL",
    code:
      "@container.doc=users\n" +
      '    document id="u1" fields={ name: "سارة", age: 28 };\n' +
      ".end/container.doc\n\n" +
      'print insertDoc("users", "u3", { name: "منى" });\n' +
      'print queryDocs("users", "city", "الرياض");\n',
    expected: "(نتيجة الإدراج، ثم نتيجة الاستعلام — لا يوجد حقل city في أي وثيقة هنا فالاستعلام متوقَّع أن يعيد قائمة فارغة. شغّل المحرّك الحقيقي أعلاه للتأكد.)"
  }
};
