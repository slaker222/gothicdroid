// an inventory window with taps on the canvas (items), closed by the button: does the stick work afterwards?
export default async ({ page, cdp, sleep, OUT }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]') && !t.includes('repeat')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const state = () => page.evaluate(() => ({ lay: document.body.dataset.lay, st: Module.ccall('og_ui_state', 'number', [], []).toString(2) }));
  const tap = (t) => page.evaluate((t) => { const b = [...document.querySelectorAll('.tbtn')].find(e => e.title.startsWith(t) && getComputedStyle(e).display !== 'none'); if (!b) return false;
    b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); return true; }, t);
  const touch = async (x, y, ms = 80) => { await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y, id: 3 }] }); await sleep(ms); await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); };
  for (let i = 0; i < 80; i++) { const v = await vis(); if (v.includes('Jump') && i > 8) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    else if (v.includes('Close') && !v.includes('Jump')) await page.keyboard.press('Escape');
    await sleep(500); }
  await sleep(1500);
  const stickKeys = async (label) => {
    const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top), rad: s.offsetWidth / 2 }; });
    log.length = 0;
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(80);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y - r.rad * 0.9, id: 1 }] }); await sleep(400);
    const got = JSON.stringify(log);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(300);
    console.log(label, 'stick ->', got);
  };
  await stickKeys('before');
  for (let round = 0; round < 3; round++) {
    await tap('Inventory'); await sleep(1500);
    // taps on the item grid and on empty space (the engine takes them as the mouse)
    const cr = await page.evaluate(() => { const c = document.getElementById('canvas').getBoundingClientRect(); return { x: c.left, y: c.top, w: c.width, h: c.height }; });
    await touch(cr.x + cr.w * 0.66, cr.y + cr.h * 0.45); await sleep(300);
    await touch(cr.x + cr.w * 0.5, cr.y + cr.h * 0.2); await sleep(300);
    console.log('round', round, 'layout', JSON.stringify(await vis()));
    await tap('Close'); await sleep(1200);
    console.log('after close', JSON.stringify(await vis()));
    await stickKeys('after inventory ' + round);
  }
};
