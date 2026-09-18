// موقع Rin — المحرّر التفاعلي (Playground)
// ============================================================================
// يحاول هذا الملف تحميل محرّك Rin الحقيقي مُصرَّفاً WebAssembly من
// assets/engine/rin.js (+ assets/engine/rin.wasm بجانبه). هذان الملفان لا
// يأتيان مع هذا الموقع افتراضياً لأن بناءهما يحتاج Emscripten (انظر
// assets/engine/build-engine.sh) — وهو نفس المحرّك تماماً (rin_lexer.cpp /
// rin_parser.cpp / rin_interpreter.cpp) المُستخدَم في تطبيق أندرويد وسطر
// الأوامر، لا نسخة مصغَّرة أو محاكاة منفصلة.
//
// إن لم يوجد الملفان، تعمل الصفحة في "وضع المعاينة": المحرّر يقبل الكتابة
// والأمثلة الجاهزة تعرض ناتجها المُسجَّل مسبقاً (موسوم بوضوح كمعاينة)، لكن
// زر التشغيل الفعلي يبقى معطّلاً مع شرح كيفية تفعيله.
(function () {
  "use strict";

  var editor = document.getElementById("pgEditor");
  var output = document.getElementById("pgOutput");
  var runBtn = document.getElementById("pgRun");
  var resetBtn = document.getElementById("pgReset");
  var exampleSelect = document.getElementById("pgExample");
  var statusBadge = document.getElementById("pgStatus");
  var statusTxt = document.getElementById("pgStatusText");
  var banner = document.getElementById("pgBanner");
  var liveToggle = document.getElementById("pgLive");
  var liveDot = document.getElementById("pgLiveDot");
  var liveLabel = document.getElementById("pgLiveLabel");

  if (!editor || !output) return;

  var EXAMPLES = window.RIN_EXAMPLES || {};
  var engineModule = null; // كائن Module الجاهز بعد التحميل (RinModule())
  var engineReady = false;

  // مهلة التأجيل قبل التشغيل التلقائي بعد توقّف الكتابة (مللي ثانية)
  var LIVE_DEBOUNCE_MS = 500;
  var liveTimer = null;
  var lastRunSource = null; // لتفادي إعادة تشغيل نفس الشيفرة بلا داعٍ

  function setStatus(kind, text) {
    statusBadge.className = "badge " + kind;
    statusTxt.textContent = text;
  }

  function setOutput(text, isError) {
    output.textContent = text;
    output.classList.toggle("is-error", !!isError);
  }

  // -------------------------- مؤشّر التشغيل التلقائي ----------------------
  // pending: بانتظار توقّف الكتابة — running: قيد التنفيذ الآن —
  // ok/err: آخر نتيجة — idle: لا شيء بعد.
  function setLiveIndicator(state, text) {
    if (!liveDot || !liveLabel) return;
    liveDot.className = "pg-live-dot" + (state !== "idle" ? " is-" + state : "");
    liveLabel.textContent = text;
  }

  function isLiveEnabled() {
    return !!(liveToggle && liveToggle.checked);
  }

  function scheduleLiveRun() {
    if (!engineReady || !isLiveEnabled()) return;
    if (liveTimer) clearTimeout(liveTimer);
    setLiveIndicator("pending", "بانتظار توقّفك عن الكتابة…");
    liveTimer = setTimeout(function () {
      liveTimer = null;
      var source = editor.value;
      if (source === lastRunSource) return; // لم يتغيّر شيء فعلياً
      setLiveIndicator("running", "جارٍ التشغيل…");
      runReal(source);
    }, LIVE_DEBOUNCE_MS);
  }

  function cancelLiveRun() {
    if (liveTimer) {
      clearTimeout(liveTimer);
      liveTimer = null;
    }
  }

  function loadDefaultExample() {
    var params = new URLSearchParams(window.location.search);
    var requested = params.get("ex");
    var key = (requested && EXAMPLES[requested]) ? requested : Object.keys(EXAMPLES)[0];
    if (key) {
      editor.value = EXAMPLES[key].code;
      if (exampleSelect) exampleSelect.value = key;
      runOrPreview(key);
    }
  }

  // يشغّل المثال فوراً إن كان المحرّك جاهزاً والتشغيل التلقائي مفعّلاً،
  // وإلا يعرض الناتج المُسجَّل مسبقاً كمعاينة.
  function runOrPreview(key) {
    if (engineReady && isLiveEnabled()) {
      cancelLiveRun();
      setLiveIndicator("running", "جارٍ التشغيل…");
      runReal(EXAMPLES[key].code);
    } else {
      showPreviewOutput(key);
    }
  }

  function showPreviewOutput(key) {
    var ex = EXAMPLES[key];
    if (!ex) return;
    if (engineReady) return; // المحرّك الحقيقي هو من يملأ المخرجات إذن
    setOutput(
      "» معاينة غير حيّة (المحرّك الحقيقي غير مُحمَّل بعد):\n\n" + ex.expected,
      false
    );
  }

  if (exampleSelect) {
    Object.keys(EXAMPLES).forEach(function (key) {
      var opt = document.createElement("option");
      opt.value = key;
      opt.textContent = EXAMPLES[key].label;
      exampleSelect.appendChild(opt);
    });
    exampleSelect.addEventListener("change", function () {
      var ex = EXAMPLES[exampleSelect.value];
      if (ex) {
        editor.value = ex.code;
        runOrPreview(exampleSelect.value);
      }
    });
  }

  // دعم Tab داخل المحرّر بدل الانتقال للعنصر التالي
  editor.addEventListener("keydown", function (e) {
    if (e.key === "Tab") {
      e.preventDefault();
      var start = editor.selectionStart, end = editor.selectionEnd;
      editor.value = editor.value.slice(0, start) + "    " + editor.value.slice(end);
      editor.selectionStart = editor.selectionEnd = start + 4;
    }
  });

  function runReal(source) {
    lastRunSource = source;
    try {
      var resultPtr = engineModule.ccall(
        "rin_run_source", "string", ["string"], [source]
      );
      if (typeof resultPtr === "string" && resultPtr.indexOf("__RIN_ERROR__:") === 0) {
        var rest = resultPtr.slice("__RIN_ERROR__:".length);
        var sep = rest.indexOf(":");
        var line = rest.slice(0, sep);
        var msg = rest.slice(sep + 1);
        setOutput("خطأ في السطر " + line + ":\n" + msg, true);
        setLiveIndicator("err", "خطأ — السطر " + line);
      } else {
        setOutput(resultPtr || "(بلا ناتج)", false);
        setLiveIndicator("ok", "تشغيل ناجح");
      }
    } catch (err) {
      setOutput("خطأ داخلي أثناء التشغيل:\n" + String(err), true);
      setLiveIndicator("err", "خطأ داخلي");
    }
  }

  if (runBtn) {
    runBtn.addEventListener("click", function () {
      cancelLiveRun();
      var source = editor.value;
      if (!engineReady) {
        setOutput(
          "المحرّك الحقيقي (WebAssembly) غير مُحمَّل في هذه النسخة من الموقع.\n" +
          "راجع الصندوق أعلاه لطريقة بنائه محلياً بأمر واحد — بعدها يعمل هذا " +
          "الزر بلا أي تغيير آخر في الصفحة.",
          true
        );
        return;
      }
      setLiveIndicator("running", "جارٍ التشغيل…");
      runReal(source);
    });
  }

  if (resetBtn) {
    resetBtn.addEventListener("click", function () {
      cancelLiveRun();
      var key = exampleSelect ? exampleSelect.value : Object.keys(EXAMPLES)[0];
      var ex = EXAMPLES[key];
      if (ex) {
        editor.value = ex.code;
        if (engineReady && isLiveEnabled()) {
          setLiveIndicator("running", "جارٍ التشغيل…");
          runReal(ex.code);
        } else {
          showPreviewOutput(key);
        }
      }
    });
  }

  // إعادة التشغيل تلقائياً كلّما توقّف المستخدم عن الكتابة (بعد المهلة)
  editor.addEventListener("input", function () {
    if (!engineReady) return;
    if (!isLiveEnabled()) return;
    scheduleLiveRun();
  });

  if (liveToggle) {
    liveToggle.addEventListener("change", function () {
      if (!engineReady) return;
      if (liveToggle.checked) {
        setLiveIndicator("running", "جارٍ التشغيل…");
        runReal(editor.value);
      } else {
        cancelLiveRun();
        setLiveIndicator("idle", "التشغيل التلقائي متوقّف");
      }
    });
  }

  // ------------------------------------------------------------------------
  // محاولة تحميل المحرّك الحقيقي. rin.js يُعرِّف RinModule() عالمياً عند
  // تحميله (MODULARIZE=1 / EXPORT_NAME=RinModule)، تماماً كما في web/index.html
  // الرسمي داخل المستودع. rin.wasm يُجلَب تلقائياً بجانبه بواسطة Emscripten.
  // ------------------------------------------------------------------------
  function tryLoadEngine() {
    setStatus("off", "جاري البحث عن المحرّك المُجمَّع…");
    var script = document.createElement("script");
    script.src = "assets/engine/rin.js";
    script.onload = function () {
      if (typeof RinModule !== "function") {
        engineUnavailable();
        return;
      }
      RinModule()
        .then(function (Module) {
          engineModule = Module;
          engineReady = true;
          setStatus("ok", "المحرّك الحقيقي مُحمَّل — التشغيل فعلي داخل متصفحك");
          if (runBtn) runBtn.disabled = false;
          if (liveToggle) liveToggle.disabled = false;
          if (banner) banner.classList.add("is-hidden");
          if (isLiveEnabled()) {
            setLiveIndicator("running", "جارٍ التشغيل…");
            runReal(editor.value);
          } else {
            setOutput("المحرّك جاهز. اضغط «تشغيل» لتنفيذ الشيفرة الحالية.", false);
            setLiveIndicator("idle", "التشغيل التلقائي متوقّف");
          }
        })
        .catch(engineUnavailable);
    };
    script.onerror = engineUnavailable;
    document.head.appendChild(script);
  }

  function engineUnavailable() {
    engineReady = false;
    setStatus("off", "وضع المعاينة — المحرّك المُجمَّع غير موجود بعد");
    if (runBtn) runBtn.disabled = false; // يبقى مفعّلاً ليشرح الحالة عند الضغط
    if (liveToggle) liveToggle.disabled = true;
    if (banner) banner.classList.remove("is-hidden");
    setLiveIndicator("idle", "بحاجة إلى المحرّك المُجمَّع");
  }

  if (liveToggle) liveToggle.disabled = true; // يُفعَّل فقط بعد نجاح تحميل المحرّك
  setLiveIndicator("idle", "—");

  loadDefaultExample();
  tryLoadEngine();
})();
