export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(700); }
  await sleep(1000);
  console.log(JSON.stringify(await page.evaluate(() => { const c = document.getElementById('canvas'); const r = c.getBoundingClientRect(); return { w: r.width, h: r.height, cw: c.width, ch: c.height, iw: innerWidth, ih: innerHeight, dpr: devicePixelRatio, touch: document.body.classList.contains('touch-on') }; })));
  await page.evaluate(() => { const b = document.querySelector('.tbtn[title="Inventory"]'); b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); });
  await sleep(2500);
  await page.screenshot({ path: OUT + '/inv_dev.png' });
};
