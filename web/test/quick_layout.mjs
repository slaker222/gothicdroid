// the new bottom row: four slots, the inventory button right of them; the top row: menu, character, more
export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(2000);
  console.log('slots', await page.evaluate(() => Module.ccall('og_quickslots', 'string', [], [])));
  await page.screenshot({ path: OUT + '/ql_world.png' });
  const tap = (t) => page.evaluate((t) => { const b = document.querySelector('.tbtn[title^="' + t + '"]');
    b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); }, t);
  await tap('More'); await sleep(300);
  await page.screenshot({ path: OUT + '/ql_tools.png' });
  await tap('Inventory'); await sleep(1200);
  console.log('after inventory tap', JSON.stringify(await vis()));
};
