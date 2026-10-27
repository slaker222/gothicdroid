import { attach, sleep } from './phone_lib.mjs';
const { b, page, vis, tap, st } = await attach();
console.log('before', JSON.stringify(await vis()));
console.log('talk', await tap('Talk'));
await sleep(2500);
console.log('after talk', JSON.stringify(await vis()), (await st()).toString(2));
await page.screenshot({ path: 'out/pt_1.png' });
await b.disconnect();
