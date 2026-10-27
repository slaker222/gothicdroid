// does a tap on the canvas take the pointer lock in touch mode? (that breaks the stick: touch coordinates become 0,0)
export default async ({ page, cdp, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 80; i++) { const v = await vis(); if (v.includes('Jump') && i > 8) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    else if (v.includes('Close') && !v.includes('Jump')) await page.keyboard.press('Escape');
    await sleep(500); }
  await sleep(1500);
  const lock = () => page.evaluate(() => !!document.pointerLockElement);
  console.log('before tap, locked:', await lock());
  // open the inventory and tap an item (the canvas takes the tap)
  await page.evaluate(() => { const b = [...document.querySelectorAll('.tbtn')].find(e => e.title.startsWith('Inventory') && getComputedStyle(e).display !== 'none'); b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); setTimeout(() => b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })), 150); });
  await sleep(1500);
  console.log('inventory layout', JSON.stringify(await vis()));
  const c = await page.evaluate(() => { const r = document.getElementById('canvas').getBoundingClientRect(); return { x: r.left + r.width * 0.5, y: r.top + r.height * 0.4 }; });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: c.x, y: c.y, id: 1 }] }); await sleep(100);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(600);
  console.log('after a tap on the canvas, locked:', await lock(), JSON.stringify(await vis()));
  await page.evaluate(() => { const b = [...document.querySelectorAll('.tbtn')].find(e => e.title.startsWith('Close') && getComputedStyle(e).display !== 'none'); if (!b) return; b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); setTimeout(() => b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })), 150); });
  await sleep(1500);
  for (let i = 0; i < 6 && !(await vis()).includes('Jump'); i++) { await page.keyboard.press('Escape'); await sleep(900); }
  console.log('back in the world:', JSON.stringify(await vis()), 'locked:', await lock());
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'), u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top) }; });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] }); await sleep(80);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x + 30, y: r.y - 30, id: 1 }] }); await sleep(300);
  console.log('stick works after the inventory:', await page.evaluate(() => document.getElementById('touch-stick').classList.contains('on')));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
};
