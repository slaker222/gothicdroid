import { attach, sleep, touches } from './phone_lib.mjs';
const { b, page, cdp, vis, st } = await attach();
const { tapBtn } = touches(page, cdp);
const stickOk = async (label) => {
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'), u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top) }; });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(80);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x + 30, y: r.y - 30, id: 1 }] }); await sleep(250);
  const on = await page.evaluate(() => document.getElementById('touch-stick').classList.contains('on'));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(300);
  console.log(label, 'stick works:', on);
};
const wait = async (cond, ms = 6000) => { for (let i = 0; i < ms / 300; i++) { if (await cond()) return true; await sleep(300); } return false; };
await stickOk('baseline');
// 1. talk and finish at once
await tapBtn('Talk', 180); await wait(async () => (await vis()).includes('OK'));
await tapBtn('Down', 150); await sleep(300); await tapBtn('Down', 150); await sleep(300); await tapBtn('OK', 180);
await wait(async () => (await vis()).includes('Jump'));
await sleep(500);
await stickOk('after dialog without trade');
// 2. talk, trade window, close, finish
await tapBtn('Talk', 180); await wait(async () => (await vis()).includes('OK'));
await tapBtn('Down', 150); await sleep(300); await tapBtn('OK', 180);
await wait(async () => (await vis()).includes('Close'));
await sleep(500);
await tapBtn('Close', 150);
await wait(async () => (await vis()).includes('OK'));
await tapBtn('Down', 150); await sleep(300); await tapBtn('Down', 150); await sleep(300); await tapBtn('OK', 180);
await wait(async () => (await vis()).includes('Jump'));
await sleep(500);
await stickOk('after the trade');
await b.disconnect();
