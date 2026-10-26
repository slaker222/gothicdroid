// Injected before the page (gtest --gputime): GPU time of every pass through timestamp queries.
// Result: window.__gpuAgg() -> [{key, ms, n}], mean GPU ms of the pass per frame, most expensive first.
(() => {
  const state = { dev: null, texInfo: new WeakMap(), viewTex: new WeakMap(), agg: new Map(), frames: 0, pending: 0 };
  window.__gpuState = state;
  const reqDev = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function (d = {}) {
    d = { ...d, requiredFeatures: [...(d.requiredFeatures || [])] };
    if (this.features.has('timestamp-query') && !d.requiredFeatures.includes('timestamp-query')) d.requiredFeatures.push('timestamp-query');
    const dev = await reqDev.call(this, d);
    state.dev = dev; state.ts = dev.features.has('timestamp-query');
    console.log('[GPUTIME] timestamp-query=' + state.ts);
    return dev;
  };
  const ct = GPUDevice.prototype.createTexture;
  GPUDevice.prototype.createTexture = function (d) {
    const t = ct.call(this, d);
    const s = d.size; state.texInfo.set(t, { w: s.width ?? s[0], h: s.height ?? s[1] ?? 1, f: d.format });
    return t;
  };
  const cv = GPUTexture.prototype.createView;
  GPUTexture.prototype.createView = function (d) { const v = cv.call(this, d); state.viewTex.set(v, this); return v; };
  const MAXP = 128;
  const cce = GPUDevice.prototype.createCommandEncoder;
  GPUDevice.prototype.createCommandEncoder = function (d) {
    const enc = cce.call(this, d);
    if (!state.ts) return enc;
    enc.__passes = []; enc.__qs = null;
    const qs = () => enc.__qs || (enc.__qs = this.createQuerySet({ type: 'timestamp', count: MAXP * 2 }));
    const wrap = (name, describe) => {
      const o = enc[name].bind(enc);
      enc[name] = function (desc) {
        const i = enc.__passes.length;
        if (i >= MAXP) return o(desc);
        desc = { ...desc, timestampWrites: { querySet: qs(), beginningOfPassWriteIndex: 2 * i, endOfPassWriteIndex: 2 * i + 1 } };
        const info = { i, kind: name === 'beginRenderPass' ? 'R' : 'C', key: '' };
        enc.__passes.push(info);
        describe(info, desc);
        const pass = o(desc);
        if (info.kind === 'C') { info.disp = 0; const dw = pass.dispatchWorkgroups.bind(pass); pass.dispatchWorkgroups = (...a) => { info.disp++; info.wg = a.join('x'); return dw(...a); };
          const di = pass.dispatchWorkgroupsIndirect.bind(pass); pass.dispatchWorkgroupsIndirect = (...a) => { info.disp++; info.wg = 'ind'; return di(...a); }; }
        else { info.draws = 0; for (const m of ['draw', 'drawIndexed', 'drawIndirect', 'drawIndexedIndirect']) { const f = pass[m].bind(pass); pass[m] = (...a) => { info.draws++; return f(...a); }; }
          const eb = pass.executeBundles.bind(pass); pass.executeBundles = b => { info.bundles = (info.bundles || 0) + b.length; return eb(b); }; }
        return pass;
      };
    };
    wrap('beginRenderPass', (info, desc) => {
      const a = desc.colorAttachments && desc.colorAttachments[0] && desc.colorAttachments[0].view;
      const z = desc.depthStencilAttachment && desc.depthStencilAttachment.view;
      const t = state.viewTex.get(a || z); const ti = t && state.texInfo.get(t);
      info.key = 'R ' + (ti ? ti.w + 'x' + ti.h + ' ' + ti.f : '?') + ' ca=' + (desc.colorAttachments ? desc.colorAttachments.length : 0) + (z ? ' +z' : '');
    });
    wrap('beginComputePass', () => {});
    const fin = enc.finish.bind(enc);
    enc.finish = function (d) {
      if (enc.__qs && enc.__passes.length) {
        const n = enc.__passes.length * 2;
        const buf = state.dev.createBuffer({ size: n * 8, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
        const rd = state.dev.createBuffer({ size: n * 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        enc.resolveQuerySet(enc.__qs, 0, n, buf, 0);
        enc.copyBufferToBuffer(buf, 0, rd, 0, n * 8);
        const passes = enc.__passes;
        let done = null;
        if (state.pending < 4) { state.pending++; done = () => {
          rd.mapAsync(GPUMapMode.READ).then(() => {
            const t = new BigUint64Array(rd.getMappedRange().slice(0));
            rd.unmap(); rd.destroy(); buf.destroy(); state.pending--; state.frames++;
            let total = 0;
            passes.forEach((p, k) => {
              const ms = Number(t[2 * k + 1] - t[2 * k]) / 1e6; total += ms;
              const key = p.key || ('C' + (p.disp ? ' disp=' + p.disp + ' wg=' + p.wg : ''));
              const full = p.i + ' ' + key + (p.kind === 'R' ? ' draws=' + (p.draws | 0) + ' bundles=' + (p.bundles | 0) : '');
              const e = state.agg.get(p.i + '|' + key) || { key: full, ms: 0, n: 0 }; e.key = full; e.ms += ms; e.n++; state.agg.set(p.i + '|' + key, e);
            });
            const e = state.agg.get('total') || { key: 'TOTAL passes', ms: 0, n: 0 }; e.ms += total; e.n++; state.agg.set('total', e);
          }).catch(() => { state.pending--; });
        }; }
        const cb = fin(d); cb.__done = done; return cb;
      }
      return fin(d);
    };
    return enc;
  };
  // the map has to wait for the submit: mapAsync on a buffer in a pending copy waits by itself
  const sub = GPUQueue.prototype.submit;
  GPUQueue.prototype.submit = function (cbs) {
    const r = sub.call(this, cbs);
    for (const c of cbs) if (c.__done) { c.__done(); c.__done = null; }
    return r;
  };
  window.__gpuAgg = () => [...state.agg.values()].map(e => ({ key: e.key, ms: +(e.ms / e.n).toFixed(2), n: e.n })).sort((a, b) => b.ms - a.ms);
  window.__gpuReset = () => state.agg.clear();
})();
