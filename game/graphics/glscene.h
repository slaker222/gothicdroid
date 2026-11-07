#pragma once

#include <Tempest/RenderPipeline>
#include <Tempest/StorageBuffer>
#include <Tempest/UniformBuffer>
#include <Tempest/Encoder>
#include <Tempest/CommandBuffer>
#include <Tempest/Matrix4x4>

#include <unordered_map>
#include <vector>

#include "material.h"

class VisualObjects;
class Frustrum;

// The world of the WebGL 2 compatibility renderer: every object is drawn by plain vertex/index buffers
// with its texture, culled on the cpu. No compute, no lights, shadows, fog or post effects.
class GlScene final {
  public:
    GlScene();

    struct View {
      Tempest::Matrix4x4 viewProj;
      Tempest::Vec3      camPos;
      Tempest::Vec3      sunDir;
      float              sunIntensity = 1.f;
      uint64_t           tickCount    = 0;
      float              drawDistance  = 0; // objects, cm; 0 = no limit
      float              worldDistance = 0; // world mesh, cm; 0 = no limit
      };

    void draw(Tempest::Encoder<Tempest::CommandBuffer>& cmd, VisualObjects& vis, const Frustrum& f, const View& v);

  private:
    enum Pass : uint8_t { P_Opaque, P_Blend };

    struct Push {
      Tempest::Matrix4x4 mvp;
      Tempest::Matrix4x4 model;
      };

    struct SceneUbo {
      float sunDir[4];
      float sunColor[4];
      float ambient[4];
      };

    struct Draw {
      const Tempest::RenderPipeline* pso   = nullptr;
      const Tempest::Texture2d*      tex   = nullptr;
      const void*                    vbo   = nullptr; // StaticMesh or AnimMesh
      bool                           skin  = false;
      uint32_t                       iboOff = 0;
      uint32_t                       iboLen = 0;
      uint32_t                       bones  = 0;      // byte offset in bonesGpu
      uint32_t                       obj    = 0;      // object id: its matrix
      };

    const Tempest::RenderPipeline* pipeline(bool skin, Material::AlphaFunc a) const;

    Tempest::RenderPipeline  obj[6], skin[6];

    Tempest::UniformBuffer<SceneUbo> sceneUbo;
    Tempest::StorageBuffer   bonesGpu;
    std::vector<uint8_t>     bonesCpu;
    std::unordered_map<uint32_t,uint32_t> bonesAt;
    std::vector<Draw>        draws[2];
  };
