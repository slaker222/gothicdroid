// swipe along the quick row: screenshots in the middle of the scroll and after it
export default async ({ page, cdp, sleep, OUT }) => {
  const r = await page.evaluate(() => { const e = document.getElementById('tq-hit').getBoundingClientRect(); return { x: e.left, y: e.top, w: e.width, h: e.height }; });
  const y = r.y + r.h / 2, x0 = r.x + r.w * 0.8;
  const t = (type, x) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' ? [] : [{ x, y, id: 1 }] });
  await t('touchStart', x0); await sleep(50);
  for (let i = 1; i <= 6; i++) { await t('touchMove', x0 - i * r.w * 0.04); await sleep(30); }
  await sleep(200);
  await page.screenshot({ path: OUT + '/hb_drag.png' });
  await t('touchEnd'); await sleep(60);
  await page.screenshot({ path: OUT + '/hb_settle.png' });
  await sleep(800);
  await page.screenshot({ path: OUT + '/hb_end.png' });
};
