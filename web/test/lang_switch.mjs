// Language bar test: switch text/voice language on the page, wait for the install and the world.
//   node gtest.mjs --profile=profile_lang2 --nomenu --run=lang_switch.mjs   (env LANG_TEXT, LANG_VOICE, SHOT)
export default async ({ page, sleep, OUT }) => {
  const text = process.env.LANG_TEXT || 'de', voice = process.env.LANG_VOICE || 'de', shot = process.env.SHOT || 'lang_switch';
  const lines = [];
  page.on('console', m => { const t = m.text(); if (/unable to (load|open)|Union|\[SHELL\] game installed|corrupt/.test(t)) lines.push(t.slice(0, 160)); });
  await page.select('#lang-text', text);
  await page.select('#lang-voice', voice);
  page.once('dialog', d => d.accept());
  await Promise.all([page.waitForNavigation({ timeout: 60000 }).catch(() => {}), page.click('#lang-apply')]);
  const t0 = Date.now();
  let status = '';
  while (Date.now() - t0 < 240000) {
    await sleep(3000);
    await page.evaluate(() => { if (window.Module) Module.__ogVideoDone = true; }).catch(() => {});
    status = await page.evaluate(() => document.getElementById('status-text')?.innerText || '').catch(() => '');
    if (lines.some(l => /game installed/.test(l))) break;
  }
  for (let i = 0; i < 14; i++) { await sleep(3000); await page.evaluate(() => { if (window.Module) Module.__ogVideoDone = true; }).catch(() => {}); }
  await page.screenshot({ path: `${OUT}/${shot}.png` });
  const st = await page.evaluate(async () => {
    const root = await navigator.storage.getDirectory();
    const f = await (await root.getFileHandle('gothic-langs.json')).getFile();
    const j = JSON.parse(await f.text());
    return { text: j.text, voice: j.voice, nText: j.files.text.length, nVoice: j.files.voice.length };
  }).catch(e => String(e));
  console.log(JSON.stringify({ installSec: Math.round((Date.now() - t0) / 1000), lastStatus: status, state: st, log: lines }));
};
