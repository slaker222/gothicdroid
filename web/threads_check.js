// Threaded build: wasm memory is a SharedArrayBuffer, which browsers give only
// to cross-origin isolated pages (COOP/COEP headers, see web/serve.py).
if (typeof window === 'object' && !self.crossOriginIsolated) {
  var ogMsg = 'This build runs on several threads and needs the page served with the headers ' +
              '"Cross-Origin-Opener-Policy: same-origin" and "Cross-Origin-Embedder-Policy: require-corp" ' +
              '(web/serve.py sends them), over HTTPS or localhost.';
  console.error(ogMsg);
  window.__ogIsolationError = ogMsg;
  var ogStatus = document.getElementById('status-text');
  if (ogStatus) {
    ogStatus.style.color = 'var(--danger)';
    ogStatus.innerText = ogMsg;
  }
  throw new Error(ogMsg);
}

// iOS / iPadOS Safari: a shared WebAssembly.Memory reserves its whole maximum (4 GB) as address space and fails at random with
// "RangeError: Out of memory" (other tabs, the device). The maximum steps down 4 GB -> 3 -> 2 -> 1.5 -> 1 GB until the browser agrees.
(function () {
  if (typeof WebAssembly !== 'object' || !WebAssembly.Memory || WebAssembly.Memory.__ogWrapped) return;
  const Orig = WebAssembly.Memory;
  const P = new Proxy(Orig, {
    construct(target, args, newTarget) {
      const d = args[0];
      if (!d || !d.shared || !d.maximum) return Reflect.construct(target, args, newTarget);
      // iOS 17 refused even 1 GB: down to 512 MB (Gothic 1 needs about 520 MB at the end of its load, Gothic 2 and Archolos more:
      // they may run out of memory later, still better than nothing at all)
      const steps = [d.maximum, 49152, 32768, 24576, 16384, 14336, 12288, 10240, 8192]
                      .filter((m, i, a) => m <= d.maximum && a.indexOf(m) === i && (!d.initial || m >= d.initial));
      let err;
      for (const m of steps) {
        try {
          const mem = Reflect.construct(target, [Object.assign({}, d, { maximum: m })], newTarget);
          if (m !== d.maximum) console.warn('[mem] wasm memory maximum lowered to ' + (m * 64 / 1024) + ' MB (' + (d.maximum * 64 / 1024) + ' MB refused)');
          return mem;
        } catch (e) { err = e; console.warn('[mem] wasm memory with maximum ' + (m * 64 / 1024) + ' MB refused: ' + e); }
      }
      try {
        window.__ogMemError = 'The browser gives this page too little memory (' + err + '). Close other tabs and apps and reload; ' +
                              'on an iPhone, a newer iOS gives more.';
        const st = document.getElementById('status-text');
        if (st) { st.style.color = 'var(--danger)'; st.innerText = window.__ogMemError; }
      } catch (e) {}
      throw err;
    }
  });
  P.__ogWrapped = true;
  WebAssembly.Memory = P;
})();
