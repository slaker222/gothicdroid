// the ring of ten number keys: arrows (a page), swipe along the row (a slot per slot width), a tap on a filled slot
export default async ({ page, cdp, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  for (let i = 0; i < 40; i++) { const v = await vis(); if (v.includes('Close') && !v.includes('Jump')) await page.keyboard.press('Escape'); else if (v.includes('Jump') && i > 6) break; await sleep(700); }
  await sleep(1500);
  const keys = () => page.evaluate(() => [...document.querySelectorAll('.tq-slot span')].map(e => e.textContent).join(' '));
  console.log('start', await keys(), JSON.parse(await page.evaluate(() => Module.ccall('og_quickslots', 'string', [], []))).t);
  await page.screenshot({ path: OUT + '/qs_start.png' });
  // swipe: drag to the left by 2 slots
  const r = await page.evaluate(() => { const h = document.getElementById('tq-hit').getBoundingClientRect(); return { x: h.left, y: h.top + h.height / 2, w: h.width }; });
  const pitch = r.w / 4;
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x + r.w - 10, y: r.y, id: 1 }] });
  for (let i = 1; i <= 8; i++) { await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x + r.w - 10 - pitch * 2 * i / 8, y: r.y, id: 1 }] }); await sleep(40); }
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await sleep(900);
  console.log('after swipe left by 2 slots', await keys(), 'arrows', await page.evaluate(() => document.querySelectorAll('.tq-page').length));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x + 10, y: r.y, id: 1 }] });
  for (let i = 1; i <= 8; i++) { await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x + 10 + pitch * 3 * i / 8, y: r.y, id: 1 }] }); await sleep(40); }
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await sleep(900);
  console.log('after swipe right by 3 slots', await keys());
  await page.screenshot({ path: OUT + '/qs_end.png' });
};
