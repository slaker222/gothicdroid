import { attach, sleep } from './phone_lib.mjs';
const { b, page, vis, tap, st } = await attach();
console.log('start', JSON.stringify(await vis()));
console.log('down', await tap('Down', 150)); await sleep(500);
console.log('select', await tap('OK', 200)); await sleep(3500);
console.log('after select', JSON.stringify(await vis()), (await st()).toString(2));
await page.screenshot({ path: 'out/pt_2.png' });
await b.disconnect();
