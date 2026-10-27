export default async ({ page, sleep, OUT }) => {
  await sleep(3000);
  console.log('slots', await page.evaluate(() => Module.ccall('og_quickslots', 'string', [], [])));
};
