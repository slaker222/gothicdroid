// keySneak in Gothic.ini after start, and the tool bar with the camera button
export default async ({ page, sleep, OUT }) => {
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await page.evaluate(() => ogSet('skipFarAnim', 0)); // flushes Gothic.ini
  await sleep(1500);
  const ini = await page.evaluate(async () => {
    const root = await navigator.storage.getDirectory();
    const walk = async (d, parts) => { for (const p of parts) d = await d.getDirectoryHandle(p); return d; };
    try {
      const g = await walk(root, ['gothic', 'system']);
      const f = await (await g.getFileHandle('Gothic.ini')).getFile();
      return (await f.text()).split('\n').filter(l => /keySneak|keyLookFP|webSkipFarAnim|keyUp|keyLeft/.test(l)).join(' | ') + ' mtime=' + new Date(f.lastModified).toISOString() + ' now=' + new Date().toISOString();
    } catch (e) { return 'err ' + e; }
  });
  console.log('ini:', ini);
  return;
  // tool bar: open, enable cheats
  const tap = (t) => page.evaluate((t) => { const b = document.querySelector('.tbtn[title="' + t + '"]');
    b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); b.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })); }, t);
  await tap('More'); await sleep(300);
  await page.screenshot({ path: OUT + '/ts_tools.png' });
  await tap('Enable cheats'); await sleep(500);
  await page.screenshot({ path: OUT + '/ts_cheats.png' });
  console.log('buttons', JSON.stringify(await vis()));
};
