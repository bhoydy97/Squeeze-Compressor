// Before/after player: the original and the processed version play in sync,
// so switching between them changes only the processing, never the position.
// Data comes from assets/demo/<plugin>-data.js (window.LBAUDIO_DEMO); the Squeeze page uses SQUEEZE_DEMO.
// A preset can bring its own original (dryFile, dry waveform, duration) when the page demos more than one clip.
(function () {
  "use strict";
  var D = window.LBAUDIO_DEMO || window.SQUEEZE_DEMO;
  var root = document.getElementById("player");
  if (!D || !root) return;

  var AC = window.AudioContext || window.webkitAudioContext;
  var tabs = root.querySelectorAll("[data-preset]");
  var desc = document.getElementById("player-desc");
  var settings = document.getElementById("player-settings");
  var svg = document.getElementById("wave-svg");
  var wave = document.getElementById("wave");
  var head = document.getElementById("playhead");
  var playBtn = document.getElementById("play");
  var playLabel = document.getElementById("play-label");
  var abDry = document.getElementById("ab-dry");
  var abWet = document.getElementById("ab-wet");
  var timeEl = document.getElementById("time");
  var status = document.getElementById("player-status");

  var current = tabs.length ? tabs[0].getAttribute("data-preset") : Object.keys(D.presets)[0], showWet = true;
  var ctx = null, buffers = {}, loading = {};
  var srcDry = null, srcWet = null, gDry = null, gWet = null;
  var playing = false, startedAt = 0, offset = 0, raf = 0;
  var duration = D.duration;

  function fileOf(key) { return (D.presets[key] && D.presets[key].file) || key; }
  function dryOf(key) { return (D.presets[key] && D.presets[key].dryFile) || D.dryFile || "dry"; }
  function durOf(key) { return (D.presets[key] && D.presets[key].duration) || D.duration; }

  // ---- Waveform -------------------------------------------------------------
  var W = 960, H = 140, MID = H / 2, FLOOR = -48;
  function y(v) {
    var db = v > 0 ? 20 * Math.log10(v) : FLOOR;
    var t = Math.max(0, (db - FLOOR) / -FLOOR);
    return t * (MID - 4);
  }
  function shape(data) {
    var n = data.length, top = "", bottom = "";
    for (var i = 0; i < n; i++) {
      var x = (i / (n - 1) * W).toFixed(1);
      top += (i ? "L" : "M") + x + " " + (MID - y(data[i])).toFixed(1);
    }
    for (var j = n - 1; j >= 0; j--) {
      bottom += "L" + (j / (n - 1) * W).toFixed(1) + " " + (MID + y(data[j])).toFixed(1);
    }
    return top + bottom + "Z";
  }
  var NS = "http://www.w3.org/2000/svg";
  var pathDry = document.createElementNS(NS, "path");
  var pathWet = document.createElementNS(NS, "path");
  pathDry.setAttribute("class", "w-dry");
  pathWet.setAttribute("class", "w-wet");
  svg.appendChild(pathWet);
  svg.appendChild(pathDry);

  function fmt(t) { t = Math.max(0, Math.floor(t)); return Math.floor(t / 60) + ":" + ("0" + (t % 60)).slice(-2); }

  function showPreset() {
    var p = D.presets[current];
    desc.textContent = p.desc;
    settings.textContent = p.settings;
    pathWet.setAttribute("d", shape(p.wave));
    pathDry.setAttribute("d", shape(p.dry || D.dry));
    duration = (buffers[fileOf(current)] && buffers[fileOf(current)].duration) || durOf(current);
    wave.setAttribute("aria-valuemax", String(Math.round(duration)));
    tabs.forEach(function (t) {
      var on = t.getAttribute("data-preset") === current;
      t.setAttribute("aria-selected", on ? "true" : "false");
      t.tabIndex = on ? 0 : -1;
    });
  }

  function showAB() {
    abWet.setAttribute("aria-pressed", showWet ? "true" : "false");
    abDry.setAttribute("aria-pressed", showWet ? "false" : "true");
    root.classList.toggle("is-dry", !showWet);
    if (gDry && ctx) {
      gDry.gain.setTargetAtTime(showWet ? 0 : 1, ctx.currentTime, 0.012);
      gWet.gain.setTargetAtTime(showWet ? 1 : 0, ctx.currentTime, 0.012);
    }
  }

  // ---- Audio ----------------------------------------------------------------
  function load(file) {
    if (buffers[file]) return Promise.resolve(buffers[file]);
    if (loading[file]) return loading[file];
    loading[file] = fetch(D.base + file + ".mp3")
      .then(function (r) { if (!r.ok) throw new Error(r.status); return r.arrayBuffer(); })
      .then(function (data) {
        return new Promise(function (resolve, reject) { ctx.decodeAudioData(data, resolve, reject); });
      })
      .then(function (b) { buffers[file] = b; return b; });
    loading[file].catch(function () { delete loading[file]; });
    return loading[file];
  }

  function position() {
    if (!playing) return offset;
    return (ctx.currentTime - startedAt) % duration;
  }

  function stopSources() {
    [srcDry, srcWet].forEach(function (s) { if (s) { try { s.stop(); } catch (e) {} s.disconnect(); } });
    srcDry = srcWet = null;
  }

  function startAt(off) {
    stopSources();
    gDry = gDry || ctx.createGain();
    gWet = gWet || ctx.createGain();
    gDry.connect(ctx.destination);
    gWet.connect(ctx.destination);
    gDry.gain.value = showWet ? 0 : 1;
    gWet.gain.value = showWet ? 1 : 0;
    srcDry = ctx.createBufferSource();
    srcWet = ctx.createBufferSource();
    srcDry.buffer = buffers[dryOf(current)];
    srcWet.buffer = buffers[fileOf(current)];
    duration = srcWet.buffer.duration;
    off = Math.max(0, Math.min(off, duration - 0.05));
    srcDry.loop = srcWet.loop = true;
    srcDry.connect(gDry);
    srcWet.connect(gWet);
    var when = ctx.currentTime + 0.03;
    srcDry.start(when, off);
    srcWet.start(when, off);
    startedAt = when - off;
    playing = true;
    tick();
  }

  function setPlaying(on) {
    playBtn.setAttribute("aria-label", on ? "Pause" : "Play");
    playLabel.textContent = on ? "Pause" : "Play";
    root.classList.toggle("is-playing", on);
  }

  function tick() {
    cancelAnimationFrame(raf);
    var p = position();
    head.style.left = (p / duration * 100) + "%";
    timeEl.textContent = fmt(p) + " / " + fmt(duration);
    wave.setAttribute("aria-valuenow", Math.round(p));
    wave.setAttribute("aria-valuetext", fmt(p));
    if (playing) raf = requestAnimationFrame(tick);
  }

  function play() {
    if (!AC) { status.textContent = "Your browser can't play this demo."; return; }
    if (!ctx) ctx = new AC();
    if (ctx.resume) ctx.resume();
    status.textContent = "Loading audio…";
    root.classList.add("is-loading");
    Promise.all([load(dryOf(current)), load(fileOf(current))]).then(function () {
      status.textContent = "";
      root.classList.remove("is-loading");
      startAt(offset);
      setPlaying(true);
      Object.keys(D.presets).forEach(function (k) {
        load(fileOf(k)).catch(function () {});
        load(dryOf(k)).catch(function () {});
      });
    }).catch(function () {
      root.classList.remove("is-loading");
      status.textContent = "The audio didn't load. Check your connection and try again.";
    });
  }

  function pause() {
    offset = position();
    stopSources();
    playing = false;
    setPlaying(false);
    tick();
  }

  function seek(t) {
    t = Math.max(0, Math.min(duration - 0.05, t));
    if (playing) startAt(t); else { offset = t; tick(); }
  }

  playBtn.addEventListener("click", function () { if (playing) pause(); else play(); });
  abDry.addEventListener("click", function () { showWet = false; showAB(); });
  abWet.addEventListener("click", function () { showWet = true; showAB(); });

  tabs.forEach(function (t) {
    t.addEventListener("click", function () {
      var name = t.getAttribute("data-preset");
      if (name === current) return;
      // Same original: keep the place. A different clip: start it from the top.
      var sameClip = dryOf(name) === dryOf(current);
      var p = sameClip ? position() : 0;
      current = name;
      showPreset();
      if (playing) {
        Promise.all([load(dryOf(name)), load(fileOf(name))]).then(function () {
          if (current === name && playing) startAt(p);
        });
      } else {
        offset = Math.min(p, duration - 0.05);
        tick();
      }
    });
    t.addEventListener("keydown", function (e) {
      if (e.key !== "ArrowRight" && e.key !== "ArrowLeft") return;
      var list = Array.prototype.slice.call(tabs);
      var i = list.indexOf(t) + (e.key === "ArrowRight" ? 1 : -1);
      var next = list[(i + list.length) % list.length];
      next.focus();
      next.click();
    });
  });

  wave.addEventListener("click", function (e) {
    var r = wave.getBoundingClientRect();
    seek((e.clientX - r.left) / r.width * duration);
  });
  wave.addEventListener("keydown", function (e) {
    if (e.key === "ArrowRight") { seek(position() + 5); e.preventDefault(); }
    if (e.key === "ArrowLeft") { seek(position() - 5); e.preventDefault(); }
  });

  wave.setAttribute("aria-valuemin", "0");
  wave.setAttribute("aria-valuemax", String(Math.round(duration)));
  showPreset();
  showAB();
  tick();
})();
