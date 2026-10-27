import { attach, sleep } from './phone_lib.mjs';
const { b, page, cdp } = await attach();
const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'), u = document.getElementById('touch-ui'); const ur = u.getBoundingClientRect();
  const x = ur.left + parseFloat(s.style.left), y = ur.top + parseFloat(s.style.top);
  const el = document.elementFromPoint(x, y); const d = n => n ? (n.tagName + '#' + n.id + '.' + n.className) : null;
  const look = document.getElementById('touch-look');
  return { x, y, top: d(el), lookPE: getComputedStyle(look).pointerEvents, lookDisp: getComputedStyle(look).display, uiPE: getComputedStyle(u).pointerEvents, uiCls: u.className, bodyCls: document.body.className,
    stack: document.elementsFromPoint(x, y).slice(0, 6).map(d) }; });
console.log(JSON.stringify(r, null, 1));
// listen on the look element and the window
await page.evaluate(() => { window.__ev = []; for (const t of ['pointerdown', 'pointerup', 'pointercancel', 'lostpointercapture']) { window.addEventListener(t, e => window.__ev.push(t + ':' + e.pointerId + ':' + (e.target.id || e.target.className || e.target.tagName)), true); } });
await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(100);
await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x + 40, y: r.y - 40, id: 1 }] }); await sleep(500);
console.log('events', JSON.stringify(await page.evaluate(() => window.__ev)));
console.log('stick class', await page.evaluate(() => document.getElementById('touch-stick').className));
await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
await b.disconnect();
