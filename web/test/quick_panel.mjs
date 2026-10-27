// the quick panel: the letter of the start inventory (a scroll) is in it; a tap uses it (reads the letter)
export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(2000);
  console.log('slots', await page.evaluate(() => Module.ccall('og_quickslots', 'string', [], [])));
  console.log('arrow', await page.evaluate(() => !document.getElementById('tq-open').classList.contains('hidden')));
  await page.screenshot({ path: OUT + '/qp_closed.png' });
  await page.click('#tq-open'); await sleep(1500);
  await page.screenshot({ path: OUT + '/qp_open.png' });
  await page.click('.tq-slot'); await sleep(1800);
  console.log('after tap', JSON.stringify(await vis()));
  await page.screenshot({ path: OUT + '/qp_used.png' });
};
