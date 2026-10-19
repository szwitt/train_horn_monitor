// Train Horn Monitor charts: small dependency-free SVG charts with hover tooltips.
(function () {
  "use strict";
  var NS = "http://www.w3.org/2000/svg";
  var SERIES = ["--series-1", "--series-2", "--series-3", "--series-4"];

  function el(tag, attrs, parent) {
    var node = document.createElementNS(NS, tag);
    for (var k in attrs) node.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(node);
    return node;
  }

  function text(parent, x, y, str, attrs) {
    var t = el("text", Object.assign({ x: x, y: y }, attrs || {}), parent);
    t.textContent = str;
    return t;
  }

  function niceMax(v) {
    if (v <= 0) return 1;
    var step = Math.pow(10, Math.floor(Math.log10(v)));
    var m = [1, 2, 2.5, 5, 10].find(function (f) { return f * step >= v; });
    return m * step;
  }

  function tooltip(container) {
    var tip = document.createElement("div");
    tip.className = "tooltip";
    container.appendChild(tip);
    return {
      show: function (html, x, y) {
        tip.innerHTML = html;
        tip.style.display = "block";
        var w = tip.offsetWidth, cw = container.clientWidth;
        tip.style.left = Math.max(0, Math.min(x + 12, cw - w)) + "px";
        tip.style.top = Math.max(0, y - tip.offsetHeight - 8) + "px";
      },
      hide: function () { tip.style.display = "none"; }
    };
  }

  function hourLabel(h) {
    var suffix = h < 12 ? "am" : "pm";
    var hh = h % 12 === 0 ? 12 : h % 12;
    return hh + " " + suffix;
  }

  // Bars by hour of day; night hours in the accent, day hours de-emphasized.
  function hornHours(container, data) {
    container.innerHTML = "";
    var W = Math.max(container.clientWidth, 320), H = 220;
    var m = { l: 32, r: 8, t: 8, b: 28 };
    var iw = W - m.l - m.r, ih = H - m.t - m.b;
    var max = niceMax(Math.max.apply(null, data.hours.map(function (d) { return d.count; })));
    var svg = el("svg", { viewBox: "0 0 " + W + " " + H, role: "img",
      "aria-label": "Trains that sounded the horn, by hour of day" }, container);
    var tip = tooltip(container);

    [0, 0.5, 1].forEach(function (f) {
      var y = m.t + ih - f * ih;
      el("line", { x1: m.l, x2: W - m.r, y1: y, y2: y, stroke: "var(--grid)" }, svg);
      text(svg, m.l - 6, y + 4, String(Math.round(f * max)), { "text-anchor": "end" });
    });
    el("line", { x1: m.l, x2: W - m.r, y1: m.t + ih, y2: m.t + ih, stroke: "var(--axis)" }, svg);

    var slot = iw / 24, bw = Math.max(2, slot - 2);
    data.hours.forEach(function (d, i) {
      var x = m.l + i * slot + (slot - bw) / 2;
      var h = (d.count / max) * ih;
      var y = m.t + ih - h;
      var fill = d.night ? "var(--series-1)" : "var(--deemph)";
      if (h > 0) {
        var r = Math.min(4, bw / 2, h);
        el("path", { d: "M" + x + "," + (m.t + ih) + "V" + (y + r) + "Q" + x + "," + y + " " + (x + r) + "," + y +
          "H" + (x + bw - r) + "Q" + (x + bw) + "," + y + " " + (x + bw) + "," + (y + r) + "V" + (m.t + ih) + "Z",
          fill: fill }, svg);
      }
      if (i % 3 === 0) text(svg, x + bw / 2, H - 8, hourLabel(d.hour), { "text-anchor": "middle" });
      var hit = el("rect", { x: m.l + i * slot, y: m.t, width: slot, height: ih, fill: "transparent" }, svg);
      hit.addEventListener("mousemove", function (e) {
        var b = container.getBoundingClientRect();
        tip.show("<strong>" + hourLabel(d.hour) + "</strong> <span class='muted'>" +
          (d.night ? "night" : "day") + "</span><br>" + d.count + " train" + (d.count === 1 ? "" : "s") +
          " with horn", e.clientX - b.left, e.clientY - b.top);
      });
      hit.addEventListener("mouseleave", tip.hide);
    });
  }

  // One line per node distance, direct-labelled at the right, crosshair tooltip.
  function dnlLines(container, data) {
    container.innerHTML = "";
    var W = Math.max(container.clientWidth, 320), H = 240;
    var m = { l: 36, r: 56, t: 8, b: 28 };
    var iw = W - m.l - m.r, ih = H - m.t - m.b;
    var all = [];
    data.series.forEach(function (s) { s.values.forEach(function (v) { if (v != null) all.push(v); }); });
    if (!all.length) { container.innerHTML = "<p class='empty'>No full days of data yet.</p>"; return; }
    var lo = Math.floor((Math.min.apply(null, all) - 3) / 5) * 5;
    var hi = Math.ceil((Math.max.apply(null, all) + 3) / 5) * 5;
    var n = data.dates.length;
    var X = function (i) { return m.l + (n === 1 ? iw / 2 : (i / (n - 1)) * iw); };
    var Y = function (v) { return m.t + ih - ((v - lo) / (hi - lo)) * ih; };
    var svg = el("svg", { viewBox: "0 0 " + W + " " + H, role: "img",
      "aria-label": "Day-night noise level by distance from the track" }, container);
    var tip = tooltip(container);

    for (var v = lo; v <= hi; v += 5) {
      el("line", { x1: m.l, x2: m.l + iw, y1: Y(v), y2: Y(v), stroke: "var(--grid)" }, svg);
      text(svg, m.l - 6, Y(v) + 4, String(v), { "text-anchor": "end" });
    }
    [0, Math.floor((n - 1) / 2), n - 1].forEach(function (i) {
      text(svg, X(i), H - 8, data.dates[i].slice(5), { "text-anchor": "middle" });
    });

    var ends = [];
    data.series.forEach(function (s, si) {
      var color = "var(" + SERIES[si % SERIES.length] + ")";
      var d = "", pen = false, last = null;
      s.values.forEach(function (v, i) {
        if (v == null) { pen = false; return; }
        d += (pen ? "L" : "M") + X(i) + "," + Y(v);
        pen = true;
        last = { i: i, v: v };
      });
      el("path", { d: d, fill: "none", stroke: color, "stroke-width": 2, "stroke-linejoin": "round",
        "stroke-linecap": "round" }, svg);
      if (last) ends.push({ y: Y(last.v), x: X(last.i), color: color, label: s.dist_ft + " ft" });
    });
    // Direct labels, nudged apart so they never overlap
    ends.sort(function (a, b) { return a.y - b.y; });
    for (var k = 1; k < ends.length; k++) {
      if (ends[k].y - ends[k - 1].y < 13) ends[k].y = ends[k - 1].y + 13;
    }
    ends.forEach(function (e) {
      text(svg, m.l + iw + 8, e.y + 4, e.label, { "class": "label-strong" });
    });

    var cross = el("line", { y1: m.t, y2: m.t + ih, stroke: "var(--axis)", visibility: "hidden" }, svg);
    var hit = el("rect", { x: m.l, y: m.t, width: iw, height: ih, fill: "transparent" }, svg);
    hit.addEventListener("mousemove", function (e) {
      var b = container.getBoundingClientRect();
      var scale = W / b.width;
      var px = (e.clientX - b.left) * scale;
      var i = Math.max(0, Math.min(n - 1, Math.round(((px - m.l) / iw) * (n - 1))));
      cross.setAttribute("x1", X(i)); cross.setAttribute("x2", X(i));
      cross.setAttribute("visibility", "visible");
      var rows = data.series.map(function (s, si) {
        var v = s.values[i];
        return "<span style='color:var(" + SERIES[si % SERIES.length] + ")'>&#9632;</span> " + s.dist_ft +
          " ft: " + (v == null ? "no data" : v.toFixed(1) + " dB(A)");
      });
      tip.show("<strong>" + data.dates[i] + "</strong><br>" + rows.join("<br>"),
        e.clientX - b.left, e.clientY - b.top);
    });
    hit.addEventListener("mouseleave", function () { tip.hide(); cross.setAttribute("visibility", "hidden"); });
  }

  function mount(id, fn, data) {
    var node = document.getElementById(id);
    if (!node) return;
    var draw = function () { fn(node, data); };
    draw();
    var pending;
    window.addEventListener("resize", function () { clearTimeout(pending); pending = setTimeout(draw, 100); });
  }

  window.THM = { hornHours: hornHours, dnlLines: dnlLines, mount: mount };
})();
