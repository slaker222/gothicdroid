import puppeteer from 'puppeteer-core';
import path from 'path';
const HERE = path.dirname(new URL(import.meta.url).pathname);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const browser = await puppeteer.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false, timeout: 120000,
  userDataDir: path.join(HERE, 'profile'), defaultViewport: null,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,WebGPU', '--ozone-platform=x11', '--window-size=1280,800', '--no-first-run'] });
const page = (await browser.pages())[0];
page.on('console', m => { const t = m.text(); if (/video|bink|logo/i.test(t) && !/WebGPU\] frame/.test(t)) console.log('  ' + t.slice(0, 200)); });
await page.goto('http://127.0.0.1:8322/?archive=/game_web.zip');
for (let i = 0; i < 20; ++i) { await sleep(1500); await page.screenshot({ path: path.join(HERE, 'out', 'bik' + i + '.png') }); }
await browser.close();
