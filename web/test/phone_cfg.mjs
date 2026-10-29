// run in the world on a phone: measures fps for a list of settings (--run=phone_cfg.mjs)
const CFGS = [
  ['res3 sh2048', { resScale: 3, shadows: 2 }],
  ['res3 sh1024', { resScale: 3, shadows: 1 }],
  ['res3 sh0', { resScale: 3, shadows: 0 }],
  ['res2 sh2048', { resScale: 2, shadows: 2 }],
  ['res1 sh0', { resScale: 1, shadows: 0 }],
];
export default async ({ page, sleep }) => {
  const get = k => page.evaluate(k => ogGet(k), k);
  const keys = ['fpsLimit', 'resScale', 'shadows', 'drawDist', 'worldDist', 'skipFarAnim', 'reflections', 'ssao'];
  const show = async () => Object.fromEntries(await Promise.all(keys.map(async k => [k, await get(k)])));
  console.log('settings now', JSON.stringify(await show()));
  const orig = await show();
  for (const [name, set] of CFGS) {
    for (const k of keys) await page.evaluate((k, v) => ogSet(k, v), k, k in set ? set[k] : orig[k]);
    await sleep(8000);
    const f0 = await page.evaluate(() => window.__frames); const t0 = Date.now();
    await sleep(15000);
    const f1 = await page.evaluate(() => window.__frames);
    console.log(name, 'fps', ((f1 - f0) * 1000 / (Date.now() - t0)).toFixed(1));
  }
};
