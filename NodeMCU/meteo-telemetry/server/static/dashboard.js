"use strict";

const period = document.getElementById("period");
const statusText = document.getElementById("status");
const earlier = document.getElementById("earlier");
const later = document.getElementById("later");
const resetView = document.getElementById("reset-view");
const groups = {
  temperature: [["temp_bme_c", "BME280", "#7ae0c8"], ["temp_ds_c", "DS18B20", "#ffc878"]],
  humidity: [["humidity_pct", "Влажность", "#76c8ff"]],
  pressure: [["pressure_mmhg", "Давление", "#c9aeff"]],
  co2: [["co2_ppm", "CO₂", "#ffc878"], ["co2_min", "Минимум", "#a3b9d0"], ["co2_max", "Максимум", "#e8a389"]],
};
const scaleLimits = {
  temperature: {span: 3, lower: -Infinity},
  humidity: {span: 5, upper: 100},
  pressure: {span: 5},
  co2: {span: 100},
};
const fullDate = new Intl.DateTimeFormat("ru-RU", {dateStyle: "short", timeStyle: "medium"});
const clock = new Intl.DateTimeFormat("ru-RU", {hour: "2-digit", minute: "2-digit"});
const day = new Intl.DateTimeFormat("ru-RU", {day: "numeric", month: "short"});
const month = new Intl.DateTimeFormat("ru-RU", {month: "short", year: "numeric"});
const hourlyPeriods = new Set(["1h", "6h", "24h"]);
const monthlyPeriods = new Set(["180d", "1y"]);
const charts = new Map();
const yRanges = new Map();
let viewEnd = null;
let serverNow = 0;
let drag;
let windowSpan = 0;
let liveAnchor = 0;
let currentRequest;
let refreshTimer;

function points(rows, field, gapLimit) {
  const result = [];
  for (const row of rows) {
    const previous = result.at(-1);
    if (previous && row.t - previous.x > gapLimit) {
      result.push({x: Math.floor((previous.x + row.t) / 2), y: null});
    }
    result.push({x: row.t, y: row[field] ?? null});
  }
  return result;
}

function minimumScale(datasets, {span, lower = 0, upper = Infinity}) {
  let lowest = Infinity;
  let highest = -Infinity;
  for (const {data} of datasets) {
    for (const {y} of data) {
      if (!Number.isFinite(y)) continue;
      lowest = Math.min(lowest, y);
      highest = Math.max(highest, y);
    }
  }
  if (lowest === Infinity) return {suggestedMin: 0, suggestedMax: span};
  if (highest - lowest >= span) return {};

  const center = (lowest + highest) / 2;
  const suggestedMin = Math.min(upper - span, Math.max(lower, center - span / 2));
  return {suggestedMin, suggestedMax: suggestedMin + span};
}

function draw(data, selected, preview) {
  serverNow = data.now_ms ?? data.end_ms;
  windowSpan = data.end_ms - data.start_ms;
  earlier.disabled = false;
  syncControls();
  const gapLimit = data.bucket_ms ? data.bucket_ms * 1.5 : 75_000;
  const tickFormat = hourlyPeriods.has(selected) ? clock : monthlyPeriods.has(selected) ? month : day;
  for (const [id, group] of Object.entries(groups)) {
    const datasets = group
      .filter(([field]) => !field.startsWith("co2_") || field === "co2_ppm" || data.bucket_ms)
      .filter(([field]) => field !== "temp_ds_c" || data.rows.some(row => row[field] !== null))
      .map(([field, label, color]) => ({
        label, data: points(data.rows, field, gapLimit), borderColor: color,
        borderWidth: field === "co2_min" || field === "co2_max" ? 1 : 2,
        spanGaps: false,
        pointRadius: data.rows.length <= 120 ? 2 : ({dataset, dataIndex}) => {
          const series = dataset.data;
          const isolated = series[dataIndex]?.y != null &&
            series[dataIndex - 1]?.y == null && series[dataIndex + 1]?.y == null;
          return isolated ? 3 : 0;
        },
        pointHoverRadius: 3,
      }));
    const scales = {
      x: {
        type: "linear", min: preview ? preview.end - preview.span : data.start_ms,
        max: preview?.end ?? data.end_ms,
        ticks: {maxTicksLimit: 7, color: "#bdcdd7", callback: value => tickFormat.format(value)},
        grid: {color: "#2d414d"},
      },
      y: {
        ...minimumScale(datasets, scaleLimits[id]),
        ...(preview?.id === id ? preview.y : yRanges.get(id)),
        ticks: {color: "#bdcdd7"}, grid: {color: "#2d414d"},
      },
    };
    const old = charts.get(id);
    if (old) {
      old.data.datasets = datasets;
      old.options.scales = scales;
      old.update("none");
    } else {
      const chart = new Chart(document.getElementById(id), {
        type: "line", data: {datasets}, options: {
          scales, animation: false, maintainAspectRatio: false, parsing: false,
          interaction: {mode: "nearest", axis: "x", intersect: false},
          plugins: {
            legend: {display: group.length > 1, labels: {color: "#edf4f6"}},
            tooltip: {callbacks: {title: entries => fullDate.format(entries[0].parsed.x)}},
          },
        },
      });
      charts.set(id, chart);
      bindPan(id, chart);
    }
  }
}

function syncControls() {
  later.disabled = viewEnd === null;
  resetView.disabled = viewEnd === null && yRanges.size === 0;
}

function goToTime(end) {
  if (viewEnd === null) liveAnchor = serverNow;
  viewEnd = end < serverNow ? end : null;
  syncControls();
  load();
}

function bindPan(id, chart) {
  const canvas = chart.canvas;
  canvas.addEventListener("pointerdown", event => {
    const {left, top, right, bottom} = chart.chartArea;
    const y = event.clientY - canvas.getBoundingClientRect().top;
    const legend = chart.legend;
    const onLegend = legend?.options.display && y >= legend.top && y < legend.bottom;
    if (drag || event.button !== 0 || onLegend || right <= left || bottom <= top) return;
    const scales = chart.scales;
    drag = {
      id, pointer: event.pointerId, x: event.clientX, y: event.clientY,
      xMin: scales.x.min, xMax: scales.x.max, yMin: scales.y.min, yMax: scales.y.max,
      width: right - left, height: bottom - top, requestedEnd: scales.x.max,
    };
    canvas.setPointerCapture(event.pointerId);
    canvas.classList.add("dragging");
  });

  function target(event, start) {
    const dx = event.clientX - start.x;
    const dy = event.pointerType === "touch" ? 0 : event.clientY - start.y;
    const span = start.xMax - start.xMin;
    const end = Math.abs(dx) >= 5
      ? Math.min(serverNow, Math.max(31_536_000_000, Math.round(start.xMax - dx * span / start.width)))
      : start.xMax;
    const shift = Math.abs(dy) >= 5 ? dy * (start.yMax - start.yMin) / start.height : 0;
    const y = shift ? {min: start.yMin + shift, max: start.yMax + shift} : yRanges.get(id);
    return {id, end, span, shift, y};
  }

  function show({end, span, y}) {
    for (const graph of charts.values()) {
      if (graph !== chart && graph.scales.x.max === end) continue;
      Object.assign(graph.options.scales.x, {min: end - span, max: end});
      if (graph === chart) Object.assign(graph.options.scales.y, {min: y?.min, max: y?.max});
      graph.update("none");
    }
  }

  canvas.addEventListener("pointermove", event => {
    if (drag?.id !== id || drag.pointer !== event.pointerId) return;
    const start = drag;
    cancelAnimationFrame(start.frame);
    start.frame = requestAnimationFrame(() => {
      start.position = target(event, start);
      show(start.position);
      scheduleHistoryPreview(start);
    });
  });

  function finish(event) {
    if (drag?.id !== id || drag.pointer !== event.pointerId) return;
    const start = drag;
    cancelPreview(start);
    drag = undefined;
    canvas.classList.remove("dragging");
    if (event.type !== "pointerup") {
      show({end: start.xMax, span: start.xMax - start.xMin, y: yRanges.get(id)});
      if (start.requestedEnd !== start.xMax) load();
      return;
    }

    const result = target(event, start);
    if (result.shift) yRanges.set(id, result.y);
    show(result);
    if (result.end !== start.xMax) goToTime(result.end);
    else if (start.requestedEnd !== start.xMax) load();
    syncControls();
  }

  canvas.addEventListener("pointerup", finish);
  canvas.addEventListener("pointercancel", finish);
  canvas.addEventListener("lostpointercapture", finish);
}

function cancelPreview(start) {
  cancelAnimationFrame(start.frame);
  clearTimeout(start.timer);
  start.previewRequest?.abort();
}

function scheduleHistoryPreview(start) {
  if (start.timer || start.previewRequest || start.position.end === start.requestedEnd) return;
  start.timer = setTimeout(async () => {
    start.timer = undefined;
    const end = start.position.end;
    if (end === start.requestedEnd) return;
    const selected = period.value;
    const request = new AbortController();
    start.previewRequest = request;
    start.requestedEnd = end;
    try {
      const data = await fetchHistory(selected, end, request.signal);
      if (drag === start) draw(data, selected, start.position);
    } catch {
      // Keep the current plot while dragging. The final load reports any error.
    } finally {
      start.previewRequest = undefined;
      if (drag === start) scheduleHistoryPreview(start);
    }
  }, 150);
}

async function fetchHistory(selected, end, signal) {
  const query = new URLSearchParams({period: selected});
  if (end !== null) query.set("end_ms", end);
  const response = await fetch("/api/history?" + query, {signal, cache: "no-store"});
  if (!response.ok) throw new Error("HTTP " + response.status);
  return response.json();
}

async function load() {
  clearTimeout(refreshTimer);
  currentRequest?.abort();
  const request = new AbortController();
  currentRequest = request;
  const selected = period.value;
  try {
    const data = await fetchHistory(selected, viewEnd, request.signal);
    if (request.signal.aborted || drag) return;
    draw(data, selected);
    statusText.textContent = viewEnd !== null
      ? fullDate.format(data.start_ms) + " — " + fullDate.format(data.end_ms)
      : data.latest_ms
        ? "Последнее измерение · " + fullDate.format(data.latest_ms)
        : "Ожидаем первое измерение";
  } catch (error) {
    if (error.name !== "AbortError") statusText.textContent = "Не удалось обновить графики";
  } finally {
    if (currentRequest === request) {
      const delay = hourlyPeriods.has(selected) ? 15_000 : selected === "7d" || selected === "30d" ? 60_000 : 300_000;
      refreshTimer = setTimeout(load, delay);
    }
  }
}

function reset() {
  if (drag) {
    cancelPreview(drag);
    charts.get(drag.id).canvas.classList.remove("dragging");
  }
  drag = undefined;
  viewEnd = null;
  yRanges.clear();
  syncControls();
  load();
}

earlier.addEventListener("click", () => goToTime(Math.max(31_536_000_000, (viewEnd ?? serverNow) - windowSpan)));
later.addEventListener("click", () => {
  const end = viewEnd + windowSpan;
  goToTime(end >= liveAnchor ? serverNow : end);
});
period.addEventListener("change", reset);
resetView.addEventListener("click", reset);
load();
