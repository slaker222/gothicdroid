import { attach, sleep } from './phone_lib.mjs';
const { b, page, cdp, vis } = await attach();
const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top), rad: s.offsetWidth / 2 }; });
await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(100);
for (let i = 0; i < 30; i++) {
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y + r.rad * 0.9, id: 1 }] }); await sleep(150);
  if ((await vis()).includes('Talk')) break;
}
await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
await sleep(800);
console.log(JSON.stringify(await vis()));
await page.screenshot({ path: 'out/pt_back.png' });
await b.disconnect();
