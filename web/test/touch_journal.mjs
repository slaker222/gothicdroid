// the touch journal on fake quests (the game has none at the start): list, entries, close, reopen
import fs from 'fs';
export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(1500);
  console.log('real export', await page.evaluate(() => Module.ccall('og_quests', 'string', [], [])));
  const fake = fs.readFileSync(new URL('./fake_quests.json', import.meta.url), 'utf8');
  await page.evaluate((fake) => { const c = Module.ccall; Module.ccall = (n, ...a) => n === 'og_quests' ? fake : c(n, ...a); }, fake);
  await sleep(1500);
  console.log('panel', JSON.stringify(await page.evaluate(() => ({ tj: !document.getElementById('tj').classList.contains('hidden'), arrow: !document.getElementById('tj-open').classList.contains('hidden'), rows: [...document.querySelectorAll('#tj .tj-row')].map(r => r.textContent) }))));
  await page.screenshot({ path: OUT + '/tj_list.png' });
  await page.click('#tj .tj-row:nth-child(3)'); await sleep(500);
  await page.screenshot({ path: OUT + '/tj_detail.png' });
  await page.click('#tj .tj-x'); await sleep(600);
  console.log('closed', JSON.stringify(await page.evaluate(() => ({ tj: !document.getElementById('tj').classList.contains('hidden'), arrow: !document.getElementById('tj-open').classList.contains('hidden') }))));
  await page.screenshot({ path: OUT + '/tj_closed.png' });
  await page.click('#tj-open'); await sleep(500);
  console.log('reopened', JSON.stringify(await page.evaluate(() => ({ tj: !document.getElementById('tj').classList.contains('hidden'), arrow: !document.getElementById('tj-open').classList.contains('hidden') }))));
};
