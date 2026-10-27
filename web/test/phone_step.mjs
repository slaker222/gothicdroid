// one step of the trade flow with real touches: node phone_step.mjs talk|down|ok|close|stick|state
import { attach, sleep, touches } from './phone_lib.mjs';
const { b, page, cdp, vis, st } = await attach();
const { tapBtn } = touches(page, cdp);
await page.evaluate(() => { window.__touchDebug = true; if (!window.__hooked) { window.__tl = []; window.__hooked = true; const l = console.log; console.log = (...a) => { if (String(a[0]).startsWith('[touch]') && !String(a[0]).includes('repeat')) window.__tl.push(String(a[0])); l.apply(console, a); }; } window.__tl.length = 0; });
const what = process.argv[2];
const map = { talk: 'Talk', down: 'Down', up: 'Up', ok: 'OK', close: 'Close', trade: 'Trade', back: 'Back' };
if (map[what]) console.log(what, await tapBtn(map[what], 180));
await sleep(2500);
console.log('layout', JSON.stringify(await vis()), (await st()).toString(2), 'keys', JSON.stringify(await page.evaluate(() => window.__tl)));
await page.screenshot({ path: 'out/ps_' + what + '.png' });
await b.disconnect();
