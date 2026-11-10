// Second pass of the lazy npc poses: the camera turns away so that the npc near the start leave the view for a few
// seconds, then turns back as fast as the engine allows and a screenshot is taken right after the turn ends. The npc
// that re-entered the view must be in a natural animated pose at its place with its weapon, never a T-pose, a frozen
// old pose at a wrong orientation or a mesh detached from its position (compare with pe_<tag>_ref.png).
// The engine limits the camera to 720 degrees per second (MainWindow::tickMouse), a single big og_input_look is mostly
// lost, so a turn is a burst of looks (every millisecond, from the page) for PE_TURNS[i] milliseconds (~0.7 deg/ms
// at most: 250 ms = a half turn).
// PE_TAG names the screenshots out/pe_<tag>_<n>_<ms>[_away].png, PE_TURNS the turn durations in ms (default
// 60,90,120,250,100,200), PE_AWAY the seconds spent looking away (default 1.5).
// node gtest.mjs --url=...?renderer=R --nomenu --uncapped --manual --set=... --run=pose_enter.mjs
export default async ({ page, sleep, OUT }) => {
  const tag = process.env.PE_TAG || 'x';
  const turns = (process.env.PE_TURNS || '60,90,120,250,100,200').split(',').map(Number);
  const away = +(process.env.PE_AWAY || 1.5) * 1000;
  // dir -1/1: the page sends big looks every millisecond until the time is over (the engine clamps them per tick)
  const turn = (ms, dir) => page.evaluate((ms, dir) => new Promise(res => {
    const t0 = performance.now();
    const id = setInterval(() => {
      Module.ccall('og_input_look', null, ['number', 'number'], [dir * 4000, 0]);
      if (performance.now() - t0 >= ms) { clearInterval(id); requestAnimationFrame(() => requestAnimationFrame(res)); }
    }, 1);
  }), ms, dir);
  // leave the dialog with Diego (his speech takes a while, a key before the answers show up is lost)
  await sleep(+(process.env.PE_WAIT || 20000));
  await page.bringToFront();
  await page.keyboard.press('ArrowUp'); await sleep(800);
  await page.keyboard.press('Enter'); await sleep(+(process.env.PE_POST || 1000));
  await page.screenshot({ path: `${OUT}/pe_${tag}_ref.png` });
  let n = 0;
  for (const ms of turns) {
    await turn(ms, -1);
    await sleep(away);
    const id = `${OUT}/pe_${tag}_${String(n++).padStart(2, '0')}_${ms}`;
    await page.screenshot({ path: `${id}_away.png` });
    await turn(ms, 1);
    await page.screenshot({ path: `${id}.png` });
    await sleep(300);
  }
  await page.screenshot({ path: `${OUT}/pe_${tag}_end.png` });
};
