// Screenshots of the page while the game starts: the picture under the status text (#boot-bg) must show
// instead of black and fade out when the engine draws.
//
//   node bootbg.mjs [--url=http://127.0.0.1:8324/?game=2] [--profile=profile_g2]
//
// Needs the game installed in the profile (run gtest.mjs once). Writes out/boot_<n>.png and prints, per shot,
// the frame count of the engine, whether #boot-bg is still there and the colour of its centre pixel.
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
await page.goto(url, { waitUntil: 'domcontentloaded' });
const rows = [];
for (let i = 0; i < 12; i++) {
  const st = await page.evaluate(() => {
    const b = document.getElementById('boot-bg');
    return { frames: (window.ogFrameStats || {}).count || 0, bg: !!b, gone: !!(b && b.classList.contains('gone')),
             img: b ? getComputedStyle(b).backgroundImage.slice(-14) : '', status: (document.getElementById('status-text') || {}).innerText };
  });
  await page.screenshot({ path: path.join(OUT, `boot_${i}.png`) });
  rows.push({ i, ...st });
  console.log(JSON.stringify({ i, ...st }));
  if (st.frames > 8 && !st.bg) break;
  await sleep(i < 4 ? 700 : 2500);
}
await browser.close();
