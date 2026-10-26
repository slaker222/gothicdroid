import fs from 'fs';
export default async ({ page, cdp, sleep, OUT }) => {
  await cdp.send('Profiler.enable'); await cdp.send('Profiler.setSamplingInterval', { interval: 150 }); await cdp.send('Profiler.start');
  await sleep(15000);
  const { profile } = await cdp.send('Profiler.stop');
  const byId = new Map(profile.nodes.map(n => [n.id, n]));
  const parent = new Map();
  for (const n of profile.nodes) for (const c of n.children || []) parent.set(c, n.id);
  const name = id => byId.get(id).callFrame.functionName || '(anon)';
  let total = 0; const dt = profile.timeDeltas;
  const self = new Map(), inclusive = new Map(), callers = {};
  const WATCH = ['memcmp', 'writeBuffer', '_emscripten_get_now', 'wasm-to-js', 'setBindGroup', 'emscripten_futex_wake'];
  profile.samples.forEach((id, k) => {
    const d = dt[k] || 0; total += d;
    const nm = name(id); self.set(nm, (self.get(nm) || 0) + d);
    const seen = new Set(); let cur = id;
    while (cur !== undefined) { const f = name(cur); if (!seen.has(f)) { seen.add(f); inclusive.set(f, (inclusive.get(f) || 0) + d); } cur = parent.get(cur); }
    if (WATCH.includes(nm)) {
      let p = parent.get(id); let chain = [];
      for (let i = 0; i < (nm === 'writeBuffer' ? 9 : 3) && p !== undefined; i++) { chain.push(name(p).slice(0, 48)); p = parent.get(p); }
      const key = nm + ' <- ' + chain.join(' <- ');
      callers[key] = (callers[key] || 0) + d;
    }
  });
  const pct = us => +(us / total * 100).toFixed(1);
  const out = { totalMs: Math.round(total / 1000),
    inclusiveTop: [...inclusive.entries()].sort((a, b) => b[1] - a[1]).slice(0, 60).map(([f, us]) => [f.slice(0, 90), pct(us)]),
    callers: Object.entries(callers).sort((a, b) => b[1] - a[1]).slice(0, 30).map(([k, us]) => [k, pct(us)]) };
  fs.writeFileSync(OUT + '/phone_prof.json', JSON.stringify(out, null, 1));
};
