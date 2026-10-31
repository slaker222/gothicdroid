// Live text change in Gothic 2: the text language is switched in the running game, twice (to LANG_A, then to LANG_B),
// a screenshot after each. For the garbled text (glyphs of the old language's font) this shows where it comes from.
//   node gtest.mjs --url='http://127.0.0.1:8324/?game=2' --profile=profile_g2 --nomenu --run=g2_lang_live.mjs
//   (env LANG_A default en, LANG_B default ru)
export default async ({ page, sleep, OUT }) => {
  const seq = [process.env.LANG_A || 'en', process.env.LANG_B || 'ru'];
  const logs = [];
  page.on('console', m => { const t = m.text(); if (/switchText|text language|remapped|unable to|text change|text files|Cannot load|Loading unknown|cannot read|font/i.test(t)) logs.push(t.slice(0, 220)); });
  // leave the dialog with Xardas (the text change waits for the end of a dialog): the last answer, then Enter
  await sleep(9000);
  await page.screenshot({ path: `${OUT}/g2ll_dlg1.png` });
  for (let k = 0; k < 4; k++) {
    await page.keyboard.press('ArrowUp'); await sleep(500);
    await page.keyboard.press('Enter'); await sleep(2500);
  }
  await page.screenshot({ path: `${OUT}/g2ll_0.png` });
  let i = 1;
  for (const lang of seq) {
    await page.select('#lang-text', lang);
    const t0 = Date.now();
    let final = '';
    while (Date.now() - t0 < 300000) {
      await sleep(500);
      final = await page.evaluate(() => document.getElementById('voice-text').textContent + ' | ' + document.getElementById('lang-note').textContent);
      if (/ready|failed|finish/.test(final) || (!/waiting/.test(final) && Date.now() - t0 > 20000)) break;
    }
    await sleep(12000);
    await page.screenshot({ path: `${OUT}/g2ll_${i}_${lang}.png` });
    console.log(JSON.stringify({ step: i, lang, final }));
    i++;
  }
  console.log(JSON.stringify({ logs }));
};
