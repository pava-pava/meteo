"use strict";

const period = document.getElementById("period");
const statusText = document.getElementById("status");
const groups = {
  temperature: [["temp_bme_c", "BME280", "#7ae0c8"], ["temp_ds_c", "DS18B20", "#ffc878"]],
  humidity: [["humidity_pct", "Влажность", "#76c8ff"]],
  pressure: [["pressure_mmhg", "Давление", "#c9aeff"]],
  co2: [["co2_ppm", "CO₂", "#ffc878"], ["co2_min", "Минимум", "#a3b9d0"], ["co2_max", "Максимум", "#e8a389"]],
};
const fullDate = new Intl.DateTimeFormat("ru-RU", {dateStyle: "short", timeStyle: "medium"});
const clock = new Intl.DateTimeFormat("ru-RU", {hour: "2-digit", minute: "2-digit"});
const day = new Intl.DateTimeFormat("ru-RU", {day: "numeric", month: "short"});
const month = new Intl.DateTimeFormat("ru-RU", {month: "short", year: "numeric"});
const hourlyPeriods = new Set(["1h", "6h", "24h"]);
const monthlyPeriods = new Set(["180d", "1y"]);
const charts = new Map();
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

function draw(data, selected) {
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
        type: "linear", min: data.start_ms, max: data.end_ms,
        ticks: {maxTicksLimit: 7, color: "#bdcdd7", callback: value => tickFormat.format(value)},
        grid: {color: "#2d414d"},
      },
      y: {ticks: {color: "#bdcdd7"}, grid: {color: "#2d414d"}},
    };
    const old = charts.get(id);
    if (old) {
      old.data.datasets = datasets;
      old.options.scales = scales;
      old.update("none");
    } else {
      charts.set(id, new Chart(document.getElementById(id), {
        type: "line", data: {datasets}, options: {
          scales, animation: false, maintainAspectRatio: false, parsing: false,
          interaction: {mode: "nearest", axis: "x", intersect: false},
          plugins: {
            legend: {display: group.length > 1, labels: {color: "#edf4f6"}},
            tooltip: {callbacks: {title: entries => fullDate.format(entries[0].parsed.x)}},
          },
        },
      }));
    }
  }
}

async function load() {
  clearTimeout(refreshTimer);
  currentRequest?.abort();
  const request = new AbortController();
  currentRequest = request;
  const selected = period.value;
  try {
    const response = await fetch("/api/history?period=" + selected, {signal: request.signal, cache: "no-store"});
    if (!response.ok) throw new Error("HTTP " + response.status);
    const data = await response.json();
    if (request.signal.aborted) return;
    draw(data, selected);
    statusText.textContent = data.latest_ms
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

period.addEventListener("change", load);
load();
