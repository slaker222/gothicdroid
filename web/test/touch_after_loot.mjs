// after a loot window (like a chest / trade): do the touch controls still move the hero?
import fs from 'fs';
export default async ({ page, cdp, sleep, OUT }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]') && !t.includes('repeat')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const st = () => page.evaluate(() => Module.ccall('og_ui_state', 'number', [], []));
  const tap = (t) => page.evaluate((t) => { const b = [...document.querySelectorAll('.tbtn')].find(e => e.title.startsWith(t) && getComputedStyle(e).display !== 'none'); if (!b) return false;
    b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); return true; }, t);
  await page.evaluate(() => ogSet('marvin', 1));
  for (let i = 0; i < 80; i++) { const v = await vis(); if (v.includes('Talk') && v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    else if (v.includes('Close') && !v.includes('Jump')) await page.keyboard.press('Escape');
    await sleep(400); }
  console.log('focus', JSON.stringify(await vis()));
  await page.keyboard.press('F2'); await sleep(600); await page.keyboard.type('kill'); await page.keyboard.press('Enter'); await sleep(600);
  await page.keyboard.press('Escape'); await sleep(3000);
  console.log('after kill', JSON.stringify(await vis()));
  for (let i = 0; i < 8; i++) { const v = await vis(); if (v.includes('Loot')) break; await page.keyboard.down('KeyW'); await sleep(200); await page.keyboard.up('KeyW'); await sleep(500); }
  console.log('near the body', JSON.stringify(await vis()));
  const looted = await tap('Loot'); await sleep(1500);
  console.log('loot tapped', looted, 'layout', JSON.stringify(await vis()), 'state', (await st()).toString(2));
  await page.screenshot({ path: OUT + '/tl_loot.png' });
  await tap('Close'); await sleep(1200);
  console.log('after close', JSON.stringify(await vis()), 'state', (await st()).toString(2));
  await page.screenshot({ path: OUT + '/tl_after.png' });
  // does the stick work: keys sent and the hero moves?
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top), rad: s.offsetWidth / 2 }; });
  log.length = 0;
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(80);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y - r.rad * 0.9, id: 1 }] }); await sleep(1500);
  await page.screenshot({ path: OUT + '/tl_moved.png' });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
  console.log('stick keys', JSON.stringify(log));
};
