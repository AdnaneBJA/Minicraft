// The dashboard's activity chart in a browser whose timezone isn't a whole number of hours from UTC (India,
// UTC+5:30): the server counts per UTC hour, and the chart must still line its hours up with them.
//   node stats-chart.mjs
import { chromium } from 'playwright';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const appJs = await readFile(join(here, '../../stats/internal/web/static/app.js'), 'utf8');

// An hour two hours ago, as the server reports it: the start of a UTC hour.
const hour = new Date(Math.floor(Date.now() / 3600000) * 3600000 - 2 * 3600000).toISOString();
const api = {
  summary: { online: 0 },
  leaderboards: {},
  recent: [],
  timeline: [{ hour, joins: 3, kills: 5 }],
};

const browser = await chromium.launch();
const page = await browser.newPage({ timezoneId: 'Asia/Kolkata' });
await page.route('http://stats.test/**', (route) => {
  const path = new URL(route.request().url()).pathname;
  if (path === '/stats') {
    return route.fulfill({
      contentType: 'text/html',
      body: `<span id="online-pill"><span id="online-text"></span></span><canvas id="timeline"></canvas>
             <script>window.Chart = class { constructor(canvas, config) { window.drawn = config; } update() {} };</script>
             <script src="/stats/static/app.js"></script>`,
    });
  }
  if (path === '/stats/static/app.js') return route.fulfill({ contentType: 'text/javascript', body: appJs });
  const name = path.replace('/stats/api/', '');
  return route.fulfill({ contentType: 'application/json', body: JSON.stringify(api[name] ?? []) });
});
await page.goto('http://stats.test/stats');
await page.waitForFunction(() => window.drawn, null, { timeout: 5000 });
const [joins, kills] = await page.evaluate(() => window.drawn.data.datasets.map((d) => d.data.reduce((a, b) => a + b, 0)));
await browser.close();

if (joins !== 3 || kills !== 5) {
  console.error(`FAIL: the chart shows ${joins} joins and ${kills} kills, not 3 and 5 (hours misaligned with UTC)`);
  process.exit(1);
}
console.log('OK: the chart lines its hours up with the server\'s, in UTC+5:30 too');
