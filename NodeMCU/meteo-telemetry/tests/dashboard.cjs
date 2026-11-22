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
    await page.selectOption("#period", "1y");
    await page.waitForFunction(() => Chart.getChart("co2").data.datasets.some(
      dataset => dataset.label === "Максимум" && dataset.data.some(point => point.y === 1200)
    ));
    await page.setViewportSize({width: 550, height: 820});
    assert.equal(await page.locator(".charts").evaluate(el => getComputedStyle(el).gridTemplateColumns.split(" ").length), 1);
    assert.deepEqual(errors, []);
    console.log("Dashboard charts, history selector, and mobile layout verified in Chromium.");
  } finally {
    await browser.close();
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
