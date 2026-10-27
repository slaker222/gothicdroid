import { attach, sleep } from './phone_lib.mjs';
const { b, page, cdp } = await attach();
const out = await page.evaluate(async () => {
  const res = [];
  const t0 = performance.now();
  const lays = new Set();
  for (let i = 0; i < 40; i++) {
    const v = [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').length;
    const st = Module.ccall('og_ui_state', 'number', [], []);
    res.push(v + ':' + st.toString(16));
    await new Promise(r => setTimeout(r, 50));
  }
  return res.join(' ');
});
console.log(out);
// listener census on the look element is not possible; test a plain pointerdown on the look layer
const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'), u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top) }; });
await page.evaluate(() => { window.__ev = []; const look = document.getElementById('touch-look'); for (const t of ['pointerdown', 'pointermove', 'pointerup', 'pointercancel']) look.addEventListener(t, e => window.__ev.push(t + ':' + e.pointerId + ':' + Math.round(e.clientX) + ',' + Math.round(e.clientY))); });
await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(100);
await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x + 30, y: r.y - 30, id: 1 }] }); await sleep(300);
console.log('look events', JSON.stringify(await page.evaluate(() => window.__ev)), 'stick', await page.evaluate(() => document.getElementById('touch-stick').className));
await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(200);
console.log('look events', JSON.stringify(await page.evaluate(() => window.__ev)), 'stick', await page.evaluate(() => document.getElementById('touch-stick').className));
await b.disconnect();
