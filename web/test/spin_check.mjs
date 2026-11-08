// the boot status spinner of the page must stay round (phone landscape, little height): prints its size, OUT/spin_<w>x<h>.png
import puppeteer from 'puppeteer-core';
import path from 'path';
const HERE = path.dirname(new URL(import.meta.url).pathname);
const browser = await puppeteer.launch({ executablePath: '/opt/google/chrome/chrome', headless: true, args: ['--no-first-run'] });
for (const [w, h] of [[800, 380], [380, 800], [1280, 720], [640, 200]]) {
  const page = await browser.newPage();
  await page.emulate({ viewport: { width: w, height: h, deviceScaleFactor: 2, isMobile: true, hasTouch: true }, userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 16_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.3 Mobile/15E148 Safari/604.1' });
  await page.goto(process.argv[2] || 'http://127.0.0.1:8322/game.html', { waitUntil: 'domcontentloaded' });
  const r = await page.evaluate(() => {
    document.getElementById('ui-overlay')?.classList.add('hidden');
    document.getElementById('player')?.classList.add('playing');
    const bs = document.getElementById('boot-status');
    bs.style.display = 'flex';
    document.getElementById('boot-status-text').textContent = 'Starting the engine...';
    const s = bs.querySelector('.spinner').getBoundingClientRect(), c = bs.getBoundingClientRect();
    return { spinner: [Math.round(s.width * 10) / 10, Math.round(s.height * 10) / 10], box: [Math.round(c.width), Math.round(c.height)] };
  });
  console.log(w + 'x' + h, JSON.stringify(r));
  await page.screenshot({ path: path.join(HERE, 'out', `spin_${w}x${h}.png`) });
  await page.close();
}
await browser.close();
