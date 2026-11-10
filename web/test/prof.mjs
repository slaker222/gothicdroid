// CPU profile of the main thread and of the workers + GPU pass times (when --gputime).
import fs from 'fs';
const SEC = +(process.env.PROF_SEC || 20);
const TAG = process.env.PROF_TAG || 'prof';
function summarize(profile) {
  const byId = new Map(profile.nodes.map(n => [n.id, n]));
  const parent = new Map();
  for (const n of profile.nodes) for (const c of n.children || []) parent.set(c, n.id);
  const name = id => (byId.get(id).callFrame.functionName || '(anon)').slice(0, 110);
  let total = 0; const dt = profile.timeDeltas;
  const self = new Map(), incl = new Map(), edges = new Map();
  profile.samples.forEach((id, k) => {
    const d = dt[k] || 0; total += d;
    const nm = name(id); self.set(nm, (self.get(nm) || 0) + d);
    const seen = new Set(); let cur = id, child = null;
    while (cur !== undefined) {
      const f = name(cur);
      if (!seen.has(f)) { seen.add(f); incl.set(f, (incl.get(f) || 0) + d); }
      if (child !== null) { const e = f + ' -> ' + child; if (!seen.has(e)) { seen.add(e); edges.set(e, (edges.get(e) || 0) + d); } }
      child = f; cur = parent.get(cur);
    }
  });
  const pct = us => +(us / total * 100).toFixed(2);
  const top = (m, n) => [...m.entries()].sort((a, b) => b[1] - a[1]).slice(0, n).map(([f, us]) => [pct(us), f]);
  return { totalMs: Math.round(total / 1000), self: top(self, 90), inclusive: top(incl, 140), edges: top(edges, 400) };
}
export default async ({ page, cdp, sleep, OUT }) => {
  const sessions = [['main', cdp]];
  let i = 0;
  for (const w of page.workers()) { try { sessions.push(['worker' + (i++), w.client]); } catch (e) {} }
  for (const [, s] of sessions) {
    await s.send('Profiler.enable'); await s.send('Profiler.setSamplingInterval', { interval: 200 }); await s.send('Profiler.start');
  }
  try { await page.evaluate(() => window.__gpuReset && window.__gpuReset()); } catch (e) {}
  await sleep(SEC * 1000);
  const out = {};
  for (const [nm, s] of sessions) { const { profile } = await s.send('Profiler.stop'); out[nm] = summarize(profile); }
  try {
    out.gpu = await page.evaluate(() => window.__gpuAgg ? ({ ts: window.__gpuState.ts, frames: window.__gpuState.frames, agg: window.__gpuAgg() }) : null);
  } catch (e) { out.gpu = String(e); }
  const dir = process.env.PROF_OUT || OUT;
  fs.writeFileSync(dir + '/' + TAG + '.json', JSON.stringify(out));
  console.log('profile saved', dir + '/' + TAG + '.json', 'sessions=' + sessions.length);
};
