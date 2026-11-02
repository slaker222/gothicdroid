/* Layout editor of the touch buttons (Android app). One file for the main menu (launcher.html) and the game's page (ui.html):
   TouchEditor.open({ defs, ICONS, ICON_OF, NAMES, TGROUP, items, opacity, bg, orient(on), onSave(items), onClose() })
   items: { key: {x, y, k} } - x, y: the centre of a button in % of the screen, k: size factor. The key is TGROUP[id] || id. */
(function() {
  const CSS = `
    #ted { position: fixed; inset: 0; z-index: 1000; background: #000; user-select: none; -webkit-user-select: none; touch-action: none; font: 13px/1.4 -apple-system, 'Segoe UI', Roboto, sans-serif; color: #ddd; }
    #ted[hidden] { display: none; }
    #ted-area { position: absolute; inset: 0; container-type: size; overflow: hidden; background: var(--ted-bg); }
    #ted-area::before { content: ''; position: absolute; inset: 0; pointer-events: none;
      background-image: linear-gradient(to right, rgba(255,255,255,.13) 1px, transparent 1px), linear-gradient(to bottom, rgba(255,255,255,.13) 1px, transparent 1px);
      background-size: var(--cell, 40px) var(--cell, 40px); }
    #ted-btns { position: absolute; inset: 0; opacity: var(--tpa, 1); }
    #ted-btns .tbtn { cursor: grab; touch-action: none; pointer-events: auto; }
    #ted-btns .tbtn.sel, #ted-btns .ted-stick.sel { outline: 2px dashed #ffd54a; outline-offset: 3px; }
    .ted-stick { position: absolute; width: calc(26cqmin * var(--k, 1)); height: calc(26cqmin * var(--k, 1)); border-radius: 50%; border: 1px solid rgba(255,255,255,.45); background: rgba(0,0,0,.25); touch-action: none; transform: translate(-50%, -50%); }
    .ted-stick::after { content: ''; position: absolute; left: 50%; top: 50%; width: 42%; height: 42%; margin: -21% 0 0 -21%; border-radius: 50%; background: rgba(255,255,255,.4); }
    #ted-panel { position: absolute; left: 50%; top: 50%; width: min(360px, 62vw); transform: translate(-50%, -50%); background: rgba(16,16,16,.95); border: 1px solid #555; border-radius: 12px; box-shadow: 0 8px 30px rgba(0,0,0,.6); }
    #ted-head { display: flex; align-items: center; justify-content: space-between; padding: 8px 12px; cursor: move; touch-action: none; border-bottom: 1px solid #3a3a3a; font-weight: 600; color: #fff; }
    #ted-head button { font: inherit; background: none; border: 0; color: #999; font-size: 20px; line-height: 1; padding: 0 4px; cursor: pointer; }
    #ted-body { padding: 8px 12px 10px; display: flex; flex-direction: column; gap: 8px; }
    #ted-panel.min #ted-body { display: none; }
    #ted-tabs { display: flex; gap: 6px; overflow-x: auto; padding-bottom: 2px; }
    #ted-tabs button { flex: none; font: inherit; font-size: 12px; padding: 5px 10px; color: #ddd; background: #262626; border: 1px solid #3a3a3a; border-radius: 14px; cursor: pointer; }
    #ted-tabs button.on { color: #000; background: #fff; border-color: #fff; }
    #ted-size { display: flex; align-items: center; gap: 8px; }
    #ted-size input { flex: 1; min-width: 0; accent-color: #fff; }
    #ted-sel { min-width: 74px; color: #ffd54a; }
    #ted-body label.snap { display: flex; align-items: center; gap: 6px; color: #999; }
    #ted-hint { font-size: 11px; color: #999; }
    #ted-body .row { display: flex; gap: 10px; }
    #ted-body .row button { flex: 1; font: inherit; font-size: 13px; font-weight: 600; height: 38px; color: #ddd; background: #222; border: 1px solid #3a3a3a; border-radius: 10px; cursor: pointer; }
    #ted-body .row button.primary { color: #000; background: #fff; border-color: #fff; }`;
  const HTML = `
    <div id="ted-area"><div id="ted-btns"></div></div>
    <div id="ted-panel">
      <div id="ted-head"><span>Edit layout</span><button type="button" id="ted-min" aria-label="Minimise">&minus;</button></div>
      <div id="ted-body">
        <div id="ted-tabs"></div>
        <div id="ted-size"><span id="ted-sel">Tap a button</span><input type="range" id="ted-k" min="0.5" max="2" step="0.05" value="1" disabled><output id="ted-kv">100%</output></div>
        <label class="snap"><input type="checkbox" id="ted-snap" checked> Snap to the grid</label>
        <div id="ted-hint">Drag a button to move it. Drag this window by its title.</div>
        <div class="row"><button id="ted-reset">Reset</button><button id="ted-cancel">Cancel</button><button id="ted-save" class="primary">Save</button></div>
      </div>
    </div>`;
  const COLS = 32;
  const TABS = [
    ['World',     b => b.layouts.includes('world') && b.id !== 'aim' && b.id !== 'inv', true],
    ['Bow',       b => ['aim', 'action', 'weapon', 'jump'].includes(b.id)],
    ['More',      b => b.id === 'tools' || b.layouts.includes('wtools')],
    ['Dialog',    b => b.layouts.some(l => ['talk', 'cutscene', 'choice', 'chapter'].includes(l))],
    ['Menu',      b => b.layouts.includes('menu')],
    ['Inventory', b => b.layouts.includes('inv')]
  ];
  let root = null, o = null, st = null;
  const $ = id => document.getElementById(id);

  function init() {
    const style = document.createElement('style'); style.textContent = CSS; document.head.appendChild(style);
    root = document.createElement('div'); root.id = 'ted'; root.hidden = true; root.innerHTML = HTML;
    document.body.appendChild(root);
    const area = $('ted-area'), btns = $('ted-btns');
    $('ted-snap').addEventListener('change', () => { st.snap = $('ted-snap').checked; });
    area.addEventListener('pointerdown', () => select(null));
    $('ted-k').addEventListener('input', () => {
      const el = st.sel && btns.querySelector('[data-ck="' + st.sel + '"]');
      if (!el) return;
      const k = +$('ted-k').value;
      item(el).k = k;
      el.style.setProperty('--k', k);
      $('ted-kv').textContent = Math.round(k * 100) + '%';
    });
    $('ted-tabs').addEventListener('click', e => {
      const b = e.target.closest('button');
      if (!b) return;
      st.tab = +b.dataset.i; st.sel = null; tabs(); build();
    });
    $('ted-reset').addEventListener('click', () => { st.items = {}; st.sel = null; build(); });
    $('ted-cancel').addEventListener('click', close);
    $('ted-save').addEventListener('click', () => { const it = st.items; close(); if (o.onSave) o.onSave(it); });
    // the panel is moved by its title
    const panel = $('ted-panel'), head = $('ted-head');
    let pd = null;
    head.addEventListener('pointerdown', e => {
      if (e.target.closest('button')) return;
      const r = panel.getBoundingClientRect();
      pd = { id: e.pointerId, dx: e.clientX - r.left, dy: e.clientY - r.top };
      head.setPointerCapture(e.pointerId);
    });
    head.addEventListener('pointermove', e => {
      if (!pd || e.pointerId !== pd.id) return;
      panel.style.transform = 'none';
      panel.style.left = Math.max(0, Math.min(innerWidth - 60, e.clientX - pd.dx)) + 'px';
      panel.style.top = Math.max(0, Math.min(innerHeight - 40, e.clientY - pd.dy)) + 'px';
    });
    head.addEventListener('pointerup', () => { pd = null; });
    head.addEventListener('pointercancel', () => { pd = null; });
    $('ted-min').addEventListener('click', () => { panel.classList.toggle('min'); $('ted-min').innerHTML = panel.classList.contains('min') ? '+' : '&minus;'; });
    window.addEventListener('resize', () => { if (!root.hidden) build(); });
  }

  const ckOf = id => o.TGROUP[id] || id;
  const nameOf = id => id === 'stick' ? 'Stick' : (o.NAMES[o.ICON_OF[id] || id] || id);
  const cell = () => $('ted-area').clientWidth / COLS;
  const place = (el, x, y) => { el.style.left = x + '%'; el.style.top = y + '%'; };
  const svgOf = n => '<svg viewBox="0 0 24 24">' + o.ICONS[n] + '</svg>';

  function tabs() {
    $('ted-tabs').innerHTML = TABS.map((t, i) => '<button type="button" data-i="' + i + '"' + (i === st.tab ? ' class="on"' : '') + '>' + t[0] + '</button>').join('');
  }
  function build() {
    const area = $('ted-area'), btns = $('ted-btns');
    btns.innerHTML = '';
    area.style.setProperty('--cell', cell() + 'px');
    btns.style.setProperty('--tpa', o.opacity > 0 ? o.opacity : 1);
    const W = area.clientWidth, H = area.clientHeight, tab = TABS[st.tab], seen = new Set();
    const mk = (ck, name, el, defX, defY) => {
      const it = st.items[ck];
      el.dataset.ck = ck; el.dataset.name = name;
      if (it) { place(el, it.x, it.y); if (it.k) el.style.setProperty('--k', it.k); } else place(el, defX, defY);
      el.style.transform = 'translate(-50%,-50%)';
      el.addEventListener('pointerdown', down);
      el.addEventListener('pointermove', move);
      el.addEventListener('pointerup', up);
      el.addEventListener('pointercancel', up);
      el.addEventListener('contextmenu', e => e.preventDefault());
    };
    for (const [id, k, cls, pos, layouts] of o.defs) {
      if (id === 'inv' || !tab[1]({ id, layouts: layouts.split(' ') })) continue;
      const ck = ckOf(id);
      if (seen.has(ck)) continue;
      seen.add(ck);
      const el = document.createElement('div');
      el.className = 'tbtn ' + cls;
      el.style.cssText = pos;
      el.innerHTML = svgOf(o.ICON_OF[id] || id);
      btns.appendChild(el);
      // the default place of the button: measured, as the game's page puts it
      const r = el.getBoundingClientRect(), a = area.getBoundingClientRect();
      mk(ck, nameOf(id), el, (r.left + r.width / 2 - a.left) / W * 100, (r.top + r.height / 2 - a.top) / H * 100);
    }
    if (tab[2]) {
      const el = document.createElement('div');
      el.className = 'ted-stick';
      btns.appendChild(el);
      const m = Math.min(W, H);
      mk('stick', 'Stick', el, 20 * m / W, (H - m * 0.24) / H * 100);
    }
    select(st.sel && btns.querySelector('[data-ck="' + st.sel + '"]') ? st.sel : null);
  }
  function select(ck) {
    st.sel = ck;
    const btns = $('ted-btns');
    btns.querySelectorAll('[data-ck]').forEach(e => e.classList.toggle('sel', e.dataset.ck === ck));
    const el = ck && btns.querySelector('[data-ck="' + ck + '"]'), k = $('ted-k');
    $('ted-sel').textContent = el ? el.dataset.name : 'Tap a button';
    k.disabled = !el;
    const it = ck && st.items[ck];
    k.value = String(it && it.k ? it.k : 1);
    $('ted-kv').textContent = Math.round(+k.value * 100) + '%';
  }
  // the item of a button (made from where the button stands now)
  function item(el) {
    const ck = el.dataset.ck;
    if (!st.items[ck]) {
      const area = $('ted-area'), W = area.clientWidth, H = area.clientHeight, r = el.getBoundingClientRect(), a = area.getBoundingClientRect();
      st.items[ck] = { x: +((r.left + r.width / 2 - a.left) / W * 100).toFixed(2), y: +((r.top + r.height / 2 - a.top) / H * 100).toFixed(2) };
    }
    return st.items[ck];
  }
  let drag = null;
  function down(e) {
    e.preventDefault(); e.stopPropagation();
    const el = e.currentTarget;
    select(el.dataset.ck);
    const r = el.getBoundingClientRect();
    drag = { id: e.pointerId, el, dx: e.clientX - (r.left + r.width / 2), dy: e.clientY - (r.top + r.height / 2) };
    try { el.setPointerCapture(e.pointerId); } catch (err) {}
  }
  function move(e) {
    if (!drag || e.pointerId !== drag.id) return;
    const a = $('ted-area').getBoundingClientRect(), W = a.width, H = a.height;
    let px = e.clientX - drag.dx - a.left, py = e.clientY - drag.dy - a.top;
    if (st.snap) { const c = cell(); px = Math.round(px / c) * c; py = Math.round(py / c) * c; }
    px = Math.min(W - 6, Math.max(6, px)); py = Math.min(H - 6, Math.max(6, py));
    const it = item(drag.el);
    it.x = +(px / W * 100).toFixed(2); it.y = +(py / H * 100).toFixed(2);
    place(drag.el, it.x, it.y);
  }
  function up(e) { if (drag && e.pointerId === drag.id) drag = null; }

  function open(opts) {
    if (!root) init();
    o = opts;
    st = { tab: 0, items: JSON.parse(JSON.stringify(o.items || {})), sel: null, snap: $('ted-snap').checked };
    $('ted-area').style.setProperty('--ted-bg', o.bg || 'rgba(0,0,0,.45)');
    root.hidden = false;
    if (o.orient) o.orient(true);
    tabs();
    // the screen may turn: the buttons are measured again when it has
    build();
    setTimeout(build, 300); setTimeout(build, 800);
  }
  function close() {
    root.hidden = true;
    if (o.orient) o.orient(false);
    if (o.onClose) o.onClose();
  }
  window.TouchEditor = { open, close, isOpen: () => !!root && !root.hidden };
})();
