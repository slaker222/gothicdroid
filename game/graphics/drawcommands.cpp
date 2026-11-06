#include "drawcommands.h"

#include <mutex>

#include <Tempest/Log>
#include <Tempest/Application>

#include "graphics/mesh/submesh/animmesh.h"
#include "graphics/mesh/submesh/staticmesh.h"
#include "graphics/mesh/submesh/packedmesh.h"
#include "graphics/visualobjects.h"
#include "shaders.h"

#include "gothic.h"
#include "gapi/vulkan/vpipelinelogger.h"

using namespace Tempest;

static bool needtoReallocate(const StorageBuffer& b, const size_t desiredSz) {
  return b.byteSize()<desiredSz || b.byteSize()>=2*desiredSz;
  }


bool DrawCommands::DrawCmd::isForwardShading() const {
  return Material::isForwardShading(alpha);
  }

bool DrawCommands::DrawCmd::isShadowmapRequired() const {
  // the plain water does not take shadows
  if(pMainSimple!=nullptr && pMain==pMainSimple)
    return false;
  return Material::isShadowmapRequired(alpha);
  }

bool DrawCommands::DrawCmd::isSceneInfoRequired() const {
  return Material::isSceneInfoRequired(alpha);
  }

bool DrawCommands::DrawCmd::isTextureInShadowPass() const {
  return Material::isTextureInShadowPass(alpha);
  }

bool DrawCommands::DrawCmd::isBindless() const {
  return bucketId==uint32_t(-1);
  }

bool DrawCommands::DrawCmd::isMeshShader() const {
  auto& opt = Gothic::inst().options();
  if(!opt.doMeshShading)
    return false;
  if(Material::isTesselated(alpha) && type==DrawCommands::Landscape && Resources::device().properties().tesselationShader)
    return false;
  return true;
  }


DrawCommands::DrawCommands(VisualObjects& owner, DrawBuckets& buckets, DrawClusters& clusters, const SceneGlobals& scene)
    : owner(owner), buckets(buckets), clusters(clusters), scene(scene), vsmSupported(Shaders::isVsmSupported()) {
  for(uint8_t v=0; v<SceneGlobals::V_Count; ++v) {
    views[v].viewport = SceneGlobals::VisCamera(v);
    }

  if(vsmSupported) {
    Tempest::DispatchIndirectCommand cmd = {2000,1,1};
    vsmIndirectCmd = Resources::device().ssbo(&cmd, sizeof(cmd));
    }
  // vsmSwrImage = Resources::device().image2d(TextureFormat::R16,  4096, 4096);
  // vsmSwrImage = Resources::device().image2d(TextureFormat::R32U, 4096, 4096);
  }

DrawCommands::~DrawCommands() {
  }

bool DrawCommands::isViewEnabled(SceneGlobals::VisCamera viewport) const {
  if(viewport==SceneGlobals::V_Vsm && !(vsmSupported && scene.vsmEnabled))
    return false;
#if defined(__ANDROID__)
  if(viewport==SceneGlobals::V_Shadow1)
    return false;
  if(viewport==SceneGlobals::V_HiZ && !opengothic_debug_use_hiz_prepass())
    return false;
  if(opengothic_debug_skip_shadows() && (viewport==SceneGlobals::V_Shadow0 || viewport==SceneGlobals::V_Shadow1))
    return false;
#endif
  if(viewport==SceneGlobals::V_Shadow0 && scene.shadowMap[0]->size()==Size(1,1))
    return false;
  if(viewport==SceneGlobals::V_Shadow1 && scene.shadowMap[1]->size()==Size(1,1))
    return false;
  return true;
  }

float DrawCommands::drawDistance() {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
  // web settings menu: [INTERNAL] webDrawDist 0 = no limit, 1..4 = 150/80/40/20 m
  switch(Gothic::settingsGetI("INTERNAL","webDrawDist")) {
    case 1: return 15000.f;
    case 2: return 8000.f;
    case 3: return 4000.f;
    case 4: return 2000.f;
    }
#endif
  return 0.f;
  }

float DrawCommands::worldDistance() {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
  // web settings menu: [INTERNAL] webWorldDistM in meters, 0 = no limit
  const int m = Gothic::settingsGetI("INTERNAL","webWorldDistM");
  if(m>0)
    return float(std::max(m, 10))*100.f;
#endif
  return 0.f;
  }

void DrawCommands::setBindings(Tempest::Encoder<CommandBuffer>& cmd, const DrawCmd& cx, SceneGlobals::VisCamera v, bool viewBound) {
  static const DrawBuckets::Bucket nullBk;

  const auto  bId = cx.bucketId;
  const auto& bx  = cx.isBindless() ? nullBk : buckets.buckets()[bId];

#if defined(__EMSCRIPTEN__)
  // web: bindings stay set across pipeline changes, the per-view ones are
  // already in place after the first draw of the loop
  if(!viewBound)
#else
  (void)viewBound;
#endif
  {
  cmd.setBinding(L_Scene,    scene.uboGlobal[v]);
  cmd.setBinding(L_Payload,  views[v].visClusters);
  cmd.setBinding(L_Instance, owner.instanceSsbo());
  cmd.setBinding(L_Bucket,   buckets.ssbo());
  }

  if(cx.isBindless()) {
    cmd.setBinding(L_Ibo, buckets.ibo());
    cmd.setBinding(L_Vbo, buckets.vbo());
    }
  else if(bx.staticMesh!=nullptr) {
    cmd.setBinding(L_Ibo, bx.staticMesh->ibo8);
    cmd.setBinding(L_Vbo, bx.staticMesh->vbo);
    }
  else {
    cmd.setBinding(L_Ibo, bx.animMesh->ibo8);
    cmd.setBinding(L_Vbo, bx.animMesh->vbo);
    }

  if(v==SceneGlobals::V_Main || cx.isTextureInShadowPass()) {
    if(cx.isBindless()) {
      cmd.setBinding(L_Diffuse, buckets.textures());
      }
    else if(bx.mat.hasFrameAnimation()) {
      uint64_t timeShift = 0;
      auto     frame     = size_t((timeShift+scene.tickCount)/bx.mat.texAniFPSInv);
      auto*    t         = bx.mat.frames[frame%bx.mat.frames.size()];
      cmd.setBinding(L_Diffuse, *t);
      }
    else {
      cmd.setBinding(L_Diffuse, *bx.mat.tex);
      }
#if defined(__ANDROID__)
    auto smp = Sampler::bilinear();
#else
    auto smp = SceneGlobals::isShadowView(v) ? Sampler::trillinear() : Sampler::anisotrophy();
#endif
    cmd.setBinding(L_Sampler, smp);
    }

  if(v==SceneGlobals::V_Main && cx.isShadowmapRequired()) {
    cmd.setBinding(L_Shadow0, *scene.shadowMap[0], Resources::shadowSampler());
    cmd.setBinding(L_Shadow1, *scene.shadowMap[1], Resources::shadowSampler());
    }

  if(cx.type==Morph && cx.isBindless()) {
    cmd.setBinding(L_MorphId,  buckets.morphId());
    cmd.setBinding(L_Morph,    buckets.morph());
    }
  else if(cx.type==Morph && bx.staticMesh!=nullptr) {
    cmd.setBinding(L_MorphId,  *bx.staticMesh->morph.index);
    cmd.setBinding(L_Morph,    *bx.staticMesh->morph.samples);
    }

  if(v==SceneGlobals::V_Main && cx.isSceneInfoRequired()) {
    cmd.setBinding(L_SceneClr, *scene.sceneColor, Sampler::bilinear(ClampMode::MirroredRepeat));
    cmd.setBinding(L_GDepth,   *scene.sceneDepth, Sampler::nearest (ClampMode::MirroredRepeat));
    if(cx.alpha==Material::Water)
      cmd.setBinding(L_SwampMask, *scene.swampMask, Sampler::bilinear(ClampMode::ClampToEdge));
    }

  if(v==SceneGlobals::V_Vsm) {
    cmd.setBinding(L_CmdOffsets, views[v].indirectCmd);
    cmd.setBinding(L_VsmPages,   *scene.vsmPageList);
    cmd.setBinding(L_Lights,     *scene.lights);
    }
  }

uint16_t DrawCommands::commandId(const Material& m, Type type, uint32_t bucketId) {
  const bool bindlessSys = Gothic::inst().options().doBindless && !opengothic_debug_no_bindless();
  const bool bindless    = bindlessSys && !m.hasFrameAnimation();

  auto pMain    = Shaders::inst().materialPipeline(m, type, Shaders::T_Main,   bindless);
  auto pShadow  = Shaders::inst().materialPipeline(m, type, Shaders::T_Shadow, bindless);
  auto pVsm     = Shaders::inst().materialPipeline(m, type, Shaders::T_Vsm,    bindless);
  auto pHiZ     = Shaders::inst().materialPipeline(m, type, Shaders::T_Depth,  bindless);
  if(pMain==nullptr && pShadow==nullptr && pHiZ==nullptr)
    return uint16_t(-1);

  const RenderPipeline* pSimple = nullptr;
  if(m.alpha==Material::Water) {
    Material ms = m;
    ms.alpha = Material::Transparent;
    ms.flatWater = m.tileWater ? 1 : 2;
    pSimple  = Shaders::inst().materialPipeline(ms, type, Shaders::T_Main, bindless);
    }

  for(size_t i=0; i<cmd.size(); ++i) {
    const auto* cmain = (cmd[i].pMainWater!=nullptr ? cmd[i].pMainWater : cmd[i].pMain);
    if(cmain!=pMain || cmd[i].pShadow!=pShadow || cmd[i].pHiZ!=pHiZ)
      continue;
    if(!bindless && cmd[i].bucketId!=bucketId)
      continue;
    return uint16_t(i);
    }

  auto ret = uint16_t(cmd.size());

  DrawCmd cx;
  cx.pMain       = pMain;
  cx.pShadow     = pShadow;
  cx.pVsm        = pVsm;
  cx.pHiZ        = pHiZ;
  cx.bucketId    = bindless ? 0xFFFFFFFF : bucketId;
  cx.type        = type;
  cx.alpha       = m.alpha;
  if(pSimple!=nullptr) {
    cx.pMainWater  = pMain;
    cx.pMainSimple = pSimple;
    if(Material::simpleWater) {
      cx.pMain = pSimple;
      cx.alpha = Material::Transparent;
      }
    }
  cmd.push_back(std::move(cx));
  cmdDurtyBit = true;
  ++cmdGen;
  return ret;
  }

// boxes of the commands: grown by the npc animation workers (VisualObjects::updateInstance), read and reset by the main thread
static std::mutex g_boundsSync;

bool DrawCommands::DrawCmd::isOutsideOf(const Frustrum& f) const {
  // the spheres the gpu culls with (see VisualObjects::expandBounds): outside of them all, nothing of it is drawn
  std::lock_guard<std::mutex> guard(g_boundsSync);
  if(type==Type::Pfx)
    return false;
  if(unbounded!=0 || boundsMin.x>boundsMax.x)
    return false;
  if(f.isOutside(boundsMin,boundsMax))
    return true;
  for(auto& i:cells)
    if(!f.isOutside(i.mn,i.mx))
      return false;
  return true;
  }

void DrawCommands::resetDynamicBounds() {
  std::lock_guard<std::mutex> guard(g_boundsSync);
  for(auto& c:cmd) {
    if(c.type!=Type::Movable && c.type!=Type::Animated && c.type!=Type::Morph)
      continue;
    c.boundsMin = Tempest::Vec3( 1e30f, 1e30f, 1e30f);
    c.boundsMax = Tempest::Vec3(-1e30f,-1e30f,-1e30f);
    c.cells.clear();
    }
  }

void DrawCommands::expandBounds(uint16_t cmdId, const Tempest::Vec3& mn, const Tempest::Vec3& mx) {
  std::lock_guard<std::mutex> guard(g_boundsSync);
  auto& c = cmd[cmdId];
  c.boundsMin = Tempest::Vec3(std::min(c.boundsMin.x,mn.x), std::min(c.boundsMin.y,mn.y), std::min(c.boundsMin.z,mn.z));
  c.boundsMax = Tempest::Vec3(std::max(c.boundsMax.x,mx.x), std::max(c.boundsMax.y,mx.y), std::max(c.boundsMax.z,mx.z));

  constexpr float cellSz = 2000.f; // 20 m
  const int32_t   cx     = int32_t(std::floor((mn.x+mx.x)*0.5f/cellSz));
  const int32_t   cz     = int32_t(std::floor((mn.z+mx.z)*0.5f/cellSz));
  for(auto& i:c.cells)
    if(i.x==cx && i.z==cz) {
      i.mn = Tempest::Vec3(std::min(i.mn.x,mn.x), std::min(i.mn.y,mn.y), std::min(i.mn.z,mn.z));
      i.mx = Tempest::Vec3(std::max(i.mx.x,mx.x), std::max(i.mx.y,mx.y), std::max(i.mx.z,mx.z));
      return;
      }
  c.cells.push_back({cx,cz,mn,mx});
  }

void DrawCommands::addUnbounded(uint16_t cmdId, int delta) {
  std::lock_guard<std::mutex> guard(g_boundsSync);
  auto& c = cmd[cmdId];
  c.unbounded = uint32_t(int(c.unbounded)+delta);
  }

void DrawCommands::addClusters(uint16_t cmdId, uint32_t meshletCount) {
  cmdDurtyBit = true;
  ++cmdGen;
  cmd[cmdId].maxPayload += meshletCount;
  }

void DrawCommands::resetRendering() {
  for(auto& v:views) {
    Resources::recycle(std::move(v.indirectCmd));
    Resources::recycle(std::move(v.visClusters));
    Resources::recycle(std::move(v.vsmClusters));
    }
  cmdDurtyBit = true;
  ++cmdGen;
  }

void DrawCommands::commit(Encoder<CommandBuffer>& enc) {
  // water quality can change at any time: water is drawn as a plain blended texture or as the water shader
  for(auto& i:cmd) {
    if(i.pMainWater==nullptr || i.pMainSimple==nullptr)
      continue;
    const auto alpha = Material::simpleWater ? Material::Transparent : Material::Water;
    if(i.alpha==alpha)
      continue;
    i.alpha = alpha;
    i.pMain = Material::simpleWater ? i.pMainSimple : i.pMainWater;
    cmdDurtyBit = true;
    }

  if(!cmdDurtyBit)
    return;
  cmdDurtyBit = false;
  ++cmdGen;

  ord.resize(cmd.size());
  for(size_t i=0; i<cmd.size(); ++i)
    ord[i] = &cmd[i];
  std::sort(ord.begin(), ord.end(), [](const DrawCmd* l, const DrawCmd* r){
    return l->alpha < r->alpha;
    });

  size_t totalPayload = 0;
  bool   layChg       = false;
  for(auto& i:cmd) {
    layChg |= (i.firstPayload != uint32_t(totalPayload));
    i.firstPayload = uint32_t(totalPayload);
    totalPayload  += i.maxPayload;
    }

  totalPayload = (totalPayload + 0xFF) & ~size_t(0xFF);
  const size_t visClustersSz = totalPayload*sizeof(uint32_t)*4;

  auto& v      = views[SceneGlobals::V_Main];
  bool  cmdChg = needtoReallocate(v.indirectCmd, sizeof(IndirectCmd)*cmd.size());
  bool  visChg = needtoReallocate(v.visClusters, visClustersSz);
  this->maxPayload = totalPayload;

  if(!layChg && !visChg) {
    return;
    }

  std::vector<IndirectCmd> cx(cmd.size());
  for(size_t i=0; i<cmd.size(); ++i) {
    auto mesh = cmd[i].isMeshShader();

    cx[i].vertexCount   = PackedMesh::MaxInd;
    cx[i].writeOffset   = cmd[i].firstPayload;
    cx[i].firstVertex   = mesh ? 1 : 0;
    cx[i].firstInstance = mesh ? 1 : 0;
    }

  auto& device = Resources::device();
  for(auto& v:views) {
    if(!isViewEnabled(v.viewport))
      continue;

    if(visChg) {
      const size_t vsmMax = 1024*1024*4*4; // arbitrary: ~1k clusters per page
      const size_t size   = v.viewport==SceneGlobals::V_Vsm ? vsmMax : visClustersSz;
      Resources::recycle(std::move(v.visClusters));
      v.visClusters = device.ssbo(nullptr, size);
      }
    if(visChg && v.viewport==SceneGlobals::V_Vsm) {
      Resources::recycle(std::move(v.vsmClusters));
      v.vsmClusters = device.ssbo(nullptr, v.visClusters.byteSize());
      }
    if(cmdChg) {
      Resources::recycle(std::move(v.indirectCmd));
      v.indirectCmd = device.ssbo(cx.data(), sizeof(IndirectCmd)*cx.size());
      }
    else if(layChg) {
      auto staging = device.ssbo(BufferHeap::Upload, cx.data(), sizeof(IndirectCmd)*cx.size());

      enc.setBinding(0, v.indirectCmd);
      enc.setBinding(1, staging);
      enc.setPipeline(Shaders::inst().copyBuf);
      enc.dispatchThreads(staging.byteSize()/sizeof(uint32_t));

      Resources::recycle(std::move(staging));
      }
    }
  }

void DrawCommands::visibilityPass(Encoder<CommandBuffer>& cmd, int pass) {
  static bool freeze = false;
  if(freeze)
    return;

#if defined(__ANDROID__)
  if(pass==1)
    logGeometryStats();
#endif

  cmd.setFramebuffer({});
  if(pass==0) {
    for(auto& v:views) {
      if(this->cmd.empty())
        continue;
      if(!isViewEnabled(v.viewport))
        continue;
      const uint32_t isMeshShader = (Gothic::options().doMeshShading ? 1 : 0);
      cmd.setBinding(T_Indirect, v.indirectCmd);
      cmd.setPushData(&isMeshShader, sizeof(isMeshShader));
      cmd.setPipeline(Shaders::inst().clusterInit);
      cmd.dispatchThreads(this->cmd.size());
      }
    }

  for(uint8_t v=0; v<SceneGlobals::V_Count; ++v) {
    const auto viewport = SceneGlobals::VisCamera(v);
    if(viewport==SceneGlobals::V_HiZ && pass!=0)
      continue;
    if(viewport!=SceneGlobals::V_HiZ && pass==0)
      continue;
    if(viewport==SceneGlobals::V_Vsm)
      continue;
    if(!isViewEnabled(viewport))
      continue;

    struct Push { uint32_t firstMeshlet; uint32_t meshletCount; float znear; float drawDistance; } push = {};
    push.firstMeshlet = 0;
    push.meshletCount = uint32_t(clusters.size());
    push.znear        = scene.znear;
    push.drawDistance  = drawDistance();

    auto* pso = &Shaders::inst().visibilityPassSh;
    if(viewport==SceneGlobals::V_Main) {
      pso = opengothic_debug_use_hiz_prepass() ? &Shaders::inst().visibilityPassHiZ
                                               : &Shaders::inst().visibilityPassNoHiZ;
      }
    else if(viewport==SceneGlobals::V_HiZ)
      pso = &Shaders::inst().visibilityPassHiZCr;

    cmd.setBinding(T_Scene,    scene.uboGlobal[viewport]);
    cmd.setBinding(T_Payload,  views[viewport].visClusters);
    cmd.setBinding(T_Instance, owner.instanceSsbo());
    cmd.setBinding(T_Bucket,   buckets.ssbo());
    cmd.setBinding(T_Indirect, views[viewport].indirectCmd);
    cmd.setBinding(T_Clusters, clusters.ssbo());
    cmd.setBinding(T_HiZ,      *scene.hiZ);
    cmd.setPushData(push);
    cmd.setPipeline(*pso);
    cmd.dispatchThreads(push.meshletCount);
    }
  }

void DrawCommands::visibilityVsm(Encoder<CommandBuffer>& cmd) {
  auto& shaders = Shaders::inst();

  struct Push { uint32_t meshletCount; } push = {};
  push.meshletCount = uint32_t(clusters.size());

  auto& view = views[SceneGlobals::V_Vsm];
  cmd.setBinding(T_Scene,    scene.uboGlobal[SceneGlobals::V_Vsm]);
  cmd.setBinding(T_Payload,  view.vsmClusters); //unsorted clusters
  cmd.setBinding(T_Instance, owner.instanceSsbo());
  cmd.setBinding(T_Bucket,   buckets.ssbo());
  cmd.setBinding(T_Indirect, view.indirectCmd);
  cmd.setBinding(T_Clusters, clusters.ssbo());
  cmd.setBinding(T_Lights,   *scene.lights);
  cmd.setBinding(T_HiZ,      *scene.vsmPageHiZ);
  cmd.setBinding(T_VsmPages, *scene.vsmPageList);
  cmd.setBinding(9,          scene.vsmDbg);
  cmd.setPushData(&push, sizeof(push));
  cmd.setPipeline(shaders.vsmVisibilityPass);
  cmd.dispatchThreads(push.meshletCount, size_t(scene.vsmPageTbl->d() + 1));

  cmd.setBinding(1, view.vsmClusters);
  cmd.setBinding(2, view.visClusters);
  cmd.setBinding(3, view.indirectCmd);
  cmd.setBinding(4, *scene.vsmPageList);
  cmd.setBinding(5, vsmIndirectCmd);
  cmd.setPipeline(shaders.vsmPackDraw0);
  cmd.dispatch(1);

  cmd.setBinding(1, view.vsmClusters);
  cmd.setBinding(2, view.visClusters);
  cmd.setBinding(3, view.indirectCmd);
  cmd.setBinding(4, *scene.vsmPageList);
  cmd.setPipeline(shaders.vsmPackDraw1);
  cmd.dispatch(8096); // TODO: indirect
  // cmd.dispatchIndirect(vsmIndirectCmd, 0);
  }

void DrawCommands::drawVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd) {
  // return;
  struct Push { uint32_t commandId; } push = {};

  auto  viewId = SceneGlobals::V_Vsm;
  auto& view   = views[viewId];

  for(size_t i=0; i<ord.size(); ++i) {
    auto& cx = *ord[i];
    if(cx.alpha!=Material::Solid && cx.alpha!=Material::AlphaTest)
      continue;
    if(cx.pVsm==nullptr)
      continue;

    auto id  = size_t(std::distance(this->cmd.data(), &cx));
    push.commandId = uint32_t(id);

    // cmd.setUniforms(*cx.pVsm, desc[viewId], &push, sizeof(push));
    setBindings(cmd, cx, viewId);
    cmd.setPushData(push);
    cmd.setPipeline(*cx.pVsm);
    if(cx.isMeshShader())
      cmd.dispatchMeshIndirect(view.indirectCmd, sizeof(IndirectCmd)*id + sizeof(uint32_t)); else
      cmd.drawIndirect(view.indirectCmd, sizeof(IndirectCmd)*id);
    }

  if(false) {
    struct Push { uint32_t meshletCount; } push = {};
    push.meshletCount = uint32_t(clusters.size());

    cmd.setFramebuffer({});
    cmd.setBinding(0, *scene.vsmPageData);
    cmd.setBinding(1, scene.uboGlobal[SceneGlobals::V_Vsm]);
    cmd.setBinding(2, *scene.vsmPageList);
    cmd.setBinding(3, clusters.ssbo());
    cmd.setBinding(4, owner.instanceSsbo());
    cmd.setBinding(5, buckets.ibo());
    cmd.setBinding(6, buckets.vbo());
    cmd.setBinding(7, buckets.textures());
    cmd.setBinding(8, Sampler::bilinear());
    cmd.setPushData(&push, sizeof(push));
    cmd.setPipeline(Shaders::inst().vsmRendering);
    // const auto sz = Shaders::inst().vsmRendering.workGroupSize();
    cmd.dispatch(1024u);
    }
  }

void DrawCommands::drawHiZ(Tempest::Encoder<Tempest::CommandBuffer>& cmd) {
  if(opengothic_debug_skip_hiz())
    return;
  struct Push { uint32_t firstMeshlet; uint32_t meshletCount; } push = {};

  auto  viewId = SceneGlobals::V_HiZ;
  auto& view   = views[viewId];

  for(size_t i=0; i<ord.size(); ++i) {
    auto& cx = *ord[i];
    if(cx.alpha!=Material::Solid && cx.alpha!=Material::AlphaTest)
      continue;
    if(cx.type!=Landscape && cx.type!=Static)
      continue;
    if(cx.pHiZ==nullptr)
      continue;

    auto id  = size_t(std::distance(this->cmd.data(), &cx));
    push.firstMeshlet = cx.firstPayload;
    push.meshletCount = cx.maxPayload;

    setBindings(cmd, cx, viewId);
    cmd.setPushData(push);
    cmd.setPipeline(*cx.pHiZ);
    if(cx.isMeshShader())
      cmd.dispatchMeshIndirect(view.indirectCmd, sizeof(IndirectCmd)*id + sizeof(uint32_t)); else
      cmd.drawIndirect(view.indirectCmd, sizeof(IndirectCmd)*id);
    }
  }

// everything besides the GPU resources themselves (the backend watches those) that makes
// drawCommon record other commands than in the previous frame
uint64_t DrawCommands::replayGen(SceneGlobals::VisCamera viewId, Material::AlphaFunc func) {
  auto mix = [](uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ull; };
  uint64_t h = 0xcbf29ce484222325ull;
  h = mix(h, cmdGen);
  h = mix(h, buckets.generation());
  h = mix(h, uint64_t(viewId));
  // objects the bindings of this view point to (setBindings)
  if(viewId==SceneGlobals::V_Main) {
    h = mix(h, uintptr_t(scene.shadowMap[0]));
    h = mix(h, uintptr_t(scene.shadowMap[1]));
    h = mix(h, uintptr_t(scene.sceneColor));
    h = mix(h, uintptr_t(scene.sceneDepth));
    }

  // animated textures: the frame is part of the commands of this call
  const uint64_t gen2 = (uint64_t(cmdGen)<<32) | buckets.generation();
  if(animCmdsGen!=gen2) {
    animCmdsGen = gen2;
    animCmds.clear();
    for(auto& cx:cmd) {
      if(cx.isBindless())
        continue;
      if(buckets.buckets()[cx.bucketId].mat.hasFrameAnimation())
        animCmds.push_back(&cx);
      }
    }
  for(auto cx:animCmds) {
    if(cx->alpha!=func || cx->maxPayload==0)
      continue;
    if(viewId!=SceneGlobals::V_Main && !cx->isTextureInShadowPass())
      continue;
    auto& mat = buckets.buckets()[cx->bucketId].mat;
    h = mix(h, size_t(scene.tickCount/mat.texAniFPSInv) % mat.frames.size());
    }
  return h;
  }

bool DrawCommands::hasCommands(Material::AlphaFunc func) const {
  for(auto i:ord)
    if(i->alpha==func)
      return true;
  return false;
  }

bool DrawCommands::hasVisible(Material::AlphaFunc func, const Frustrum& f) const {
  // the same test as in drawCommon: commands that are not recorded draw nothing
  for(auto i:ord)
    if(i->alpha==func && i->maxPayload!=0 && !i->isOutsideOf(f))
      return true;
  return false;
  }

void DrawCommands::drawCommon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, SceneGlobals::VisCamera viewId, Material::AlphaFunc func) {
  struct Push { uint32_t firstMeshlet; uint32_t meshletCount; } push = {};

  auto b = std::lower_bound(ord.begin(), ord.end(), func, [](const DrawCmd* l, Material::AlphaFunc f){
    if(l->alpha != f)
      return l->alpha < f;
    return false;
    });
  auto e = std::upper_bound(ord.begin(), ord.end(), func, [](Material::AlphaFunc f, const DrawCmd* r){
    if(f != r->alpha)
      return f < r->alpha;
    return false;
    });

  auto& view = views[viewId];
  bool  viewBound = false;

#if defined(__EMSCRIPTEN__)
  // the same draws as in the previous frame are replayed without walking the commands again
  const uint64_t replayToken = (uint64_t(viewId)<<8) | uint64_t(func);
  const uint64_t gen         = replayGen(viewId, func);
  if(cmd.tryReplay(replayToken, gen))
    return;
#endif

  for(auto i=b; i!=e; ++i) {
    auto& cx = **i;
    if(cx.alpha!=func)
      continue;

    if(cx.maxPayload==0)
      continue;

    const RenderPipeline* pso = nullptr;
    switch(viewId) {
      case SceneGlobals::V_Shadow0:
      case SceneGlobals::V_Shadow1:
      case SceneGlobals::V_Vsm:
        pso = cx.pShadow;
        break;
      case SceneGlobals::V_Main:
        pso = cx.pMain;
        break;
      case SceneGlobals::V_HiZ:
      case SceneGlobals::V_Count:
        break;
      }
    if(pso==nullptr)
      continue;

#if !defined(__EMSCRIPTEN__)
    // (not in the browser: the recorded commands are replayed there while nothing changes, the camera is not a part of that)
    // (the shadow views: the same sphere test on the gpu, by the frustum of the view; the sun below the horizon clears it)
    if((viewId==SceneGlobals::V_Main || viewId==SceneGlobals::V_Shadow0 || viewId==SceneGlobals::V_Shadow1) &&
       !opengothic_debug_no_cmd_cull() && cx.isOutsideOf(scene.frustrum[viewId]))
      continue;
#endif

    auto id  = size_t(std::distance(this->cmd.data(), &cx));
    push.firstMeshlet = cx.firstPayload;
    push.meshletCount = cx.maxPayload;

    setBindings(cmd, cx, viewId, viewBound);
    viewBound = true;
    cmd.setPushData(push);
    cmd.setPipeline(*pso);
    if(cx.isMeshShader())
      cmd.dispatchMeshIndirect(view.indirectCmd, sizeof(IndirectCmd)*id + sizeof(uint32_t)); else
      cmd.drawIndirect(view.indirectCmd, sizeof(IndirectCmd)*id);
    }

#if defined(__EMSCRIPTEN__)
  cmd.endReplay(replayToken, gen);
#endif
  }

void DrawCommands::logGeometryStats() {
  static uint64_t s_lastLogTime = 0;
  uint64_t now = Application::tickCount();
  if(now - s_lastLogTime < 1000)
    return;
  s_lastLogTime = now;

  auto& v = views[SceneGlobals::V_Main];
  if(v.indirectCmd.byteSize() == 0 || cmd.empty())
    return;

  std::vector<IndirectCmd> cmds(cmd.size());
  try {
    Resources::device().readBytes(v.indirectCmd, cmds.data(), cmds.size() * sizeof(IndirectCmd));
    }
  catch(...) {
    return;
    }

  uint32_t activeDrawCalls = 0;
  uint32_t totalClusters = 0;

  uint32_t lndClusters = 0, statClusters = 0, animClusters = 0, atestClusters = 0;
  uint32_t lndDraws = 0, statDraws = 0, animDraws = 0, atestDraws = 0;

  for(size_t i = 0; i < cmds.size(); ++i) {
    uint32_t clusters = cmds[i].instanceCount;
    if(clusters > 0) {
      activeDrawCalls++;
      totalClusters += clusters;

      const auto& cx = cmd[i];
      if(cx.alpha == Material::AlphaTest) {
        atestDraws++;
        atestClusters += clusters;
        } else if(cx.type == Landscape) {
        lndDraws++;
        lndClusters += clusters;
        } else if(cx.type == Static || cx.type == Movable) {
        statDraws++;
        statClusters += clusters;
        } else if(cx.type == Animated || cx.type == Morph) {
        animDraws++;
        animClusters += clusters;
        }
      }
    }

  uint64_t triangles = uint64_t(totalClusters) * PackedMesh::MaxPrim; // 64 triangles per cluster
  uint64_t vertices  = uint64_t(totalClusters) * PackedMesh::MaxVert; // up to 64 vertices per cluster

  char buf[512] = {0};
  std::snprintf(buf, sizeof(buf),
    "[PERF_GEO] DrawCalls=%u (Lnd=%u Stat=%u Anim=%u Atest=%u) Clusters=%u Triangles=%llu Vertices=%llu\n",
    activeDrawCalls, lndDraws, statDraws, animDraws, atestDraws,
    totalClusters, (unsigned long long)triangles, (unsigned long long)vertices);

  Log::i(buf);

  FILE* f = std::fopen("/sdcard/Gothic2/perf.log", "a");
  if(!f) f = std::fopen("/storage/emulated/0/Gothic2/perf.log", "a");
  if(f) {
    std::fputs(buf, f);
    std::fflush(f);
    std::fclose(f);
    }
  }
