export default async ({ page, sleep, OUT }) => {
  await page.keyboard.press('Tab'); await sleep(5000);
  const tag = process.env.TAG || 'base';
  const r = await page.evaluate(() => { const c = document.getElementById('canvas').getBoundingClientRect(); return { x: c.left, y: c.top, w: c.width, h: c.height }; });
  await page.screenshot({ path: OUT + '/inv_' + tag + '.png', clip: { x: r.x + r.w * 0.68, y: r.y + r.h * 0.1, width: r.w * 0.28, height: r.h * 0.3, scale: 3 } });
};
