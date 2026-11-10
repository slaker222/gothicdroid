// Spins the camera in big steps and takes a screenshot right after each turn: npc that enter the view must have a
// proper pose at once (no T-pose, no frozen pose). SPIN_TAG names the screenshots: out/spin_<tag>_<n>.png
// node gtest.mjs --nomenu --uncapped --manual --set=... --run=spin_npc.mjs
export default async ({ page, sleep, OUT }) => {
  const tag = process.env.SPIN_TAG || 'x';
  const look = (dx, dy) => page.evaluate((a, b) => Module.ccall('og_input_look', null, ['number', 'number'], [a, b]), dx, dy);
  // leave the dialog with Diego: the last answer, then Enter
  // (Diego's speech takes a while, the answers show up late; a key before that is lost)
  await sleep(+(process.env.SPIN_WAIT || 20000));
  await page.bringToFront();
  await page.keyboard.press('ArrowUp'); await sleep(800);
  await page.keyboard.press('Enter'); await sleep(8000);
  const turn = +(process.env.SPIN_STEP || 250);
  for (let i = 0; i < 14; ++i) {
    await look(turn, i % 3 === 2 ? -40 : 0);
    await sleep(i % 2 ? 30 : 120);
    await page.screenshot({ path: `${OUT}/spin_${tag}_${String(i).padStart(2, '0')}.png` });
  }
};
