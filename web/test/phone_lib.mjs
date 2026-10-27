import puppeteer from 'puppeteer-core';
export const sleep = ms => new Promise(r => setTimeout(r, ms));
export async function attach() {
  const b = await puppeteer.connect({ browserURL: 'http://127.0.0.1:9222', defaultViewport: null });
  const pages = await b.pages();
  const page = pages.find(p => p.url().includes('192.168.0.100:8321')) || pages[0];
  const cdp = await page.createCDPSession();
  const vis = () => page.evaluate(() => [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title));
  const tap = async (t, hold = 200) => {
    const ok = await page.evaluate((t) => { const b = [...document.querySelectorAll('.tbtn')].find(e => e.title.startsWith(t) && getComputedStyle(e).display !== 'none'); if (!b) return false;
      window.__tb = b; b.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, pointerId: 9 })); return true; }, t);
    if (!ok) return false;
    await sleep(hold);
    await page.evaluate(() => window.__tb.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, pointerId: 9 })));
    return true; };
  const st = () => page.evaluate(() => Module.ccall('og_ui_state', 'number', [], []));
  return { b, page, cdp, vis, tap, st };
}
// real touches (CDP) instead of synthetic pointer events
export function touches(page, cdp) {
  const center = (t) => page.evaluate((t) => { const b = [...document.querySelectorAll('.tbtn')].find(e => e.title.startsWith(t) && getComputedStyle(e).display !== 'none'); if (!b) return null;
    const r = b.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; }, t);
  const tapAt = async (x, y, hold = 120) => { await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y, id: 5 }] }); await sleep(hold); await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); };
  const tapBtn = async (t, hold = 150) => { const c = await center(t); if (!c) return false; await tapAt(c.x, c.y, hold); return true; };
  const canvasAt = async (fx, fy, hold = 100) => { const r = await page.evaluate(() => { const c = document.getElementById('canvas').getBoundingClientRect(); return { x: c.left, y: c.top, w: c.width, h: c.height }; }); await tapAt(r.x + r.w * fx, r.y + r.h * fy, hold); };
  return { tapAt, tapBtn, canvasAt };
}
