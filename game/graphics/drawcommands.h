#pragma once

#include <Tempest/RenderPipeline>
#include <Tempest/StorageBuffer>
#include <vector>

#include "sceneglobals.h"

class VisualObjects;
class DrawBuckets;
class DrawClusters;

class DrawCommands {
  public:
    enum Type : uint8_t {
      Landscape,
      Static,
      Movable,
      Animated,
      Pfx,
      Morph,
      };

    struct DrawCmd {
      const Tempest::RenderPipeline* pMain        = nullptr;
      const Tempest::RenderPipeline* pShadow      = nullptr;
      const Tempest::RenderPipeline* pVsm         = nullptr;
      const Tempest::RenderPipeline* pHiZ         = nullptr;
      Type                           type         = Type::Landscape;

      // water only: the high quality pipeline (pMain is the active one) and the plain one
      const Tempest::RenderPipeline* pMainWater   = nullptr;
      const Tempest::RenderPipeline* pMainSimple  = nullptr;

      Material::AlphaFunc            alpha        = Material::Solid;
      uint32_t                       firstPayload = 0;
      uint32_t                       maxPayload   = 0;

      // bindfull only
      uint32_t                       bucketId     = 0;

      // Where everything this command draws can be (world space): a command that is wholly outside of the camera
      // is not recorded at all. A draw that comes out empty costs a state change on a tile based gpu (15 us).
      // unbounded: objects with no matrix yet or that sway (wind), the command is always recorded
      Tempest::Vec3                  boundsMin    = Tempest::Vec3( 1e30f, 1e30f, 1e30f);
      Tempest::Vec3                  boundsMax    = Tempest::Vec3(-1e30f,-1e30f,-1e30f);
      uint32_t                       unbounded    = 0;
      // the same boxes by cells of the XZ grid (boundsMin/Max is their union): a command is outside of the camera when
      // every cell is, a material that is spread over the whole world is not recorded for what is far behind
      struct Cell { int32_t x, z; Tempest::Vec3 mn, mx; };
      std::vector<Cell>              cells;
      bool                           isOutsideOf(const Frustrum& f) const;

      bool                           isForwardShading() const;
      bool                           isShadowmapRequired() const;
      bool                           isSceneInfoRequired() const;
      bool                           isTextureInShadowPass() const;
      bool                           isBindless() const;
      bool                           isMeshShader() const;
      };

    DrawCommands(VisualObjects& owner, DrawBuckets& buckets, DrawClusters& clusters, const SceneGlobals& scene);
    ~DrawCommands();

    const DrawCmd& operator[](size_t i) const { return cmd[i]; }
    size_t   maxMeshlets() const { return maxPayload; }

    void     commit(Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    uint16_t commandId(const Material& m, Type type, uint32_t bucketId);
    void     addClusters(uint16_t cmdId, uint32_t meshletCount);
    void     expandBounds(uint16_t cmdId, const Tempest::Vec3& mn, const Tempest::Vec3& mx);
    void     addUnbounded(uint16_t cmdId, int delta);
    void     resetDynamicBounds();

    void     resetRendering();

    void     visibilityPass(Tempest::Encoder<Tempest::CommandBuffer>& cmd, int pass);
    void     visibilityVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd);

    void     drawHiZ(Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    bool     hasCommands(Material::AlphaFunc func) const;
    bool     hasVisible (Material::AlphaFunc func, const Frustrum& f) const;
    void     drawCommon(Tempest::Encoder<Tempest::CommandBuffer>& cmd, SceneGlobals::VisCamera viewId, Material::AlphaFunc func);

    void     drawVsm(Tempest::Encoder<Tempest::CommandBuffer>& cmd);
    void     logGeometryStats();

    // web settings: object and world mesh draw distance in cm, 0 = no limit
    static float drawDistance();
    static float worldDistance();

  private:
    enum TaskLinkpackage : uint8_t {
      T_Scene      = 0,
      T_Payload    = 1,
      T_Instance   = 2,
      T_Bucket     = 3,
      T_Indirect   = 4,
      T_Clusters   = 5,
      T_Lights     = 6,
      T_HiZ        = 7,
      T_VsmPages   = 8,
      T_CmdOffsets = 9,
      };

    enum UboLinkpackage : uint8_t {
      L_Scene      = 0,
      L_Payload    = 1,
      L_Instance   = 2,
      L_Pfx        = L_Instance,
      L_Bucket     = 3,
      L_Ibo        = 4,
      L_Vbo        = 5,
      L_Diffuse    = 6,
      L_Sampler    = 7,
      L_Shadow0    = 8,
      L_Shadow1    = 9,
      L_MorphId    = 10,
      L_Morph      = 11,
      L_SceneClr   = 12,
      L_GDepth     = 13,
      L_CmdOffsets = 14,
      L_SwampMask  = 15,
      L_VsmPages   = L_Shadow0,
      L_Lights     = L_Shadow1,
      };

    struct IndirectCmd {
      uint32_t vertexCount   = 0;
      uint32_t instanceCount = 0;
      uint32_t firstVertex   = 0;
      uint32_t firstInstance = 0;
      uint32_t writeOffset   = 0;
      };

    struct View {
      SceneGlobals::VisCamera viewport = SceneGlobals::V_Main;
      Tempest::StorageBuffer  visClusters, indirectCmd;
      Tempest::StorageBuffer  vsmClusters;
      };

    bool                     isViewEnabled(SceneGlobals::VisCamera v) const;
    uint64_t                 replayGen(SceneGlobals::VisCamera viewId, Material::AlphaFunc func);

    void                     setBindings(Tempest::Encoder<Tempest::CommandBuffer>& cmd, const DrawCmd& cx, SceneGlobals::VisCamera viewId,
                                         bool viewBound = false);

    VisualObjects&           owner;
    DrawBuckets&             buckets;
    DrawClusters&            clusters;
    const SceneGlobals&      scene;
    size_t                   maxPayload = 0;

    std::vector<DrawCmd>     cmd;
    std::vector<DrawCmd*>    ord;
    bool                     cmdDurtyBit = false;
    uint32_t                 cmdGen = 0; // changes with the draw commands: see drawCommon
    std::vector<const DrawCmd*> animCmds; // commands of frame animated materials
    uint64_t                 animCmdsGen = ~uint64_t(0);
    View                     views[SceneGlobals::V_Count];

    const bool               vsmSupported;
    Tempest::StorageBuffer   vsmIndirectCmd;
  };
