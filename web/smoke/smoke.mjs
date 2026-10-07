// Smoke test for the web build, in headless Chromium:
//   1. Alice and Bob open the page, go online, Alice creates a world, Bob joins it, they move and chat.
//   2. A third visitor starts a single-player world, saves it, reloads the page, and finds it under Load World.
// Screenshots go to out/. Any uncaught page error fails the run.
//
// Needs: the web build (built with -DMINICRAFT_SERVER_URL=ws://localhost:7777) and a server on port 7777:
//   docker run --rm -p 7777:7777 minicraft-server
//   node smoke.mjs ../../build-web
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { mkdir, readFile } from 'node:fs/promises';
import { extname, join, resolve } from 'node:path';

const buildDir = resolve(process.argv[2] ?? '../../build-web');
const outDir = resolve('out');
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
  await sleep(800);  // the title screen comes up once the saved files have loaded
  return page;
}

async function press(page, key, times = 1) {
  for (let i = 0; i < times; ++i) {
    await page.keyboard.press(key);
    await sleep(120);
  }
}

async function goOnline(page, name) {
  await press(page, 'ArrowDown');      // Multiplayer
  await press(page, 'Enter');
  await press(page, 'Backspace', 12);  // clear the name field
  await page.keyboard.type(name, { delay: 60 });
  await press(page, 'Enter');          // connect
  await sleep(1500);                   // the lobby list arrives
}

async function shot(page, name) {
  await page.screenshot({ path: join(outDir, `${name}.png`) });
}

await mkdir(outDir, { recursive: true });
const browser = await chromium.launch();
try {
  // --- Two players online.
  const alice = await openGame(browser, 'alice');
  const bob = await openGame(browser, 'bob');
  await shot(alice, '1-title');
  await goOnline(alice, 'Alice');
  await goOnline(bob, 'Bob');
  await shot(bob, '2-lobbies-empty');
  await press(alice, 'Enter');         // Create new world
  await sleep(2500);
  await shot(bob, '3-lobbies-with-world');
  await press(bob, 'ArrowDown');       // Alice's world
  await press(bob, 'Enter');
  await sleep(3000);
  await alice.keyboard.down('ArrowRight');
  await sleep(700);
  await alice.keyboard.up('ArrowRight');
  await press(bob, 'Enter');           // chat
  await bob.keyboard.type('hi alice', { delay: 40 });
  await press(bob, 'Enter');
  await sleep(1000);
  await shot(alice, '4-alice-online');
  await shot(bob, '5-bob-online');

  // --- Single-player, saved and loaded again after a reload.
  const solo = await openGame(browser, 'solo');
  await press(solo, 'Enter');          // Play: no worlds yet, so straight to New World
  await solo.keyboard.type('smoke', { delay: 60 });
  await press(solo, 'Enter');
  await sleep(2500);
  await shot(solo, '6-solo-world');
  await press(solo, 'Escape');         // pause menu
  await press(solo, 'ArrowDown', 2);   // Save Game
  await press(solo, 'Enter');
  await sleep(1500);                   // the save reaches IndexedDB
  await shot(solo, '7-solo-saved');
  await solo.reload();
  await solo.locator('#play').waitFor({ state: 'visible', timeout: 30000 });
  await solo.click('#play');
  await sleep(1000);
  await press(solo, 'Enter');          // Play
  await press(solo, 'Enter');          // Load World
  await sleep(300);
  await shot(solo, '8-solo-load-list');

  // --- The canvas follows the browser window.
  await solo.setViewportSize({ width: 1280, height: 800 });
  await sleep(500);
  const canvas = await solo.evaluate(() => {
    const c = document.getElementById('canvas');
    return { width: c.width, height: c.height };
  });
  if (canvas.width !== 1280 || canvas.height !== 800) {
    errors.push(`canvas is ${canvas.width}x${canvas.height} after resizing the page to 1280x800`);
  }
  await shot(solo, '9-resized');
} finally {
  await browser.close();
  files.close();
}

if (errors.length) {
  console.error('Page errors:\n' + errors.join('\n'));
  process.exit(1);
}
console.log(`OK. Screenshots in ${outDir}`);
