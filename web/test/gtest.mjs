// Browser test harness for the OpenGothic web port.
//
//   cd web/test && npm install          # once (puppeteer-core)
//   node gtest.mjs [options]
//
// Starts Chrome with WebGPU on a separate profile (web/test/profile, its OPFS
// gets the game installed from /game_web.zip on the first run, a few minutes),
// opens the page, waits for the world and does what the options ask.
// The page has to be served first: python3 web/serve.py 8322
//
// Options:
//   --url=URL          page (default http://127.0.0.1:8322/game.html)
//   --nomenu           start a new game right away (dialog with Diego)
//   --load=N           load save slot N from the main menu (instead of --nomenu)
//   --uncapped         frames do not wait for the display (benchmarks)
//   --nolimit          drop FPS_Limit from SystemPack.ini (only in this profile)
//   --phone            phone emulation: 800x380 css px, dpr 3, touch, mobile UA
//   --portrait         with --phone: 380x800
//   --dpr=N            with --phone: device pixel ratio (default 3)
//   --set=k:v,k:v      game settings via ogSet (shadows:0,drawDist:2,...)
//   --warm=S           seconds to wait in the world before measuring (default 15)
//   --measure=S        measure FPS for S seconds (engine [PERF] lines and frames)
//   --run=FILE.mjs    script run in the world: default export async ({ page, cdp, sleep, OUT }) => {...}
//   --shot=NAME        screenshot out/NAME.png at the end
//   --keep             leave the browser open (for a person to play), exit with Ctrl+C
//   --profile=DIR      profile dir (default profile); a fresh dir installs the game again
//   --jsflags=FLAGS    Chrome --js-flags (e.g. --no-wasm-inlining for honest self-time in profiles)
//   --device           Chrome on an android phone over adb (adb reverse/forward set up by this script);
//                      the page is then http://localhost:PORT/ of --url, profile options do not apply
//   --devpkg=PKG       with --device: another browser (com.opera.browser; its devtools socket is found by itself)
//   --gputime          GPU time per pass (timestamp queries): run script reads window.__gpuAgg()
//   --manual           turn the page's automatic graphics settings off (gfxManual) for stable measurements
//   --log              print the game's console
//   --nowebgpu=MODE    no usable WebGPU in the page: api (no navigator.gpu) or device (requestDevice fails); the page
//                      should fall back to WebGL 2
//   --glnos3tc=N       WebGL 2 renderer: as a phone without s3tc (1: ASTC if the browser has it, 2: RGBA8)
//   --glsrc            WebGL 2 renderer (--url=...?renderer=webgl2): print the GLSL of the gl_* shaders
//
// Prints one JSON line with the results.
import puppeteer from 'puppeteer-core';
import fs from 'fs';
import path from 'path';

const HERE = path.dirname(new URL(import.meta.url).pathname);
const args = Object.fromEntries(process.argv.slice(2).map(a => {
  const m = a.match(/^--([^=]+)(?:=(.*))?$/);
  return m ? [m[1], m[2] === undefined ? true : m[2]] : [a, true];
}));
const OUT = path.join(HERE, 'out');
fs.mkdirSync(OUT, { recursive: true });
const sleep = ms => new Promise(r => setTimeout(r, ms));

let url = args.url || 'http://127.0.0.1:8322/game.html';
const q = ['archive=/game_web.zip'];
if (args.nomenu) q.push('nomenu');
if (args.uncapped) q.push('uncapped');
if (args.device) url = url.replace('127.0.0.1', 'localhost');
url += (url.includes('?') ? '&' : '?') + q.join('&');

let deviceUrlPort = 0;
async function connectDevice() {
  const { execSync } = await import('child_process');
  const sh = c => execSync(c, { encoding: 'utf8' });
  deviceUrlPort = +new URL(url).port || 80;
  sh(`adb reverse tcp:${deviceUrlPort} tcp:${deviceUrlPort}`);
  sh('adb shell svc power stayon usb');
  sh('adb shell input keyevent KEYCODE_WAKEUP');
  const pkg = args.devpkg || 'com.android.chrome';
  sh(`adb shell am force-stop ${pkg}`);
  sh(`adb shell am start -a android.intent.action.VIEW -d about:blank ${pkg}`);
  await sleep(5000);
  // the devtools socket of a chromium browser: chrome_devtools_remote or <package>.devtools / _devtools_remote
  const socks = sh('adb shell cat /proc/net/unix').split('\n').map(l => (l.match(/@(\S*devtools\S*)$/) || [])[1]).filter(Boolean);
  const sock = socks.find(x => x.includes(pkg)) || (pkg === 'com.android.chrome' ? 'chrome_devtools_remote' : socks.find(x => x === 'chrome_devtools_remote'));
  console.log('devtools socket: ' + sock);
  sh(`adb forward tcp:9222 localabstract:${sock}`);
  return puppeteer.connect({ browserURL: 'http://127.0.0.1:9222', defaultViewport: null });
}
const browser = args.device ? await connectDevice() : await puppeteer.launch({
  executablePath: '/usr/bin/google-chrome-stable',
  headless: false,
  timeout: 120000, // the first start of a fresh profile is slow
  userDataDir: path.join(HERE, args.profile || 'profile'),
  defaultViewport: null,
  acceptInsecureCerts: true,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,WebGPU', '--ozone-platform=x11',
         '--window-size=1280,800', '--no-first-run', '--ignore-certificate-errors',
         '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
         '--disable-backgrounding-occluded-windows', ...(args.jsflags ? ['--js-flags=' + args.jsflags] : [])],
});
const page = args.device && !args.devpkg ? await browser.newPage() :
  (await browser.pages()).find(p => p.url().startsWith('about:') || p.url().includes('localhost')) || (await browser.pages())[0];
const cdp = await page.createCDPSession();

if (args.phone) {
  const portrait = !!args.portrait;
  await cdp.send('Emulation.setDeviceMetricsOverride',
    { width: portrait ? 380 : 800, height: portrait ? 800 : 380, deviceScaleFactor: +(args.dpr || 3), mobile: true });
  await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
  await cdp.send('Emulation.setUserAgentOverride', { userAgent:
    'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Mobile Safari/537.36' });
}

// game state from the console
let draws = 0, lastPerf = 0;
const perf = [], errors = [];
page.on('console', m => {
  const t = m.text();
  const d = t.match(/\[Web(?:GPU|GL)\] frame \d+: .*draws=(\d+)/);
  if (d) draws = +d[1];
  if (t.includes('[PERF]')) {
    lastPerf = Date.now();
    const g = k => +((t.match(new RegExp(k + '=([0-9.]+)')) || [])[1] || 0);
    perf.push({ at: Date.now(), frame: g('frame'), tick: g('tick'), draw: g('draw') });
  }
  if (/\[WebGPU\] error|Uncaught|Aborted/.test(t)) errors.push(t.slice(0, 300));
  if (args.log) console.log('  ' + t.slice(0, 200));
});
page.on('pageerror', e => errors.push('pageerror: ' + e.message));

if (args.manual) await page.evaluateOnNewDocument(() => { try { localStorage.setItem('gfxManual', '1'); } catch (e) {} });
if (args.glsrc) await page.evaluateOnNewDocument(() => { window.__OG_GL_SRC = 1; });
if (args.glnos3tc) await page.evaluateOnNewDocument(n => { window.__OG_GL_NOS3TC = n; }, +args.glnos3tc);
if (args.nowebgpu === 'api') await page.evaluateOnNewDocument(() => { Object.defineProperty(Navigator.prototype, 'gpu', { get: () => undefined }); });
if (args.nowebgpu === 'device') await page.evaluateOnNewDocument(() => {
  if (window.GPUAdapter) GPUAdapter.prototype.requestDevice = function () { return Promise.reject(new Error('test: no device')); };
});
if (args.gputime) await page.evaluateOnNewDocument(fs.readFileSync(path.join(HERE, 'gputime.js'), 'utf8'));
// count the frames the engine renders (one canvas texture each)
await page.evaluateOnNewDocument(() => {
  window.__frames = 0;
  if (window.GPUCanvasContext) {
    const o = GPUCanvasContext.prototype.getCurrentTexture;
    GPUCanvasContext.prototype.getCurrentTexture = function () { window.__frames++; return o.apply(this, arguments); };
  }
});
if (args.nolimit) await page.evaluateOnNewDocument(() => {
  // only this test profile: the game's own FPS limit off before it starts
  const iv = setInterval(() => {
    const M = window.Module;
    if (!M || !M.callMain || M.__wrapped) return;
    const orig = M.callMain; M.__wrapped = true; clearInterval(iv);
    M.callMain = function () {
      try {
        const p = '/gothic/system/SystemPack.ini';
        const t = new TextDecoder().decode(M.FS.readFile(p));
        M.FS.writeFile(p, t.replace(/^(\s*FPS_Limit\s*=).*$/m, '$10'));
      } catch (e) {}
      return orig.apply(this, arguments);
    };
  }, 2);
});

const t0 = Date.now();
await page.goto(url);

// install the game into a fresh profile
await sleep(4000);
await page.evaluate(() => {
  const s = document.getElementById('status-text');
  if (!document.getElementById('canvas').width && s && /download|скач/i.test(s.innerText) && typeof downloadGame === 'function')
    downloadGame();
}).catch(() => {});

const skipVideo = () => page.evaluate(() => { if (document.getElementById('og-video')) Module.__ogVideoDone = true; }).catch(() => {});
async function waitWorld(maxSec) {
  for (let i = 0; i < maxSec; ++i) {
    await sleep(1000);
    await skipVideo();
    if (draws > 50) return true;
  }
  return false;
}
async function waitMenu(maxSec) {
  for (let i = 0; i < maxSec && !lastPerf; ++i) { await sleep(1000); await skipVideo(); }
  await sleep(3000);
}

if (args.load !== undefined) {
  // main menu: Down skips the disabled entries to "settings", Up is "load game"
  await waitMenu(900);
  await page.bringToFront();
  const key = async (k, n = 1) => { for (let i = 0; i < n; ++i) { await page.keyboard.press(k); await sleep(500); } };
  await key('ArrowDown'); await key('ArrowUp'); await key('Enter');
  await sleep(800);
  await key('ArrowDown', Math.max(0, +args.load - 1));
  await key('Enter');
}
const inWorld = (args.nomenu || args.load !== undefined) ? await waitWorld(900) : (await waitMenu(900), false);

if (args.set) {
  for (const kv of String(args.set).split(',')) {
    const [k, v] = kv.split(':');
    await page.evaluate((k, v) => ogSet(k, v), k, +v);
  }
  await sleep(1500);
}

const res = { url, inWorld, startSec: +((Date.now() - t0) / 1000).toFixed(1) };
if (inWorld) await sleep((+args.warm || 15) * 1000);
if (args.measure) {
  const ms = +args.measure * 1000, from = Date.now();
  const f0 = await page.evaluate(() => window.__frames);
  await sleep(ms);
  const f1 = await page.evaluate(() => window.__frames);
  const p = perf.filter(x => x.at >= from);
  const avg = k => p.length ? +(p.reduce((a, x) => a + x[k], 0) / p.length).toFixed(2) : 0;
  Object.assign(res, { fps: +((f1 - f0) * 1000 / ms).toFixed(1), frameMs: avg('frame'), tickMs: avg('tick'), drawMs: avg('draw') });
}
if (args.run) await (await import(path.resolve(args.run))).default({ page, cdp, sleep, OUT });
if (args.shot) { await page.screenshot({ path: path.join(OUT, args.shot + '.png') }); res.shot = 'web/test/out/' + args.shot + '.png'; }
res.errors = errors.slice(0, 10);
console.log(JSON.stringify(res));

if (args.keep) { console.log('browser left open; Ctrl+C to quit'); await new Promise(() => {}); }
if (args.device) await browser.disconnect(); else await browser.close();
