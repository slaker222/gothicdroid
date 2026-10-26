export default async ({ page, sleep }) => {
  await page.evaluate(() => window.__gpuReset());
  await sleep(15000);
  const r = await page.evaluate(() => ({ ts: window.__gpuState.ts, frames: window.__gpuState.frames, agg: window.__gpuAgg() }));
  console.log('timestamp=' + r.ts, 'frames=' + r.frames);
  for (const e of r.agg.slice(0, 24)) console.log(String(e.ms).padStart(7), 'ms', e.key, '(n=' + e.n + ')');
};
