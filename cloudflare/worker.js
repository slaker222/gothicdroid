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

export default {
  async fetch(request, env) {
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
