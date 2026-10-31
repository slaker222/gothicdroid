// teleports to the swamp (marvin console) and takes shots: node gtest.mjs --nomenu --profile=profile2 --set=waterQuality:2 --run=swampshot.mjs
export default async ({ page, sleep, OUT }) => {
  await page.evaluate(() => ogSet('marvin', 1)); await sleep(500);
  await page.keyboard.press('F2'); await sleep(1000);
  await page.keyboard.type('goto waypoint ' + (process.env.WP || 'PSI_SWAMP_0')); await page.keyboard.press('Enter'); await sleep(500);
  await page.keyboard.press('F2'); await sleep(8000);
  await page.screenshot({ path: OUT + '/' + (process.env.SHOT || 'swamp') + '.png' });
};
