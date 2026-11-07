#include "glscene.h"

#include <Tempest/Device>
#include <Tempest/Log>

#include <cstring>

#include "graphics/mesh/submesh/staticmesh.h"
#include "graphics/mesh/submesh/animmesh.h"
#include "graphics/mesh/submesh/packedmesh.h"
#include "graphics/dynamic/frustrum.h"
#include "graphics/visualobjects.h"
#include "shaders.h"
#include "resources.h"

#include "shader.h"

using namespace Tempest;

static constexpr uint32_t MaxBones = 128; // see shader/webgl/gl_obj.vert

static int modeOf(Material::AlphaFunc a) {
  switch(a) {
    case Material::Solid:         return 0;
    case Material::AlphaTest:     return 1;
    case Material::Water:
    case Material::Ghost:
    case Material::Transparent:   return 2;
    case Material::AdditiveLight: return 3;
    case Material::Multiply:      return 4;
    case Material::Multiply2:     return 5;
    }
  return 0;
  }

GlScene::GlScene() {
  auto& device = Resources::device();

  auto mk = [&](const char* vsName, int mode) {
    RenderState st;
    st.setZTestMode(RenderState::ZTestMode::Less);
    // as the material pipelines (see Shaders): the world space of Gothic is mirrored
    st.setCullFaceMode(mode==0 ? RenderState::CullMode::Front : RenderState::CullMode::NoCull);
    switch(mode) {
      case 0:
      case 1:
        break;
      case 2:
        st.setBlendSource(RenderState::BlendMode::SrcAlpha);
        st.setBlendDest  (RenderState::BlendMode::OneMinusSrcAlpha);
        st.setZWriteEnabled(false);
        break;
      case 3:
        st.setBlendSource(RenderState::BlendMode::SrcAlpha);
        st.setBlendDest  (RenderState::BlendMode::One);
        st.setZWriteEnabled(false);
        break;
      case 4:
        st.setBlendSource(RenderState::BlendMode::DstColor);
        st.setBlendDest  (RenderState::BlendMode::Zero);
        st.setZWriteEnabled(false);
        break;
      case 5:
        st.setBlendSource(RenderState::BlendMode::DstColor);
        st.setBlendDest  (RenderState::BlendMode::SrcColor);
        st.setZWriteEnabled(false);
        break;
      }
    auto sv = GothicShader::get(vsName);
    auto vs = device.shader(sv.data, sv.len);
    auto sf = GothicShader::get(mode==1 ? "gl_obj_at.frag.sprv" : "gl_obj.frag.sprv");
    auto fs = device.shader(sf.data, sf.len);
    return device.pipeline(Triangles, st, vs, fs);
    };

  for(int i=0; i<6; ++i) {
    obj [i] = mk("gl_obj.vert.sprv",  i);
    skin[i] = mk("gl_skin.vert.sprv", i);
    }
  sceneUbo = device.ubo<SceneUbo>(SceneUbo());
  }

const RenderPipeline* GlScene::pipeline(bool sk, Material::AlphaFunc a) const {
  const int m = modeOf(a);
  return sk ? &skin[m] : &obj[m];
  }

void GlScene::draw(Encoder<CommandBuffer>& cmd, VisualObjects& vis, const Frustrum& f, const View& v) {
  // the sun from above lights the most; the light does not go below a dim ambient at night
  {
    SceneUbo u = {};
    const float day = std::max(0.f, std::min(1.f, v.sunIntensity));
    u.sunDir[0]   = v.sunDir.x;
    u.sunDir[1]   = v.sunDir.y;
    u.sunDir[2]   = v.sunDir.z;
    u.sunColor[0] = u.sunColor[1] = u.sunColor[2] = 0.6f*day;
    u.ambient[0]  = u.ambient[1]  = u.ambient[2]  = 0.5f + 0.35f*day;
    u.ambient[3]  = 1.f;
    sceneUbo.update(&u);
  }

  draws[0].clear();
  draws[1].clear();
  bonesCpu.clear();
  bonesAt.clear();

  const float maxDist = v.drawDistance;
  auto& objects  = vis.objects;
  auto& clusters = vis.clustersMem;
  auto& instData = vis.instanceMem.cpu();

  for(size_t id=0; id<objects.size(); ++id) {
    auto& o = objects[id];
    if(o.isEmpty() || o.type==DrawCommands::Pfx || o.iboLen==0)
      continue;
    auto& bucket = *o.bucketId;
    const Material& mat = bucket.mat;
    const Texture2d* tex = mat.tex;
    if(mat.hasFrameAnimation())
      tex = mat.frames[size_t(v.tickCount/mat.texAniFPSInv) % mat.frames.size()];
    if(tex==nullptr)
      continue;

    const bool skin = (bucket.animMesh!=nullptr);
    Draw d;
    d.pso  = pipeline(skin, mat.alpha);
    d.tex  = tex;
    d.skin = skin;
    d.vbo  = skin ? static_cast<const void*>(bucket.animMesh) : static_cast<const void*>(bucket.staticMesh);
    d.obj  = uint32_t(id);
    if(d.vbo==nullptr)
      continue;
    const Pass pass = (modeOf(mat.alpha)<=1) ? P_Opaque : P_Blend;

    if(o.type==DrawCommands::Landscape) {
      // one cluster per meshlet: the visible runs of meshlets are drawn
      const uint32_t cnt = o.iboLen/PackedMesh::MaxInd;
      uint32_t run = 0, begin = 0;
      for(uint32_t i=0; i<=cnt; ++i) {
        bool vis = false;
        if(i<cnt) {
          auto& c = clusters[o.clusterId+i];
          vis = c.r>0 && f.testPoint(c.pos, c.r) &&
                (v.worldDistance<=0 || (c.pos-v.camPos).length() <= v.worldDistance + c.r);
          }
        if(vis) {
          if(run==0)
            begin = i;
          ++run;
          continue;
          }
        if(run>0) {
          d.iboOff = o.iboOff + begin*PackedMesh::MaxInd;
          d.iboLen = run*PackedMesh::MaxInd;
          draws[pass].push_back(d);
          run = 0;
          }
        }
      continue;
      }

    if(o.clusterId!=uint32_t(-1)) {
      auto& c = clusters[o.clusterId];
      if(c.r>0 && !f.testPoint(c.pos, c.r))
        continue;
      if(maxDist>0 && (c.pos-v.camPos).length() > maxDist + c.r)
        continue;
      }

    d.iboOff = o.iboOff;
    d.iboLen = o.iboLen;
    if(skin) {
      if(o.animCount==0)
        continue;
      auto it = bonesAt.find(o.animPtr);
      if(it==bonesAt.end()) {
        const size_t src = size_t(o.animPtr)*sizeof(Matrix4x4);
        const size_t n   = std::min<size_t>(o.animCount, MaxBones);
        if(src + n*sizeof(Matrix4x4) > instData.size())
          continue;
        const size_t at = bonesCpu.size();
        bonesCpu.resize(at + MaxBones*sizeof(Matrix4x4)); // the whole block: the shader declares MaxBones
        std::memcpy(bonesCpu.data()+at, instData.data()+src, n*sizeof(Matrix4x4));
        it = bonesAt.emplace(o.animPtr, uint32_t(at)).first;
        }
      d.bones = it->second;
      }
    draws[pass].push_back(d);
    }

  if(!bonesCpu.empty()) {
    if(bonesGpu.byteSize()<bonesCpu.size())
      bonesGpu = Resources::device().ssbo(Tempest::Uninitialized, bonesCpu.size()*2);
    bonesGpu.update(bonesCpu.data(), 0, bonesCpu.size());
    }

  cmd.setBinding(0, sceneUbo);
  const RenderPipeline* curPso = nullptr;
  Push push;
  for(auto& list:draws) {
    for(auto& d:list) {
      if(d.pso!=curPso) {
        cmd.setPipeline(*d.pso);
        curPso = d.pso;
        }
      cmd.setBinding(2, *d.tex, Sampler::anisotrophy());
      auto& o = objects[d.obj];
      if(d.skin) {
        push.mvp   = v.viewProj;
        push.model.identity();
        cmd.setBinding(1, bonesGpu, d.bones);
        cmd.setPushData(push);
        auto& m = *static_cast<const AnimMesh*>(d.vbo);
        if(d.iboOff<m.ibo.size())
          cmd.draw(m.vbo, m.ibo, d.iboOff, std::min<size_t>(d.iboLen, m.ibo.size()-d.iboOff));
        } else {
        if(o.type==DrawCommands::Landscape)
          push.model.identity(); else
          push.model = o.pos;
        push.mvp = v.viewProj;
        push.mvp.mul(push.model);
        cmd.setPushData(push);
        auto& m = *static_cast<const StaticMesh*>(d.vbo);
        // meshes made at run time say MaxInd indices and have fewer
        if(d.iboOff<m.ibo.size())
          cmd.draw(m.vbo, m.ibo, d.iboOff, std::min<size_t>(d.iboLen, m.ibo.size()-d.iboOff));
        }
      }
    }
  }
