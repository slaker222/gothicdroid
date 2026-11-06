// pinch with two fingers in the look area changes the camera distance
export default async ({ page, cdp, sleep, OUT }) => {
  await page.evaluate(() => { const c = document.getElementById('touch-chk'); if (!c.checked) { c.checked = true; c.dispatchEvent(new Event('change')); } });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(1500);
  await page.evaluate(() => { window.__z = []; const o = Module.ccall; Module.ccall = function (n, ...r) { if (n === 'og_input_zoom') window.__z.push(r[2][0]); return o.call(this, n, ...r); }; });
  await page.evaluate(() => { window.__ev = []; for (const n of ['pointerdown','pointerup','pointercancel']) window.addEventListener(n, e => window.__ev.push(n[7]+e.pointerId+(e.target.id||e.target.className||e.target.tagName)), true); });
  const t = (type, pts) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: pts });
  const P = (id, x, y) => ({ x, y, id });
  const cx = 400, cy = 120;
  await page.screenshot({ path: OUT + '/pz_0.png' });
  // fingers apart
  await t('touchStart', [P(1, cx - 30, cy)]);
  await t('touchStart', [P(1, cx - 30, cy), P(2, cx + 30, cy)]);
  for (let d = 30; d <= 150; d += 10) { await t('touchMove', [P(1, cx - d, cy), P(2, cx + d, cy)]); await sleep(30); }
  await t('touchEnd', [P(2, cx + 150, cy)]); await t('touchEnd', []);
  await sleep(1500);
  await page.screenshot({ path: OUT + '/pz_closer.png' });
  const a = await page.evaluate(() => window.__z.slice());
  // fingers together
  await t('touchStart', [P(1, cx - 100, cy)]);
  await t('touchStart', [P(1, cx - 100, cy), P(2, cx + 100, cy)]);
  for (let d = 100; d >= 20; d -= 10) { await t('touchMove', [P(1, cx - d, cy), P(2, cx + d, cy)]); await sleep(30); }
  await t('touchEnd', [P(2, cx + 20, cy)]); await t('touchEnd', []);
  await sleep(1500);
  await page.screenshot({ path: OUT + '/pz_farther.png' });
  const b = await page.evaluate(() => window.__z.slice());
  console.log('events', JSON.stringify(await page.evaluate(() => window.__ev)));
  console.log('zoom calls apart:', JSON.stringify(a), 'after together:', JSON.stringify(b));
};
