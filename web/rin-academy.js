/*
 * Rin Academy runtime
 * -------------------
 * Official browser-side helper for site.rin's learning area.
 * It does NOT implement Rin. It calls the real WebAssembly Rin engine
 * exported by rin_wasm_bridge.cpp (rin_run_source).
 */
(function () {
  "use strict";

  var lessonCodes = [
    "learn1code", "learn2code", "learn3code", "learn4code", "learn5code",
    "learn6code", "learn7code", "learn8code", "learn9code", "learn10code",
    "learn11code", "learn12code"
  ];
  var bound = Object.create(null);

  function css() {
    if (document.getElementById("rin-academy-runtime-style")) return;
    var style = document.createElement("style");
    style.id = "rin-academy-runtime-style";
    style.textContent = [
      ".rin-run-tools{display:flex;gap:7px;align-items:center;justify-content:flex-start;direction:rtl;pointer-events:auto}",
      ".rin-run-btn{border:1px solid #285c35;background:#142319;color:#b9f6c5;border-radius:8px;padding:6px 11px;font:700 12px system-ui,sans-serif;cursor:pointer}",
      ".rin-run-btn:hover{filter:brightness(1.15)}",
      ".rin-run-btn:disabled{opacity:.55;cursor:wait}",
      ".rin-copy-btn{border:1px solid #29332b;background:#111711;color:#cfd6cf;border-radius:8px;padding:6px 11px;font:700 12px system-ui,sans-serif;cursor:pointer}",
      ".rin-result{margin-top:7px;box-sizing:border-box;white-space:pre-wrap;direction:ltr;text-align:left;font:12px/1.55 ui-monospace,SFMono-Regular,Consolas,monospace;color:#cfd6cf;background:#080b08;border:1px solid #202920;border-radius:9px;padding:9px;max-height:170px;overflow:auto}",
      ".rin-result.ok{border-color:#275b34;color:#b9f6c5}",
      ".rin-result.err{border-color:#6b3030;color:#ffb4b4}",
      ".rin-result.info{direction:rtl;text-align:right;color:#b7c0b8}",
      ".rin-progress{position:fixed;left:12px;bottom:12px;z-index:9999;padding:7px 10px;border:1px solid #263029;border-radius:9px;background:#0c110d;color:#9aa39c;font:700 11px system-ui,sans-serif;opacity:0;transform:translateY(8px);transition:.2s;pointer-events:none}",
      ".rin-progress.show{opacity:1;transform:translateY(0)}"
    ].join("");
    document.head.appendChild(style);
  }

  function textOf(id) {
    var el = document.getElementById(id);
    return el ? (el.textContent || "").trim() : "";
  }

  function showProgress(message) {
    var p = document.getElementById("rin-runtime-progress");
    if (!p) {
      p = document.createElement("div");
      p.id = "rin-runtime-progress";
      p.className = "rin-progress";
      document.body.appendChild(p);
    }
    p.textContent = message;
    p.classList.add("show");
    clearTimeout(showProgress.timer);
    showProgress.timer = setTimeout(function () { p.classList.remove("show"); }, 1800);
  }

  function normalizeResult(raw) {
    raw = String(raw == null ? "" : raw);
    if (raw.indexOf("__RIN_ERROR__:") === 0) {
      var parts = raw.split(":");
      return { ok: false, text: "السطر " + (parts[1] || "0") + ": " + parts.slice(2).join(":") };
    }
    return { ok: true, text: raw || "تم التنفيذ بنجاح — لا يوجد خرج مطبوع." };
  }

  function run(code, output, button) {
    if (!code) {
      output.className = "rin-result info";
      output.textContent = "لا يوجد كود قابل للتشغيل في هذا الدرس.";
      return;
    }

    // RinHTML snippets are intentionally not sent to the plain Rin interpreter.
    if (/<rin-app\b|rin-click=|rin-text=/.test(code)) {
      output.className = "rin-result info";
      output.textContent = "هذا المثال من RinHTML. استخدم معاينة RinHTML/المتصفح بدل تشغيله كمصدر Rin عادي.";
      return;
    }

    var runtime = window.RinSiteRuntime;
    if (!runtime || typeof runtime.runSource !== "function") {
      output.className = "rin-result err";
      output.textContent = "محرك Rin في المتصفح لم يجهز بعد. أعد المحاولة بعد اكتمال تحميل الصفحة.";
      return;
    }

    button.disabled = true;
    button.textContent = "جارٍ التشغيل…";
    output.className = "rin-result info";
    output.textContent = "تشغيل الكود عبر محرك Rin الرسمي…";
    showProgress("Rin · تشغيل المثال");

    // Keep the UI responsive before entering the WASM call.
    setTimeout(function () {
      try {
        var result = normalizeResult(runtime.runSource(code));
        output.className = "rin-result " + (result.ok ? "ok" : "err");
        output.textContent = result.text;
      } catch (e) {
        output.className = "rin-result err";
        output.textContent = "تعذّر تشغيل المثال: " + (e && e.message ? e.message : String(e));
      } finally {
        button.disabled = false;
        button.textContent = "▶ تشغيل";
      }
    }, 0);
  }

  function bindLesson(id) {
    if (bound[id]) return;
    var codeEl = document.getElementById(id);
    if (!codeEl || !codeEl.parentNode) return;

    var wrap = document.createElement("div");
    wrap.className = "rin-run-tools";

    var runBtn = document.createElement("button");
    runBtn.type = "button";
    runBtn.className = "rin-run-btn";
    runBtn.textContent = "▶ تشغيل";
    runBtn.title = "تشغيل هذا المثال بمحرك Rin الرسمي";

    var copyBtn = document.createElement("button");
    copyBtn.type = "button";
    copyBtn.className = "rin-copy-btn";
    copyBtn.textContent = "نسخ الكود";

    var output = document.createElement("div");
    output.className = "rin-result info";
    output.hidden = true;
    output.setAttribute("aria-live", "polite");

    runBtn.addEventListener("click", function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      output.hidden = false;
      run(textOf(id), output, runBtn);
    });

    copyBtn.addEventListener("click", function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      var code = textOf(id);
      if (!navigator.clipboard || !navigator.clipboard.writeText) {
        output.hidden = false;
        output.className = "rin-result err";
        output.textContent = "النسخ غير متاح في هذا السياق؛ انسخ النص يدويًا.";
        return;
      }
      navigator.clipboard.writeText(code).then(function () {
        copyBtn.textContent = "تم النسخ ✓";
        setTimeout(function () { copyBtn.textContent = "نسخ الكود"; }, 1400);
      }).catch(function () {
        output.hidden = false;
        output.className = "rin-result err";
        output.textContent = "تعذّر الوصول إلى الحافظة.";
      });
    });

    wrap.appendChild(runBtn);
    wrap.appendChild(copyBtn);
    codeEl.parentNode.appendChild(wrap);
    codeEl.parentNode.appendChild(output);
    bound[id] = true;
  }

  function enhance() {
    css();
    for (var i = 0; i < lessonCodes.length; i++) bindLesson(lessonCodes[i]);
  }

  function boot() {
    enhance();
    window.addEventListener("rin:ready", enhance);
    window.addEventListener("rin:rendered", function () {
      // The Fabric is rebuilt on every render; rebind against the new DOM.
      bound = Object.create(null);
      setTimeout(enhance, 0);
    });
    window.addEventListener("hashchange", function () { setTimeout(enhance, 80); });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();
