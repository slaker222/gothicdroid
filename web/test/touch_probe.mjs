// phone emulation on a fresh profile: what the touch layer shows while the game files install
import puppeteer from 'puppeteer-core';
import fs from 'fs';
const OUT = process.argv[2], url = process.argv[3] || 'http://127.0.0.1:8322/?archive=/game_web.zip';
const prof = fs.mkdtempSync(OUT + '/prof-');
const b = await puppeteer.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false, userDataDir: prof,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,WebGPU', '--ozone-platform=x11', '--window-size=1000,500'] });
const page = (await b.pages())[0];
const cdp = await page.createCDPSession();
await cdp.send('Emulation.setDeviceMetricsOverride', { width: 800, height: 380, deviceScaleFactor: 2, mobile: true });
await cdp.send('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
await cdp.send('Emulation.setUserAgentOverride', { userAgent: 'Mozilla/5.0 (Linux; Android 14; moto g84 5G) AppleWebKit/537.36 Chrome/150 Mobile Safari/537.36' });
await page.goto(url);
for (const t of [3, 8]) {
  await new Promise(r => setTimeout(r, t * 1000));
  const info = await page.evaluate(() => ({
    touchOn: document.body.classList.contains('touch-on'),
    visible: [...document.querySelectorAll('.tbtn')].filter(e => getComputedStyle(e).display !== 'none').map(e => e.title),
    stick: getComputedStyle(document.getElementById('touch-stick')).display,
    status: (document.getElementById('status') || {}).textContent }));
  console.log(t, JSON.stringify(info));
  await page.screenshot({ path: OUT + '/probe' + t + '.png' });
}
await b.close();
