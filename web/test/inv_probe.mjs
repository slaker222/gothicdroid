// the inventory on a real phone screen size (914x411 css px, dpr 2.625): are the items on their slots?
export default async ({ page, cdp, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 914, height: 411, deviceScaleFactor: 2.625, mobile: true });
  await sleep(2500);
  await page.keyboard.press('Tab'); await sleep(1800);
  await page.screenshot({ path: OUT + '/inv_real.png' });
  console.log(JSON.stringify(await page.evaluate(() => { const c = document.getElementById('canvas'); const r = c.getBoundingClientRect(); return { w: r.width, h: r.height, cw: c.width, ch: c.height, iw: innerWidth, ih: innerHeight }; })));
};
