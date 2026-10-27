// slow / normal walk from the stick deflection, and the F2 console (marvin on)
export default async ({ page, cdp, sleep, OUT }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const state = () => page.evaluate(() => Module.ccall('og_ui_state', 'number', [], []));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(1000);
  const bit = (st, n) => !!(st & (1 << n));
  console.log('start slow=', bit(await state(), 28));
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect();
    return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top), rad: s.offsetWidth / 2 }; });
  // a little pull: slow walk
  log.length = 0;
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] });
  await sleep(80);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y - r.rad * 0.35, id: 1 }] });
  await sleep(900);
  console.log('small pull: slow=', bit(await state(), 28), JSON.stringify(log.filter(l => !l.includes('repeat'))));
  // pull further: normal
  log.length = 0;
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y - r.rad * 0.9, id: 1 }] });
  await sleep(900);
  console.log('big pull: slow=', bit(await state(), 28), JSON.stringify(log.filter(l => !l.includes('repeat'))));
  // back to a little pull and release
  log.length = 0;
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y - r.rad * 0.35, id: 1 }] });
  await sleep(900);
  console.log('small again: slow=', bit(await state(), 28));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await sleep(900);
  console.log('released: slow=', bit(await state(), 28));
  // console
  await page.evaluate(() => ogSet('marvin', 1));
  await sleep(500);
  await page.keyboard.press('F2');
  await sleep(1500);
  const t0 = await page.evaluate(() => performance.now());
  await page.keyboard.type('hello');
  await sleep(1000);
  const t1 = await page.evaluate(() => performance.now());
  console.log('page alive after F2:', t1 - t0 < 3000, 'ms', Math.round(t1 - t0));
  await page.screenshot({ path: OUT + '/tw_console.png' });
  await page.keyboard.press('Escape'); await sleep(800);
  console.log('after Esc', JSON.stringify(await vis()));
};
