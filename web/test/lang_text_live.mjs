// Live text change test: the text language is switched in the running game (no page reload).
//   node gtest.mjs --profile=profile_lang2 --nomenu --run=lang_text_live.mjs     (env LANG_TEXT, default ru)
// Checks: the page is not reloaded, the engine saves the game to memory and loads it again with the new
// scripts (symbol indices mapped by name), the world is back and the interface is in the new language.
export default async ({ page, sleep, OUT }) => {
  const text = process.env.LANG_TEXT || 'ru';
  const logs = [];
  page.on('console', m => { const t = m.text(); if (/switchText|text language|remapped|unable to|text change|text files|Cannot load|Loading unknown|cannot read/i.test(t)) logs.push(t.slice(0, 200)); });
  const origin0 = await page.evaluate(() => performance.timeOrigin);
  const before = await page.evaluate(async () => {
    const root = await navigator.storage.getDirectory();
    const j = JSON.parse(await (await (await root.getFileHandle('gothic-langs.json')).getFile()).text());
    return { text: j.text, voice: j.voice };
  });

  // leave the dialog with Diego: the last answer, then Enter
  await sleep(4000);
  await page.keyboard.press('ArrowUp'); await sleep(600);
  await page.keyboard.press('Enter'); await sleep(4000);
  await page.screenshot({ path: `${OUT}/tl_before.png` });

  await page.select('#lang-text', text);
  let t0 = Date.now(), final = '';
  while (Date.now() - t0 < 300000) {
    await sleep(500);
    final = await page.evaluate(() => document.getElementById('voice-text').textContent + ' | ' + document.getElementById('lang-note').textContent);
    if (/ready|failed|finish/.test(final)) break;
  }
  await sleep(15000);
  const origin1 = await page.evaluate(() => performance.timeOrigin);
  await page.screenshot({ path: `${OUT}/tl_after.png` });
  await page.keyboard.press('Tab'); await sleep(1500);
  await page.screenshot({ path: `${OUT}/tl_inv.png` });
  await page.keyboard.press('Tab'); await sleep(800);
  await page.keyboard.press('Escape'); await sleep(1500);
  await page.screenshot({ path: `${OUT}/tl_menu.png` });
  await page.keyboard.press('Escape'); await sleep(500);
  console.log(JSON.stringify({ before, reloaded: origin0 !== origin1, final, logs }));
};
