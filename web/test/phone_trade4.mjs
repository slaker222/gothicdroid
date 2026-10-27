import { attach, sleep } from './phone_lib.mjs';
const { b, page, cdp, vis, tap, st } = await attach();
await page.evaluate(() => { window.__touchDebug = true; window.__tl = []; const l = console.log; console.log = (...a) => { if (String(a[0]).startsWith('[touch]')) window.__tl.push(String(a[0])); l.apply(console, a); }; });
await tap('Down', 150); await sleep(400); await tap('Down', 150); await sleep(400);
await page.screenshot({ path: 'out/pt_4a.png' });
await tap('OK', 200);
for (let i = 0; i < 8; i++) { await sleep(700); console.log(i, JSON.stringify(await vis()), (await st()).toString(2)); const v = await vis(); if (v.includes('Jump')) break; }
await page.screenshot({ path: 'out/pt_4.png' });
await b.disconnect();
