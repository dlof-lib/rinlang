// موقع Rin — سلوك مشترك بسيط: فتح/غلق قائمة الجوال، ونسخ الشيفرة من لوحاتها.
(function () {
  "use strict";

  var burger = document.querySelector(".nav-burger");
  var links = document.querySelector(".nav-links");
  if (burger && links) {
    burger.addEventListener("click", function () {
      var open = links.classList.toggle("is-open");
      burger.setAttribute("aria-expanded", open ? "true" : "false");
    });
    links.querySelectorAll("a").forEach(function (a) {
      a.addEventListener("click", function () {
        links.classList.remove("is-open");
        burger.setAttribute("aria-expanded", "false");
      });
    });
  }

  document.querySelectorAll(".code-panel").forEach(function (panel) {
    var pre = panel.querySelector("pre.code");
    var head = panel.querySelector(".code-head");
    if (!pre || !head) return;
    var btn = document.createElement("button");
    btn.type = "button";
    btn.className = "copy-btn";
    btn.textContent = "نسخ";
    btn.style.cssText =
      "margin-inline-start:auto;border:1px solid var(--line);background:transparent;" +
      "color:var(--ink-dim);font-family:var(--font-code);font-size:.75rem;" +
      "padding:4px 10px;border-radius:6px;cursor:pointer;direction:rtl;";
    btn.addEventListener("click", function () {
      var text = pre.innerText;
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(function () {
          btn.textContent = "تم النسخ";
          setTimeout(function () { btn.textContent = "نسخ"; }, 1600);
        });
      }
    });
    head.appendChild(btn);
  });
})();
