// probe: what the intro <video> looks like while it plays
import puppeteer from 'puppeteer-core';
import path from 'path';
const HERE = path.dirname(new URL(import.meta.url).pathname);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const browser = await puppeteer.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false, timeout: 120000,
  userDataDir: path.join(HERE, 'profile'), defaultViewport: null,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,WebGPU', '--ozone-platform=x11', '--window-size=1280,800', '--no-first-run'] });
const page = (await browser.pages())[0];
const cdp = await page.createCDPSession();
await cdp.send('Emulation.setDeviceMetricsOverride', { width: 800, height: 380, deviceScaleFactor: 3, mobile: true });
await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
await cdp.send('Emulation.setUserAgentOverride', { userAgent: 'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Mobile Safari/537.36' });
page.on('console', m => { const t = m.text(); if (/video|error/i.test(t)) console.log('  ' + t.slice(0, 200)); });
await page.goto('http://127.0.0.1:8322/?archive=/game_web.zip&nomenu');
for (let i = 0; i < 120; ++i) {
  await sleep(500);
  const st = await page.evaluate(() => { const v = document.getElementById('og-video'); if (!v) return null;
    const r = v.getBoundingClientRect();
    return { rs: v.readyState, w: v.videoWidth, h: v.videoHeight, paused: v.paused, t: v.currentTime, err: v.error && v.error.message, rect: [r.x, r.y, r.width, r.height], bg: getComputedStyle(v).backgroundColor, dur: v.duration, mean: (()=>{const c=document.createElement('canvas');c.width=64;c.height=36;const x=c.getContext('2d');x.drawImage(v,0,0,64,36);const d=x.getImageData(0,0,64,36).data;let s=0;for(let i=0;i<d.length;i+=4)s+=d[i]+d[i+1]+d[i+2];return s/(64*36*3);})() }; }).catch(() => null);
  if (st) { console.log(JSON.stringify(st)); if (i==30) { await page.evaluate(()=>{document.getElementById('og-video').style.visibility='visible'}); await sleep(300); await page.screenshot({ path: path.join(HERE, 'out', 'vphone.png') }); await page.evaluate(()=>{document.getElementById('og-video').style.visibility=''}); } if (i > 60) break; }
}
await browser.close();
