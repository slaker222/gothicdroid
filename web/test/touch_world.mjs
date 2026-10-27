// world with the touch layer: screenshots of the button layouts
export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const tap = (title) => page.evaluate((t) => { const b = document.querySelector('.tbtn[title="' + t + '"]');
    b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 }));
    b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); }, title);
  // leave the dialog
  for (let i = 0; i < 20; i++) {
    const v = await vis();
    if (v.includes('Jump') || v.includes('Draw weapon')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(1500);
  }
  await sleep(1500);
  console.log('world', JSON.stringify(await vis()));
  await page.screenshot({ path: OUT + '/tw_world.png' });
  await tap('More'); await sleep(300);
  await page.screenshot({ path: OUT + '/tw_tools.png' });
  // weapon out
  await tap('More'); await sleep(200);
  await tap('Draw weapon'); await sleep(1500);
  console.log('weapon', JSON.stringify(await vis()));
  await page.screenshot({ path: OUT + '/tw_weapon.png' });
};
