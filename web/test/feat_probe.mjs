export default async ({ page }) => {
  const r = await page.evaluate(async () => {
    const a = await navigator.gpu.requestAdapter();
    return { feats: [...a.features].filter(f => /texture|float|rg11|indirect|timestamp/.test(f)), info: a.info ? { v: a.info.vendor, a: a.info.architecture, d: a.info.description } : null,
             maxBuf: a.limits.maxBufferSize, maxTex: a.limits.maxTextureDimension2D };
  });
  console.log('FEAT', JSON.stringify(r));
};
