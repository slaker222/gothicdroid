import { attach, sleep, touches } from './phone_lib.mjs';
const { b, page, cdp, vis, st } = await attach();
const { tapAt, tapBtn, canvasAt } = touches(page, cdp);
await page.evaluate(() => { window.__touchDebug = true; window.__tl = []; const l = console.log; if (!window.__hooked) { window.__hooked = true; console.log = (...a) => { if (String(a[0]).startsWith('[touch]') && !String(a[0]).includes('repeat')) window.__tl.push(String(a[0])); l.apply(console, a); }; } });
const show = async (l) => console.log(l, JSON.stringify(await vis()), (await st()).toString(2));
await show('start');
await tapBtn('Talk', 200); await sleep(2500); await show('dialog');
await tapBtn('Down', 150); await sleep(400); await tapBtn('OK', 200); await sleep(3500); await show('trade');
// touches in the trade window: items of the trader (left) and of the hero (right), empty place
await canvasAt(0.12, 0.38); await sleep(500);
await canvasAt(0.72, 0.38); await sleep(500);
await canvasAt(0.3, 0.2); await sleep(500);
await show('after canvas taps');
await page.screenshot({ path: 'out/pt_6a.png' });
await tapBtn('Close', 150); await sleep(2500); await show('closed');
await tapBtn('Down', 150); await sleep(300); await tapBtn('Down', 150); await sleep(300); await tapBtn('OK', 200); await sleep(3000); await show('dialog over');
await page.screenshot({ path: 'out/pt_6b.png' });
// the stick with a real touch
const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top), rad: s.offsetWidth / 2 }; });
await page.evaluate(() => { window.__tl.length = 0; });
await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(100);
await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y - r.rad * 0.9, id: 1 }] }); await sleep(1200);
console.log('stick keys', JSON.stringify(await page.evaluate(() => window.__tl)));
await page.screenshot({ path: 'out/pt_6c.png' });
await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
await b.disconnect();
