import { attach, sleep } from './phone_lib.mjs';
const { b, page, vis, tap, st } = await attach();
console.log('start', JSON.stringify(await vis()), (await st()).toString(2));
console.log('close', await tap('Close', 200)); 
for (let i = 0; i < 6; i++) { await sleep(700); console.log(i, JSON.stringify(await vis()), (await st()).toString(2)); }
await page.screenshot({ path: 'out/pt_3.png' });
await b.disconnect();
