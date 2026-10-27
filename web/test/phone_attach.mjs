// attach to the owner's open game tab on the phone (https://192.168.0.100:8321) and print / shoot its state
import puppeteer from 'puppeteer-core';
const b = await puppeteer.connect({ browserURL: 'http://127.0.0.1:9222', defaultViewport: null });
const pages = await b.pages();
for (const p of pages) console.log(p.url().slice(0, 60), '|', await p.title());
const page = pages.find(p => p.url().includes('192.168.0.100:8321') && /OpenGothic/.test(p.url() + '') ) || pages.find(p => p.url().includes('8321'));
const g = pages.filter(p => p.url().includes('192.168.0.100:8321'));
for (let i = 0; i < g.length; i++) {
  const info = await g[i].evaluate(() => ({ t: document.title, run: typeof g_runtimeUp, w: innerWidth, h: innerHeight, lay: [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title) })).catch(e => String(e));
  console.log(i, JSON.stringify(info));
  await g[i].screenshot({ path: 'out/phone_tab' + i + '.png' }).catch(e => console.log('shot', String(e).slice(0, 80)));
}
await b.disconnect();
