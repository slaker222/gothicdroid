export default async ({ page, sleep }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 40; i++) { const v = await vis(); if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    else if (v.includes('Close') && !v.includes('Jump')) await page.keyboard.press('Escape');
    else if (v.includes('Jump') && i > 8) break; await sleep(700); }
  await sleep(1500);
  const keys = () => page.evaluate(() => [...document.querySelectorAll('.tq-slot span')].map(e => e.textContent).join(' '));
  const arrows = await page.$$('.tq-page');
  for (const [name, a] of [['prev', 0], ['prev', 0], ['prev', 0], ['next', 1], ['next', 1]]) {
    await arrows[a].click(); await sleep(900);
    console.log(name, await keys(), await page.evaluate(() => JSON.stringify({ f: window.__f })));
  }
};
