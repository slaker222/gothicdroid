/* Layout editor of the touch buttons (Android app). One file for the main menu (launcher.html) and the game's page (ui.html):
   TouchEditor.open({ defs, ICONS, ICON_OF, NAMES, TGROUP, KEYS, SPECIAL, keys, custom, items, opacity, bg, orient(on), onSave(items, keys, custom), onClose() })
   defs: the buttons of the game (without the player's ones); KEYS: names of the keys that can be assigned; SPECIAL: ids of buttons that do not just press a key
   keys: { id: 'Shift+S' } - other keys of the buttons (the stick: stickW / stickS / stickQ / stickE); custom: [{id, label, key, layouts}] - buttons made by the player
   items: { key: {x, y, k} } - x, y: the centre of a button in % of the screen, k: size factor. The key is TGROUP[id] || id.
   quick: { cols: 4..10, back: bool, vert: bool, split: bool } - the quick bar of the number keys (items.quick: the centre of
   its slots, without the inventory block); vert: a column, split: two rows (columns); onSave gets it as the 4th argument. */
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
    #ted-btns .tbtn .clabel { font: 700 3.4cqmin/1.05 sans-serif; text-align: center; max-width: 92%; overflow: hidden; word-break: break-word; pointer-events: none; }
    #ted-btns .tbtn.sel, #ted-btns .ted-stick.sel { outline: 2px dashed #ffd54a; outline-offset: 3px; }
    .ted-stick { position: absolute; width: calc(26cqmin * var(--k, 1)); height: calc(26cqmin * var(--k, 1)); border-radius: 50%; border: 1px solid rgba(255,255,255,.45); background: rgba(0,0,0,.25); touch-action: none; transform: translate(-50%, -50%); }
    /* the quick bar: as the engine draws it - a slot is 12% of the screen height, the gap 1/6 of a slot, the inventory
       block right of the slots; the place (left / top) is the centre of the slots */
    .ted-quick { position: absolute; touch-action: none; }
    .ted-quick i { position: absolute; box-sizing: border-box; font-style: normal;
      border: 1px solid rgba(255,255,255,.55); border-radius: 1.2cqmin; background: rgba(0,0,0,.3); pointer-events: none; }
    .ted-quick i span { position: absolute; left: 3px; top: 1px; font: 700 clamp(9px, 2.4cqmin, 13px)/1 sans-serif; color: #fff; text-shadow: 0 1px 2px #000; }
    .ted-quick.back i:not(.inv) { background: radial-gradient(rgba(0,0,0,.62) 55%, rgba(0,0,0,.35)); border-color: rgba(255,255,255,.7); }
    .ted-quick i.inv { display: flex; align-items: center; justify-content: center; }
    .ted-quick i.inv svg { width: 62%; height: 62%; fill: none; stroke: #fff; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
    .ted-quick.sel { outline: 2px dashed #ffd54a; outline-offset: 3px; }
    #ted-keyrow .qn { min-width: 34px; text-align: center; font-size: 16px; font-weight: 700; color: #fff; }
    #ted-keyrow label.qb { display: flex; align-items: center; gap: 6px; color: #ddd; flex-basis: 100%; }
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
    #ted-keyrow { display: flex; align-items: center; gap: 6px; flex-wrap: wrap; }
    #ted-keyrow.col { flex-direction: column; align-items: stretch; }
    #ted-keyrow .sr { display: flex; align-items: center; gap: 6px; }
    #ted-keyrow .sr .kl { width: 22px; font-size: 16px; text-align: center; }
    #ted-keyrow .sr button:first-of-type { flex: 1; }
    #ted-hold { display: flex; align-items: center; gap: 6px; color: #ddd; white-space: nowrap; }
    #ted-keyrow .sp { flex: 1; }
    #ted-keyrow button.del { min-width: 0; color: #ff7a7a; }
    #ted-keyrow[hidden], #ted-cust[hidden] { display: none; }
    #ted-keyrow .kl { color: #999; }
    #ted-keyrow button { font: inherit; font-size: 13px; font-weight: 600; min-width: 64px; padding: 6px 12px; color: #fff; background: #2a2a2a; border: 1px solid #555; border-radius: 8px; cursor: pointer; }
    #ted-keyrow button.mod { border-color: #ffd54a; color: #ffd54a; }
    #ted-keyrow button:disabled { color: #777; border-style: dashed; }
    #ted-keyrow button.x { min-width: 0; color: #999; }
    #ted-cust { display: flex; gap: 8px; }
    #ted-cust input { flex: 1; min-width: 0; font: inherit; padding: 6px 10px; color: #fff; background: #1e1e1e; border: 1px solid #555; border-radius: 8px; }
    #ted-add { color: #ffd54a !important; font-size: 14px !important; font-weight: 600; border: 1px solid #ffd54a !important; border-radius: 8px; padding: 3px 12px !important; margin-right: 8px; }
    #ted-kp { position: absolute; inset: 0; z-index: 5; background: rgba(0,0,0,.7); display: flex; align-items: center; justify-content: center; }
    #ted-kp[hidden] { display: none; }
    #ted-kpbox { width: min(560px, 94vw); max-height: 92vh; overflow-y: auto; background: #1b1b1b; border: 1px solid #555; border-radius: 12px; padding: 12px 14px; }
    #ted-kpbox h3 { font-size: 15px; color: #ffd54a; margin-bottom: 6px; }
    #ted-kpsel { min-height: 34px; margin: 6px 0; padding: 6px 10px; border: 1px solid #ffd54a; border-radius: 8px; font-size: 16px; font-weight: 700; display: flex; align-items: center; color: #fff; }
    .ted-kg { margin: 8px 0 3px; font-size: 11px; color: #999; text-transform: uppercase; letter-spacing: .05em; }
    .ted-kr { display: flex; flex-wrap: wrap; gap: 5px; }
    .ted-kr button { min-width: 38px; padding: 7px 8px; font: inherit; font-size: 13px; color: #ddd; background: #262626; border: 1px solid #3a3a3a; border-radius: 8px; cursor: pointer; }
    .ted-kr button.on { color: #000; background: #fff; border-color: #fff; }
    #ted-kpbtns { display: flex; gap: 8px; margin-top: 10px; }
    #ted-kpbtns button { flex: 1; font: inherit; font-weight: 600; height: 38px; color: #ddd; background: #222; border: 1px solid #3a3a3a; border-radius: 10px; cursor: pointer; }
    #ted-kpbtns button.primary { color: #000; background: #fff; border-color: #fff; }
    #ted-kpbtns button:disabled { opacity: .4; }
    #ted-hint { font-size: 11px; color: #999; }
    #ted-body .row { display: flex; gap: 10px; }
    #ted-body .row button { flex: 1; font: inherit; font-size: 13px; font-weight: 600; height: 38px; color: #ddd; background: #222; border: 1px solid #3a3a3a; border-radius: 10px; cursor: pointer; }
    #ted-body .row button.primary { color: #000; background: #fff; border-color: #fff; }`;
  const HTML = `
    <div id="ted-area"><div id="ted-btns"></div></div>
    <div id="ted-panel">
      <div id="ted-head"><span>Edit layout</span><span><button type="button" id="ted-add" aria-label="Add a button" title="Add a button">Add</button><button type="button" id="ted-min" aria-label="Minimise">&minus;</button></span></div>
      <div id="ted-body">
        <div id="ted-tabs"></div>
        <div id="ted-size"><span id="ted-sel">Tap a button</span><input type="range" id="ted-k" min="0.5" max="2" step="0.05" value="1" disabled><output id="ted-kv">100%</output></div>
        <div id="ted-cust" hidden><input id="ted-lab" type="text" maxlength="8" placeholder="Text on the button" autocapitalize="off" autocomplete="off"></div>
        <div id="ted-keyrow" hidden></div>
        <label class="snap"><input type="checkbox" id="ted-snap" checked> Snap to the grid</label>
        <div id="ted-hint">Drag a button to move it, tap it to change its key. Add makes a button of your own. Drag this window by its title.</div>
        <div class="row"><button id="ted-reset">Reset</button><button id="ted-cancel">Cancel</button><button id="ted-save" class="primary">Save</button></div>
      </div>
    </div>
    <div id="ted-kp" hidden><div id="ted-kpbox"></div></div>`;
  const COLS = 80;
  const TABS = [
    ['World',     b => b.layouts.includes('world') && b.id !== 'aim' && b.id !== 'inv', true],
    ['Bow',       b => ['aim', 'action', 'weapon', 'jump'].includes(b.id)],
    ['More',      b => b.id === 'tools' || b.layouts.includes('wtools')],
    ['Dialog',    b => b.layouts.some(l => ['talk', 'cutscene', 'choice', 'chapter'].includes(l))],
    ['Menu',      b => b.layouts.includes('menu')],
    ['Inventory', b => b.layouts.includes('inv')]
  ];
  let root = null, o = null, st = null;
  { const sx = document.createElement('style'); sx.textContent = CSS; document.head.appendChild(sx); }
  const $ = id => document.getElementById(id);

  function init() {
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
      if (st.sel === 'quick') build();   // the slots are measured in pixels
    });
    $('ted-tabs').addEventListener('click', e => {
      const b = e.target.closest('button');
      if (!b) return;
      st.tab = +b.dataset.i; st.sel = null; tabs(); build();
    });
    $('ted-reset').addEventListener('click', () => { st.items = {}; st.keys = {}; st.quick = { cols: 4, back: false, vert: false, split: false }; st.sel = null; build(); });
    $('ted-add').addEventListener('click', addButton);
    $('ted-lab').addEventListener('input', () => {
      const c = selCustom();
      if (!c) return;
      c.label = $('ted-lab').value.trim();
      const el = $('ted-btns').querySelector('[data-ck="' + c.id + '"]');
      if (el) { el.firstChild.textContent = c.label || keyText(c.key); el.dataset.name = c.label || keyText(c.key); $('ted-sel').textContent = el.dataset.name; }
    });
    $('ted-keyrow').addEventListener('change', e => {
      if (e.target.id === 'ted-holdc') { const c = selCustom(); if (c) c.hold = e.target.checked; return; }
      const f = { 'ted-qback': 'back', 'ted-qvert': 'vert', 'ted-qsplit': 'split' }[e.target.id];
      if (!f) return;
      st.quick[f] = e.target.checked;
      if (f === 'back') { const el = $('ted-btns').querySelector('.ted-quick'); if (el) el.classList.toggle('back', st.quick.back); return; }
      // another shape: the bar keeps its place when it was moved, otherwise it goes to the default place of the new shape
      build();
    });
    $('ted-keyrow').addEventListener('click', e => {
      const b = e.target.closest('button');
      if (!b) return;
      if (b.id === 'ted-del') {
        const c = selCustom();
        if (c) { st.custom = st.custom.filter(x => x !== c); delete st.items[c.id]; st.sel = null; build(); }
        return;
      }
      if (b.dataset.reset) { delete st.keys[b.dataset.reset]; showKey(); return; }
      if (b.dataset.q) { st.quick.cols = Math.min(10, Math.max(4, quickCols() + +b.dataset.q)); build(); return; }
      if (b.dataset.id) chooseKey(b.dataset.id, b.dataset.dflt, +b.dataset.max, b.dataset.title);
    });
    $('ted-save').addEventListener('click', () => { const it = st.items; close(); if (o.onSave) o.onSave(it, cleanKeys(), st.custom.filter(c => c.key), { cols: quickCols(), back: !!st.quick.back, vert: !!st.quick.vert, split: !!st.quick.split }); });
    $('ted-cancel').addEventListener('click', close);
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
    $('ted-min').addEventListener('click', () => { panel.classList.toggle('min'); $('ted-min').innerHTML = panel.classList.contains('min') ? '&#9633;' : '&minus;'; });
    window.addEventListener('resize', () => { if (!root.hidden) build(); });
  }

  const ckOf = id => o.TGROUP[id] || id;
  const KLABEL = { Up: '↑', Down: '↓', Left: '←', Right: '→', Ctrl: 'Ctrl', CapsLock: 'Caps', Backspace: 'Backsp', Space: 'Space' };
  const keyText = k => k ? k.split('+').map(x => KLABEL[x] || x).join(' + ') : '—';
  const customOf = id => st.custom.find(c => c.id === id);
  const selCustom = () => st.sel && customOf(st.sel);
  const nameOf = id => { const c = customOf(id); return id === 'stick' ? 'Stick' : id === 'quick' ? 'Quick bar' : (c ? (c.label || keyText(c.key)) : (o.NAMES[o.ICON_OF[id] || id] || id)); };
  // the buttons of the game and the ones of the player; the layouts of a button of the player depend on the tab it was made on
  const allDefs = () => o.defs.concat(st.custom.map(c => [c.id, c.key, 'custom', 'left:50%;top:45%;transform:translate(-50%,-50%)', c.layouts || 'world']));
  const dfltOf = id => { const c = customOf(id); if (c) return c.key; const d = o.defs.find(x => x[0] === id); return d ? d[1] : ''; };
  const keyOf = id => { const c = customOf(id); return c ? c.key : (st.keys[id] || dfltOf(id)); };
  const cleanKeys = () => { const k = {}; for (const id in st.keys) if (!customOf(id) && st.keys[id]) k[id] = st.keys[id]; return k; };
  const TAB_LAYOUTS = ['world', 'world', 'wtools', 'talk cutscene choice', 'menu', 'inv'];
  const cell = () => $('ted-area').clientWidth / COLS;
  const quickCols = () => Math.min(10, Math.max(4, +st.quick.cols || 4));
  // the slots as the engine puts them: a slot is 12% of the screen height (times the size), the gap 1/6 of a slot;
  // `per` slots in a line, one or two lines, along x (a row) or y (vertical)
  const quickGeo = () => {
    const a = $('ted-area'), W = a.clientWidth, H = a.clientHeight, it = st.items.quick, k = it && it.k ? it.k : 1;
    const n = quickCols(), lines = st.quick.split ? 2 : 1, per = Math.ceil(n / lines), vert = !!st.quick.vert;
    const sz = Math.max(24, H * 0.12 * k), pitch = sz + sz / 6, lenMain = per * pitch - sz / 6, lenCross = lines * pitch - sz / 6;
    return { W, H, n, lines, per, vert, sz, pitch, bw: vert ? lenCross : lenMain, bh: vert ? lenMain : lenCross };
  };
  // the default place of the engine (the centre of the slots): a row 3.5% of the screen height above the bottom edge,
  // in the middle; a column 3.5% of the height right of the left edge, in the middle
  const quickDef = () => {
    const g = quickGeo();
    return g.vert ? { x: +((g.H * 0.035 + g.bw / 2) / g.W * 100).toFixed(2), y: 50 }
                  : { x: 50, y: +(100 - 3.5 - g.bh / 2 / g.H * 100).toFixed(2) };
  };
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
    for (const [id, k, cls, pos, layouts] of allDefs()) {
      if (id === 'inv' || !tab[1]({ id, layouts: layouts.split(' ') })) continue;
      const ck = ckOf(id);
      if (seen.has(ck)) continue;
      seen.add(ck);
      const el = document.createElement('div');
      el.className = 'tbtn ' + cls;
      el.style.cssText = pos;
      el.dataset.id = id;
      const cu = customOf(id);
      if (cu) { el.innerHTML = '<span class="clabel"></span>'; el.firstChild.textContent = cu.label || keyText(cu.key); }   // a button made by the player: its text
      else el.innerHTML = svgOf(o.ICON_OF[id] || id);
      btns.appendChild(el);
      // the default place of the button: measured, as the game's page puts it
      const r = el.getBoundingClientRect(), a = area.getBoundingClientRect();
      mk(ck, nameOf(id), el, (r.left + r.width / 2 - a.left) / W * 100, (r.top + r.height / 2 - a.top) / H * 100);
    }
    if (tab[2]) {
      // the quick bar: the slots of the number keys 1..9, 0 and the inventory block (right of the first row, under the
      // first column when vertical)
      const el = document.createElement('div'), g = quickGeo();
      el.className = 'ted-quick' + (st.quick.back ? ' back' : '');
      el.style.width = g.bw + 'px'; el.style.height = g.bh + 'px';
      const at = (main, cross) => g.vert ? 'left:' + (cross * g.pitch) + 'px;top:' + (main * g.pitch) + 'px'
                                         : 'left:' + (main * g.pitch) + 'px;top:' + (cross * g.pitch) + 'px';
      const box = 'width:' + g.sz + 'px;height:' + g.sz + 'px;';
      el.innerHTML = Array.from({ length: g.n }, (_, i) => '<i style="' + box + at(i % g.per, Math.floor(i / g.per)) + '"><span>' + ((i + 1) % 10) + '</span></i>').join('') +
        '<i class="inv" style="' + box + at(g.per, 0) + '">' + (o.ICONS && o.ICONS.inv ? svgOf('inv') : '') + '</i>';
      btns.appendChild(el);
      const d = quickDef();
      mk('quick', nameOf('quick'), el, d.x, d.y);
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
    showKey();
  }
  // the key of the selected button: shown and changed here
  function showKey() {
    const row = $('ted-keyrow'), ck = st.sel, c = selCustom();
    $('ted-cust').hidden = !c;
    if (c) $('ted-lab').value = c.label || '';
    row.classList.toggle('col', ck === 'stick');
    if (!ck) { row.hidden = true; return; }
    row.hidden = false;
    const kb = (id, title, max, label) => {
      const d = id.startsWith('stick') ? id.slice(5) : dfltOf(id), cur = keyOf(id), mod = !customOf(id) && cur !== d;
      return '<span class="kl">' + label + '</span><button type="button" data-id="' + id + '" data-dflt="' + d + '" data-max="' + max + '" data-title="' + title + '" class="' + (mod ? 'mod' : '') + '">' + keyText(cur) + '</button>' +
        (mod ? '<button type="button" class="x" data-reset="' + id + '" title="Default: ' + keyText(d) + '">↺</button>' : '');
    };
    if (ck === 'quick') {
      const n = quickCols();
      row.innerHTML = '<span class="kl">Slots</span><button type="button" data-q="-1"' + (n <= 4 ? ' disabled' : '') + '>&minus;</button>' +
        '<span class="qn">' + n + '</span><button type="button" data-q="1"' + (n >= 10 ? ' disabled' : '') + '>+</button>' +
        '<label class="qb"><input type="checkbox" id="ted-qvert"' + (st.quick.vert ? ' checked' : '') + '> Vertical</label>' +
        '<label class="qb"><input type="checkbox" id="ted-qsplit"' + (st.quick.split ? ' checked' : '') + '> Two rows (' + Math.ceil(n / 2) + ' + ' + Math.floor(n / 2) + ')</label>' +
        '<label class="qb"><input type="checkbox" id="ted-qback"' + (st.quick.back ? ' checked' : '') + '> Darken behind the slots</label>';
      return;
    }
    if (ck === 'stick') {
      row.innerHTML = [['stickW', '↑'], ['stickS', '↓'], ['stickQ', '←'], ['stickE', '→']].map(([id, l]) => {
        const d = id.slice(5), cur = st.keys[id] || d, mod = cur !== d;
        return '<div class="sr"><span class="kl">' + l + '</span><button type="button" data-id="' + id + '" data-dflt="' + d + '" data-max="1" data-title="Stick ' + l + '" class="' + (mod ? 'mod' : '') + '">' + keyText(cur) + '</button>' + (mod ? '<button type="button" class="x" data-reset="' + id + '">↺</button>' : '') + '</div>';
      }).join('');
      return;
    }
    const el = $('ted-btns').querySelector('[data-ck="' + ck + '"]'), id = el && el.dataset.id;
    if (!id) { row.hidden = true; return; }
    if ((o.SPECIAL || []).includes(id)) { row.innerHTML = '<span class="kl">Key</span><button type="button" disabled>special button</button>'; return; }
    row.innerHTML = kb(id, el.dataset.name, 3, 'Key') + (c ? '<span class="sp"></span><label id="ted-hold"><input type="checkbox" id="ted-holdc"' + (c.hold ? ' checked' : '') + '> Hold</label><button type="button" class="del" id="ted-del">Delete</button>' : '');
  }
  // the picker of the keys (several are pressed together)
  const KGROUPS = [
    ['Letters', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('')],
    ['Digits', '0123456789'.split('')],
    ['Arrows', ['Up', 'Down', 'Left', 'Right']],
    ['Keys', ['Space', 'Enter', 'Esc', 'Tab', 'Backspace', 'Delete', 'Insert', 'Home', 'End', 'CapsLock']],
    ['Modifiers', ['Shift', 'Ctrl', 'Alt']],
    ['F1 – F12', Array.from({ length: 12 }, (_, i) => 'F' + (i + 1))]
  ];
  function pickKey(title, cur, max, cb, onCancel) {
    const ok = new Set(o.KEYS || []), kp = $('ted-kp'), box = $('ted-kpbox');
    let sel = (cur || '').split('+').filter(k => ok.has(k)).slice(0, max);
    box.innerHTML = '<h3></h3><div id="ted-kpsel"></div>' +
      KGROUPS.map(g => '<div class="ted-kg">' + g[0] + '</div><div class="ted-kr">' + g[1].filter(k => ok.has(k)).map(k => '<button type="button" data-k="' + k + '">' + (KLABEL[k] || k) + '</button>').join('') + '</div>').join('') +
      '<div id="ted-kpbtns"><button type="button" id="ted-kpno">Cancel</button><button type="button" id="ted-kpclr">Clear</button><button type="button" id="ted-kpok" class="primary">OK</button></div>';
    box.querySelector('h3').textContent = title + (max > 1 ? ' - tap the keys pressed together (up to ' + max + ')' : '');
    const draw = () => {
      $('ted-kpsel').textContent = sel.length ? keyText(sel.join('+')) : 'No key';
      box.querySelectorAll('[data-k]').forEach(b => b.classList.toggle('on', sel.includes(b.dataset.k)));
      $('ted-kpok').disabled = !sel.length;
    };
    box.querySelectorAll('[data-k]').forEach(b => b.onclick = () => {
      const k = b.dataset.k;
      if (sel.includes(k)) sel = sel.filter(x => x !== k); else if (max === 1) sel = [k]; else if (sel.length < max) sel.push(k);
      draw();
    });
    $('ted-kpclr').onclick = () => { sel = []; draw(); };
    $('ted-kpno').onclick = () => { kp.hidden = true; if (onCancel) onCancel(); };
    $('ted-kpok').onclick = () => { kp.hidden = true; cb(sel.join('+')); };
    kp.hidden = false; box.scrollTop = 0;
    draw();
  }
  function chooseKey(id, dflt, max, title) {
    pickKey(title || 'Key', keyOf(id), max || 3, k => {
      const c = customOf(id);
      if (c) { c.key = k; const el = $('ted-btns').querySelector('[data-ck="' + id + '"]'); if (el && !c.label) { el.firstChild.textContent = keyText(k); el.dataset.name = keyText(k); $('ted-sel').textContent = keyText(k); } }
      else if (k === dflt) delete st.keys[id]; else st.keys[id] = k;
      showKey();
    });
  }
  // "+": a new simple button on the current tab; its key is chosen at once, the place - by dragging
  function addButton() {
    const id = 'u' + Date.now().toString(36);
    pickKey('Key of the new button', '', 3, k => {
      st.custom.push({ id, label: '', key: k, layouts: TAB_LAYOUTS[st.tab] });
      // not under the window of the editor: the new button is put into the free part of the screen, where its text is seen
      const a = $('ted-area').getBoundingClientRect(), r = $('ted-panel').getBoundingClientRect();
      const left = r.left - a.left, right = a.right - r.right;
      const x = left >= right ? left / 2 : r.right - a.left + right / 2;
      st.items[id] = { x: +Math.min(92, Math.max(8, x / a.width * 100)).toFixed(2), y: 45 };
      st.sel = id; build();
    });
  }
  // the item of a button (made from where the button stands now)
  function item(el) {
    const ck = el.dataset.ck;
    if (!st.items[ck] && ck === 'quick') st.items[ck] = quickDef();
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
    let r = el.getBoundingClientRect(), cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    if (el.dataset.ck === 'quick') {
      const a = $('ted-area').getBoundingClientRect(), it = st.items.quick || quickDef();
      cx = a.left + it.x / 100 * a.width; cy = a.top + it.y / 100 * a.height;
    }
    // the button is only selected by a tap; it moves after the finger has moved a little
    drag = { id: e.pointerId, el, dx: e.clientX - cx, dy: e.clientY - cy, sx: e.clientX, sy: e.clientY, moved: false };
    try { el.setPointerCapture(e.pointerId); } catch (err) {}
  }
  function move(e) {
    if (!drag || e.pointerId !== drag.id) return;
    if (!drag.moved) { if (Math.hypot(e.clientX - drag.sx, e.clientY - drag.sy) < 10) return; drag.moved = true; }
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
    st = { tab: 0, items: JSON.parse(JSON.stringify(o.items || {})), keys: Object.assign({}, o.keys || {}), custom: JSON.parse(JSON.stringify(o.custom || [])).filter(c => c && c.id && c.key), quick: Object.assign({ cols: 4, back: false, vert: false, split: false }, o.quick || {}), sel: null, snap: $('ted-snap').checked };
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
  // a small picture of the current layout (the World tab: the buttons, the stick, the quick bar) in `box`; drawn again when the
  // box changes its size (the tab becomes visible) or when preview() is called again. opts: as for open() (defs, ICONS, ICON_OF,
  // TGROUP, items, custom, quick, opacity, bg)
  function preview(box, opts) {
    if (!document.getElementById('ted-pv-css')) document.head.appendChild(Object.assign(document.createElement('style'), { id: 'ted-pv-css', textContent:
      '.ted-pv { position: relative; container-type: size; overflow: hidden; border-radius: 10px; aspect-ratio: 20 / 9; width: 100%; background: #2a3a2a; }' +
      '.ted-pv .tbtn { pointer-events: none; } .ted-pv .tbtn .clabel { font: 700 3.4cqmin/1.05 sans-serif; text-align: center; max-width: 92%; overflow: hidden; }' +
      '.ted-pv .ted-stick, .ted-pv .ted-quick { pointer-events: none; }' }));
    const draw = () => {
      const items = opts.items || {}, q = Object.assign({ cols: 4, back: false, vert: false, split: false }, opts.quick || {});
      const custom = (opts.custom || []).filter(c => c && c.id && c.key);
      box.classList.add('ted-pv');
      if (opts.bg) box.style.background = opts.bg;
      box.innerHTML = '';
      const H = box.clientHeight, W = box.clientWidth;
      if (!H) return;
      const wrap = document.createElement('div');
      wrap.style.cssText = 'position:absolute;inset:0;opacity:' + (opts.opacity > 0 ? opts.opacity : 1);
      box.appendChild(wrap);
      const svg = n => '<svg viewBox="0 0 24 24">' + opts.ICONS[n] + '</svg>';
      const seen = new Set();
      const defs = opts.defs.concat(custom.map(c => [c.id, c.key, 'custom', 'left:50%;top:45%;transform:translate(-50%,-50%)', c.layouts || 'world']));
      for (const [id, , cls, pos, layouts] of defs) {
        if (id === 'inv' || id === 'aim' || !layouts.split(' ').includes('world')) continue;
        const ck = opts.TGROUP[id] || id;
        if (seen.has(ck)) continue;
        seen.add(ck);
        const el = document.createElement('div');
        el.className = 'tbtn ' + cls;
        el.style.cssText = pos;
        const cu = custom.find(c => c.id === id);
        if (cu) { el.innerHTML = '<span class="clabel"></span>'; el.firstChild.textContent = cu.label || (cu.key || '').split('+').map(x => KLABEL[x] || x).join(' + '); }
        else el.innerHTML = svg(opts.ICON_OF[id] || id);
        const it = items[ck];
        if (it) { el.style.left = it.x + '%'; el.style.top = it.y + '%'; el.style.right = el.style.bottom = 'auto'; el.style.transform = 'translate(-50%,-50%)'; if (it.k) el.style.setProperty('--k', it.k); }
        wrap.appendChild(el);
      }
      const m = Math.min(W, H), sk = document.createElement('div'), si = items.stick;
      sk.className = 'ted-stick';
      sk.style.left = si ? si.x + '%' : (20 * m / W) + '%'; sk.style.top = si ? si.y + '%' : ((H - m * 0.24) / H * 100) + '%';
      if (si && si.k) sk.style.setProperty('--k', si.k);
      wrap.appendChild(sk);
      // the quick bar, as the engine puts it (see quickGeo / quickDef)
      const qi = items.quick, k = qi && qi.k ? qi.k : 1, n = Math.min(10, Math.max(4, +q.cols || 4)), lines = q.split ? 2 : 1;
      const per = Math.ceil(n / lines), sz = Math.max(24 * H / 380, H * 0.12 * k), pitch = sz + sz / 6;
      const lenMain = per * pitch - sz / 6, lenCross = lines * pitch - sz / 6, bw = q.vert ? lenCross : lenMain, bh = q.vert ? lenMain : lenCross;
      const qx = qi ? qi.x : (q.vert ? (H * 0.035 + bw / 2) / W * 100 : 50), qy = qi ? qi.y : (q.vert ? 50 : 100 - 3.5 - bh / 2 / H * 100);
      const qe = document.createElement('div');
      qe.className = 'ted-quick' + (q.back ? ' back' : '');
      qe.style.cssText = 'width:' + bw + 'px;height:' + bh + 'px;left:' + qx + '%;top:' + qy + '%;transform:translate(-50%,-50%)';
      const at = (main, cross) => q.vert ? 'left:' + (cross * pitch) + 'px;top:' + (main * pitch) + 'px' : 'left:' + (main * pitch) + 'px;top:' + (cross * pitch) + 'px';
      const bx = 'width:' + sz + 'px;height:' + sz + 'px;';
      qe.innerHTML = Array.from({ length: n }, (_, i) => '<i style="' + bx + at(i % per, Math.floor(i / per)) + '"><span>' + ((i + 1) % 10) + '</span></i>').join('') +
        '<i class="inv" style="' + bx + at(per, 0) + '">' + (opts.ICONS.inv ? svg('inv') : '') + '</i>';
      wrap.appendChild(qe);
    };
    if (box._tedPv) box._tedPv.disconnect();
    if (window.ResizeObserver) { const ro = new ResizeObserver(draw); ro.observe(box); box._tedPv = ro; }
    draw();
  }
  window.TouchEditor = { open, close, preview, isOpen: () => !!root && !root.hidden };
})();
