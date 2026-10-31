// Text language changed in the MAIN MENU of Gothic 2 (no game running): the menu must show the new language
// (the font of that language, not the glyphs of the old one). Screenshots of the menu after each change.
//
//   node g2_lang_menu.mjs [--url=http://127.0.0.1:8324/?game=2] [--profile=profile_g2] [--seq=en,ru]
//
// Needs the game installed in the profile (gtest.mjs once). Writes out/g2menu_<n>_<lang>.png.
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
const seq = (args.seq || 'en,ru').split(',');

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
const logs = [];
page.on('console', m => { const t = m.text(); if (/switchText|text language|remapped|unable to|text change|text files|Cannot load|font|reloadScripts/i.test(t)) logs.push(t.slice(0, 220)); });
await page.goto(args.url || 'http://127.0.0.1:8324/?game=2', { waitUntil: 'domcontentloaded' });
// the main menu is up when the engine draws frames
for (let i = 0; i < 120; i++) {
  const n = await page.evaluate(() => (window.ogFrameStats || {}).count || 0);
  if (n > 150) break;
  await sleep(1000);
}
await sleep(4000);
await page.screenshot({ path: path.join(OUT, 'g2menu_0_start.png') });
let step = 1;
for (const lang of seq) {
  await page.select('#lang-text', lang);
  const t0 = Date.now();
  let final = '';
  while (Date.now() - t0 < 180000) {
    await sleep(500);
    final = await page.evaluate(() => document.getElementById('voice-text').textContent + ' | ' + document.getElementById('lang-note').textContent);
    if (/ready|failed|finish/.test(final) || (Date.now() - t0 > 15000 && !/waiting/.test(final))) break;
  }
  await sleep(4000);
  await page.screenshot({ path: path.join(OUT, `g2menu_${step}_${lang}.png`) });
  console.log(JSON.stringify({ step, lang, final }));
  step++;
}
console.log(JSON.stringify({ logs }));
await browser.close();
