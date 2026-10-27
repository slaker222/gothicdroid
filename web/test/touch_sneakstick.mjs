// sneak icon on the stick (learned): hidden while pulled, double tap sends X; the K button in the cheats
export default async ({ page, cdp, sleep, OUT }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]') && !t.includes('repeat')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(1000);
  await page.evaluate(() => { const c = Module.ccall; Module.ccall = (n, ...a) => { const r = c(n, ...a); return n === 'og_ui_state' ? (r | (1 << 23)) : r; }; });
  await sleep(600);
  const shown = () => page.evaluate(() => { const i = document.querySelector('#touch-stick .stick-icon'); return getComputedStyle(i).display; });
  console.log('icon at rest:', await shown());
  await page.screenshot({ path: OUT + '/ss_rest.png' });
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top) }; });
  const down = () => cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] });
  const up = () => cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  // drag: icon hidden
  await down(); await sleep(100);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y - 40, id: 1 }] }); await sleep(300);
  console.log('icon while pulled:', await shown());
  await page.screenshot({ path: OUT + '/ss_pull.png' });
  await up(); await sleep(500);
  log.length = 0;
  // single tap: nothing; double tap: X
  await down(); await sleep(60); await up(); await sleep(1000);
  console.log('single tap:', JSON.stringify(log));
  log.length = 0;
  await down(); await sleep(60); await up(); await sleep(150); await down(); await sleep(60); await up(); await sleep(300);
  console.log('double tap:', JSON.stringify(log));
  // cheats: K button
  const tap = (t) => page.evaluate((t) => { const b = document.querySelector('.tbtn[title^="' + t + '"]');
    b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); }, t);
  await tap('More'); await sleep(300); await tap('Enable cheats'); await sleep(500);
  await page.screenshot({ path: OUT + '/ss_cheats.png' });
  console.log('buttons', JSON.stringify(await vis()));
};
