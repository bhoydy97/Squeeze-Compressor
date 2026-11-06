(function () {
  "use strict";
  var cfg = window.LBAUDIO_CONFIG || {};

  // ---- Settings from config.js -------------------------------------------
  var products = cfg.products || {};
  if (cfg.payhipProduct || cfg.price) {  // older single-product settings
    products.squeeze = products.squeeze || { payhipProduct: cfg.payhipProduct, price: cfg.price };
  }
  function product(name) { return products[name || "squeeze"] || {}; }

  document.querySelectorAll("[data-price]").forEach(function (el) {
    var p = product(el.getAttribute("data-price"));
    if (p.price) el.textContent = p.price;
    else el.hidden = true;
  });

  var anyLive = false;
  document.querySelectorAll("[data-buy]").forEach(function (a) {
    var code = (product(a.getAttribute("data-buy")).payhipProduct || "").trim();
    var label = a.querySelector("[data-buy-label]");
    if (code) {
      anyLive = true;
      a.href = "https://payhip.com/b/" + encodeURIComponent(code);
      a.classList.add("payhip-buy-button");
      a.setAttribute("data-product", code);
      a.setAttribute("data-theme", "none");
    } else {
      a.removeAttribute("href");
      a.setAttribute("aria-disabled", "true");
      a.classList.add("is-soon");
      if (label) label.textContent = "Coming soon";
      a.querySelectorAll("[data-price]").forEach(function (p) { p.hidden = true; });
    }
  });
  if (anyLive) {
    var s = document.createElement("script");
    s.src = "https://payhip.com/payhip.js";
    s.async = true;
    document.head.appendChild(s);
  }

  // "Out now" once a product has a Payhip code, "Coming soon" until then
  document.querySelectorAll("[data-availability]").forEach(function (el) {
    var live = !!(product(el.getAttribute("data-availability")).payhipProduct || "").trim();
    el.textContent = live ? "Out now" : "Coming soon";
    el.classList.toggle("pill-live", live);
  });

  // Bundle maths from the prices above, so a page never shows a stale saving. Any product with an
  // "includes" list is a bundle; elements name theirs, e.g. data-saving-amount="fxbundle" ("bundle" if blank).
  (function () {
    function amount(p) { return parseFloat(String(p || "").replace(/[^\d.]/g, "")) || 0; }
    function maths(key) {
      var bundle = products[key];
      if (!bundle || !bundle.price) return null;
      var symbol = bundle.price.replace(/[\d.,\s]/g, "");
      var money = function (v) { return symbol + (Math.round(v) === v ? String(v) : v.toFixed(2)); };
      var keys = bundle.includes || Object.keys(products).filter(function (k) { return !products[k].includes; });
      var total = keys.reduce(function (sum, k) { return sum + amount(product(k).price); }, 0);
      var saving = total - amount(bundle.price);
      // "Buy four, get one free" only while it's true: five plugins, and the saving covers even the dearest
      var dearest = keys.reduce(function (m, k) { return Math.max(m, amount(product(k).price)); }, 0);
      return { total: money(total), saving: money(saving), hasSaving: saving > 0,
               fourForFive: keys.length === 5 && saving + 0.001 >= dearest };
    }
    function each(attr, fn) {
      document.querySelectorAll("[" + attr + "]").forEach(function (el) {
        var m = maths(el.getAttribute(attr) || "bundle");
        if (m) fn(el, m);
      });
    }
    each("data-bundle-total", function (el, m) { el.textContent = m.total; });
    each("data-saving-amount", function (el, m) { el.textContent = m.saving; });
    each("data-bundle-saving", function (el, m) { if (!m.hasSaving) el.hidden = true; });
    each("data-promo-four", function (el, m) { el.hidden = !m.fourForFive; });
  })();

  document.querySelectorAll("[data-email]").forEach(function (el) {
    var email = (cfg.supportEmail || "").trim();
    if (email) {
      el.textContent = email;
      if (el.tagName === "A") el.href = "mailto:" + email;
    } else {
      el.textContent = el.getAttribute("data-email-fallback") || "our support email (coming soon)";
      if (el.tagName === "A") el.removeAttribute("href");
    }
  });

  // ---- Interactive transfer curve -----------------------------------------
  // Same static gain computer the plugin runs (CompressorDSP.h).
  var svg = document.getElementById("curve-svg");
  if (!svg) return;

  var NS = "http://www.w3.org/2000/svg";
  var MIN = -60, MAX = 0;
  var box = { x: 40, y: 12, w: 300, h: 300 };
  function px(db) { return box.x + (db - MIN) / (MAX - MIN) * box.w; }
  function py(db) { return box.y + box.h - (db - MIN) / (MAX - MIN) * box.h; }

  function gainReduction(x, t, r, k) {
    var over = x - t, slope = 1 / r - 1;
    if (k > 0 && 2 * Math.abs(over) <= k) { var q = over + k / 2; return slope * q * q / (2 * k); }
    return over > 0 ? slope * over : 0;
  }

  function el(name, attrs, parent) {
    var n = document.createElementNS(NS, name);
    for (var a in attrs) n.setAttribute(a, attrs[a]);
    (parent || svg).appendChild(n);
    return n;
  }

  // Static scaffolding
  var defs = el("defs", {});
  var grad = el("linearGradient", { id: "curveFill", x1: "0", y1: "0", x2: "0", y2: "1" }, defs);
  el("stop", { offset: "0", class: "c-stop-top" }, grad);
  el("stop", { offset: "1", class: "c-stop-bottom" }, grad);
  el("rect", { x: box.x, y: box.y, width: box.w, height: box.h, class: "c-plot" });
  for (var d = MIN; d <= MAX; d += 12) {
    el("line", { x1: px(d), y1: box.y, x2: px(d), y2: box.y + box.h, class: "c-grid" });
    el("line", { x1: box.x, y1: py(d), x2: box.x + box.w, y2: py(d), class: "c-grid" });
    el("text", { x: px(d), y: box.y + box.h + 18, class: "c-label", "text-anchor": "middle" }).textContent = d;
    el("text", { x: box.x - 8, y: py(d) + 4, class: "c-label", "text-anchor": "end" }).textContent = d;
  }
  el("line", { x1: px(MIN), y1: py(MIN), x2: px(MAX), y2: py(MAX), class: "c-unity" });
  var knee = el("rect", { y: box.y, height: box.h, class: "c-knee" });
  var thr = el("line", { y1: box.y, y2: box.y + box.h, class: "c-thr" });
  var fill = el("path", { class: "c-fill", fill: "url(#curveFill)" });
  var path = el("path", { class: "c-curve" });
  var halo = el("circle", { r: 11, class: "c-halo" });
  var dot = el("circle", { r: 5.5, class: "c-dot" });

  var inThr = document.getElementById("in-thr");
  var inRatio = document.getElementById("in-ratio");
  var inKnee = document.getElementById("in-knee");
  var inLevel = document.getElementById("in-level");
  var outThr = document.getElementById("out-thr");
  var outRatio = document.getElementById("out-ratio");
  var outKnee = document.getElementById("out-knee");
  var outLevel = document.getElementById("out-level");
  var rIn = document.getElementById("read-in");
  var rOut = document.getElementById("read-out");
  var rGr = document.getElementById("read-gr");
  var grBar = document.getElementById("gr-bar");

  // Ratio slider is skewed so 4:1 sits in the middle, like the plugin's knob.
  var skew = Math.log(0.5) / Math.log(3 / 19);
  function ratioFromSlider(v) { return 1 + 19 * Math.pow(v / 1000, 1 / skew); }

  var level = -12;
  function fmt(v) { return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v).toFixed(1); }

  function render() {
    var t = +inThr.value, r = ratioFromSlider(+inRatio.value), k = +inKnee.value;
    outThr.textContent = fmt(t) + " dB";
    outRatio.textContent = (r < 10 ? r.toFixed(1) : Math.round(r)) + ":1";
    outKnee.textContent = k.toFixed(0) + " dB";
    outLevel.textContent = fmt(level) + " dB";

    var dCurve = "", n = 120;
    for (var i = 0; i <= n; i++) {
      var x = MIN + (MAX - MIN) * i / n;
      var y = x + gainReduction(x, t, r, k);
      dCurve += (i ? "L" : "M") + px(x).toFixed(1) + " " + py(y).toFixed(1);
    }
    path.setAttribute("d", dCurve);
    fill.setAttribute("d", dCurve + "L" + px(MAX) + " " + py(MIN) + "L" + px(MIN) + " " + py(MIN) + "Z");
    thr.setAttribute("x1", px(t)); thr.setAttribute("x2", px(t));
    var k0 = Math.max(MIN, t - k / 2), k1 = Math.min(MAX, t + k / 2);
    knee.setAttribute("x", px(k0)); knee.setAttribute("width", Math.max(0, px(k1) - px(k0)));

    var g = gainReduction(level, t, r, k), out = level + g;
    dot.setAttribute("cx", px(level)); dot.setAttribute("cy", py(out));
    halo.setAttribute("cx", px(level)); halo.setAttribute("cy", py(out));
    rIn.textContent = fmt(level);
    rOut.textContent = fmt(out);
    rGr.textContent = Math.abs(g) < 0.05 ? "0.0" : fmt(g);
    grBar.style.transform = "scaleX(" + Math.min(1, -g / 24).toFixed(3) + ")";
  }

  [inThr, inRatio, inKnee].forEach(function (i) { i.addEventListener("input", render); });

  // The input level drifts on its own (like a signal) until someone grabs the slider.
  var reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var auto = !reduce, visible = true, start = performance.now();
  inLevel.addEventListener("input", function () { auto = false; level = +inLevel.value; render(); });

  if ("IntersectionObserver" in window) {
    new IntersectionObserver(function (entries) { visible = entries[0].isIntersecting; }).observe(svg);
  }
  function tick(now) {
    if (auto && visible) {
      var p = (now - start) / 1000;
      level = -30 + 22 * Math.sin(p * 2 * Math.PI / 6.5) + 3 * Math.sin(p * 2 * Math.PI / 1.3);
      level = Math.max(-58, Math.min(-1, level));
      inLevel.value = level.toFixed(1);
      render();
    }
    if (auto) requestAnimationFrame(tick);
  }
  level = +inLevel.value;
  render();
  if (auto) requestAnimationFrame(tick);
})();
