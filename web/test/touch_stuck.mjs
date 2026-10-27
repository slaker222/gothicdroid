// a finger held on the stick during a dialog must drive the stick when the world is back
export default async ({ page, cdp, sleep, OUT }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; window.addEventListener('pointerdown', e => console.log('[touch] pd ' + e.pointerType + ' ' + e.pointerId + ' ' + e.target.id + e.target.className), true); window.addEventListener('pointerup', e => console.log('[touch] pu ' + e.pointerId), true); window.addEventListener('pointercancel', e => console.log('[touch] pc ' + e.pointerId), true); });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  // finger down on the stick's resting place and a bit up, while the dialog is on
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top) }; });
  const fx = r.x, fy = r.y;
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: fx, y: fy, id: 1 }] });
  await sleep(200);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: fx, y: fy - 45, id: 1 }] });
  console.log('layout before', JSON.stringify(await vis()));
  for (let i = 0; i < 20; i++) {
    const v = await vis();
    if (v.includes('Jump') || v.includes('Draw weapon')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(1000);
  }
  await sleep(600);
  console.log('world', JSON.stringify(await vis()), 'log', JSON.stringify(log.slice(-6)));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  await sleep(300);
  console.log('after release', JSON.stringify(log.slice(-3)));
};
