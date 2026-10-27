// give the hero a sword and a bow with the console, take them, equip them, look at the quick panel
export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const tap = (t) => page.evaluate((t) => { const b = document.querySelector('.tbtn[title^="' + t + '"]'); if (!b) return false;
    b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); return true; }, t);
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(1000);
  await page.evaluate(() => ogSet('marvin', 1)); await sleep(500);
  for (const name of ['ITMW_1H_SWORD_OLD_01', 'ITRW_BOW_SMALL_01']) {
    await page.keyboard.press('F2'); await sleep(600);
    await page.keyboard.type('insert ' + name); await page.keyboard.press('Enter'); await sleep(600);
    await page.keyboard.press('Escape'); await sleep(800);
    console.log(name, 'buttons', JSON.stringify(await vis()));
    // take what lies in the focus
    for (let k = 0; k < 4; k++) { if (await tap('Take')) { await sleep(1500); break; } await sleep(500); }
  }
  await page.screenshot({ path: OUT + '/qe_world.png' });
  // inventory: equip the sword and the bow
  await page.keyboard.press('Tab'); await sleep(1500);
  await page.screenshot({ path: OUT + '/qe_inv.png' });
  console.log('inv', JSON.stringify(await vis()));
};
