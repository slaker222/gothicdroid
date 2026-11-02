// Закрытый доступ: HTTP Basic Auth перед раздачей статических файлов.
// Логин и пароль хранятся в секретах воркера (AUTH_USER, AUTH_PASS), не в репозитории:
//   npx wrangler secret put AUTH_USER ; npx wrangler secret put AUTH_PASS
// Без секретов сайт закрыт для всех (fail closed).
// Чтобы открыть сайт всем: убрать "main" и "run_worker_first" в wrangler.jsonc и сделать deploy.

function safeEqual(a, b) {
  const ea = new TextEncoder().encode(a), eb = new TextEncoder().encode(b);
  let d = ea.length ^ eb.length;
  const n = Math.max(ea.length, eb.length);
  for (let i = 0; i < n; i++) d |= (ea[i] || 0) ^ (eb[i] || 0);
  return d === 0;
}

function unauthorized(status = 401) {
  return new Response('Authentication required', {
    status,
    headers: {
      'WWW-Authenticate': 'Basic realm="gothicplay", charset="UTF-8"',
      'Cache-Control': 'no-store',
      'X-Robots-Tag': 'noindex, nofollow',
    },
  });
}

// ---- device logs: the game page sends the device info and the engine log here (D1 table, see schema.sql).
// No IP is stored. Read them with: npx wrangler d1 execute gothic-logs --remote --command "SELECT ..."
const SID = /^[a-z0-9]{10,32}$/;
async function takeLog(request, env) {
  if (request.method !== 'POST' || !env.gothic_logs) return new Response('', { status: 405 });
  const text = await request.text();
  if (text.length > 65536) return new Response('', { status: 413 });
  let m; try { m = JSON.parse(text); } catch (e) { return new Response('', { status: 400 }); }
  if (!m || typeof m.sid !== 'string' || !SID.test(m.sid)) return new Response('', { status: 400 });
  const now = Date.now(), db = env.gothic_logs, st = [];
  if (m.dev && typeof m.dev === 'object') {
    st.push(db.prepare('INSERT OR REPLACE INTO sessions (sid, t, ua, info, country) VALUES (?, ?, ?, ?, ?)')
      .bind(m.sid, now, String(m.dev.ua || '').slice(0, 300), JSON.stringify(m.dev).slice(0, 8000), (request.cf && request.cf.country) || ''));
    // old logs go away (14 days)
    st.push(db.prepare('DELETE FROM lines WHERE t < ?').bind(now - 14 * 864e5));
    st.push(db.prepare('DELETE FROM sessions WHERE t < ?').bind(now - 14 * 864e5));
  }
  if (Array.isArray(m.lines)) {
    for (const l of m.lines.slice(0, 200))
      if (Array.isArray(l) && l.length >= 2) st.push(db.prepare('INSERT INTO lines (sid, t, k, txt) VALUES (?, ?, ?, ?)').bind(m.sid, +l[2] || now, String(l[0]).slice(0, 2), String(l[1]).slice(0, 500)));
  }
  if (st.length) { try { await db.batch(st); } catch (e) { return new Response('', { status: 500 }); } }
  return new Response('', { status: 204 });
}

export default {
  async fetch(request, env) {
    if (new URL(request.url).pathname === '/__log') return takeLog(request, env);
    if (env.OPEN === '1') return env.ASSETS.fetch(request);   // the site is open (wrangler.open.jsonc)
    if (!env.AUTH_USER || !env.AUTH_PASS) return unauthorized(503);
    const h = request.headers.get('Authorization') || '';
    if (h.startsWith('Basic ')) {
      let cred = '';
      try { cred = new TextDecoder().decode(Uint8Array.from(atob(h.slice(6)), (c) => c.charCodeAt(0))); } catch (e) {}
      const i = cred.indexOf(':');
      if (i >= 0) {
        const okUser = safeEqual(cred.slice(0, i), env.AUTH_USER);
        const okPass = safeEqual(cred.slice(i + 1), env.AUTH_PASS);
        if (okUser && okPass) return env.ASSETS.fetch(request);
      }
    }
    return unauthorized();
  },
};
