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
