// put the original Bink files into the test profile's OPFS (replacing the mp4 repack) to test the Bink path
import puppeteer from 'puppeteer-core';
import fs from 'fs';
import path from 'path';
const HERE = path.dirname(new URL(import.meta.url).pathname);
const browser = await puppeteer.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false, userDataDir: path.join(HERE, 'profile'), defaultViewport: null, args: ['--no-first-run'] });
const page = (await browser.pages())[0];
await page.goto('http://127.0.0.1:8322/gpucheck.html');
const list = await page.evaluate(async () => {
  const out = [];
  async function walk(d, p) { for await (const [n, h] of d.entries()) { if (h.kind === 'directory') await walk(h, p + n + '/'); else if (/\.bik$/i.test(n)) out.push(p + n); } }
  await walk(await navigator.storage.getDirectory(), '');
  return out;
});
console.log(list);
const names = ['logo1'];
for (const n of names) {
  const f = list.find(x => x.toLowerCase().endsWith('/' + n + '.bik'));
  if (!f) continue;
  const b = fs.readFileSync('/tmp/claude-1000/-home-alex-Downloads-1-gothic-android/82df0a2d-aefd-4886-a1ed-1236c2c402e3/scratchpad/v/game/_work/DATA/video/' + n + '.bik');
  await page.evaluate(async (f, arr) => {
    let d = await navigator.storage.getDirectory(); const parts = f.split('/');
    for (const p of parts.slice(0, -1)) d = await d.getDirectoryHandle(p);
    const w = await (await d.getFileHandle(parts.at(-1))).createWritable(); await w.write(new Uint8Array(arr)); await w.close();
  }, f, [...b]);
  console.log('replaced', f, b.length);
}
await browser.close();
