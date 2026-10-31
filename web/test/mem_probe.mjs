export default async ({ page, sleep }) => {
  await sleep(5000);
  const r = await page.evaluate(() => {
    const g = k => Module.ccall('og_gpu_mem', 'number', ['number'], [k]) / 1048576;
    return { texAllMB: +g(0).toFixed(1), fileTexMB: +g(1).toFixed(1), fileTexCount: Math.round(g(2) * 1048576), rgbaDxtMB: +g(3).toFixed(1), rgbaDxtCount: Math.round(g(4) * 1048576),
             wasmHeapMB: Math.round(Module.HEAP8.length / 1048576),
             jsHeapMB: performance.memory ? Math.round(performance.memory.usedJSHeapSize / 1048576) : null };
  });
  console.log('MEM', JSON.stringify(r));
};
