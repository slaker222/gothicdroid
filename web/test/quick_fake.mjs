// fake ring of 6 entries: arrows appear, the slots press their keys, the arrows ask the engine for the next page
export default async ({ page, sleep, OUT }) => {
  const log = [];
  page.on('console', m => { const t = m.text(); if (t.startsWith('[touch]') && !t.includes('repeat')) log.push(t); });
  await page.evaluate(() => { window.__touchDebug = true; });
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  for (let i = 0; i < 60; i++) { const v = await vis(); if (v.includes('Jump')) break;
    if (v.includes('Skip')) await page.keyboard.press('Escape');
    else if (v.includes('OK')) { await page.keyboard.press('ArrowUp'); await sleep(300); await page.keyboard.press('Enter'); }
    await sleep(500); }
  await sleep(1500);
  const fake = (f) => JSON.stringify({ w: 675, h: 379, f, c: 4, t: 6, s: [0, 1, 2, 3].map(i => ({ k: (f + i) % 6 + 1, e: 0, x: 237 + i * 52, y: 321, b: 45, on: i === 1 ? 1 : 0 })) });
  const sent = [];
  await page.evaluate((f0) => { const c = Module.ccall; window.__first = 0; window.__sent = [];
    Module.ccall = (n, ...a) => { if (n === 'og_quickslots') return JSON.stringify({ w: 675, h: 379, f: window.__first, c: 4, t: 6, s: [0, 1, 2, 3].map(i => ({ k: (window.__first + i) % 6 + 1, e: 0, x: 237 + i * 52, y: 321, b: 45, on: i === 1 ? 1 : 0 })) });
      if (n === 'og_quickpanel') { window.__sent.push(a[2]); window.__first = ((a[2][1] % 6) + 6) % 6; return; } return c(n, ...a); }; }, 0);
  await sleep(1200);
  await page.screenshot({ path: OUT + '/qf_page1.png' });
  await page.click('.tq-slot:nth-of-type(2)').catch(() => {});
  const slots = await page.$$('.tq-slot');
  await slots[2].click(); await sleep(300);
  console.log('slot tap', JSON.stringify(log));
  const arrows = await page.$$('.tq-page');
  await arrows[1].click(); await sleep(900);
  console.log('after next', JSON.stringify(await page.evaluate(() => ({ sent: window.__sent.slice(-2), keys: [...document.querySelectorAll('.tq-slot span')].map(e => e.textContent) }))));
  await page.screenshot({ path: OUT + '/qf_page2.png' });
};
