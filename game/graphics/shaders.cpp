#include "shaders.h"

#include <Tempest/Application>
#include <Tempest/Device>
#include <Tempest/Log>

#include "gothic.h"
#include "resources.h"

#include "shader.h"
#include "utils/string_frm.h"

using namespace Tempest;

extern "C" {
int  opengothic_debug_no_atest_depth();
int  opengothic_debug_prewarm();
int  opengothic_prewarm_check_crashed(const char* key);
void opengothic_prewarm_begin_attempt(const char* key);
void opengothic_prewarm_end_attempt(const char* key, int ok);
}

Shaders* Shaders::instance = nullptr;

Shaders::Shaders() {
  instance = this;
  compileKeyShaders();
#if defined(__EMSCRIPTEN__)
  // single-threaded wasm build: std::async cannot spawn a thread without -pthread,
  // so compile synchronously and satisfy waitCompiler() through a promise
  std::promise<void> done;
  deferredCompilation = done.get_future();
  compileShaders();
  done.set_value();
#else
  deferredCompilation = std::async(std::launch::async, [this]() {
    Workers::setThreadName("Shader compilation");
    auto time = Application::tickCount();
    compileShaders();
    time = Application::tickCount() - time;
    if(time>1000)
      Log::i("Shader compilation took: ",time/1000," seconds");
    });
#endif
  }

Shaders::~Shaders() {
  deferredCompilation.wait();
  instance = nullptr;
  }

void Shaders::waitCompiler() {
  deferredCompilation.wait();
  }

Shaders& Shaders::inst() {
  instance->waitCompiler();
  return *instance;
  }

void Shaders::compileKeyShaders() {
  bink = postEffect("bink");
  }

void Shaders::compileShaders() {
  auto& device = Resources::device();

  const bool meshlets = Gothic::options().doMeshShading;

  copyBuf = computeShader("copy.comp.sprv");
  copyImg = computeShader("copy_img.comp.sprv");
  copy    = postEffect("copy");

  patch   = computeShader("patch.comp.sprv");

  stash   = postEffect("stash");

  clusterInit         = computeShader("cluster_init.comp.sprv");
  clusterPatch        = computeShader("cluster_patch.comp.sprv");
  visibilityPassSh    = computeShader("visibility_pass.comp.sprv");
  visibilityPassHiZ   = computeShader("visibility_pass_hiz.comp.sprv");
  visibilityPassHiZCr = computeShader("visibility_pass_hiz_cr.comp.sprv");
  visibilityPassNoHiZ = computeShader("visibility_pass_nohiz.comp.sprv");

  ssao                = computeShader("ssao.comp.sprv");
  ssaoBlur            = computeShader("ssao_blur.comp.sprv");

  directLight      = postEffect("direct_light",    RenderState::ZTestMode::NoEqual);
  directLightSh    = postEffect("direct_light_sh", RenderState::ZTestMode::NoEqual);
  if(Gothic::options().doRayQuery && device.properties().descriptors.nonUniformIndexing)
    directLightRq  = postEffect("direct_light_rq", RenderState::ZTestMode::NoEqual);

  ambientLight       = ambientLightShader("ambient_light", true);
  ambientLightSsao   = ambientLightShader("ambient_light_ssao", true);
  ambientLightSimple = ambientLightShader("ambient_light_simple", false);

  irradiance         = computeShader("irradiance.comp.sprv");
  cloudsLut          = computeShader("clouds_lut.comp.sprv");
  skyTransmittance   = postEffect("sky_transmittance");
  skyMultiScattering = postEffect("sky_multi_scattering");
  skyViewLut         = postEffect("sky_view_lut");
  skyViewCldLut      = postEffect("sky_view_clouds_lut");

  fogViewLut3d       = computeShader("fog_view_lut.comp.sprv");
  fogViewLutSep      = computeShader("fog_view_lut_sep.comp.sprv");
  fogOcclusion       = computeShader("fog3d.comp.sprv");

  skyExposure        = computeShader("sky_exposure.comp.sprv");
  sky                = postEffect("sky");
  skySep             = postEffect("sky_sep");
  fog                = fogShader ("fog");
  barrierZone        = fogShader ("barrier_zone");
  barrierBolts       = fogShader ("barrier_bolts");
  fog3dHQ            = fogShader ("fog3d_hq");

  {
    RenderState state;
    state.setCullFaceMode (RenderState::CullMode::Front);
    state.setBlendSource  (RenderState::BlendMode::One);
    state.setBlendDest    (RenderState::BlendMode::SrcAlpha);
    state.setZTestMode    (RenderState::ZTestMode::Always);
    state.setZWriteEnabled(false);

    auto sh      = GothicShader::get("triangle.vert.sprv");
    auto vsLight = device.shader(sh.data,sh.len);
    sh           = GothicShader::get("sky_pathtrace.frag.sprv");
    auto fsLight = device.shader(sh.data,sh.len);
    skyPathTrace = device.pipeline(Triangles, state, vsLight, fsLight);
  }

  underwaterT        = inWaterShader("underwater_t", false);
  underwaterS        = inWaterShader("underwater_s", true);
  waterReflection    = reflectionShader("water_reflection.frag.sprv",meshlets);
  waterReflectionSSR = reflectionShader("water_reflection_ssr.frag.sprv",meshlets);

  {
  RenderState state;
  state.setCullFaceMode (RenderState::CullMode::Front);
  state.setBlendSource  (RenderState::BlendMode::One);
  state.setBlendDest    (RenderState::BlendMode::SrcAlpha);
  state.setZTestMode    (RenderState::ZTestMode::Equal);
  state.setZWriteEnabled(false);

  auto sh      = GothicShader::get("sun.vert.sprv");
  auto vsLight = device.shader(sh.data,sh.len);
  sh           = GothicShader::get("sun.frag.sprv");
  auto fsLight = device.shader(sh.data,sh.len);
  sun          = device.pipeline(Triangles, state, vsLight, fsLight);

  sh           = GothicShader::get("triangle.vert.sprv");
  vsLight      = device.shader(sh.data,sh.len);
  sh           = GothicShader::get("barrier.frag.sprv");
  fsLight      = device.shader(sh.data,sh.len);
  state.setCullFaceMode(RenderState::CullMode::NoCull);
  barrier      = device.pipeline(Triangles, state, vsLight, fsLight);
  }

  {
  RenderState state;
  state.setCullFaceMode (RenderState::CullMode::Front);
  state.setBlendSource  (RenderState::BlendMode::One);
  state.setBlendDest    (RenderState::BlendMode::One);
  state.setZTestMode    (RenderState::ZTestMode::Less);

  state.setZWriteEnabled(false);

  auto sh      = GothicShader::get("light.vert.sprv");
  auto vsLight = device.shader(sh.data,sh.len);
  sh           = GothicShader::get("light.frag.sprv");
  auto fsLight = device.shader(sh.data,sh.len);
  lights       = device.pipeline(Triangles, state, vsLight, fsLight);
  if(Gothic::options().doRayQuery) {
    if(Resources::device().properties().descriptors.nonUniformIndexing) {
      sh      = GothicShader::get("light_rq_at.frag.sprv");
      fsLight = device.shader(sh.data,sh.len);
      } else {
      sh      = GothicShader::get("light_rq.frag.sprv");
      fsLight = device.shader(sh.data,sh.len);
      }
    lightsRq = device.pipeline(Triangles, state, vsLight, fsLight);
    }
  if(Shaders::isVsmSupported()) {
    sh      = GothicShader::get("light_vsm.frag.sprv");
    fsLight = device.shader(sh.data,sh.len);
    lightsVsm = device.pipeline(Triangles, state, vsLight, fsLight);
    }
  }

  tonemapping        = postEffect("triangle_uv", "tonemapping",    RenderState::ZTestMode::Always);
  tonemappingUpscale = postEffect("triangle_uv", "tonemapping_up", RenderState::ZTestMode::Always);

  cmaa2EdgeColor2x2Presets[uint32_t(AaPreset::OFF)]    = Tempest::ComputePipeline();
  cmaa2EdgeColor2x2Presets[uint32_t(AaPreset::MEDIUM)] = computeShader("cmaa2_edges_color2x2_quality_0.comp.sprv");
  cmaa2EdgeColor2x2Presets[uint32_t(AaPreset::ULTRA)]  = computeShader("cmaa2_edges_color2x2_quality_1.comp.sprv");

  cmaa2ProcessCandidates = computeShader("cmaa2_process_candidates.comp.sprv");
  {
    auto sh = GothicShader::get("cmaa2_deferred_color_apply_2x2.vert.sprv");
    auto vs = device.shader(sh.data,sh.len);
    sh = GothicShader::get("cmaa2_deferred_color_apply_2x2.frag.sprv");
    auto fs = device.shader(sh.data,sh.len);
    cmaa2DeferredColorApply2x2 = device.pipeline(Tempest::Points,RenderState(),vs,fs);
  }

  hiZPot = computeShader("hiz_pot.comp.sprv");
  hiZMip = computeShader("hiz_mip.comp.sprv");

  if(Gothic::options().doRayQuery) {
    RenderState state;
    state.setCullFaceMode(RenderState::CullMode::NoCull);
    state.setZTestMode   (RenderState::ZTestMode::Less);

    auto sh = GothicShader::get("probe_dbg.vert.sprv");
    auto vs = device.shader(sh.data,sh.len);
    sh = GothicShader::get("probe_dbg.frag.sprv");
    auto fs = device.shader(sh.data,sh.len);
    probeDbg = device.pipeline(Triangles,state,vs,fs);

    sh = GothicShader::get("probe_hit_dbg.vert.sprv");
    vs = device.shader(sh.data,sh.len);
    sh = GothicShader::get("probe_hit_dbg.frag.sprv");
    fs = device.shader(sh.data,sh.len);
    probeHitDbg = device.pipeline(Triangles,state,vs,fs);

    probeInit      = computeShader("probe_init.comp.sprv");
    probeClear     = computeShader("probe_clear.comp.sprv");
    probeClearHash = computeShader("probe_clear_hash.comp.sprv");
    probeMakeHash  = computeShader("probe_make_hash.comp.sprv");
    probeVote      = computeShader("probe_vote.comp.sprv");
    probePrune     = computeShader("probe_prune.comp.sprv");
    probeAlocation = computeShader("probe_allocation.comp.sprv");
    probeTrace     = computeShader("probe_trace.comp.sprv");
    probeLighting  = computeShader("probe_lighting.comp.sprv");

    state.setBlendSource  (RenderState::BlendMode::One);
    state.setBlendDest    (RenderState::BlendMode::SrcAlpha);  // for debugging
    state.setZTestMode    (RenderState::ZTestMode::NoEqual);
    state.setZWriteEnabled(false);
    sh = GothicShader::get("probe_ambient.vert.sprv");
    vs = device.shader(sh.data,sh.len);
    sh = GothicShader::get("probe_ambient.frag.sprv");
    fs = device.shader(sh.data,sh.len);
    probeAmbient = device.pipeline(Triangles,state,vs,fs);
    }

  if(Shaders::isVsmSupported()) {
    fogEpipolarOcclusion = computeShader("fog_epipolar_occlusion.comp.sprv");
    fogEpipolarVsm       = computeShader("fog_epipolar_vsm.comp.sprv");
    }

  if(Shaders::isVsmSupported()) {
    vsmVisibilityPass  = computeShader("vsm_visibility_pass.comp.sprv");
    vsmClear           = computeShader("vsm_clear.comp.sprv");
    vsmClearOmni       = computeShader("vsm_clear_omni.comp.sprv");
    vsmCullLights      = computeShader("vsm_cull_lights.comp.sprv");
    vsmMarkPages       = computeShader("vsm_mark_pages.comp.sprv");
    vsmMarkOmniPages   = computeShader("vsm_mark_omni_pages.comp.sprv");
    vsmPostprocessOmni = computeShader("vsm_postprocess_omni.comp.sprv");
    vsmTrimPages       = computeShader("vsm_trim_pages.comp.sprv");
    vsmClumpPages      = computeShader("vsm_clump_pages.comp.sprv");
    vsmListPages       = computeShader("vsm_list_pages.comp.sprv");
    vsmSortPages       = computeShader("vsm_sort_pages.comp.sprv");
    vsmAllocPages      = computeShader("vsm_alloc_pages.comp.sprv");
    vsmAlloc2Pages     = computeShader("vsm_alloc_pages2.comp.sprv");
    vsmMergePages      = computeShader("vsm_merge_pages.comp.sprv");
    vsmPackDraw0       = computeShader("vsm_pack_draws0.comp.sprv");
    vsmPackDraw1       = computeShader("vsm_pack_draws1.comp.sprv");
    vsmFogPages        = computeShader("vsm_fog_mark_pages.comp.sprv");
    vsmFogShadow       = computeShader("vsm_fog_shadow.comp.sprv");
    vsmFogTrace        = computeShader("vsm_fog_trace.comp.sprv");
    vsmFog             = fogShader("fog_epipolar");

    vsmDirectLight     = postEffect("direct_light_vsm", RenderState::ZTestMode::NoEqual);
    vsmDbg             = postEffect("vsm_dbg", RenderState::ZTestMode::Always);
    vsmRendering       = computeShader("vsm_rendering.comp.sprv");
    }

  if(Shaders::isRtsmSupported()) {
    rtsmDirectLight = postEffect("rtsm_direct_light", RenderState::ZTestMode::NoEqual);

    rtsmClear       = computeShader("rtsm_clear.comp.sprv");
    rtsmPages       = computeShader("rtsm_mark_pages.comp.sprv");
    rtsmFogPages    = computeShader("rtsm_mark_fog_pages.comp.sprv");
    rtsmHiZ         = computeShader("rtsm_hiz_pages.comp.sprv");

    rtsmCulling     = computeShader("rtsm_culling.comp.sprv");
    rtsmPosition    = computeShader("rtsm_position.comp.sprv");
    rtsmMeshletCull = computeShader("rtsm_meshlet_cull.comp.sprv");
    rtsmPrimCull    = computeShader("rtsm_primitive_cull.comp.sprv");
    rtsmRaster      = computeShader("rtsm_raster.comp.sprv");

    rtsmClearOmni    = computeShader("rtsm_omni_clear.comp.sprv");
    rtsmCullLights   = computeShader("rtsm_cull_lights.comp.sprv");
    rtsmCompactLights = computeShader("rtsm_compact_lights.comp.sprv");
    rtsmLightsOmni   = computeShader("rtsm_omni_lights.comp.sprv");
    rtsmBboxesOmni   = computeShader("rtsm_omni_bboxes.comp.sprv");
    rtsmCullingOmni  = computeShader("rtsm_omni_culling.comp.sprv");
    rtsmPositionOmni = computeShader("rtsm_omni_position.comp.sprv");

    rtsmMeshletOmni  = computeShader("rtsm_omni_meshlet.comp.sprv");
    rtsmBackfaceOmni = computeShader("rtsm_omni_backface.comp.sprv");
    rtsmCompactOmni  = computeShader("rtsm_omni_compact.comp.sprv");
    rtsmTaskOmni     = computeShader("rtsm_omni_task.comp.sprv");
    rtsmPrimOmni     = computeShader("rtsm_omni_primitive.comp.sprv");
    rtsmRasterOmni   = computeShader("rtsm_omni_raster.comp.sprv");

    rtsmRendering    = computeShader("rtsm_rendering.comp.sprv");
    rtsmRenderingOmni= computeShader("rtsm_omni_rendering.comp.sprv");
    rtsmDbg          = postEffect("rtsm_dbg", RenderState::ZTestMode::Always);
    }

  swRaytracing    = computeShader("sw_raytracing.comp.sprv");
  swRaytracing8   = computeShader("sw_raytracing8.comp.sprv");
  swRaytracing64  = computeShader("sw_raytracing64.comp.sprv");

  if(Gothic::options().swRenderingPreset>0) {
    switch(Gothic::options().swRenderingPreset) {
      case 1:
        swRendering = computeShader("sw_rendering_imm.comp.sprv");
        break;
      case 2:
        swRendering = computeShader("sw_rendering_tbr.comp.sprv");
        break;
      case 3:
        swRendering = computeShader("sw_light.comp.sprv");
        break;
      }
    swRenderingDbg = postEffect("vbuffer_blit", RenderState::ZTestMode::Always);
    }

  {
    RenderState state;
    state.setCullFaceMode(RenderState::CullMode::Front);
    state.setZTestMode   (RenderState::ZTestMode::LEqual);

    auto sh = GothicShader::get("item.vert.sprv");
    auto vs = device.shader(sh.data,sh.len);
    sh = GothicShader::get("item.frag.sprv");
    auto fs = device.shader(sh.data,sh.len);
    inventory = device.pipeline(Triangles,state,vs,fs);
  }
  }

bool Shaders::isVsmSupported() {
  auto& gpu = Resources::device().properties();
  if(gpu.compute.maxInvocations>=1024 && gpu.render.maxClipCullDistances>=4 &&
     gpu.render.maxViewportSize.w>=8192 && gpu.render.maxViewportSize.h>=8192) {
    return true;
    }
  return false;
  }

bool Shaders::isRtsmSupported() {
  if(!Gothic::options().doBindless) {
    return false;
    }
  auto& gpu = Resources::device().properties();
  if(!gpu.hasStorageFormat(TextureFormat::RG32U) || !gpu.hasStorageFormat(TextureFormat::R11G11B10UF))
    return false;
  if(gpu.compute.maxInvocations<512 || gpu.compute.maxSharedMemory<32*1024 || !gpu.descriptors.nonUniformIndexing) {
    return false;
    }
  return true;
  }

const RenderPipeline* Shaders::materialPipeline(const Material& mat, DrawCommands::Type t, PipelineType pt, bool bl) const {
  if(t==DrawCommands::Static) {
    // same shader
    t = DrawCommands::Movable;
    }

  if(pt!=PipelineType::T_Main && !mat.isSolid()) {
    return nullptr;
    }
  if(pt!=PipelineType::T_Main && mat.alpha==Material::Water) {
    return nullptr;
    }

  const auto alpha   = (mat.isGhost ? Material::Ghost : mat.alpha);
  const bool trivial = (!mat.hasUvAnimation() && alpha==Material::Solid && t==DrawCommands::Landscape);
  const bool flatWater = (mat.flatWater && alpha==Material::Transparent);

  for(auto& i:materials) {
    if(i.alpha==alpha && i.type==t && i.pipelineType==pt && i.bindless==bl && i.trivial==trivial && i.flatWater==flatWater)
      return &i.pipeline;
    }

  RenderState state;
  state.setCullFaceMode(RenderState::CullMode::Front);
  state.setZTestMode   (RenderState::ZTestMode::LEqual);
  //state.setZTestMode   (RenderState::ZTestMode::Less);

  if(pt==PipelineType::T_Shadow || pt==PipelineType::T_Vsm) {
    state.setZTestMode(RenderState::ZTestMode::Greater); //FIXME
    }
  if(pt==PipelineType::T_Vsm) {
    // state.setZTestMode(RenderState::ZTestMode::Always); //FIXME
    state.setZTestMode(RenderState::ZTestMode::Greater); //FIXME
    }

  switch(alpha) {
    case Material::Solid:
    case Material::AlphaTest:
    case Material::Water:
    case Material::Ghost:
      break;
    case Material::Transparent:
      state.setBlendSource  (RenderState::BlendMode::SrcAlpha); // premultiply in shader
      state.setBlendDest    (RenderState::BlendMode::OneMinusSrcAlpha);
      state.setZWriteEnabled(false);
      break;
    case Material::AdditiveLight:
      state.setBlendSource  (RenderState::BlendMode::SrcAlpha);
      state.setBlendDest    (RenderState::BlendMode::One);
      state.setZWriteEnabled(false);
      break;
    case Material::Multiply:
    case Material::Multiply2:
      state.setBlendSource  (RenderState::BlendMode::DstColor);
      state.setBlendDest    (RenderState::BlendMode::SrcColor);
      state.setZWriteEnabled(false);
      break;
    }

  static bool overdrawDbg = false;
  if(overdrawDbg &&
      (alpha==Material::Solid || alpha==Material::AlphaTest) && t!=DrawCommands::Landscape && pt!=T_Shadow) {
    state.setBlendSource(RenderState::BlendMode::One);
    state.setBlendDest  (RenderState::BlendMode::One);
    state.setZWriteEnabled(false);
    state.setZTestMode(RenderState::ZTestMode::Always);
    }

  const char* vsTok = "";
  const char* fsTok = "";

  switch(t) {
    case DrawCommands::Landscape:
      vsTok = "lnd";
      fsTok = "lnd";
      break;
    case DrawCommands::Static:
    case DrawCommands::Movable:
      vsTok = "obj";
      fsTok = "obj";
      break;
    case DrawCommands::Animated:
      vsTok = "ani";
      fsTok = "obj";
      break;
    case DrawCommands::Pfx:
      vsTok = "pfx";
      fsTok = "pfx";
      break;
    case DrawCommands::Morph:
      vsTok = "mph";
      fsTok = "obj";
      break;
    }

  const char* typeVsM = nullptr;
  const char* typeVsD = nullptr;
  const char* typeVsV = nullptr;
  const char* typeFsM = nullptr;
  const char* typeFsD = nullptr;
  const char* typeFsV = nullptr;

  switch(alpha) {
    case Material::Solid:
      typeVsM = "";
      typeFsM = "_g";
      typeVsD = "_d";
      typeFsD = ""; // No fragment shader needed for solid depth/shadow passes!
      typeVsV = "_v";
      typeFsV = "_v";
      if(trivial)
        typeFsM = "_g_s";
      break;
    case Material::AlphaTest:
      typeVsM = "";
      typeFsM = "_g_at";
      // TODO(Adreno): On Adreno 6xx drivers, depth-only alpha-test crashes LLVM backend (vkCreateGraphicsPipelines).
      // When bypassing alpha-test for depth prepass, both VS and FS must use solid depth variants ("_d" without varyings, and "" FS).
      typeVsD = opengothic_debug_no_atest_depth() ? "_d" : "_d_at";
      typeFsD = opengothic_debug_no_atest_depth() ? "" : "_d_at";
      typeVsV = "_v_at";
      typeFsV = "_v_at";
      break;
    case Material::Transparent:
      typeVsM = "_f";
      typeFsM = flatWater ? "_fw" : "_f";
      break;
    case Material::Multiply:
    case Material::Multiply2:
      typeVsM = "";
      typeFsM = "_e";
      break;
    case Material::AdditiveLight:
      typeVsM = "";
      typeFsM = "_e";
      break;
    case Material::Water:
      typeVsM = "_f";
      typeFsM = "_w";
      break;
    case Material::Ghost:
      typeVsM = "";
      typeFsM = "_x";
      break;
    }

  const char* typeVs = "";
  const char* typeFs = "";
  switch(pt) {
    case T_Shadow:
    case T_Depth:
      typeVs = typeVsD;
      typeFs = typeFsD;
      break;
    case T_Vsm:
      typeVs = typeVsV;
      typeFs = typeFsV;
      break;
    case T_Main:
      typeVs = typeVsM;
      typeFs = typeFsM;
      break;
    }

  if(typeVs==nullptr || typeFs==nullptr)
    return nullptr;

  if(t==DrawCommands::Pfx && (alpha==Material::Water || alpha==Material::Ghost))
    return nullptr;

  const char* bindless = bl ? "_bindless" : "_slot";

  materials.emplace_front();
  auto& b = materials.front();
  b.alpha        = alpha;
  b.type         = t;
  b.pipelineType = pt;
  b.bindless     = bl;
  b.trivial      = trivial;
  b.flatWater    = flatWater;

  auto& device = Resources::device();
  const bool hasFs = (typeFs!=nullptr && typeFs[0]!='\0');

  if(mat.isTesselated() && device.properties().tesselationShader && t==DrawCommands::Landscape && true) {
    auto shVs = GothicShader::get(string_frm("main_", vsTok, typeVs, bindless, ".vert.sprv"));
    auto shTc = GothicShader::get(string_frm("main_", vsTok, typeVs, bindless, ".tesc.sprv"));
    auto shTe = GothicShader::get(string_frm("main_", vsTok, typeVs, bindless, ".tese.sprv"));

    auto vs = device.shader(shVs.data,shVs.len);
    auto tc = device.shader(shTc.data,shTc.len);
    auto te = device.shader(shTe.data,shTe.len);
    if(hasFs) {
      auto shFs = GothicShader::get(string_frm("main_", fsTok, typeFs, bindless, ".frag.sprv"));
      auto fs = device.shader(shFs.data,shFs.len);
      b.pipeline = device.pipeline(Triangles, state, vs, tc, te, fs);
      } else {
      b.pipeline = device.pipeline(Triangles, state, vs, tc, te, Shader());
      }
    }
  else if(Gothic::options().doMeshShading && t!=DrawCommands::Pfx) {
    auto shMs = GothicShader::get(string_frm("main_", vsTok, typeVs, bindless, ".mesh.sprv"));
    auto ms = device.shader(shMs.data,shMs.len);
    if(hasFs) {
      auto shFs = GothicShader::get(string_frm("main_", fsTok, typeFs, bindless, ".frag.sprv"));
      auto fs = device.shader(shFs.data,shFs.len);
      b.pipeline = device.pipeline(state, Shader(), ms, fs);
      } else {
      b.pipeline = device.pipeline(state, Shader(), ms, Shader());
      }
    }
  else if(t!=DrawCommands::Pfx) {
    auto shVs = GothicShader::get(string_frm("main_", vsTok, typeVs, bindless, ".vert.sprv"));
    auto vs = device.shader(shVs.data,shVs.len);
    if(hasFs) {
      auto shFs = GothicShader::get(string_frm("main_", fsTok, typeFs, bindless, ".frag.sprv"));
      auto fs = device.shader(shFs.data,shFs.len);
      b.pipeline = device.pipeline(Triangles, state, vs, fs);
      } else {
      b.pipeline = device.pipeline(Triangles, state, vs, Shader());
      }
    }
  else {
    auto shVs = GothicShader::get(string_frm("main_", vsTok, typeVs, ".vert.sprv"));
    auto vs = device.shader(shVs.data,shVs.len);
    if(hasFs) {
      auto shFs = GothicShader::get(string_frm("main_", fsTok, typeFs, ".frag.sprv"));
      auto fs = device.shader(shFs.data,shFs.len);
      b.pipeline = device.pipeline(Triangles, state, vs, fs);
      } else {
      b.pipeline = device.pipeline(Triangles, state, vs, Shader());
      }
    }

  return &b.pipeline;
  }

RenderPipeline Shaders::postEffect(std::string_view name) {
  return postEffect("triangle",name);
  }

RenderPipeline Shaders::postEffect(std::string_view name, Tempest::RenderState::ZTestMode ztest) {
  return postEffect("triangle",name,ztest);
  }

RenderPipeline Shaders::postEffect(std::string_view vsName, std::string_view fsName, Tempest::RenderState::ZTestMode ztest) {
  auto& device = Resources::device();

  RenderState stateFsq;
  stateFsq.setZTestMode    (ztest);
  stateFsq.setZWriteEnabled(false);

  auto sh = GothicShader::get(string_frm(vsName,".vert.sprv"));
  auto vs = device.shader(sh.data,sh.len);

  sh      = GothicShader::get(string_frm(fsName,".frag.sprv"));
  auto fs = device.shader(sh.data,sh.len);
  return device.pipeline(Triangles,stateFsq,vs,fs);
  }

ComputePipeline Shaders::computeShader(std::string_view name) {
  auto& device = Resources::device();
  auto  sh     = GothicShader::get(name);
  return device.pipeline(device.shader(sh.data,sh.len));
  }

RenderPipeline Shaders::fogShader(std::string_view name) {
  auto& device = Resources::device();
  const bool fogDbg = false;

  RenderState state;
  state.setZWriteEnabled(false);
  state.setBlendSource  (RenderState::BlendMode::One);
  if(!fogDbg) {
    state.setBlendDest(RenderState::BlendMode::OneMinusSrcAlpha);
    }

  auto sh = GothicShader::get("triangle.vert.sprv");
  auto vs = device.shader(sh.data,sh.len);

  sh      = GothicShader::get(string_frm(name,".frag.sprv"));
  auto fs = device.shader(sh.data,sh.len);
  return device.pipeline(Triangles,state,vs,fs);
  }

RenderPipeline Shaders::inWaterShader(std::string_view name, bool isScattering) {
  auto& device = Resources::device();

  RenderState state;
  state.setZWriteEnabled(false);

  if(isScattering) {
    state.setBlendSource(RenderState::BlendMode::One);
    state.setBlendDest  (RenderState::BlendMode::One);
    } else {
    state.setBlendSource(RenderState::BlendMode::Zero);
    state.setBlendDest  (RenderState::BlendMode::SrcColor);
    }

  auto sh = GothicShader::get("triangle.vert.sprv");
  auto vs = device.shader(sh.data,sh.len);

  sh      = GothicShader::get(string_frm(name,".frag.sprv"));
  auto fs = device.shader(sh.data,sh.len);
  return device.pipeline(Triangles,state,vs,fs);
  }

RenderPipeline Shaders::reflectionShader(std::string_view name, bool hasMeshlets) {
  auto& device = Resources::device();

  RenderState state;
  state.setZTestMode    (RenderState::ZTestMode::LEqual);
  state.setZWriteEnabled(false);
  state.setBlendSource  (RenderState::BlendMode::One);
  state.setBlendDest    (RenderState::BlendMode::One);

  auto sh = GothicShader::get("triangle.vert.sprv");
  auto vs = device.shader(sh.data,sh.len);
  sh      = GothicShader::get(name);
  auto fs = device.shader(sh.data,sh.len);

  if(hasMeshlets) {
    sh = GothicShader::get("water_reflection.mesh.sprv");
    vs = device.shader(sh.data,sh.len);
    }

  return device.pipeline(Triangles, state, vs, fs);
  }

RenderPipeline Shaders::ambientLightShader(std::string_view name, bool isAdditive) {
  auto& device = Resources::device();

  RenderState state;
  state.setBlendSource  (RenderState::BlendMode::One);
  state.setBlendDest    (isAdditive ? RenderState::BlendMode::SrcAlpha : RenderState::BlendMode::Zero);
  state.setZTestMode    (RenderState::ZTestMode::NoEqual);
  state.setZWriteEnabled(false);

  auto sh = GothicShader::get("triangle.vert.sprv");
  auto vs = device.shader(sh.data,sh.len);
  sh      = GothicShader::get(string_frm(name,".frag.sprv"));
  auto fs = device.shader(sh.data,sh.len);

  return device.pipeline(Triangles, state, vs, fs);
  }

void Shaders::prewarmAll() {
  if(!opengothic_debug_prewarm())
    return;

  Log::i("Starting graphics pipeline prewarm...");
  auto startTime = Application::tickCount();

  waitCompiler();

  auto prewarmPipe = [](RenderPipeline& pipe, const char* name, uint32_t colorAtts, bool hasDepth) {
    if(pipe.isEmpty())
      return;
    if(opengothic_prewarm_check_crashed(name)) {
      Log::i("[PREWARM SKIP CRASHED] ", name);
      return;
    }
    Log::i("[PREWARM] Testing: ", name);
    opengothic_prewarm_begin_attempt(name);
    pipe.prewarm(colorAtts, hasDepth);
    opengothic_prewarm_end_attempt(name, 1);
  };

  // 1. Post-process & full-screen passes (1 color attachment, no depth)
  prewarmPipe(copy, "Post_copy", 1, false);
  prewarmPipe(stash, "Post_stash", 1, false);
  prewarmPipe(directLight, "Post_directLight", 1, false);
  prewarmPipe(directLightSh, "Post_directLightSh", 1, false);
  if(!directLightRq.isEmpty())
    prewarmPipe(directLightRq, "Post_directLightRq", 1, false);
  prewarmPipe(ambientLight, "Post_ambientLight", 1, false);
  prewarmPipe(ambientLightSsao, "Post_ambientLightSsao", 1, false);
  prewarmPipe(skyTransmittance, "Post_skyTransmittance", 1, false);
  prewarmPipe(skyMultiScattering, "Post_skyMultiScattering", 1, false);
  prewarmPipe(skyViewLut, "Post_skyViewLut", 1, false);
  prewarmPipe(skyViewCldLut, "Post_skyViewCldLut", 1, false);
  prewarmPipe(sky, "Post_sky", 1, false);
  prewarmPipe(skySep, "Post_skySep", 1, false);
  prewarmPipe(fog, "Post_fog", 1, false);
  prewarmPipe(fog3dHQ, "Post_fog3dHQ", 1, false);
  prewarmPipe(skyPathTrace, "Post_skyPathTrace", 1, false);
  prewarmPipe(underwaterT, "Post_underwaterT", 1, false);
  prewarmPipe(underwaterS, "Post_underwaterS", 1, false);
  prewarmPipe(waterReflection, "Post_waterReflection", 1, false);
  prewarmPipe(waterReflectionSSR, "Post_waterReflectionSSR", 1, false);
  prewarmPipe(tonemapping, "Post_tonemapping", 1, false);
  prewarmPipe(tonemappingUpscale, "Post_tonemappingUpscale", 1, false);
  prewarmPipe(inventory, "Post_inventory", 1, false);
  prewarmPipe(bink, "Post_bink", 1, false);

  // 2. Material combinations across passes
  static const DrawCommands::Type meshTypes[] = {
    DrawCommands::Landscape,
    DrawCommands::Movable,
    DrawCommands::Animated,
    DrawCommands::Morph,
    DrawCommands::Pfx
  };

  static const Material::AlphaFunc alphaFuncs[] = {
    Material::Solid,
    Material::AlphaTest,
    Material::Transparent,
    Material::AdditiveLight,
    Material::Multiply,
    Material::Water,
    Material::Ghost
  };

  static const PipelineType passTypes[] = {
    PipelineType::T_Depth,
    PipelineType::T_Shadow,
    PipelineType::T_Main
  };

  const bool bindlessOpts[] = { false };

  for(auto t : meshTypes) {
    for(auto a : alphaFuncs) {
      for(auto pt : passTypes) {
        for(bool bl : bindlessOpts) {
          Material mat;
          mat.alpha = a;
          try {
            const auto* pipe = materialPipeline(mat, t, pt, bl);
            if(!pipe || pipe->isEmpty())
              continue;

            const char* tStr = (t == DrawCommands::Landscape) ? "lnd" :
                               (t == DrawCommands::Movable) ? "obj" :
                               (t == DrawCommands::Animated) ? "ani" :
                               (t == DrawCommands::Morph) ? "mph" : "pfx";
            const char* aStr = (a == Material::Solid) ? "Solid" :
                               (a == Material::AlphaTest) ? "AlphaTest" :
                               (a == Material::Transparent) ? "Transparent" :
                               (a == Material::AdditiveLight) ? "AdditiveLight" :
                               (a == Material::Multiply) ? "Multiply" :
                               (a == Material::Water) ? "Water" : "Ghost";
            const char* pStr = (pt == PipelineType::T_Depth) ? "Depth" :
                               (pt == PipelineType::T_Shadow) ? "Shadow" : "Main";

            char key[128];
            std::snprintf(key, sizeof(key), "Mat_%s_%s_%s_%s", tStr, aStr, pStr, bl ? "bindless" : "slot");

            uint32_t colorAtts = 0;
            bool hasDepth = true;
            if(pt == PipelineType::T_Main) {
              colorAtts = (a == Material::Water) ? 3 : 2;
            }

            if(opengothic_prewarm_check_crashed(key)) {
              Log::i("[PREWARM SKIP CRASHED] ", key);
              continue;
            }

            Log::i("[PREWARM] Testing: ", key);
            opengothic_prewarm_begin_attempt(key);
            const_cast<RenderPipeline*>(pipe)->prewarm(colorAtts, hasDepth);
            opengothic_prewarm_end_attempt(key, 1);
          } catch(const std::exception& e) {
            Log::e("[PREWARM EXCEPTION] ", e.what());
          } catch(...) {
            Log::e("[PREWARM UNKNOWN EXCEPTION]");
          }
        }
      }
    }
  }

  auto totalTime = Application::tickCount() - startTime;
  Log::i("Graphics pipeline prewarm finished in ", totalTime, " ms");
}
