#include "renderer.h"

#include <Tempest/Application>
#include <Tempest/Color>
#include <Tempest/Fence>
#include <Tempest/Log>
#include <Tempest/StorageImage>
#include <cassert>

#include "ui/inventorymenu.h"
#include "ui/videowidget.h"
#include "camera.h"
#include "gothic.h"
#include "world/objects/npc.h"
#include "gapi/vulkan/vpipelinelogger.h"
#include "utils/string_frm.h"

// render resolution by vidResIndex; 0..7 are the old steps (kept for saved settings), 8.. are the finer ones
static const float resScaleTable[] = {1.f,0.75f,0.5f,0.4f,0.3f,0.25f,0.2f,0.15f,
                                      0.95f,0.9f,0.85f,0.8f,0.7f,0.65f,0.6f,0.55f,0.45f,0.35f};
static int resScaleCount() { return int(sizeof(resScaleTable)/sizeof(resScaleTable[0])); }

using namespace Tempest;

static const bool skyPathTrace = false;

static uint32_t nextPot(uint32_t x) {
  x--;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x++;
  return x;
  }

static float smoothstep(float edge0, float edge1, float x) {
  float t = std::min(std::max((x - edge0) / (edge1 - edge0), 0.f), 1.f);
  return t * t * (3.f - 2.f * t);
  };

static float linearstep(float edge0, float edge1, float x) {
  float t = std::min(std::max((x - edge0) / (edge1 - edge0), 0.f), 1.f);
  return t;
  };

static Size tileCount(Size sz, int s) {
  sz.w = (sz.w+s-1)/s;
  sz.h = (sz.h+s-1)/s;
  return sz;
  }

Renderer::Renderer(Tempest::Swapchain& swapchain)
  : swapchain(swapchain) {
  auto& device = Resources::device();

  static const TextureFormat sfrm[] = {
    TextureFormat::Depth16,
    TextureFormat::Depth24x8,
    TextureFormat::Depth32F,
    };

  for(auto& i:sfrm) {
    if(device.properties().hasDepthFormat(i) && device.properties().hasSamplerFormat(i)) {
      shadowFormat = i;
      break;
      }
    }

  static const TextureFormat zfrm[] = {
    TextureFormat::Depth32F,
    TextureFormat::Depth24x8,
    TextureFormat::Depth16,
    };
  for(auto& i:zfrm) {
    if(device.properties().hasDepthFormat(i) && device.properties().hasSamplerFormat(i)){
      zBufferFormat = i;
      break;
      }
    }

  // crappy rasbery-pi like hardware
  if(!device.properties().hasStorageFormat(sky.lutRGBAFormat))
    sky.lutRGBAFormat = Tempest::TextureFormat::RGBA8;
  if(!device.properties().hasStorageFormat(sky.lutRGBFormat))
    sky.lutRGBFormat = Tempest::TextureFormat::RGBA8;
#if defined(__EMSCRIPTEN__)
  // WGSL storage images need a fixed format: web shaders declare rgba16f/r32f
  if(device.properties().hasStorageFormat(TextureFormat::RGBA16F)) {
    sky.lutRGBAFormat = TextureFormat::RGBA16F;
    sky.lutRGBFormat  = TextureFormat::RGBA16F;
    }
  ssao.aoFormat = TextureFormat::R32F;
#endif

  Log::i("GPU = ",device.properties().name);
  Log::i("Depth format = ", Tempest::formatName(zBufferFormat), " Shadow format = ", Tempest::formatName(shadowFormat));

  Gothic::inst().onSettingsChanged.bind(this,&Renderer::setupSettings);
  Gothic::inst().toggleGi  .bind(this, &Renderer::toggleGi);
  Gothic::inst().toggleVsm .bind(this, &Renderer::toggleVsm);
  Gothic::inst().toggleRtsm.bind(this, &Renderer::toggleRtsm);

  settings.giEnabled   = Gothic::options().doRtGi;
  settings.vsmEnabled  = Gothic::options().doVirtualShadow;
  settings.rtsmEnabled = Gothic::options().doSoftwareShadow;
  settings.swrEnabled  = Gothic::options().swRenderingPreset>0;
  settings.swrtEnabled = Gothic::options().doSoftwareRT;

  sky.cloudsLut     = device.image2d   (sky.lutRGBAFormat,  2,  1);
  sky.transLut      = device.attachment(sky.lutRGBFormat, 256, 64);
  sky.multiScatLut  = device.attachment(sky.lutRGBFormat,  32, 32);
  sky.viewLut       = device.attachment(Tempest::TextureFormat::RGBA32F, 128, 64);
  sky.viewCldLut    = device.attachment(Tempest::TextureFormat::RGBA32F, 512, 256);
  sky.irradianceLut = device.image2d(TextureFormat::RGBA32F, 3,2);

  setupSettings();
  shaders.prewarmAll();
  }

Renderer::~Renderer() {
  Gothic::inst().onSettingsChanged.ubind(this,&Renderer::setupSettings);
  }

void Renderer::resetSwapchain() {
  auto& device = Resources::device();
  device.waitIdle();
  shaders.waitCompiler();

  const auto     res    = internalResolution();
  const uint32_t w      = uint32_t(res.w);
  const uint32_t h      = uint32_t(res.h);

  sceneLinear = device.attachment(TextureFormat::R11G11B10UF,w,h);

  if(settings.aaEnabled) {
    cmaa2.workingEdges               = device.image2d(TextureFormat::R8, (w + 1) / 2, h);
    cmaa2.shapeCandidates            = device.ssbo(Tempest::Uninitialized, w * h / 4 * sizeof(uint32_t));
    cmaa2.deferredBlendLocationList  = device.ssbo(Tempest::Uninitialized, (w * h + 3) / 6 * sizeof(uint32_t));
    cmaa2.deferredBlendItemList      = device.ssbo(Tempest::Uninitialized, w * h * sizeof(uint32_t));
    cmaa2.deferredBlendItemListHeads = device.image2d(TextureFormat::R32U, (w + 1) / 2, (h + 1) / 2);
    cmaa2.controlBuffer              = device.ssbo(nullptr, 5 * sizeof(uint32_t));
    cmaa2.indirectBuffer             = device.ssbo(nullptr, sizeof(DispatchIndirectCommand) + sizeof(DrawIndirectCommand));
    }

  zbuffer = device.zbuffer(zBufferFormat,w,h);
  if(w!=swapchain.w() || h!=swapchain.h())
    zbufferUi = device.zbuffer(zBufferFormat, swapchain.w(), swapchain.h()); else
    zbufferUi = ZBuffer();

  uint32_t pw = nextPot(w);
  uint32_t ph = nextPot(h);

  uint32_t hw = pw;
  uint32_t hh = ph;
#if defined(__ANDROID__)
  const uint32_t maxHiZDim = 32;
#else
  const uint32_t maxHiZDim = 64;
#endif
  while(hw>maxHiZDim || hh>maxHiZDim) {
    hw = std::max(1u, (hw+1)/2u);
    hh = std::max(1u, (hh+1)/2u);
    }

  hiz.atomicImg = device.properties().hasAtomicFormat(TextureFormat::R32U);
  hiz.hiZ       = device.image2d(TextureFormat::R16,  hw, hh, true);
  if(hiz.atomicImg) {
    hiz.counter = device.image2d(TextureFormat::R32U, std::max(hw/4, 1u), std::max(hh/4, 1u), false);
    } else {
    hiz.counterBuf = device.ssbo(Tempest::Uninitialized, std::max(hw/4, 1u)*std::max(hh/4, 1u)*sizeof(uint32_t));
    }

  sceneOpaque   = device.attachment(TextureFormat::R11G11B10UF,w,h);
  sceneDepth    = device.attachment(TextureFormat::R32F,       w,h);

  gbufDiffuse   = device.attachment(TextureFormat::RGBA8,w,h);
  gbufNormal    = device.attachment(TextureFormat::R32U, w,h);

  ssao.ssaoBuf  = device.image2d(ssao.aoFormat, w, h);
  ssao.ssaoBlur = device.image2d(ssao.aoFormat, w, h);

  epipolar = decltype(epipolar)();
  vsm      = decltype(vsm)();
  rtsm     = decltype(rtsm)();

  // software renderer
  swr.outputImage = StorageImage();

  // swrt
  swrt.outputImage = StorageImage();

  resetSkyFog();
  prepareUniforms();
  }

void Renderer::setupSettings() {
  settings.zEnvMappingEnabled = Gothic::settingsGetI("ENGINE","zEnvMappingEnabled")!=0;
  settings.zCloudShadowScale  = Gothic::settingsGetI("ENGINE","zCloudShadowScale") !=0;
  settings.zFogRadial         = Gothic::settingsGetI("RENDERER_D3D","zFogRadial")!=0;


#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
  // web settings menu: 0 = off (the shadow maps are only cleared: all lit), 1 = 1024, 2 = 2048,
  // 3 = ultra low: as off, with a circle under every npc
  {
  const int sh = Gothic::settingsGetI("INTERNAL","webShadows");
  settings.shadowsEnabled   = sh>0 && sh!=3; // 3: ultra low, circles under npcs
  settings.shadowResolution = sh==3 ? 512 : (sh>=2 ? 2048 : (sh==1 ? 1024 : 512));
  }
  // water quality low: no water material (it is picked at world load) and no water passes
  // HQ fog (zFogRadial) is not supported on the web: the final fog pass would be skipped
  settings.zFogRadial  = false;
  settings.simpleWater = Gothic::settingsGetI("INTERNAL","webWaterQuality")==1; // 0 high, 1 low, 2 ultra high (swamp water)
  Material::simpleWater = settings.simpleWater;
#endif

  settings.zVidBrightness     = Gothic::settingsGetF("VIDEO","zVidBrightness");
  settings.zVidContrast       = Gothic::settingsGetF("VIDEO","zVidContrast");
  settings.zVidGamma          = Gothic::settingsGetF("VIDEO","zVidGamma");

  settings.sunSize            = Gothic::settingsGetF("SKY_OUTDOOR","zSunSize");
  settings.moonSize           = Gothic::settingsGetF("SKY_OUTDOOR","zMoonSize");
  if(settings.sunSize<=1)
    settings.sunSize = 200;
  if(settings.moonSize<=1)
    settings.moonSize = 400;

  auto prevVidResIndex = settings.vidResIndex;
  settings.vidResIndex = Gothic::inst().settingsGetF("INTERNAL","vidResIndex");
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
  // web: the resolution comes from the settings menu only (debug.cfg's
  // render_scale=100 default would force full resolution back)
  int rscale = 0;
#else
  int rscale = opengothic_debug_render_scale();
#endif
  for(int i=0; i<resScaleCount(); ++i)
    if(rscale == int(resScaleTable[i]*100.f+0.5f)) {
      settings.vidResIndex = float(i);
      break;
      }
  settings.aaEnabled = (Gothic::options().aaPreset>0) && (settings.vidResIndex==0);

  // direct lighting
  if(settings.rtsmEnabled)
    shadow.directLightPso = &shaders.rtsmDirectLight;
  else if(settings.vsmEnabled)
    shadow.directLightPso = &shaders.vsmDirectLight; //TODO: naming
  else if(Gothic::options().doRayQuery && Resources::device().properties().descriptors.nonUniformIndexing &&
           settings.shadowResolution>0)
    shadow.directLightPso = &shaders.directLightRq;
#if defined(__ANDROID__)
  // shadows off: the shadow maps are only cleared, every tap of the shadow filter gives exactly 1.0 (all lit); the shader without
  // the maps gives the same light and skips ~8 gathers per pixel
  else if(settings.shadowResolution>0 && settings.shadowsEnabled)
#else
  else if(settings.shadowResolution>0)
#endif
    shadow.directLightPso = &shaders.directLightSh;
  else
    shadow.directLightPso = &shaders.directLight;

  // point-lights
  if(settings.vsmEnabled)
    lights.directLightPso = &shaders.lightsVsm;
  else if(Gothic::options().doRayQuery && Resources::device().properties().descriptors.nonUniformIndexing)
    lights.directLightPso = &shaders.lightsRq;
  else
    lights.directLightPso = &shaders.lights;

  if(prevVidResIndex!=settings.vidResIndex) {
    resetSwapchain();
    }
  resetSkyFog();
  resetShadowmap();
  prepareUniforms();

  Log::i("RENDERER_SETTINGS: vidResIndex=", settings.vidResIndex,
         " shadowResolution=", settings.shadowResolution,
         " vsmEnabled=", settings.vsmEnabled,
         " rtsmEnabled=", settings.rtsmEnabled,
         " giEnabled=", settings.giEnabled,
         " swrtEnabled=", settings.swrtEnabled,
         " aaEnabled=", settings.aaEnabled,
         " fogQuality=", int(sky.quality),
         " zCloudShadowScale=", settings.zCloudShadowScale,
         " zFogRadial=", settings.zFogRadial,
         " w=", swapchain.w(), " h=", swapchain.h(),
         " internalRes=", internalResolution().w, "x", internalResolution().h);
  }

void Renderer::toggleGi() {
  auto& device = Resources::device();
  if(!Gothic::options().doRayQuery)
    return;

  auto prop = device.properties();
  if(prop.tex2d.maxSize<4096 || !prop.hasStorageFormat(R11G11B10UF) || !prop.hasStorageFormat(R16))
    return;

  settings.giEnabled = !settings.giEnabled;

  device.waitIdle();
  if(auto wview  = Gothic::inst().worldView()) {
    wview->resetRendering();
    }
  if(settings.giEnabled) {
    // need a projective shadow, for gi
    resetShadowmap();
    }
  prepareUniforms();
  }

void Renderer::toggleVsm() {
  if(!Shaders::isVsmSupported())
    return;

  settings.vsmEnabled = !settings.vsmEnabled;

  auto& device = Resources::device();
  device.waitIdle();

  setupSettings();
  resetSwapchain();
  if(auto wview  = Gothic::inst().worldView()) {
    wview->resetRendering();
    }
  }

void Renderer::toggleRtsm() {
  if(!Shaders::isRtsmSupported())
    return;

  settings.rtsmEnabled = !settings.rtsmEnabled;

  auto& device = Resources::device();
  device.waitIdle();

  setupSettings();
  resetSwapchain();
  if(auto wview  = Gothic::inst().worldView()) {
    wview->resetRendering();
    }
  }

void Renderer::onWorldChanged() {
  sky.lutIsInitialized = false;
  shaders.waitCompiler();

  resetSwapchain();
  resetSkyFog();
  prepareUniforms();
  }

void Renderer::updateCamera(const Camera& camera) {
  proj        = camera.projective();
  viewProj    = camera.viewProj();
  viewProjLwc = camera.viewProjLwc();

  if(auto wview=Gothic::inst().worldView()) {
    for(size_t i=0; i<Resources::ShadowLayers; ++i)
      shadowMatrix[i] = camera.viewShadow(wview->mainLight().dir(),i);
    shadowMatrixVsm = camera.viewShadowVsm(wview->mainLight().dir());
    }

  auto zNear = camera.zNear();
  auto zFar  = camera.zFar();
  clipInfo.x  = zNear*zFar;
  clipInfo.y  = zNear-zFar;
  clipInfo.z  = zFar;
  }

bool Renderer::requiresTlas() const {
  if(!Gothic::options().doRayQuery)
    return false;

  if(settings.giEnabled)
    return true;
  if(!(settings.rtsmEnabled || settings.vsmEnabled))
    return true;
  return false;
  }

StorageImage& Renderer::usesImage3d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h, uint32_t d, bool mips) {
  if(ret.format()==frm && uint32_t(ret.w())==w && uint32_t(ret.h())==h && uint32_t(ret.d())==d && bool(ret.mipCount()>1)==mips)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().image3d(frm, w, h, d, mips);
  return ret;
  }

StorageImage& Renderer::usesImage2d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h, bool mips) {
  if(ret.format()==frm && uint32_t(ret.w())==w && uint32_t(ret.h())==h && ret.d()==1 && bool(ret.mipCount()>1)==mips)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().image2d(frm, w, h, mips);
  return ret;
  }

StorageImage& Renderer::usesImage2d(Tempest::StorageImage& ret, Tempest::TextureFormat frm, Tempest::Size size, bool mips) {
  if(ret.format()==frm && ret.size()==size && ret.d()==1 && (ret.mipCount()>1)==mips)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().image2d(frm, size, mips);
  return ret;
  }

ZBuffer& Renderer::usesZBuffer(Tempest::ZBuffer& ret, Tempest::TextureFormat frm, uint32_t w, uint32_t h) {
  if(textureCast<Texture2d&>(ret).format()==frm && uint32_t(ret.w())==w && uint32_t(ret.h())==h)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().zbuffer(frm, w, h);
  return ret;
  }

StorageBuffer& Renderer::usesSsbo(Tempest::StorageBuffer& ret, size_t size) {
  if(ret.byteSize()==size)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().ssbo(Uninitialized, size);
  return ret;
  }

StorageBuffer& Renderer::usesScratch(Tempest::StorageBuffer& ret, size_t size) {
  if(ret.byteSize()>=size)
    return ret;
  Resources::recycle(std::move(ret));
  ret = Resources::device().ssbo(Uninitialized, size);
  return ret;
  }

void Renderer::prepareUniforms() {
  auto wview = Gothic::inst().worldView();
  if(wview==nullptr)
    return;

  const Texture2d* sh[Resources::ShadowLayers] = {};
  for(size_t i=0; i<Resources::ShadowLayers; ++i)
    if(!shadowMap[i].isEmpty()) {
      sh[i] = &textureCast<const Texture2d&>(shadowMap[i]);
      }
  wview->setShadowMaps(sh);
  wview->setVirtualShadowMap(settings.vsmEnabled, vsm.pageData, vsm.pageTbl, vsm.pageHiZ, vsm.pageList);

  wview->setHiZ(textureCast<const Texture2d&>(hiz.hiZ));
  wview->setGbuffer(textureCast<const Texture2d&>(gbufDiffuse), textureCast<const Texture2d&>(gbufNormal));
  wview->setSceneImages(textureCast<const Texture2d&>(sceneOpaque), textureCast<const Texture2d&>(sceneDepth), zbuffer);
  }

void Renderer::resetShadowmap() {
  auto& device = Resources::device();

  for(int i=0; i<Resources::ShadowLayers; ++i)
    Resources::recycle(std::move(shadowMap[i]));

  for(int i=0; i<Resources::ShadowLayers; ++i) {
    if(settings.vsmEnabled && !(settings.giEnabled && i==1) && !(sky.quality==PathTrace && i==1) && !(settings.rtsmEnabled && i==1))
      continue; //TODO: support vsm in gi code
    if(settings.rtsmEnabled && !(settings.giEnabled && i==1) && !(sky.quality!=None && i==1))
      continue; //TODO: support vsm in gi code
    shadowMap[i] = device.zbuffer(shadowFormat, settings.shadowResolution, settings.shadowResolution);
    }
  }

void Renderer::resetSkyFog() {
  auto& device = Resources::device();

  const auto     res = internalResolution();
  const uint32_t w   = uint32_t(res.w);
  const uint32_t h   = uint32_t(res.h);

  {
    auto q = Quality::VolumetricLQ;
    if(!settings.zFogRadial) {
      q = Quality::VolumetricLQ;
      } else {
      q = Quality::VolumetricHQ;
      // q = Quality::Epipolar;
      }

    if(skyPathTrace)
      q = PathTrace;

    if(sky.quality==q && (sky.occlusionLut.isEmpty() || sky.occlusionLut.size()==res)) {
      return;
      }

    sky.quality = q;
  }

  Resources::recycle(std::move(sky.fogLut3D));
  Resources::recycle(std::move(sky.fogLut3DMs));
  Resources::recycle(std::move(sky.occlusionLut));

  sky.lutIsInitialized = false;

  switch(sky.quality) {
    case None:
    case VolumetricLQ:
#if defined(__ANDROID__)
      sky.fogLut3D     = device.image3d(sky.lutRGBAFormat, 80, 45, 16);
#else
      sky.fogLut3D     = device.image3d(sky.lutRGBAFormat, 160, 90, 64);
#endif
      sky.occlusionLut = StorageImage();
      break;
    case VolumetricHQ:
      // fogLut and oclussion are decupled
      sky.fogLut3D      = device.image3d(sky.lutRGBFormat,  128,64,32);
      sky.fogLut3DMs    = device.image3d(sky.lutRGBAFormat, 128,64,32);
      sky.occlusionLut  = device.image2d(TextureFormat::R32U, w, h);
      break;
    case Epipolar:
      sky.fogLut3D      = device.image3d(sky.lutRGBFormat,  128,64,32);
      sky.fogLut3DMs    = device.image3d(sky.lutRGBAFormat, 128,64,32);
      sky.occlusionLut  = device.image2d(TextureFormat::R32U, w, h); //TODO: remove
      break;
    case PathTrace:
      break;
    }
  }

void Renderer::prepareSky(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  auto& scene   = wview.sceneGlobals();

  cmd.setDebugMarker("Sky LUT");
  if(!sky.lutIsInitialized) {
    sky.lutIsInitialized = true;

    cmd.setFramebuffer({});
    cmd.setBinding(0, sky.cloudsLut);
    cmd.setBinding(5, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
    cmd.setBinding(6, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
    cmd.setBinding(7, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
    cmd.setBinding(8, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
    cmd.setPipeline(shaders.cloudsLut);
    cmd.dispatchThreads(size_t(sky.cloudsLut.w()), size_t(sky.cloudsLut.h()));

    auto sz = Vec2(float(sky.transLut.w()), float(sky.transLut.h()));
    cmd.setFramebuffer({{sky.transLut, Tempest::Discard, Tempest::Preserve}});
    cmd.setBinding(5, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
    cmd.setBinding(6, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
    cmd.setBinding(7, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
    cmd.setBinding(8, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
    cmd.setPushData(&sz, sizeof(sz));
    cmd.setPipeline(shaders.skyTransmittance);
    cmd.draw(nullptr, 0, 3);

    sz = Vec2(float(sky.multiScatLut.w()), float(sky.multiScatLut.h()));
    cmd.setFramebuffer({{sky.multiScatLut, Tempest::Discard, Tempest::Preserve}});
    cmd.setBinding(0, sky.transLut, Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setPushData(&sz, sizeof(sz));
    cmd.setPipeline(shaders.skyMultiScattering);
    cmd.draw(nullptr, 0, 3);
    }

  auto sz = Vec2(float(sky.viewLut.w()), float(sky.viewLut.h()));
  cmd.setFramebuffer({{sky.viewLut, Tempest::Discard, Tempest::Preserve}});
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sky.transLut,     Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(2, sky.multiScatLut, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(3, sky.cloudsLut,    Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setPushData(&sz, sizeof(sz));
  cmd.setPipeline(shaders.skyViewLut);
  cmd.draw(nullptr, 0, 3);

  sz = Vec2(float(sky.viewCldLut.w()), float(sky.viewCldLut.h()));
  cmd.setFramebuffer({{sky.viewCldLut, Tempest::Discard, Tempest::Preserve}});
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sky.viewLut);
  cmd.setBinding(2, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
  cmd.setBinding(3, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
  cmd.setBinding(4, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
  cmd.setBinding(5, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
  cmd.setPushData(&sz, sizeof(sz));
  cmd.setPipeline(shaders.skyViewCldLut);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::draw(Encoder<CommandBuffer>& cmd, uint8_t cmdId, size_t imgId,
                    VectorImage::Mesh& uiLayer, VectorImage::Mesh& numOverlay,
                    InventoryMenu& inventory, VideoWidget& video) {
  auto& result = swapchain[imgId];

  if(!video.isActive()) {
    draw(result, cmd, cmdId);
    } else {
    cmd.setFramebuffer({{result, Vec4(), Tempest::Preserve}});
    }
  cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
  cmd.setDebugMarker("UI");
  uiLayer.draw(cmd);

#if defined(OG_WEB_UI)
  const bool quick = inventory.quickVisible();
#else
  const bool quick = false;
#endif
  if(quick && inventory.isOpen()==InventoryMenu::State::Closed) {
    auto& zb = (zbufferUi.isEmpty() ? zbuffer : zbufferUi);
    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}},{zb, 1.f, Tempest::Preserve});
    cmd.setDebugMarker("Quick slots");
    const float sx = float(result.w())/float(std::max(1,inventory.w()));
    const float sy = float(result.h())/float(std::max(1,inventory.h()));
    inventory.draw(cmd,sx,sy);
    }
  if(inventory.isOpen()!=InventoryMenu::State::Closed) {
    auto& zb = (zbufferUi.isEmpty() ? zbuffer : zbufferUi);
    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}},{zb, 1.f, Tempest::Preserve});
    cmd.setDebugMarker("Inventory");
    // the items are placed in window pixels: the swapchain is not always the size of the window
    // (web on a phone: the canvas has about two pixels per css pixel)
    const float sx = float(result.w())/float(std::max(1,inventory.w()));
    const float sy = float(result.h())/float(std::max(1,inventory.h()));
    inventory.draw(cmd,sx,sy);

    cmd.setFramebuffer({{result, Tempest::Preserve, Tempest::Preserve}});
    cmd.setDebugMarker("Inventory-counters");
    numOverlay.draw(cmd);
    }
  }

void Renderer::dbgDraw(Tempest::Painter& p) {
  static bool dbg = false;
  if(!dbg)
    return;

  std::vector<const Texture2d*> tex;
  //tex.push_back(&textureCast(swr.outputImage));
  //tex.push_back(&textureCast(hiz.hiZ));
  //tex.push_back(&textureCast(hiz.smProj));
  //tex.push_back(&textureCast(hiz.hiZSm1));
  //tex.push_back(&textureCast(shadowMap[1]));
  //tex.push_back(&textureCast<const Texture2d&>(shadowMap[0]));
  //tex.push_back(&textureCast<const Texture2d&>(vsm.pageData));
  tex.push_back(&textureCast<const Texture2d&>(swrt.outputImage));

  static int size = 400;
  int left = 10;
  for(auto& t:tex) {
    p.setBrush(Brush(*t,Painter::NoBlend,ClampMode::ClampToBorder));
    auto sz = Size(p.brush().w(),p.brush().h());
    if(sz.isEmpty())
      continue;
    while(sz.w<size && sz.h<size) {
      sz.w *= 2;
      sz.h *= 2;
      }
    while(sz.w>size*2 || sz.h>size*2) {
      sz.w = (sz.w+1)/2;
      sz.h = (sz.h+1)/2;
      }
    p.drawRect(left,50,sz.w,sz.h,
               0,0,p.brush().w(),p.brush().h());
    left += (sz.w+10);
    }
  }

void Renderer::draw(Tempest::Attachment& result, Encoder<CommandBuffer>& cmd, uint8_t fId) {
  auto wview  = Gothic::inst().worldView();
  auto camera = Gothic::inst().camera();
  if(wview==nullptr || camera==nullptr) {
    cmd.setFramebuffer({{result, Vec4(), Tempest::Preserve}});
    return;
    }

  // stage timings are only logged every 10th frame: the clock is a call into js on the web
  static uint32_t s_dCnt = 0;
  const bool logPerf = ((s_dCnt+1) % 10 == 0);
  auto perfNow = [logPerf]() -> uint64_t { return logPerf ? Application::tickCount() : 0; };

  static int s_lastRenderScale = -1;
  int curScale = opengothic_debug_render_scale();
  if(s_lastRenderScale != -1 && s_lastRenderScale != curScale) {
    s_lastRenderScale = curScale;
    setupSettings();
    } else {
    s_lastRenderScale = curScale;
    }

  if(requiresTlas())
    wview->updateRtScene();
  wview->updateLights();

  updateCamera(*camera);

  static bool updFr = true;
  if(updFr){
    if(wview->mainLight().dir().y>Camera::minShadowY) {
      frustrum[SceneGlobals::V_Shadow0].make(shadowMatrix[0],shadowMap[0].w(),shadowMap[0].h());
      frustrum[SceneGlobals::V_Shadow1].make(shadowMatrix[1],shadowMap[1].w(),shadowMap[1].h());
      } else {
      frustrum[SceneGlobals::V_Shadow0].clear();
      frustrum[SceneGlobals::V_Shadow1].clear();
      }
    frustrum[SceneGlobals::V_Main].make(viewProj,zbuffer.w(),zbuffer.h());
    frustrum[SceneGlobals::V_HiZ] = frustrum[SceneGlobals::V_Main];
    frustrum[SceneGlobals::V_Vsm] = frustrum[SceneGlobals::V_Shadow1]; //TODO: remove
    wview->updateFrustrum(frustrum);
    }

  wview->preFrameUpdate(*camera,Gothic::inst().world()->tickCount(),fId);
  wview->prepareGlobals(cmd,fId);

  auto t0 = perfNow();
  wview->visibilityPass(cmd, 0);
  prepareSky(cmd,*wview);
  auto tVis0 = perfNow() - t0;

  auto t1 = perfNow();
  const bool useHizPrepass = opengothic_debug_use_hiz_prepass() && !opengothic_debug_skip_hiz();
  if(useHizPrepass) {
    drawHiZ (cmd, *wview);
    buildHiZ(cmd);
    }
  auto tHiZ = perfNow() - t1;

  auto t2 = perfNow();
  wview->visibilityPass(cmd, 1);
  drawGBuffer(cmd,fId,*wview);
  auto tGBuf = perfNow() - t2;

  const bool simpleRender = (opengothic_debug_simple_render() != 0);

  auto t3 = perfNow();
  if(!simpleRender) {
    drawShadowMap(cmd,fId,*wview);
    prepareEpipolar(cmd, *wview);
  }
  auto tShadow = perfNow() - t3;

  auto t4 = perfNow();
  prepareExposure(cmd,*wview);
  prepareIrradiance(cmd,*wview);

  if(!simpleRender) {
    drawVsm(cmd, *wview);
    drawSwr(cmd, *wview);
    drawRtsm(cmd, *wview);
    drawRtsmOmni(cmd, *wview);

    drawSwRT(cmd, *wview);

    prepareSSAO(cmd,*wview);
    prepareFog (cmd,*wview);
    prepareGi  (cmd,*wview);
  }

  if(simpleRender) {
    cmd.setFramebuffer({{sceneLinear, Tempest::Vec4(), Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
  } else {
    cmd.setFramebuffer({{sceneLinear, Tempest::Discard, Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
    drawShadowResolve(cmd,*wview);
  }
  drawAmbient(cmd,*wview);
  if(!simpleRender) {
    drawLights(cmd,*wview);
  }
  drawSky(cmd,*wview);
  auto tLight = perfNow() - t4;

  auto t5 = perfNow();
#if !defined(__EMSCRIPTEN__)
  // nothing in this frame reads the copy: no water, no glass, no ghosts (conservative: the camera in water copies)
  const bool needStash = camera->isInWater() || wview->needsSceneCopy(fId) || opengothic_debug_always_stash();
  if(needStash)
    stashSceneAux(cmd);
#else
  stashSceneAux(cmd);
#endif

  // water quality low: a world loaded before the switch still has its water
  const bool drawWater = !settings.simpleWater || wview->hasWater();
  if(drawWater)
    drawGWater(cmd, *wview);

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Preserve, Tempest::Preserve});
  cmd.setDebugMarker("Translucent");
  wview->drawTranslucent(cmd, fId);

  drawProbesDbg(cmd, *wview);
  drawProbesHitDbg(cmd);
  drawVsmDbg(cmd, *wview);
  drawSwrDbg(cmd, *wview);
  drawRtsmDbg(cmd, *wview);

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  if(!simpleRender) {
    if(drawWater)
      drawReflections(cmd, *wview);
    if(camera->isInWater()) {
      cmd.setDebugMarker("Underwater");
      drawUnderwater(cmd, *wview);
      } else {
      cmd.setDebugMarker("Fog");
      drawFog(cmd, *wview);
      }
    }
  drawBarrierZones(cmd, *wview);
  drawBarrierBolts(cmd, *wview);
  // after the fog: the haze is nearly opaque on the sky and would swallow the sun disc
  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}}, {zbuffer, Tempest::Readonly});
  cmd.setDebugMarker("Sun&Moon");
  if(!camera->isInWater()) // under the water the sun is not seen
    drawSunMoon(cmd, *wview);
  cmd.setDebugMarker("Barrier");
  drawBarrier(cmd, *wview);
  auto tTrans = perfNow() - t5;

  auto t6 = perfNow();
  if(settings.aaEnabled) {
    cmd.setDebugMarker("CMAA2 & Tonemapping");
    drawCMAA2(result, cmd, *wview);
    } else {
    cmd.setDebugMarker("Tonemapping");
    drawTonemapping(result, cmd, *wview);
    }
  auto tPost = perfNow() - t6;

  if(++s_dCnt % 10 == 0) {
    Log::i("[DRAW_PERF] vis0=", tVis0, " hiz=", tHiZ, " gbuf=", tGBuf, " sh=", tShadow, " light=", tLight, " trans=", tTrans, " post=", tPost);
  }

  wview->postFrameupdate();
  }

void Renderer::drawTonemapping(Attachment& result, Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  struct Push {
    float brightness = 0;
    float contrast   = 1;
    float gamma      = 1.f/2.2f;
    float mul        = 1;
    };

  Push p;
  p.brightness = (settings.zVidBrightness - 0.5f)*0.1f;
  p.contrast   = std::max(1.5f - settings.zVidContrast, 0.01f);
  p.gamma      = p.gamma/std::max(2.0f*settings.zVidGamma,  0.01f);

  static float mul = 0.f;
  if(mul>0)
    p.mul = mul;

  auto& pso = (settings.vidResIndex==0) ? shaders.tonemapping : shaders.tonemappingUpscale;
  cmd.setFramebuffer({ {result, Tempest::Discard, Tempest::Preserve} });
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
#if defined(__MOBILE_PLATFORM__)
  cmd.setBinding(1, sceneLinear, Sampler::bilinear(ClampMode::ClampToEdge));
#elif defined(__WEB_PLATFORM__)
  cmd.setBinding(1, sceneLinear, Sampler::bilinear(ClampMode::ClampToEdge)); // lanczosUpscaleLinear
#else
  cmd.setBinding(1, sceneLinear, Sampler::nearest(ClampMode::ClampToEdge)); // Lanczos upscale requires nearest sampling
#endif
  cmd.setPushData(p);
  cmd.setPipeline(pso);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawCMAA2(Tempest::Attachment& result, Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  auto&          pso             = shaders.cmaa2EdgeColor2x2Presets[Gothic::options().aaPreset];
  const IVec3    inputGroupSize  = pso.workGroupSize();
  const IVec3    outputGroupSize = inputGroupSize - IVec3(2, 2, 0);
  const uint32_t groupCountX     = uint32_t((sceneLinear.w() + outputGroupSize.x * 2 - 1) / (outputGroupSize.x * 2));
  const uint32_t groupCountY     = uint32_t((sceneLinear.h() + outputGroupSize.y * 2 - 1) / (outputGroupSize.y * 2));

  cmd.setFramebuffer({});
  cmd.setBinding(0, sceneLinear, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(1, cmaa2.workingEdges);
  cmd.setBinding(2, cmaa2.shapeCandidates);
  cmd.setBinding(3, cmaa2.deferredBlendLocationList);
  cmd.setBinding(4, cmaa2.deferredBlendItemList);
  cmd.setBinding(5, cmaa2.deferredBlendItemListHeads);
  cmd.setBinding(6, cmaa2.controlBuffer);
  cmd.setBinding(7, cmaa2.indirectBuffer);

  // detect edges
  cmd.setPipeline(pso);
  cmd.dispatch(groupCountX, groupCountY, 1);

  // process candidates pass
  cmd.setPipeline(shaders.cmaa2ProcessCandidates);
  cmd.dispatchIndirect(cmaa2.indirectBuffer, 0);

  // deferred color apply
  struct Push {
    float brightness = 0;
    float contrast   = 1;
    float gamma      = 1.f/2.2f;
    float mul        = 1;
    };

  Push p;
  p.brightness = (settings.zVidBrightness - 0.5f)*0.1f;
  p.contrast   = std::max(1.5f - settings.zVidContrast, 0.01f);
  p.gamma      = p.gamma/std::max(2.0f*settings.zVidGamma,  0.01f);

  static float mul = 0.f;
  if(mul>0)
    p.mul = mul;

  auto& psoTone = (settings.vidResIndex==0) ? shaders.tonemapping : shaders.tonemappingUpscale;
  cmd.setFramebuffer({{result, Tempest::Discard, Tempest::Preserve}});
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sceneLinear, Sampler::nearest());
  cmd.setPushData(&p, sizeof(p));
  cmd.setPipeline(psoTone);
  cmd.draw(nullptr, 0, 3);

  cmd.setBinding(0, sceneLinear);
  cmd.setBinding(1, cmaa2.workingEdges);
  cmd.setBinding(2, cmaa2.shapeCandidates);
  cmd.setBinding(3, cmaa2.deferredBlendLocationList);
  cmd.setBinding(4, cmaa2.deferredBlendItemList);
  cmd.setBinding(5, cmaa2.deferredBlendItemListHeads);
  cmd.setBinding(6, cmaa2.controlBuffer);
  cmd.setPushData(&p, sizeof(p));
  cmd.setPipeline(shaders.cmaa2DeferredColorApply2x2);
  cmd.drawIndirect(cmaa2.indirectBuffer, 3*sizeof(uint32_t));
  }

void Renderer::drawFog(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  if(opengothic_debug_skip_fog())
    return;
  auto& scene  = wview.sceneGlobals();

  switch(sky.quality) {
    case None:
    case VolumetricLQ: {
      cmd.setBinding(0, sky.fogLut3D, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(1, sky.fogLut3D, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(2, zbuffer, Sampler::nearest()); // NOTE: wanna here depthFetch from gles2
      cmd.setBinding(3, scene.uboGlobal[SceneGlobals::V_Main]);
      cmd.setPipeline(shaders.fog);
      break;
      }
    case VolumetricHQ: {
      cmd.setBinding(0, sky.fogLut3D,   Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(1, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(2, zbuffer,        Sampler::nearest());
      cmd.setBinding(3, scene.uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(4, sky.occlusionLut);
      cmd.setPipeline(shaders.fog3dHQ);
      break;
      }
    case Epipolar: {
      //cmd.setBinding(0, sky.fogLut3D,   Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(1, zbuffer,        Sampler::nearest());
      cmd.setBinding(2, vsm.fogDbg,     Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(3, epipolar.epipoles);
      cmd.setBinding(4, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setPipeline(shaders.vsmFog);
      break;
      }
    case PathTrace:
      return;
    }
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawSunMoon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  drawSunMoon(cmd, wview, false);
  drawSunMoon(cmd, wview, true);
  }

void Renderer::drawSunMoon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview, bool isSun) {
  const int sunMode = Gothic::inst().webSunMode(); // 0 disc + shafts, 1 disc only, 2 off
  if(isSun && sunMode==2)
    return;
  auto& scene = wview.sceneGlobals();
  auto& sun   = wview.sky().sunLight();
  auto  m     = scene.viewProject();
  auto  d     = sun.dir();

  if(!isSun) {
    // fixed pos for now
    d = Vec3::normalize({-1,1,0});
    }

  auto  dx = d;
  float w  = 0;
  m.project(dx.x, dx.y, dx.z, w);

  if(dx.z<=0)
    return;

  struct Push {
    Tempest::Vec2      pos;
    Tempest::Vec2      size;
    Tempest::Vec3      sunDir;
    float              GSunIntensity = 0;
    Tempest::Matrix4x4 viewProjectInv;
    uint32_t           isSun = 0;
    } push;
  push.pos     = Vec2(dx.x,dx.y)/dx.z;
  push.size.x  = 2.f/float(zbuffer.w());
  push.size.y  = 2.f/float(zbuffer.h());

  const float GSunIntensity  = wview.sky().sunIntensity();
  const float GMoonIntensity = wview.sky().moonIntensity();

  const float scale          = internalResolutionScale();
  const float sunSize        = settings.sunSize  * scale;
  const float moonSize       = settings.moonSize * scale;
  const float intencity      = isSun ? 0.07f : 0.4f;

  push.size          *= isSun ? sunSize : (moonSize*0.25f);
  push.GSunIntensity  = isSun ? (GSunIntensity*intencity) : (GMoonIntensity*intencity);
  push.isSun          = isSun ? 1 : 0;
  push.sunDir         = d;
  push.viewProjectInv = scene.viewProjectLwcInv();

  // HACK
  if(isSun) {
    float day = sun.dir().y;
    float stp = linearstep(-0.07f, 0.03f, day);
    push.GSunIntensity *= stp*stp*4.f;
    } else {
    push.GSunIntensity *= wview.sky().isNight();
    }
  // push.GSunIntensity *= exposure;

  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, isSun ? wview.sky().sunImage() : wview.sky().moonImage());
  cmd.setBinding(2, sky.transLut, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setPushData(push);
  cmd.setPipeline(shaders.sun);
  cmd.draw(nullptr, 0, 6);
  }

// the lethal zones of the barrier as translucent red boxes, to see what was marked (Gothic::webBarrierMark)
void Renderer::drawBarrierZones(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  auto& g = Gothic::inst();
  if(!g.webBarrierZonesShown() || wview.sky().barrierImage()==nullptr)
    return;
  auto zones = g.webBarrierZones();
  float px[4], pz[4];
  int   n = g.webBarrierPending(px,pz);
  if(n>=2) { // the zone being marked: the corners so far and the player as the next one
    if(auto pl = g.player()) {
      const auto p = pl->position();
      px[n] = p.x;
      pz[n] = p.z;
      zones.push_back(Gothic::webBarrierMakeZone(px,pz,n+1));
      }
    }
  if(zones.empty())
    return;

  auto& scene = wview.sceneGlobals();
  struct Push {
    Tempest::Vec4 a, b;
    Tempest::Vec2 yRange;
    } push;
  // the column is as high as the player needs
  float y = 0;
  if(auto pl = g.player())
    y = pl->position().y;
  push.yRange = Vec2(y-5000.f, y+5000.f);

  cmd.setBinding(0, zbuffer, Sampler::nearest());
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setPipeline(shaders.barrierZone);
  for(auto& z:zones) {
    push.a = Vec4(z.x[0],z.z[0],z.x[1],z.z[1]);
    push.b = Vec4(z.x[2],z.z[2],z.x[3],z.z[3]);
    cmd.setPushData(push);
    cmd.draw(nullptr, 0, 3);
    }
  }

// the lightning of the barrier around the player right after a strike
void Renderer::drawBarrierBolts(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  auto& g = Gothic::inst();
  const uint64_t hitAt = g.webBarrierHitAt();
  auto& scene = wview.sceneGlobals();
  const uint64_t now  = scene.tickCount;
  const uint64_t life = 1400;
  if(hitAt==0 || now<hitAt || now-hitAt>=life || g.webBarrierMode()==2 || wview.sky().barrierImage()==nullptr)
    return;
  auto pl = g.player();
  if(pl==nullptr)
    return;

  struct Push {
    Tempest::Vec4 pos;
    Tempest::Vec4 misc;
    } push;
  const auto p = pl->position();
  push.pos  = Vec4(p.x, p.y, p.z, float(now-hitAt)/float(life));
  push.misc = Vec4(float(hitAt%977)*0.113f, 0, 0, 0);

  cmd.setBinding(0, zbuffer, Sampler::nearest());
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setPipeline(shaders.barrierBolts);
  cmd.setPushData(push);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawBarrier(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  auto* tex = wview.sky().barrierImage();
  const int mode = Gothic::inst().webBarrierMode(); // 0 classic, 1 always, 2 off
  if(tex==nullptr || mode==2)
    return;
  auto& scene = wview.sceneGlobals();
  auto  bbox  = wview.bbox();

  struct Push {
    Tempest::Vec3 center;
    float         radius = 0;
    Tempest::Vec4 strike;
    } push;
  push.center = Vec3((bbox.first.x+bbox.second.x)*0.5f, bbox.first.y, (bbox.first.z+bbox.second.z)*0.5f);
  push.radius = std::max(bbox.second.x-bbox.first.x, bbox.second.z-bbox.first.z)*0.5f;

  // a white bolt every 5-8 seconds, in the direction the camera looks
  const uint64_t now      = scene.tickCount;
  const uint64_t duration = 700;
  auto& st = barrierStrike;
  if(now<st.start || now>st.next+60000) { // time went backwards (load) or far ahead
    st.start = 0;
    st.next  = now + 3000;
    st.ambientNext = 0;
    }
  if(!st.started || now<st.origin) { // the first look at the barrier
    st.started = true;
    st.origin  = now;
    }

  // classic: the barrier is there for 45 s when the game starts and for 45 s every 36 minutes, it dissolves slowly
  float vis = 1.f;
  if(mode==0) {
    const uint64_t period = 36*60*1000, shown = 45*1000;
    const uint64_t t      = now-st.origin;
    const float    ph     = float(t%period)*0.001f;
    vis = 1.f - smoothstep(float(shown)*0.001f-4.f, float(shown)*0.001f, ph);
    if(t>=period)
      vis *= smoothstep(0.f, 3.f, ph);
    vis = std::max(vis,0.f);
    }
  const bool  dome = vis>0.f;
  const float vol  = Gothic::inst().webBarrierVolume()*vis;

  if(!dome)
    return;

  if(dome && now>=st.ambientNext) { // the hum of the barrier (recorded quietly, so louder than 1): the sound has no loop, start it again when it ends
    constexpr const char* hum = "MFX_BARRIERE_AMBIENT.WAV";
    const uint64_t len = Resources::loadSoundBuffer(hum).timeLength();
    Gothic::inst().emitGlobalSoundWav(hum, 4.0f*vol);
    st.ambientNext = now + (len>500 ? len-100 : 5000);
    }
  if(dome && now>=st.next) {
    auto  rnd = [](uint64_t x) { x ^= x<<13; x ^= x>>7; x ^= x<<17; return float(x%100000)/100000.f; };
    Vec3  f(0,0,1);
    scene.viewProjectLwcInv().project(f);
    const float yaw = std::atan2(f.x,f.z)/(2.f*float(M_PI));
    st.start   = now;
    st.next    = now + 5000 + uint64_t(rnd(now*2654435761u+1)*3000.f);
    st.azimuth = yaw + (rnd(now*40503u+7)-0.5f)*0.2f;
    st.seed    = rnd(now*977u+3)*50.f;
    if(vis>0.2f)
      Gothic::inst().emitGlobalSoundWav("MFX_BARRIERE_SHOOT.WAV", 0.8f*vol);
    }
  const float age = float(now-st.start)/float(duration);
  push.strike = Vec4(st.azimuth, dome && (st.start>0 || now==st.start) && age<1.f ? age : -1.f, st.seed, vis);

  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, *tex, Sampler::trillinear());
  cmd.setPushData(push);
  cmd.setPipeline(shaders.barrier);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawSwRT(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  if(!settings.swrtEnabled)
    return;

  const auto& scene     = wview.sceneGlobals();
  const auto& bvh       = wview.landscape().bvh();
  const auto  originLwc = scene.originLwc;

  if(swrt.outputImage.size()!=zbuffer.size()) {
    Resources::recycle(std::move(swrt.outputImage));
    auto& device = Resources::device();
    // swrt.outputImage = device.image2d(TextureFormat::R32U, zbuffer.size());
    swrt.outputImage = device.image2d(TextureFormat::RGBA8, zbuffer.size());
    }

  cmd.setFramebuffer({});
  cmd.setDebugMarker("Raytracing");
  //cmd.setPipeline(shaders.swRaytracing8);
  cmd.setPipeline(shaders.swRaytracing);
  cmd.setPushData(&originLwc, sizeof(originLwc));
  cmd.setBinding(0, swrt.outputImage);
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, gbufDiffuse);
  cmd.setBinding(3, gbufNormal);
  cmd.setBinding(4, zbuffer);
  cmd.setBinding(5, bvh);

  cmd.dispatchThreads(swrt.outputImage.size());
  }

void Renderer::stashSceneAux(Encoder<CommandBuffer>& cmd) {
  auto& device = Resources::device();
  if(!device.properties().hasSamplerFormat(zBufferFormat))
    return;
  cmd.setFramebuffer({{sceneOpaque, Tempest::Discard, Tempest::Preserve}, {sceneDepth, Tempest::Discard, Tempest::Preserve}});
  cmd.setDebugMarker("Stash scene");
  cmd.setBinding(0, sceneLinear,Sampler::nearest());
  cmd.setBinding(1, zbuffer,    Sampler::nearest());
  cmd.setPipeline(shaders.stash);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawVsmDbg(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = false;
  if(!enable || !settings.vsmEnabled)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  cmd.setDebugMarker("VSM-dbg");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, zbuffer,     Sampler::nearest());
  cmd.setBinding(4, vsm.pageTbl);
  cmd.setBinding(5, vsm.pageList);
  cmd.setBinding(6, vsm.pageData);
  cmd.setBinding(8, wview.sceneGlobals().vsmDbg);
  cmd.setPushData(&settings.vsmMipBias, sizeof(settings.vsmMipBias));
  cmd.setPipeline(shaders.vsmDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawSwrDbg(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = true;
  if(!enable || !settings.swrEnabled)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  cmd.setDebugMarker("SWR-dbg");
  cmd.setBinding(0, swr.outputImage);
  cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(3, gbufNormal,  Sampler::nearest());
  cmd.setBinding(4, zbuffer,     Sampler::nearest());
  cmd.setPipeline(shaders.swRenderingDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawRtsmDbg(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const WorldView& wview) {
  static bool enable = false;
  if(!enable || !settings.rtsmEnabled)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}});
  cmd.setDebugMarker("RTSM-dbg");
#if 1
  cmd.setBinding(0, rtsm.dbg32);
#else
  cmd.setBinding(0, rtsm.primBins);
  cmd.setBinding(1, rtsm.posList);
  cmd.setBinding(2, rtsm.pages);
#endif
  cmd.setPipeline(shaders.rtsmDbg);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawHiZ(Encoder<CommandBuffer>& cmd, WorldView& view) {
  if(opengothic_debug_skip_hiz())
    return;
  cmd.setDebugMarker("HiZ-occluders");
  cmd.setFramebuffer({}, {zbuffer, 1.f, Tempest::Preserve});
  view.drawHiZ(cmd);
  }

void Renderer::buildHiZ(Tempest::Encoder<Tempest::CommandBuffer>& cmd) {
  if(opengothic_debug_skip_hiz())
    return;
  assert(hiz.hiZ.w()<=128 && hiz.hiZ.h()<=128); // shader limitation

  cmd.setDebugMarker("HiZ-mip");
  cmd.setFramebuffer({});
  cmd.setBinding(0, zbuffer, Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setBinding(1, hiz.hiZ);
  cmd.setPipeline(shaders.hiZPot);
  cmd.dispatch(size_t(hiz.hiZ.w()), size_t(hiz.hiZ.h()));

  const uint32_t maxBind = 8, mip = hiz.hiZ.mipCount();
  const uint32_t w = uint32_t(hiz.hiZ.w()), h = uint32_t(hiz.hiZ.h());
  if(hiz.atomicImg) {
    cmd.setBinding(0, hiz.counter, Sampler::nearest(), 0);
    } else {
    cmd.setBinding(0, hiz.counterBuf);
    }
  for(uint32_t i=0; i<maxBind; ++i)
    cmd.setBinding(1+i, hiz.hiZ, Sampler::nearest(), std::min(i, mip-1));
  cmd.setPushData(&mip, sizeof(mip));
  cmd.setPipeline(shaders.hiZMip);
  cmd.dispatchThreads(w,h);
  }

void Renderer::drawVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.vsmEnabled)
    return;

  static bool omniLights = true;
  const bool directLight  = !settings.rtsmEnabled;
  const bool doVirtualFog = directLight && sky.quality!=VolumetricLQ && sky.quality!=PathTrace;

  auto& scene      = wview.sceneGlobals();
  auto& sceneUbo   = scene.uboGlobal[SceneGlobals::V_Main];
  auto& lightsSsbo = wview.lights().lightsSsbo();

  auto& vsmDbg   = usesImage2d(vsm.vsmDbg,   TextureFormat::R32U, zbuffer.size());
  auto& pageTbl  = usesImage3d(vsm.pageTbl,  TextureFormat::R32U, 32, 32, 16);
  auto& pageHiZ  = usesImage3d(vsm.pageHiZ,  TextureFormat::R32U, 32, 32, 16);
  auto& pageData = usesZBuffer(vsm.pageData, shadowFormat,        8192, 8192);

  auto  pageCount   = uint32_t(vsm.pageData.w()/VSM_PAGE_SIZE) * uint32_t(vsm.pageData.h()/VSM_PAGE_SIZE);
  auto& pageList    = usesSsbo(vsm.pageList,    shaders.vsmClear.sizeofBuffer(0, pageCount));
  auto& pageListTmp = usesSsbo(vsm.pageListTmp, shaders.vsmAllocPages.sizeofBuffer(3, pageCount));

  const uint32_t lightsTotal  = omniLights ? uint32_t(wview.lights().size()) : 0;
  const size_t   numOmniPages = lightsTotal*6;
  auto& pageTblOmni   = usesSsbo(vsm.pageTblOmni,   shaders.vsmClearOmni.sizeofBuffer(0, numOmniPages));
  auto& visibleLights = usesSsbo(vsm.visibleLights, shaders.vsmClearOmni.sizeofBuffer(1, lightsTotal ));

  wview.setVirtualShadowMap(true, pageData, pageTbl, pageHiZ, pageList);

  cmd.setFramebuffer({});
  cmd.setDebugMarker("VSM-pages");
  cmd.setBinding(0, pageList);
  cmd.setBinding(1, pageTbl);
  cmd.setBinding(2, pageHiZ);
  cmd.setPipeline(shaders.vsmClear);
  cmd.dispatchThreads(size_t(pageTbl.w()), size_t(pageTbl.h()), size_t(pageTbl.d()));

  if(omniLights) {
    cmd.setBinding(0, pageTblOmni);
    cmd.setBinding(1, visibleLights);
    cmd.setPipeline(shaders.vsmClearOmni);
    cmd.dispatchThreads(numOmniPages);

    struct Push { float znear; uint32_t lightsTotal; } push = {};
    push.znear       = scene.znear;
    push.lightsTotal = lightsTotal;
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, lightsSsbo);
    cmd.setBinding(2, visibleLights);
    cmd.setBinding(3, *scene.hiZ);
    cmd.setPushData(push);
    cmd.setPipeline(shaders.vsmCullLights);
    cmd.dispatchThreads(wview.lights().size());
    }

  if(directLight) {
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, pageTbl);
    cmd.setBinding(5, pageHiZ);
    cmd.setPushData(settings.vsmMipBias);
    cmd.setPipeline(shaders.vsmMarkPages);
    cmd.dispatchThreads(zbuffer.size());
    }

  if(omniLights) {
    struct Push { Vec3 originLwc; float znear; float vsmMipBias; } push = {};
    push.originLwc  = scene.originLwc;
    push.znear      = scene.znear;
    push.vsmMipBias = settings.vsmMipBias;
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, lightsSsbo);
    cmd.setBinding(5, visibleLights);
    cmd.setBinding(6, pageTblOmni);
    cmd.setBinding(7, vsmDbg);
    cmd.setPushData(&push, sizeof(push));
    cmd.setPipeline(shaders.vsmMarkOmniPages);
    cmd.dispatchThreads(zbuffer.size());

    cmd.setBinding(0, pageTblOmni);
    cmd.setPushData(&lightsTotal, sizeof(lightsTotal));
    cmd.setPipeline(shaders.vsmPostprocessOmni);
    cmd.dispatchThreads(wview.lights().size());
    }

  // sky&fog
  if(doVirtualFog) {
    cmd.setDebugMarker("VSM-pages-epipolar");
    cmd.setBinding(0, epipolar.epTrace);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, epipolar.epipoles);
    cmd.setBinding(3, pageTbl);
    cmd.setBinding(4, pageHiZ);
    cmd.setPipeline(shaders.vsmFogPages);
    cmd.dispatchThreads(epipolar.epTrace.size());
    }

  cmd.setDebugMarker("VSM-pages-alloc");
  if(true) {
    // clump
    cmd.setBinding(0, pageList);
    cmd.setBinding(1, pageTbl);
    cmd.setPipeline(shaders.vsmClumpPages);
    cmd.dispatchThreads(size_t(pageTbl.w()), size_t(pageTbl.h()), size_t(pageTbl.d()));
    }

  cmd.setBinding(0, pageList);
  cmd.setBinding(1, pageTbl);
  cmd.setBinding(2, pageTblOmni);
  cmd.setBinding(3, pageListTmp);
  cmd.setBinding(4, scene.vsmDbg);
  // list
  cmd.setPipeline(shaders.vsmListPages);
  if(omniLights)
    cmd.dispatch(size_t(pageTbl.d() + 1)); else
    cmd.dispatch(size_t(pageTbl.d()));

  cmd.setPipeline(shaders.vsmAllocPages);
  cmd.dispatch(1);

  // hor-merge
  cmd.setPipeline(shaders.vsmMergePages);
  cmd.dispatch(1);

  cmd.setDebugMarker("VSM-visibility");
  wview.visibilityVsm(cmd);

  cmd.setDebugMarker("VSM-rendering");
  cmd.setFramebuffer({}, {pageData, 0.f, Tempest::Preserve});
  wview.drawVsm(cmd);
  }

void Renderer::drawRtsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.rtsmEnabled)
    return;

  const int RTSM_BIN_SIZE   = 32;
  const int RTSM_SMALL_TILE = 32;
  const int RTSM_LARGE_TILE = 128;

  const auto& shaders      = Shaders::inst();
  const auto& scene        = wview.sceneGlobals();
  const auto& clusters     = wview.clusters();
  const auto& drawCmd      = wview.drawCommands();
  const auto& buckets      = wview.drawBuckets();
  const auto& instanceSsbo = wview.instanceSsbo();
  const auto& sceneUbo     = scene.uboGlobal[SceneGlobals::V_Vsm];

  const auto largeTiles  = tileCount(scene.zbuffer->size(), RTSM_LARGE_TILE);
  const auto smallTiles  = tileCount(scene.zbuffer->size(), RTSM_SMALL_TILE);
  const auto binTiles    = tileCount(scene.zbuffer->size(), RTSM_BIN_SIZE);
  const auto maxMeshlets = drawCmd.maxMeshlets();

  // alloc resources
  auto& posList     = usesScratch(rtsm.posList,     64*1024*1024); // arbitrary
  auto& outputImage = usesImage2d(rtsm.outputImage, TextureFormat::R8, zbuffer.size());
  auto& pages       = usesImage3d(rtsm.pages,       TextureFormat::R32U, 32, 32, 16);
  auto& meshTiles   = usesImage2d(rtsm.meshTiles,   TextureFormat::RG32U, smallTiles);
  auto& primTiles   = usesImage2d(rtsm.primTiles,   TextureFormat::RG32U, binTiles);
  auto& visList     = usesSsbo   (rtsm.visList,     shaders.rtsmClear.sizeofBuffer(1, maxMeshlets));

  auto& dbg32       = usesImage2d(rtsm.dbg32, TextureFormat::R32U, tileCount(zbuffer.size(), 32));
  auto& dbg16       = usesImage2d(rtsm.dbg16, TextureFormat::R32U, tileCount(zbuffer.size(), 16));

  cmd.setDebugMarker("RTSM-rendering");
  cmd.setFramebuffer({});

  // clear
  {
    cmd.setBinding(0, pages);
    cmd.setBinding(1, visList);
    cmd.setBinding(2, posList);

    cmd.setPipeline(shaders.rtsmClear);
    cmd.dispatchThreads(size_t(pages.w()), size_t(pages.h()), size_t(pages.d()));
  }

  // global cull
  {
    struct Push { uint32_t meshletCount; } push = {};
    push.meshletCount = uint32_t(clusters.size());
    cmd.setPushData(push);

    if(sky.quality==VolumetricHQ) {
      cmd.setBinding(0, epipolar.epTrace);
      cmd.setBinding(1, sceneUbo);
      cmd.setBinding(2, epipolar.epipoles);
      cmd.setBinding(3, pages);
      cmd.setPipeline(shaders.rtsmFogPages);
      cmd.dispatchThreads(epipolar.epTrace.size());
      }

    cmd.setBinding(0, outputImage);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufDiffuse);
    cmd.setBinding(3, gbufNormal);
    cmd.setBinding(4, zbuffer);
    cmd.setBinding(5, clusters.ssbo());
    cmd.setBinding(6, visList);
    cmd.setBinding(7, pages);

    cmd.setPipeline(shaders.rtsmPages);
    cmd.dispatchThreads(scene.zbuffer->size());

    cmd.setBinding(0, pages);
    cmd.setPipeline(shaders.rtsmHiZ);
    cmd.dispatch(1);

    cmd.setBinding(7, posList);
    cmd.setPipeline(shaders.rtsmCulling);
    cmd.dispatchThreads(push.meshletCount);
  }

  // position
  {
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc = scene.originLwc;

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, visList);

    cmd.setBinding(5,  clusters.ssbo());
    cmd.setBinding(6,  instanceSsbo);
    cmd.setBinding(7,  buckets.ssbo());
    cmd.setBinding(8,  buckets.ibo());
    cmd.setBinding(9,  buckets.vbo());
    cmd.setBinding(10, buckets.morphId());
    cmd.setBinding(11, buckets.morph());

    cmd.setPipeline(shaders.rtsmPosition);
    cmd.dispatchIndirect(visList,0);
  }

  // binning
  {
    cmd.setBinding(0, outputImage);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, meshTiles);
    cmd.setBinding(6, primTiles);
    cmd.setBinding(9, dbg32);

    // meshlets
    cmd.setPipeline(shaders.rtsmMeshletCull);
    cmd.dispatch(largeTiles);

    // primitives
    cmd.setPipeline(shaders.rtsmPrimCull);
    cmd.dispatch(smallTiles);
  }

  // raster
  {
    cmd.setBinding(0, outputImage);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, meshTiles);
    cmd.setBinding(6, primTiles);
    cmd.setBinding(7, buckets.textures());
    cmd.setBinding(8, Sampler::trillinear());
    cmd.setBinding(9, dbg16);

    cmd.setPipeline(shaders.rtsmRaster);
    cmd.dispatchThreads(outputImage.size());
  }
  }

void Renderer::drawRtsmOmni(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.rtsmEnabled)
    return;

  static bool omniLights = true;
  if(!omniLights)
    return;

  //const uint32_t RTSM_SMALL_TILE = 32;
  const uint32_t RTSM_LIGHT_TILE = 64;

  const auto& shaders      = Shaders::inst();
  const auto& scene        = wview.sceneGlobals();
  const auto& sceneUbo     = scene.uboGlobal[SceneGlobals::V_Main];
  const auto& clusters     = wview.clusters();
  const auto& drawCmd      = wview.drawCommands();
  const auto& buckets      = wview.drawBuckets();
  const auto& instanceSsbo = wview.instanceSsbo();
  const auto& lightsSsbo   = wview.lights().lightsSsbo();
  const auto  maxMeshlets  = drawCmd.maxMeshlets();
  const auto  lightsTotal  = uint32_t(wview.lights().size());

  // alloc resources, for omni-lights
  auto& posList        = usesScratch(rtsm.posList,        64*1024*1024); // arbitrary
  auto& outputImageClr = usesImage2d(rtsm.outputImageClr, TextureFormat::R11G11B10UF, zbuffer.size());
  auto& lightTiles     = usesImage2d(rtsm.lightTiles,     TextureFormat::RG32U, tileCount(scene.zbuffer->size(), RTSM_LIGHT_TILE));
  auto& lightBins      = usesImage2d(rtsm.lightBins,      TextureFormat::RG32U, lightsTotal, 1u);
  auto& primTilesOmni  = usesImage2d(rtsm.primTilesOmni,  TextureFormat::R32U, tileCount(zbuffer.size(), RTSM_LIGHT_TILE));
  auto& drawTasks      = usesSsbo   (rtsm.drawTasks,      4*sizeof(uint32_t));
  auto& visList        = usesSsbo   (rtsm.visList,        shaders.rtsmClearOmni.sizeofBuffer(1, maxMeshlets));
  auto& visibleLights  = usesSsbo   (rtsm.visibleLights,  shaders.rtsmClearOmni.sizeofBuffer(2, lightsTotal));

  auto& dbg64          = usesImage2d(rtsm.dbg64, TextureFormat::R32U, tileCount(zbuffer.size(), 64));
  auto& dbg16          = usesImage2d(rtsm.dbg16, TextureFormat::R32U, tileCount(zbuffer.size(), 16));

  cmd.setDebugMarker("RTSM-rendering-omni");
  cmd.setFramebuffer({});
  // clear
  {
    struct Push { uint32_t meshletCount; } push = {};
    push.meshletCount = uint32_t(clusters.size());

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, visList);
    cmd.setBinding(2, visibleLights);
    cmd.setPipeline(shaders.rtsmClearOmni);
    cmd.dispatchThreads(1);
  }

  // cull lights
  {
    struct Push { float znear; uint32_t lightsTotal; } push = {};
    push.znear       = scene.znear;
    push.lightsTotal = lightsTotal;

    cmd.setPushData(push);
    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, lightsSsbo);
    cmd.setBinding(2, visibleLights);
    cmd.setBinding(3, visList);
    cmd.setBinding(4, hiz.hiZ);
    cmd.setBinding(5, clusters.ssbo());
    cmd.setBinding(6, posList);

    cmd.setPipeline(shaders.rtsmCullLights);
    cmd.dispatchThreads(lightsTotal);
  }

  // lights
  {
    struct Push { Vec3 originLwc; float znear; } push = {};
    push.originLwc = scene.originLwc;
    push.znear     = scene.znear;

    cmd.setPushData(push);
    cmd.setBinding(0, lightTiles);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, visibleLights);
    //
    cmd.setBinding(9, rtsm.dbg64);

    cmd.setPipeline(shaders.rtsmLightsOmni);
    cmd.dispatch(lightTiles.size());

    cmd.setPipeline(shaders.rtsmBboxesOmni);
    cmd.dispatchThreads(zbuffer.size());

    cmd.setPipeline(shaders.rtsmCompactOmni);
    cmd.dispatch(lightTiles.size());

    cmd.setPipeline(shaders.rtsmCompactLights);
    cmd.dispatchThreads(lightsTotal);
  }

  // meshlet culling
  {
    struct Push { float znear; uint32_t meshletCount; } push = {};
    push.znear        = scene.znear;
    push.meshletCount = uint32_t(clusters.size());

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, rtsm.visList);
    cmd.setBinding(3, lightsSsbo);
    cmd.setBinding(4, visibleLights);
    cmd.setBinding(5, clusters.ssbo());

    cmd.setPipeline(shaders.rtsmCullingOmni);
    cmd.dispatchThreads(push.meshletCount);
  }

  // position
  {
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc = scene.originLwc;

    cmd.setPushData(push);
    cmd.setBinding(0, posList);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, visList);
    //
    cmd.setBinding(5,  clusters.ssbo());
    cmd.setBinding(6,  instanceSsbo);
    cmd.setBinding(7,  buckets.ssbo());
    cmd.setBinding(8,  buckets.ibo());
    cmd.setBinding(9,  buckets.vbo());
    cmd.setBinding(10, buckets.morphId());
    cmd.setBinding(11, buckets.morph());

    cmd.setPipeline(shaders.rtsmPositionOmni);
    cmd.dispatchIndirect(visList, 0);
  }

  // per-light meshlets, primitives
  {
    struct Push { Vec3 originLwc; float znear; } push = {};
    push.originLwc = scene.originLwc;
    push.znear     = scene.znear;

    cmd.setBinding(0, sceneUbo);
    cmd.setBinding(1, lightsSsbo);
    cmd.setBinding(2, visibleLights);
    cmd.setBinding(3, lightBins);
    cmd.setBinding(4, clusters.ssbo());
    cmd.setBinding(5, posList);

    cmd.setPipeline(shaders.rtsmMeshletOmni);
    cmd.dispatchIndirect(visibleLights, 0);

    cmd.setPipeline(shaders.rtsmBackfaceOmni);
    cmd.dispatchIndirect(visibleLights, 0);
  }

  // in tile primitives
  {
    struct Push { Vec3 originLwc; float znear; } push = {};
    push.originLwc = scene.originLwc;
    push.znear     = scene.znear;
    cmd.setPushData(push);
    cmd.setBinding(0, lightTiles);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, lightBins);
    cmd.setBinding(7, primTilesOmni);
    cmd.setBinding(8, drawTasks);
    cmd.setBinding(9, dbg64);

    cmd.setPipeline(shaders.rtsmTaskOmni);
    cmd.dispatch(1);

    cmd.setPipeline(shaders.rtsmPrimOmni);
    cmd.dispatchIndirect(drawTasks, 0);
  }

  // raster
  {
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc = scene.originLwc;

    cmd.setPushData(push);
    cmd.setBinding(0, outputImageClr);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, primTilesOmni);
    cmd.setBinding(7, buckets.textures());
    cmd.setBinding(8, Sampler::trillinear());
    cmd.setBinding(9, dbg16);

    cmd.setPipeline(shaders.rtsmRasterOmni);
    cmd.dispatchThreads(outputImageClr.size());
  }

  //TODO: swrt ?
  if(0) {
    // raster (ref)
    struct Push { Vec3 originLwc; } push = {};
    push.originLwc  = scene.originLwc;
    cmd.setPushData(push);
    cmd.setBinding(0, outputImageClr);
    cmd.setBinding(1, sceneUbo);
    cmd.setBinding(2, gbufNormal);
    cmd.setBinding(3, zbuffer);
    cmd.setBinding(4, posList);
    cmd.setBinding(5, lightsSsbo);
    cmd.setBinding(6, visibleLights);
    cmd.setBinding(7, lightBins);
    cmd.setBinding(9, dbg16);

    cmd.setPipeline(shaders.rtsmRenderingOmni);
    cmd.dispatchThreads(outputImageClr.size());
    }
  }

void Renderer::drawSwr(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.swrEnabled)
    return;

  const auto& scene        = wview.sceneGlobals();
  const auto& clusters     = wview.clusters();
  const auto& buckets      = wview.drawBuckets();
  const auto& instanceSsbo = wview.instanceSsbo();

  auto& outputImage = usesImage2d(swr.outputImage, TextureFormat::R32U, zbuffer.size());

  cmd.setFramebuffer({});
  cmd.setDebugMarker("SW-rendering");

  struct Push { uint32_t firstMeshlet; uint32_t meshletCount; float znear; } push = {};
  push.firstMeshlet = 0;
  push.meshletCount = uint32_t(clusters.size());
  push.znear        = scene.znear;

  cmd.setBinding(0, outputImage);
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, *scene.gbufNormals);
  cmd.setBinding(3, *scene.zbuffer);
  cmd.setBinding(4, clusters.ssbo());
  cmd.setBinding(5, buckets.ibo());
  cmd.setBinding(6, buckets.vbo());
  cmd.setBinding(7, buckets.textures());
  cmd.setBinding(8, Sampler::bilinear());

  auto* pso = &Shaders::inst().swRendering;
  switch(Gothic::options().swRenderingPreset) {
    case 1: {
      cmd.setPushData(&push, sizeof(push));
      cmd.setPipeline(*pso);
      //cmd.dispatch(10);
      cmd.dispatch(clusters.size());
      break;
      }
    case 2: {
      IVec2  tileSize = IVec2(128);
      int    tileX    = (outputImage.w()+tileSize.x-1)/tileSize.x;
      int    tileY    = (outputImage.h()+tileSize.y-1)/tileSize.y;
      cmd.setPushData(&push, sizeof(push));
      cmd.setPipeline(*pso);
      cmd.dispatch(size_t(tileX), size_t(tileY)); //outputImage.size());
      break;
      }
    case 3: {
      cmd.setBinding(9,  *scene.lights);
      cmd.setBinding(10, instanceSsbo);
      cmd.setPushData(&push, sizeof(push));
      cmd.setPipeline(*pso);
      cmd.dispatchThreads(outputImage.size());
      break;
      }
    }
  }

void Renderer::drawGBuffer(Encoder<CommandBuffer>& cmd, uint8_t fId, WorldView& view) {
  if(opengothic_debug_skip_gbuf())
    return;
  cmd.setDebugMarker("GBuffer");
  if(opengothic_debug_use_hiz_prepass() && !opengothic_debug_skip_hiz()) {
    // depth prepass already wrote zbuffer this frame, keep its contents
    cmd.setFramebuffer({{gbufDiffuse, Tempest::Vec4(), Tempest::Preserve},
                        {gbufNormal,  Tempest::Vec4(), Tempest::Preserve}},
                       {zbuffer, Tempest::Preserve, Tempest::Preserve});
    }
  else {
    // no depth prepass: zbuffer still holds previous frame depth, clear it here
    cmd.setFramebuffer({{gbufDiffuse, Tempest::Vec4(), Tempest::Preserve},
                        {gbufNormal,  Tempest::Vec4(), Tempest::Preserve}},
                       {zbuffer, 1.f, Tempest::Preserve});
    }
  view.drawGBuffer(cmd,fId);
  }

void Renderer::drawGWater(Encoder<CommandBuffer>& cmd, WorldView& view) {
  static bool water = true;
  if(!water)
    return;

  cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve},
                      {gbufDiffuse, Vec4(0,0,0,0),     Tempest::Preserve},
                      {gbufNormal,  Vec4(0,0,0,0),     Tempest::Preserve}},
                     {zbuffer, Tempest::Preserve, Tempest::Preserve});
  // cmd.setFramebuffer({{sceneLinear, Tempest::Preserve, Tempest::Preserve}},
  //                    {zbuffer, Tempest::Preserve, Tempest::Preserve});
  cmd.setDebugMarker("GWater");
  view.drawWater(cmd);
  }

void Renderer::drawReflections(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  auto& pso = settings.zEnvMappingEnabled ? shaders.waterReflectionSSR : shaders.waterReflection;

  cmd.setDebugMarker("Reflections");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sceneOpaque, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(2, gbufDiffuse, Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(3, gbufNormal,  Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(4, zbuffer,     Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(5, sceneDepth,  Sampler::nearest (ClampMode::ClampToEdge));
  cmd.setBinding(6, sky.viewCldLut);
  cmd.setPipeline(pso);
  if(Gothic::options().doMeshShading) {
    cmd.dispatchMeshThreads(gbufDiffuse.size());
    } else {
    cmd.draw(nullptr, 0, 3);
    }
  }

void Renderer::drawUnderwater(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, zbuffer);
  cmd.setBinding(2, *wview.sceneGlobals().swampMask);

  cmd.setPipeline(shaders.underwaterT);
  cmd.draw(nullptr, 0, 3);
  cmd.setPipeline(shaders.underwaterS);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawShadowMap(Encoder<CommandBuffer>& cmd, uint8_t fId, WorldView& view) {
  if(opengothic_debug_skip_shadows())
    return;
#if defined(__ANDROID__)
  const uint8_t maxLayers = 1; // 1 cascade on mobile saves 50% shadow cost
#else
  const uint8_t maxLayers = Resources::ShadowLayers;
#endif
  for(uint8_t i=0; i<maxLayers; ++i) {
    if(shadowMap[i].isEmpty())
      continue;
    cmd.setDebugMarker(string_frm("ShadowMap #",i));
    cmd.setFramebuffer({}, {shadowMap[i], 0.f, Tempest::Preserve});
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    if(!settings.shadowsEnabled)
      continue; // cleared to the far plane: nothing casts a shadow
#endif
    if(view.mainLight().dir().y > Camera::minShadowY)
      view.drawShadow(cmd,fId,i);
    }
  }

const Tempest::RenderPipeline* Renderer::directAmbientPso() const {
#if defined(__ANDROID__)
  // the sun and the ambient light in one pass: the g-buffer is read once, not twice (the sums differ in the rounding only)
  if(opengothic_debug_skip_lights() || opengothic_debug_simple_render())
    return nullptr;
  if(settings.giEnabled && settings.zCloudShadowScale)
    return nullptr;
  const bool ssaoOn = settings.zCloudShadowScale;
  if(shadow.directLightPso==&shaders.directLight)
    return ssaoOn ? &shaders.directAmbientSsao   : &shaders.directAmbient;
  if(shadow.directLightPso==&shaders.directLightSh)
    return ssaoOn ? &shaders.directAmbientShSsao : &shaders.directAmbientSh;
#endif
  return nullptr;
  }

void Renderer::drawShadowResolve(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  static bool light = true;
  if(!light || opengothic_debug_skip_lights())
    return;

  auto& scene = wview.sceneGlobals();
  cmd.setDebugMarker(settings.vsmEnabled ? "DirectSunLight-VSM" : "DirectSunLight");

  auto originLwc = scene.originLwc;
  cmd.setPushData(&originLwc, sizeof(originLwc));
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, zbuffer,     Sampler::nearest());
  if(shadow.directLightPso==&shaders.vsmDirectLight) {
    cmd.setBinding(4, vsm.pageTbl);
    cmd.setBinding(5, vsm.pageList);
    cmd.setBinding(6, vsm.pageData);
    cmd.setBinding(8, scene.vsmDbg);
    }
  else if(shadow.directLightPso==&shaders.directLightRq) {
    for(size_t r=0; r<Resources::ShadowLayers; ++r) {
      if(shadowMap[r].isEmpty())
        continue;
      cmd.setBinding(4+r, shadowMap[r], Resources::shadowSampler());
      }
    cmd.setBinding(6, scene.rtScene.tlas);
    cmd.setBinding(7, Sampler::bilinear());
    cmd.setBinding(8, scene.rtScene.tex);
    cmd.setBinding(9, scene.rtScene.vbo);
    cmd.setBinding(10,scene.rtScene.ibo);
    cmd.setBinding(11,scene.rtScene.rtDesc);
    }
  else if(shadow.directLightPso==&shaders.rtsmDirectLight) {
    cmd.setBinding(4, rtsm.outputImage);
    cmd.setBinding(5, rtsm.outputImageClr.isEmpty() ? Resources::fallbackBlack() : textureCast<Texture2d&>(rtsm.outputImageClr));
    }
  else {
    for(size_t r=0; r<Resources::ShadowLayers; ++r) {
      if(shadowMap[r].isEmpty())
        continue;
      cmd.setBinding(4+r, shadowMap[r], Resources::shadowSampler());
      }
    }

  const auto* merged = directAmbientPso();
  if(merged!=nullptr) {
    cmd.setBinding(6, sky.irradianceLut);
    if(settings.zCloudShadowScale)
      cmd.setBinding(7, ssao.ssaoBlur, Sampler::nearest(ClampMode::ClampToEdge));
    cmd.setPipeline(*merged);
    cmd.draw(nullptr, 0, 3);
    return;
    }

  cmd.setPipeline(*shadow.directLightPso);
  if(settings.vsmEnabled) {
    cmd.setPushData(settings.vsmMipBias);
    cmd.setPipeline(*shadow.directLightPso);
    }
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::drawLights(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  if(opengothic_debug_skip_lights())
    return;
  static bool light = true;
  if(!light)
    return;

  if(settings.rtsmEnabled && !rtsm.outputImageClr.isEmpty())
    return;

  auto& scene   = wview.sceneGlobals();
  cmd.setDebugMarker("Point lights");

  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, zbuffer,     Sampler::nearest());
  cmd.setBinding(4, wview.lights().lightsSsbo());
  if(lights.directLightPso==&shaders.lightsVsm) {
    cmd.setBinding(5, vsm.pageTblOmni);
    cmd.setBinding(6, vsm.pageData);
    }
  if(lights.directLightPso==&shaders.lightsRq) {
    cmd.setBinding(6, scene.rtScene.tlas);
    cmd.setBinding(7, Sampler::bilinear());
    cmd.setBinding(8, scene.rtScene.tex);
    cmd.setBinding(9, scene.rtScene.vbo);
    cmd.setBinding(10,scene.rtScene.ibo);
    cmd.setBinding(11,scene.rtScene.rtDesc);
    }

  auto  originLwc = scene.originLwc;
  auto& ibo       = Resources::cubeIbo();
  cmd.setPushData(&originLwc, sizeof(originLwc));
  cmd.setPipeline(*lights.directLightPso);
#if defined(__ANDROID__)
  const uint32_t maxLights = 16;
  const uint32_t lightCount = std::min<uint32_t>(uint32_t(wview.lights().size()), maxLights);
#else
  const uint32_t lightCount = uint32_t(wview.lights().size());
#endif
  cmd.draw(nullptr,ibo, 0,ibo.size(), 0, lightCount);
  }

void Renderer::drawSky(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  auto& scene = wview.sceneGlobals();

  cmd.setDebugMarker("Sky");
  if(sky.quality==PathTrace) {
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, sky.transLut,     Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(2, sky.multiScatLut, Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(3, sky.cloudsLut,    Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(4, zbuffer, Sampler::nearest());
    cmd.setBinding(5, shadowMap[1], Resources::shadowSampler());
    cmd.setPipeline(shaders.skyPathTrace);
    cmd.draw(nullptr, 0, 3);
    return;
    }

  auto& skyShader = sky.quality==VolumetricLQ ? shaders.sky : shaders.skySep;
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sky.transLut,     Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(2, sky.multiScatLut, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(3, sky.viewLut,      Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(4, sky.fogLut3D);
  if(sky.quality!=VolumetricLQ)
    cmd.setBinding(5, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(6, *wview.sky().cloudsDay()  .lay[0], Sampler::trillinear());
  cmd.setBinding(7, *wview.sky().cloudsDay()  .lay[1], Sampler::trillinear());
  cmd.setBinding(8, *wview.sky().cloudsNight().lay[0], Sampler::trillinear());
  cmd.setBinding(9, *wview.sky().cloudsNight().lay[1], Sampler::trillinear());
  cmd.setPipeline(skyShader);
  cmd.draw(nullptr, 0, 3);
  }

void Renderer::prepareSSAO(Encoder<CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.zCloudShadowScale)
    return;
  // ssao
  struct PushSsao {
    Matrix4x4 proj;
    Matrix4x4 projInv;
    } push;
  push.proj    = proj;
  push.projInv = proj;
  push.projInv.inverse();

  cmd.setFramebuffer({});
  cmd.setDebugMarker("SSAO");

  cmd.setBinding(0, ssao.ssaoBuf);
  cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, gbufDiffuse, Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setBinding(3, gbufNormal,  Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setBinding(4, zbuffer,     Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setPushData(&push, sizeof(push));
  cmd.setPipeline(shaders.ssao);
  cmd.dispatchThreads(ssao.ssaoBuf.size());

  cmd.setBinding(0, ssao.ssaoBlur);
  cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, ssao.ssaoBuf);
  cmd.setBinding(3, zbuffer, Sampler::nearest(ClampMode::ClampToEdge));
  cmd.setPipeline(shaders.ssaoBlur);
  cmd.dispatchThreads(ssao.ssaoBuf.size());
  }

void Renderer::prepareFog(Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  if(opengothic_debug_skip_fog())
    return;
  auto& scene  = wview.sceneGlobals();
  auto& device = Resources::device();

  cmd.setDebugMarker("Fog-LUTs");
  if(sky.quality!=PathTrace) {
    auto& shader = sky.quality==VolumetricLQ ? shaders.fogViewLut3d : shaders.fogViewLutSep;
    cmd.setFramebuffer({});
    cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, sky.transLut,     Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(2, sky.multiScatLut, Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(3, sky.cloudsLut,    Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setBinding(4, sky.fogLut3D);
    if(sky.quality==VolumetricHQ || sky.quality==Epipolar)
      cmd.setBinding(5, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
    cmd.setPipeline(shader);
    cmd.dispatchThreads(uint32_t(sky.fogLut3D.w()), uint32_t(sky.fogLut3D.h()));
    }

  if(settings.vsmEnabled && (sky.quality==VolumetricHQ || sky.quality==Epipolar)) {
    cmd.setFramebuffer({});
    cmd.setDebugMarker("VSM-epipolar-fog");
    cmd.setBinding(0, epipolar.epTrace);
    cmd.setBinding(1, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(2, epipolar.epipoles);
    cmd.setBinding(3, vsm.pageTbl);
    cmd.setBinding(4, vsm.pageData);
    cmd.setPipeline(shaders.vsmFogShadow);
    cmd.dispatchThreads(epipolar.epTrace.size());
    }

  switch(sky.quality) {
    case None:
    case VolumetricLQ:
      break;
    case VolumetricHQ: {
      if(settings.vsmEnabled && !settings.rtsmEnabled) {
        cmd.setFramebuffer({});
        cmd.setBinding(0, sky.occlusionLut);
        cmd.setBinding(1, epipolar.epTrace);
        cmd.setBinding(2, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
        cmd.setBinding(3, epipolar.epipoles);
        cmd.setBinding(4, zbuffer);
        cmd.setPipeline(shaders.fogEpipolarOcclusion);
        cmd.dispatchThreads(zbuffer.size());
        } else {
        cmd.setFramebuffer({});
        cmd.setBinding(2, zbuffer, Sampler::nearest());
        cmd.setBinding(3, scene.uboGlobal[SceneGlobals::V_Main]);
        cmd.setBinding(4, sky.occlusionLut);
        cmd.setBinding(5, shadowMap[1], Resources::shadowSampler());
        cmd.setPipeline(shaders.fogOcclusion);
        cmd.dispatchThreads(sky.occlusionLut.size());
        }
      break;
      }
    case Epipolar:{
      // experimental
      if(vsm.fogDbg.isEmpty())
        vsm.fogDbg   = device.image2d(sky.lutRGBFormat,    1024, 2*1024);
      cmd.setFramebuffer({});
      cmd.setDebugMarker("VSM-trace");
      cmd.setBinding(0, vsm.fogDbg);
      cmd.setBinding(1, epipolar.epTrace);
      cmd.setBinding(2, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
      cmd.setBinding(3, epipolar.epipoles);
      cmd.setBinding(4, zbuffer);
      cmd.setBinding(5, sky.transLut,   Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(6, sky.cloudsLut,  Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setBinding(7, sky.fogLut3DMs, Sampler::bilinear(ClampMode::ClampToEdge));
      cmd.setPipeline(shaders.vsmFogTrace);
      cmd.dispatchThreads(epipolar.epTrace.size());
      break;
      }
    case PathTrace: {
      break;
      }
    }
  }

void Renderer::prepareEpipolar(Tempest::Encoder<Tempest::CommandBuffer>& cmd, WorldView& wview) {
  const bool doVolumetricFog = sky.quality!=VolumetricLQ && sky.quality!=PathTrace;
  if(!doVolumetricFog || (!settings.vsmEnabled && !settings.rtsmEnabled))
    return;

  auto& scene  = wview.sceneGlobals();

  auto& epTrace  = usesImage2d(epipolar.epTrace,  TextureFormat::R16,  1024, 2*1024);
  auto& epipoles = usesSsbo   (epipolar.epipoles, shaders.fogEpipolarVsm.sizeofBuffer(3, size_t(epipolar.epTrace.h())));

  cmd.setDebugMarker("Fog-epipolar");
  cmd.setFramebuffer({});
  cmd.setBinding(0, sky.occlusionLut);
  cmd.setBinding(1, epTrace);
  cmd.setBinding(2, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(3, epipoles);
  cmd.setBinding(4, zbuffer);
  if(settings.vsmEnabled)
    cmd.setPipeline(shaders.fogEpipolarVsm);
  else if(settings.rtsmEnabled)
    cmd.setPipeline(shaders.fogEpipolarVsm);
  cmd.dispatch(uint32_t(epTrace.h()));
  }

void Renderer::prepareIrradiance(Encoder<CommandBuffer>& cmd, WorldView& wview) {
  auto& scene = wview.sceneGlobals();

  cmd.setDebugMarker("Irradiance");
  cmd.setFramebuffer({});
  cmd.setBinding(0, sky.irradianceLut);
  cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(2, sky.viewCldLut);
  cmd.setPipeline(shaders.irradiance);
  cmd.dispatch(1);
  }

void Renderer::prepareGi(Encoder<CommandBuffer>& cmd, WorldView& wview) {
  if(!settings.giEnabled || !settings.zCloudShadowScale) {
    return;
    }

  const bool fisrtFrame = gi.voteTable.isEmpty();

  auto& hashTable          = usesSsbo(gi.hashTable, 2'097'152*sizeof(uint32_t)); // 8MB
  auto& voteTable          = usesSsbo(gi.voteTable, gi.hashTable.byteSize());
  auto& probes             = usesSsbo(gi.probes,    gi.maxProbes*32 + 64);       // probes and header
  auto& freeList           = usesSsbo(gi.freeList,  gi.maxProbes*sizeof(uint32_t) + sizeof(int32_t));
  auto& probesGBuffDiff    = usesImage2d(gi.probesGBuffDiff,    TextureFormat::RGBA8, gi.atlasDim*16, gi.atlasDim*16); // 16x16 tile
  auto& probesGBuffNorm    = usesImage2d(gi.probesGBuffNorm,    TextureFormat::RGBA8, gi.atlasDim*16, gi.atlasDim*16);
  auto& probesGBuffRayT    = usesImage2d(gi.probesGBuffRayT,    TextureFormat::R16,   gi.atlasDim*16, gi.atlasDim*16);
  auto& probesLighting     = usesImage2d(gi.probesLighting,     TextureFormat::R11G11B10UF, gi.atlasDim*3, gi.atlasDim*2);
  auto& probesLightingPrev = usesImage2d(gi.probesLightingPrev, TextureFormat::R11G11B10UF, uint32_t(gi.probesLighting.w()), uint32_t(gi.probesLighting.h()));

  const auto&  scene   = wview.sceneGlobals();
  const size_t maxHash = hashTable.byteSize()/sizeof(uint32_t);

  cmd.setFramebuffer({});
  if(fisrtFrame) {
    cmd.setDebugMarker("GI-Init");
    cmd.setBinding(0, voteTable);
    cmd.setBinding(1, hashTable);
    cmd.setBinding(2, probes);
    cmd.setBinding(3, freeList);
    cmd.setPipeline(shaders.probeInit);
    cmd.dispatch(1);
    cmd.setPipeline(shaders.probeClearHash);
    cmd.dispatchThreads(maxHash);

    cmd.setBinding(0, probesLighting);
    cmd.setBinding(1, Resources::fallbackBlack());
    cmd.setPipeline(shaders.copyImg);
    cmd.dispatchThreads(probesLighting.size());
    }

  static bool alloc = true;
  cmd.setDebugMarker("GI-Alloc");
  cmd.setBinding(0, voteTable);
  cmd.setBinding(1, hashTable);
  cmd.setBinding(2, probes);
  cmd.setBinding(3, freeList);
  cmd.setPipeline(shaders.probeClear);
  cmd.dispatchThreads(maxHash);

  if(alloc) {
    cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, voteTable);
    cmd.setBinding(5, hashTable);
    cmd.setBinding(6, probes);
    cmd.setBinding(7, freeList);
    cmd.setPipeline(shaders.probeVote);
    cmd.dispatchThreads(sceneDepth.size());

    cmd.setBinding(0, voteTable);
    cmd.setBinding(1, hashTable);
    cmd.setBinding(2, probes);
    cmd.setBinding(3, freeList);
    cmd.setPipeline(shaders.probePrune);
    cmd.dispatchThreads(gi.maxProbes);

    cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
    cmd.setBinding(2, gbufNormal,  Sampler::nearest());
    cmd.setBinding(3, zbuffer,     Sampler::nearest());
    cmd.setBinding(4, voteTable);
    cmd.setBinding(5, hashTable);
    cmd.setBinding(6, probes);
    cmd.setBinding(7, freeList);
    cmd.setPipeline(shaders.probeAlocation);
    cmd.dispatchThreads(sceneDepth.size());
    }

  cmd.setDebugMarker("GI-Trace");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, probesGBuffDiff);
  cmd.setBinding(2, probesGBuffNorm);
  cmd.setBinding(3, probesGBuffRayT);
  cmd.setBinding(4, hashTable);
  cmd.setBinding(5, probes);
  cmd.setBinding(6, scene.rtScene.tlas);
  cmd.setBinding(7, Sampler::bilinear());
  cmd.setBinding(8, scene.rtScene.tex);
  cmd.setBinding(9, scene.rtScene.vbo);
  cmd.setBinding(10,scene.rtScene.ibo);
  cmd.setBinding(11,scene.rtScene.rtDesc);
  cmd.setPipeline(shaders.probeTrace);
  cmd.dispatch(1024); // TODO: dispath indirect?

  cmd.setDebugMarker("GI-HashMap");
  cmd.setBinding(0, voteTable);
  cmd.setBinding(1, hashTable);
  cmd.setBinding(2, probes);
  cmd.setBinding(3, freeList);
  cmd.setPipeline(shaders.probeClearHash);
  cmd.dispatchThreads(maxHash);
  cmd.setPipeline(shaders.probeMakeHash);
  cmd.dispatchThreads(gi.maxProbes);

  cmd.setDebugMarker("GI-Lighting");
  cmd.setBinding(0, probesLightingPrev);
  cmd.setBinding(1, probesLighting);
  cmd.setPipeline(shaders.copyImg);
  cmd.dispatchThreads(probesLighting.size());

  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, probesLighting);
  cmd.setBinding(2, probesGBuffDiff, Sampler::nearest());
  cmd.setBinding(3, probesGBuffNorm, Sampler::nearest());
  cmd.setBinding(4, probesGBuffRayT, Sampler::nearest());
  cmd.setBinding(5, sky.viewCldLut,  Sampler::bilinear());
  cmd.setBinding(6, shadowMap[1],    Sampler::bilinear());
  cmd.setBinding(7, probesLightingPrev, Sampler::nearest());
  cmd.setBinding(8, hashTable);
  cmd.setBinding(9, probes);
  cmd.setPipeline(shaders.probeLighting);
  cmd.dispatch(1024);
  }

void Renderer::prepareExposure(Encoder<CommandBuffer>& cmd, WorldView& wview) {
  auto& scene = wview.sceneGlobals();

  auto sunDir = wview.sky().sunLight().dir();
  struct Push {
    float baseL        = 0.0;
    float sunOcclusion = 1.0;
    };
  Push push;
  push.sunOcclusion = smoothstep(0.0f, 0.01f, sunDir.y);

  static float override = 0;
  static float add      = 1.5f;
  if(override>0)
    push.baseL = override;
  push.baseL += add;

  cmd.setDebugMarker("Exposure");
  cmd.setFramebuffer({});
  cmd.setBinding(0, scene.uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, sky.viewCldLut);
  cmd.setBinding(2, sky.transLut,  Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(3, sky.cloudsLut, Sampler::bilinear(ClampMode::ClampToEdge));
  cmd.setBinding(4, sky.irradianceLut);
  cmd.setPushData(&push, sizeof(push));
  cmd.setPipeline(shaders.skyExposure);
  cmd.dispatch(1);
  }

void Renderer::drawProbesDbg(Encoder<CommandBuffer>& cmd, const WorldView& wview) {
  if(!settings.giEnabled)
    return;

  static bool enable = false;
  if(!enable)
    return;

  cmd.setDebugMarker("GI-dbg");
  cmd.setBinding(0, wview.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gi.probesLighting);
  cmd.setBinding(2, gi.probes);
  cmd.setBinding(3, gi.hashTable);
  cmd.setPipeline(shaders.probeDbg);
  cmd.draw(nullptr, 0, 36, 0, gi.maxProbes);
  }

void Renderer::drawProbesHitDbg(Encoder<CommandBuffer>& cmd) {
  if(!settings.giEnabled)
    return;

  static bool enable = false;
  if(!enable)
    return;

  cmd.setDebugMarker("GI-dbg");
  cmd.setBinding(1, gi.probesLighting);
  cmd.setBinding(2, gi.probesGBuffDiff);
  cmd.setBinding(3, gi.probesGBuffRayT);
  cmd.setBinding(4, gi.probes);
  cmd.setPipeline(shaders.probeHitDbg);
  cmd.draw(nullptr, 0, 36, 0, gi.maxProbes*256);
  //cmd.draw(nullptr, 0, 36, 0, 1024);
  }

void Renderer::drawAmbient(Encoder<CommandBuffer>& cmd, const WorldView& view) {
  static bool enable = true;
  if(!enable)
    return;
  if(directAmbientPso()!=nullptr)
    return; // done together with the sun, see drawShadowResolve

  if(settings.giEnabled && settings.zCloudShadowScale) {
    cmd.setDebugMarker("AmbientLight");
    cmd.setBinding(0, view.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
    cmd.setBinding(1, gi.probesLighting);
    cmd.setBinding(2, gbufDiffuse,   Sampler::nearest());
    cmd.setBinding(3, gbufNormal,    Sampler::nearest());
    cmd.setBinding(4, zbuffer,       Sampler::nearest());
    cmd.setBinding(5, ssao.ssaoBlur, Sampler::nearest());
    cmd.setBinding(6, gi.hashTable);
    cmd.setBinding(7, gi.probes);
    cmd.setPipeline(shaders.probeAmbient);
    cmd.draw(nullptr, 0, 3);
    return;
    }

  cmd.setDebugMarker("AmbientLight");
  cmd.setBinding(0, view.sceneGlobals().uboGlobal[SceneGlobals::V_Main]);
  cmd.setBinding(1, gbufDiffuse, Sampler::nearest());
  cmd.setBinding(2, gbufNormal,  Sampler::nearest());
  cmd.setBinding(3, sky.irradianceLut);
  if(opengothic_debug_simple_render()) {
    cmd.setPipeline(shaders.ambientLightSimple);
    }
  else if(settings.zCloudShadowScale) {
    cmd.setBinding(4, ssao.ssaoBlur, Sampler::nearest(ClampMode::ClampToEdge));
    cmd.setPipeline(shaders.ambientLightSsao);
    } else {
    cmd.setPipeline(shaders.ambientLight);
    }
  cmd.draw(nullptr, 0, 3);
  }

Tempest::Attachment Renderer::screenshoot(uint8_t frameId) {
  auto& device = Resources::device();
  device.waitIdle();

  uint32_t w    = uint32_t(zbuffer.w());
  uint32_t h    = uint32_t(zbuffer.h());
  auto     img  = device.attachment(Tempest::TextureFormat::RGBA8,w,h);

  CommandBuffer cmd;
  {
  auto enc = cmd.startEncoding(device);
  draw(img,enc,frameId);
  }

  auto sync = device.submit(cmd);
  sync.wait();
  return img;

  // debug
  auto d16     = device.attachment(TextureFormat::R16,    swapchain.w(),swapchain.h());
  auto normals = device.attachment(TextureFormat::RGBA16, swapchain.w(),swapchain.h());

  {
  auto enc = cmd.startEncoding(device);
  enc.setFramebuffer({{normals,Tempest::Discard,Tempest::Preserve}});
  enc.setBinding(0, gbufNormal, Sampler::nearest());
  enc.setPipeline(shaders.copy);
  enc.draw(nullptr, 0, 3);

  enc.setFramebuffer({{d16,Tempest::Discard,Tempest::Preserve}});
  enc.setBinding(0,zbuffer,Sampler::nearest());
  enc.setPipeline(shaders.copy);
  enc.draw(nullptr, 0, 3);
  }
  sync = device.submit(cmd);
  sync.wait();

  auto pm  = device.readPixels(textureCast<const Texture2d&>(normals));
  pm.save("gbufNormal.png");

  pm  = device.readPixels(textureCast<const Texture2d&>(d16));
  pm.save("zbuffer.hdr");

  return img;
  }

float Renderer::lowResScale() const {
  const int i = std::clamp(int(settings.vidResIndex), 0, resScaleCount()-1);
  return resScaleTable[i];
  }

float Renderer::internalResolutionScale() const {
  return lowResScale();
  }

Size Renderer::internalResolution() const {
  if(settings.vidResIndex==0)
     return Size(int(swapchain.w()), int(swapchain.h()));
  if(settings.vidResIndex==1)
    return Size(int(3*swapchain.w()/4), int(3*swapchain.h()/4));
  if(settings.vidResIndex==2)
    return Size(int(swapchain.w()/2), int(swapchain.h()/2));
  if(settings.vidResIndex==3)
    return Size(int(swapchain.w()*2/5), int(swapchain.h()*2/5));
  const float k = lowResScale();
  return Size(std::max(1, int(float(swapchain.w())*k)), std::max(1, int(float(swapchain.h())*k)));
  }

