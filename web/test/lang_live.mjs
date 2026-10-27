// Live voice change test: the voice language is switched in the running game (no reload).
//   node gtest.mjs --profile=profile_lang2 --nomenu --run=lang_live.mjs     (env LANG_VOICE, default pl)
// Checks: the page is not reloaded, the bar shows MB, the engine reads dialogue lines through
// the second bridge, a restart adopts the new files.
export default async ({ page, sleep, OUT }) => {
  const voice = process.env.LANG_VOICE || 'pl';
  const logs = [];
  page.on('console', m => { const t = m.text(); if (/unable to mount|voice change|unable to load sound|corrupt/.test(t)) logs.push(t.slice(0, 160)); });

  const origin0 = await page.evaluate(() => performance.timeOrigin);
  // count the engine's reads served by a bridge of a live voice (entries with their own bridge)
  await page.evaluate(() => {
    window.__liveReads = 0;
    const orig = GameFS.bridgeReadInto.bind(GameFS);
    GameFS.bridgeReadInto = (e, ...a) => { if (e.br) window.__liveReads++; return orig(e, ...a); };
  });

  await page.select('#lang-voice', voice);
  await page.click('#lang-apply');
  const bar = [];
  let t0 = Date.now(), final = '';
  while (Date.now() - t0 < 300000) {
    await sleep(500);
    const t = await page.evaluate(() => document.getElementById('voice-text').textContent);
    if (t && bar[bar.length - 1] !== t && bar.length < 400) bar.push(t);
    final = t;
    if (/ready|failed/.test(t)) break;
  }
  const origin1 = await page.evaluate(() => performance.timeOrigin);

  // let the game speak: answer the dialogue, lines are read from the archives when spoken
  await page.screenshot({ path: `${OUT}/live_${voice}_0.png` });
  for (let i = 0; i < 6; i++) { await page.keyboard.press('Enter'); await sleep(2500); }
  const reads = await page.evaluate(() => window.__liveReads);
  await page.screenshot({ path: `${OUT}/live_${voice}_1.png` });

  const st = await page.evaluate(async () => {
    const root = await navigator.storage.getDirectory();
    const j = JSON.parse(await (await (await root.getFileHandle('gothic-langs.json')).getFile()).text());
    return { voice: j.voice, live: j.liveVoice && j.liveVoice.dir, files: j.files.voice.length };
  });
  console.log(JSON.stringify({ reloaded: origin0 !== origin1, final, samples: [bar[0], bar[Math.floor(bar.length / 2)], bar[bar.length - 1]], reads, state: st, logs }));
};
