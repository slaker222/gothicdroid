import { attach, sleep, touches } from './phone_lib.mjs';
const { b, page, cdp, vis, st } = await attach();
const { tapBtn } = touches(page, cdp);
await page.evaluate(() => { window.__touchDebug = true; if (!window.__hooked) { window.__tl = []; window.__hooked = true; const l = console.log; console.log = (...a) => { if (String(a[0]).startsWith('[touch]') && !String(a[0]).includes('repeat')) window.__tl.push(String(a[0])); l.apply(console, a); }; } window.__tl.length = 0; });
const keys = () => page.evaluate(() => { const k = window.__tl.slice(); window.__tl.length = 0; return k; });
console.log('layout', JSON.stringify(await vis()));
const info = await page.evaluate(() => { const s = document.getElementById('touch-stick'), u = document.getElementById('touch-ui'); const r = s.getBoundingClientRect(), ur = u.getBoundingClientRect();
  return { x: ur.left + parseFloat(s.style.left), y: ur.top + parseFloat(s.style.top), rad: s.offsetWidth / 2, cls: s.className, nolook: u.classList.contains('no-look'), w: ur.width, h: ur.height }; });
console.log('stick', JSON.stringify(info));
// 1. the stick
await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: info.x, y: info.y, id: 1 }] }); await sleep(100);
await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: info.x + info.rad * 0.8, y: info.y - info.rad * 0.6, id: 1 }] }); await sleep(800);
console.log('stick', JSON.stringify(await keys()));
await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(300); await keys();
// 2. look: a swipe on the right part
await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: info.w * 0.6, y: info.h * 0.5, id: 2 }] });
for (let i = 1; i <= 6; i++) { await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: info.w * 0.6 + i * 25, y: info.h * 0.5, id: 2 }] }); await sleep(30); }
await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(300);
await page.screenshot({ path: 'out/pc_look.png' });
// 3. buttons
for (const t of ['Jump', 'Draw weapon', 'Put the weapon away']) { const ok = await tapBtn(t, 150); await sleep(600); console.log(t, ok, JSON.stringify(await keys())); }
await b.disconnect();
