import { attach, sleep } from './phone_lib.mjs';
const { b, page, cdp } = await attach();
console.log(JSON.stringify(await page.evaluate(() => ({ iw: innerWidth, ih: innerHeight, vv: [visualViewport.width, visualViewport.height, visualViewport.scale, visualViewport.offsetLeft, visualViewport.offsetTop, visualViewport.pageLeft, visualViewport.pageTop], scroll: [scrollX, scrollY], fs: !!document.fullscreenElement, plock: !!document.pointerLockElement, dpr: devicePixelRatio, active: document.activeElement && document.activeElement.tagName }))));
await page.evaluate(() => { window.__ev = []; for (const t of ['pointerdown', 'pointermove', 'pointerup']) window.addEventListener(t, e => window.__ev.push(t + ':' + e.pointerType + ':' + Math.round(e.clientX) + ',' + Math.round(e.clientY) + ' mv=' + e.movementX + ',' + e.movementY + ' @' + (e.target.id || e.target.tagName)), true); });
for (const [x, y] of [[200, 200], [86, 328], [600, 100]]) {
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y, id: 1 }] }); await sleep(100);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(200);
}
console.log(JSON.stringify(await page.evaluate(() => window.__ev)));
await b.disconnect();
