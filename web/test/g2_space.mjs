// Space checks of the language change, with a made-up storage quota (navigator.storage.estimate is replaced:
// quota = what is used now + FREE_MB, so there is little room left).
//
//   node g2_space.mjs [--url=http://127.0.0.1:8324/?game=2] [--profile=profile_g2] [--voice=de | --text=de] [--free=80] [--bad]
//
// A: the voice is changed in the menu with FREE_MB free: the page must say the two languages do not fit, restart,
//    remove the previous voice first and install the new one (the game starts, the old speech archives are gone).
// B (--bad, FREE_MB very small): not even that fits: an error with the reserve for savegames, the old language
//    stays untouched.
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
const voice = args.voice || 'de';
const freeMb = +(args.free || (args.bad ? 5 : 80));
const url = args.url || 'http://127.0.0.1:8324/?game=2';

const browser = await puppeteer.launch({
  executablePath: '/usr/bin/google-chrome-stable',
  headless: false,
  timeout: 120000,
  userDataDir: path.join(HERE, args.profile || 'profile_g2'),
  defaultViewport: null,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,WebGPU', '--ozone-platform=x11',
         '--window-size=1280,800', '--no-first-run', '--ignore-certificate-errors'],
});
const page = (await browser.pages())[0];
// quota made up from localStorage.__fakeQuota (bytes) while it is set
await page.evaluateOnNewDocument(() => {
  const orig = navigator.storage.estimate.bind(navigator.storage);
  navigator.storage.estimate = async () => {
    const e = await orig();
    const q = +localStorage.getItem('__fakeQuota');
    return q ? { quota: q, usage: e.usage } : e;
  };
});
const dialogs = [];
page.on('dialog', async d => { dialogs.push(d.message()); await d.accept(); });
const logs = [];
page.on('console', m => { const t = m.text(); if (/not enough|removed .* files|game installed|SHELL\] remov/i.test(t)) logs.push(t.slice(0, 240)); });

async function waitState(what, ms = 400000, origin0 = null) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    try {
      const st = await page.evaluate(() => ({ frames: (window.ogFrameStats || {}).count || 0, origin: performance.timeOrigin,
                                              status: (document.getElementById('status-text') || {}).innerText || '' }));
      if (origin0 !== null && st.origin === origin0) { await sleep(500); continue; }   // still the page before the restart
      if (what === 'menu' && st.frames > 150) return st;
      if (what === 'error' && /not enough browser storage/i.test(st.status)) return st;
    } catch (e) { /* navigating */ }
    await sleep(1000);
  }
  return null;
}
async function snapshot() {
  return await page.evaluate(async () => {
    const root = await navigator.storage.getDirectory();
    const g = await root.getDirectoryHandle('gothic2');
    const data = await g.getDirectoryHandle('Data');
    const speech = [];
    let total = 0;
    async function sum(d) { let t = 0; for await (const [n, h] of d.entries()) t += h.kind === 'file' ? (await h.getFile()).size : await sum(h); return t; }
    for await (const [n, h] of data.entries()) if (h.kind === 'file' && /^speech/i.test(n)) speech.push(n + ':' + Math.round((await h.getFile()).size / 1048576));
    const st = JSON.parse(await (await (await root.getFileHandle('gothic-langs-g2.json')).getFile()).text());
    const est = await navigator.storage.estimate();
    return { gameMB: Math.round(await sum(g) / 1048576), speech, text: st.text, voice: st.voice, usageMB: Math.round(est.usage / 1048576) };
  });
}

await page.goto(url, { waitUntil: 'domcontentloaded' });
await waitState('menu');
await sleep(3000);
const before = await snapshot();
console.log(JSON.stringify({ phase: 'before', ...before }));
await page.evaluate(mb => {
  return navigator.storage.estimate().then(e => localStorage.setItem('__fakeQuota', String(e.usage + mb * 1048576)));
}, freeMb);
const fake = await page.evaluate(() => navigator.storage.estimate());
console.log(JSON.stringify({ phase: 'fake quota', quotaMB: Math.round(fake.quota / 1048576), usageMB: Math.round(fake.usage / 1048576), freeMB: Math.round((fake.quota - fake.usage) / 1048576) }));

const origin0 = await page.evaluate(() => performance.timeOrigin);
if (args.text) await page.select('#lang-text', args.text); else await page.select('#lang-voice', voice);
const res = await waitState(args.bad ? 'error' : 'menu', 400000, origin0);
await sleep(3000);
await page.screenshot({ path: path.join(OUT, args.bad ? 'space_bad.png' : 'space_ok.png') });
let after = null;
try { after = await snapshot(); } catch (e) { after = { error: String(e) }; }
console.log(JSON.stringify({ phase: 'after', status: res && res.status.slice(0, 300), ...after }));
console.log(JSON.stringify({ dialogs, logs }));
await page.evaluate(() => localStorage.removeItem('__fakeQuota'));
await browser.close();
