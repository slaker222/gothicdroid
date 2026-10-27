// press the stick right after a dialog ends, several times; every press must send a key
export default async ({ page, cdp, sleep }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const st = () => page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui');
    return { home: s.classList.contains('home'), on: s.classList.contains('on'), nolook: u.classList.contains('no-look'), l: s.style.left, t: s.style.top }; });
  let t0 = 0;
  for (let i = 0; i < 400; i++) {
    const v = await vis();
    if (v.includes('Jump')) { t0 = Date.now(); break; }
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(100);
  }
  console.log('world after', Date.now() - t0, 'ms', JSON.stringify(await st()));
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top) }; });
  for (let n = 0; n < 4; n++) {
    log.length = 0;
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] });
    await sleep(60);
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x + 40, y: r.y - 40, id: 1 }] });
    await sleep(200);
    console.log('press', n, JSON.stringify(log), JSON.stringify(await st()));
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    await sleep(300);
  }
};
