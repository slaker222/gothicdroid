// finger on the stick in the world, a dialog starts, ends: the stick works again without a new touch
export default async ({ page, cdp, sleep }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const leave = async () => { for (let i = 0; i < 25; i++) { const v = await vis();
    if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(1000); } };
  await leave(); await sleep(1000);
  const r = await page.evaluate(() => { const s = document.getElementById('touch-stick'); const u = document.getElementById('touch-ui').getBoundingClientRect(); return { x: u.left + parseFloat(s.style.left), y: u.top + parseFloat(s.style.top) }; });
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y, id: 1 }] });
  await sleep(100);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ x: r.x, y: r.y + 45, id: 1 }] });
  await sleep(300);
  console.log('in world', JSON.stringify(log));
  log.length = 0;
  await page.keyboard.press('Escape');         // the game menu opens, the finger stays down
  await sleep(1500);
  console.log('menu', JSON.stringify(await vis()), JSON.stringify(log));
  log.length = 0;
  await page.keyboard.press('Escape'); await sleep(1500);
  console.log('world again', JSON.stringify(await vis()), JSON.stringify(log));
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
};
