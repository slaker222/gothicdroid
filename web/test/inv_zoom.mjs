export default async ({ page, cdp, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(1000);
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 800, height: 380, deviceScaleFactor: 4, mobile: true });
  await sleep(2500);
  await page.keyboard.press('Tab'); await sleep(2000);
  await page.screenshot({ path: OUT + '/inv_zoom.png', clip: { x: 425, y: 130, width: 70, height: 60, scale: 8 } });
  await page.screenshot({ path: OUT + '/inv_zoom2.png', clip: { x: 520, y: 270, width: 120, height: 100, scale: 6 } });
};
