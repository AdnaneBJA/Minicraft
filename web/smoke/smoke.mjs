// Smoke test for the web build, in headless Chromium:
//   1. Alice and Bob open the page, type their names and land in the same world; Bob chats.
//   2. Alice leaves through the menu, lands on the name screen, and plays again.
//   3. A third visitor tries the name "Alice" while Alice is online and is turned away.
//   4. The canvas follows the browser window.
// Screenshots go to out/. Any uncaught page error fails the run.
//
// Needs: the web build (built with -DMINICRAFT_SERVER_URL=ws://localhost:7777) and a server on port 7777:
//   docker run --rm -p 7777:7777 minicraft-server      (or build/minicraft-server)
//   node smoke.mjs [build-web folder] [server log] [stats base URL]
// With the server's log, the run also checks what the server saw: the names the players typed, and no desync.
// With the stats service's base URL (e.g. http://127.0.0.1:8090), it checks the dashboard counted the visit.
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { mkdir, readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('.', import.meta.url));
const buildDir = resolve(process.argv[2] ?? join(here, '../../build-web'));
const outDir = join(here, 'out');
const serverLog = process.argv[3];
const statsBase = process.argv[4];
const joinedBefore = statsBase ? (await (await fetch(`${statsBase}/stats/api/summary`)).json()).playersJoined : 0;
const port = 8080;
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };

const files = createServer(async (request, response) => {
  const requested = new URL(request.url, 'http://localhost').pathname;
  const path = requested === '/' ? '/Minicraft.html' : requested;
  try {
    const body = await readFile(join(buildDir, path));
    response.writeHead(200, { 'Content-Type': types[extname(path)] ?? 'application/octet-stream' });
    response.end(body);
  } catch {
    response.writeHead(404).end();
  }
}).listen(port);

const sleep = (ms) => new Promise((done) => setTimeout(done, ms));
const errors = [];

async function openGame(browser, label) {
  const context = await browser.newContext({ viewport: { width: 960, height: 540 } });
  const page = await context.newPage();
  page.on('pageerror', (error) => errors.push(`${label}: ${error.message}`));
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(`${label} console: ${message.text()}`);
  });
  await page.goto(`http://localhost:${port}/`);
  await page.locator('#play').waitFor({ state: 'visible', timeout: 30000 });
  await page.click('#play');
  await sleep(800);  // the name screen comes up once the stored files have loaded
  return page;
}

async function press(page, key, times = 1) {
  for (let i = 0; i < times; ++i) {
    await page.keyboard.press(key);
    await sleep(120);
  }
}

// On the name screen, typing goes straight into the name, like a visitor would: replace it and play.
async function play(page, name) {
  await press(page, 'Backspace', 12);
  await page.keyboard.type(name, { delay: 60 });
  await press(page, 'Enter');
  await sleep(3000);  // connect, download the world, catch up
}

async function shot(page, name) {
  await page.screenshot({ path: join(outDir, `${name}.png`) });
}

await mkdir(outDir, { recursive: true });
const browser = await chromium.launch();
try {
  const alice = await openGame(browser, 'alice');
  await shot(alice, '1-name-screen');
  await play(alice, 'Alice');
  const bob = await openGame(browser, 'bob');
  await play(bob, 'Bob');
  await alice.keyboard.down('ArrowRight');
  await sleep(700);
  await alice.keyboard.up('ArrowRight');
  await press(bob, 'Enter');           // chat
  await bob.keyboard.type('hi alice', { delay: 40 });
  await press(bob, 'Enter');
  await sleep(1000);
  await shot(alice, '2-alice-in-world');
  await shot(bob, '3-bob-in-world');

  // --- Alice leaves, and comes back.
  await press(alice, 'Escape');        // the menu
  await press(alice, 'ArrowDown', 2);  // Leave Game
  await press(alice, 'Enter');
  await sleep(500);
  await shot(alice, '4-alice-left');
  await press(alice, 'Enter');         // Play, with her name still there
  await sleep(3000);
  await shot(alice, '5-alice-back');

  // --- Someone else can't take Alice's name while she's online.
  const impostor = await openGame(browser, 'impostor');
  await play(impostor, 'Alice');
  await shot(impostor, '6-name-in-use');

  // --- The canvas follows the browser window.
  await bob.setViewportSize({ width: 1280, height: 800 });
  await sleep(500);
  const canvas = await bob.evaluate(() => {
    const c = document.getElementById('canvas');
    return { width: c.width, height: c.height };
  });
  if (canvas.width !== 1280 || canvas.height !== 800) {
    errors.push(`canvas is ${canvas.width}x${canvas.height} after resizing the page to 1280x800`);
  }
  await shot(bob, '7-resized');
} finally {
  await browser.close();
  files.close();
}

if (serverLog) {
  if (!existsSync(serverLog)) errors.push(`no server log at ${serverLog}`);
  else {
    const log = await readFile(serverLog, 'utf8');
    const named = (name) => (log.match(new RegExp(`is ${name}\r?$`, 'gm')) ?? []).length;
    if (named('Alice') !== 2) errors.push(`the server saw Alice join ${named('Alice')} times, not 2 (join + rejoin)`);
    if (named('Bob') !== 1) errors.push(`the server saw Bob join ${named('Bob')} times, not 1`);
    if (/out of sync/.test(log)) errors.push('the server reported a desync');
  }
}

if (statsBase) {
  await sleep(2500);  // the server reports once a second
  const summary = await (await fetch(`${statsBase}/stats/api/summary`)).json();
  if (summary.playersJoined < joinedBefore + 3) {
    errors.push(`the dashboard counted ${summary.playersJoined - joinedBefore} joins, not 3 (Alice, Bob, Alice again)`);
  }
  const recent = await (await fetch(`${statsBase}/stats/api/recent`)).json();
  for (const name of ['Alice', 'Bob']) {
    if (!recent.some((event) => event.text === `${name} joined the game`)) errors.push(`no "${name} joined" on the dashboard`);
  }
  const page = await (await fetch(`${statsBase}/stats`)).text();
  if (!page.includes('Players joined')) errors.push('the dashboard page did not render');
}

if (errors.length) {
  console.error('Page errors:\n' + errors.join('\n'));
  process.exit(1);
}
console.log(`OK. Screenshots in ${outDir}`);
