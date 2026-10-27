export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { await page.keyboard.press('ArrowUp'); await sleep(200); await page.keyboard.press('Enter'); await sleep(700);
    const ok = await page.evaluate(() => Module.ccall('og_ui_state', 'number', [], []) & 1); if (ok && !(await page.evaluate(() => Module.ccall('og_ui_state', 'number', [], []) & 12))) break; }
  await sleep(1500);
  await page.keyboard.press('Tab'); await sleep(2000);
  await page.screenshot({ path: OUT + '/inv_pc.png' });
};
