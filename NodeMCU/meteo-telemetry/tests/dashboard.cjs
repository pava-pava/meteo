const assert = require("node:assert/strict");
const {chromium} = require(process.argv[3]);

async function main() {
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage({viewport: {width: 1280, height: 820}});
    const errors = [];
    page.on("pageerror", error => errors.push(error.message));
    await page.goto(process.argv[2]);
    await page.waitForFunction(() => document.getElementById("status").textContent.includes("Последнее измерение"));
    assert.equal(await page.locator("canvas").count(), 4);
    assert(await page.evaluate(() => Chart.getChart("co2").data.datasets[0].data.some(point => point.y === 885)));
    await page.selectOption("#period", "1h");
    await page.waitForFunction(() => {
      const {min, max} = Chart.getChart("co2").scales.x;
      return max - min === 3_600_000;
    });
    const beforePan = await page.evaluate(() => {
      const chart = Chart.getChart("temperature");
      const {left, top, right, bottom} = chart.chartArea;
      const rect = chart.canvas.getBoundingClientRect();
      return {
        x: rect.left + (left + right) / 2,
        y: rect.top + (top + bottom) / 2,
        latest: chart.scales.x.max,
        yMin: chart.scales.y.min,
      };
    });
    await page.mouse.move(beforePan.x, beforePan.y);
    const movingHistory = page.waitForResponse(response =>
      new URL(response.url()).searchParams.has("end_ms") && response.ok());
    await page.mouse.down();
    await page.mouse.move(beforePan.x + 100, beforePan.y + 25, {steps: 5});
    await page.waitForFunction(before => {
      const temperature = Chart.getChart("temperature").scales;
      const humidity = Chart.getChart("humidity");
      return temperature.x.max < before.latest &&
        humidity.scales.x.max === temperature.x.max &&
        temperature.y.min > before.yMin && humidity.options.scales.y.min === undefined;
    }, beforePan);
    const movingData = await (await movingHistory).json();
    assert.equal(movingData.rows.length, 0);
    await page.waitForFunction(() => Chart.getChart("pressure").data.datasets[0].data.length === 0);
    await page.mouse.up();
    await page.click("#reset-view");
    await page.waitForFunction(before => {
      const {x, y} = Chart.getChart("temperature").scales;
      return x.max >= before.latest && y.min === before.yMin;
    }, beforePan);
    const oneHourBack = page.waitForResponse(response => {
      const url = new URL(response.url());
      return url.pathname === "/api/history" && url.searchParams.has("end_ms");
    });
    await page.click("#earlier");
    const older = await (await oneHourBack).json();
    await page.waitForFunction(end => Chart.getChart("pressure").scales.x.max === end, older.end_ms);
    await page.click("#later");
    await page.waitForFunction(() => document.getElementById("reset-view").disabled);
    await page.selectOption("#period", "1y");
    await page.waitForFunction(() => Chart.getChart("co2").data.datasets.some(
      dataset => dataset.label === "Максимум" && dataset.data.some(point => point.y === 1200)
    ));
    const bucket = 900_000;
    const start = Date.UTC(2026, 0, 1);
    const rows = Array.from({length: 121}, (_, i) => ({
      t: start + (i === 120 ? i + 2 : i) * bucket,
      temp_bme_c: 20,
      temp_ds_c: i === 60 ? 22 : null,
    }));
    await page.route("**/api/history?period=7d", route => route.fulfill({json: {
      start_ms: start, end_ms: start + 123 * bucket, latest_ms: rows.at(-1).t,
      bucket_ms: bucket, rows,
    }}));
    const history = page.waitForResponse("**/api/history?period=7d");
    await page.selectOption("#period", "7d");
    await history;
    await page.waitForFunction(() => Chart.getChart("temperature").data.datasets.length === 2);
    const radii = await page.evaluate(() => {
      const chart = Chart.getChart("temperature");
      const bme = chart.getDatasetMeta(0).data;
      const ds = chart.getDatasetMeta(1).data;
      return [ds[60].options.radius, bme.at(-1).options.radius, bme[60].options.radius];
    });
    assert(radii[0] > 0, "show an isolated reading surrounded by nulls");
    assert(radii[1] > 0, "show an isolated reading after a time gap");
    assert.equal(radii[2], 0, "keep markers hidden on a dense, continuous line");
    await page.setViewportSize({width: 550, height: 820});
    assert.equal(await page.locator(".charts").evaluate(el => getComputedStyle(el).gridTemplateColumns.split(" ").length), 1);
    assert.deepEqual(errors, []);
    console.log("Dashboard charts, history selector, and mobile layout verified in Chromium.");
  } finally {
    await browser.close();
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
