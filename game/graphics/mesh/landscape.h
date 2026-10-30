#pragma once

#include <Tempest/Device>
#include <Tempest/Matrix4x4>
#include <Tempest/UniformBuffer>

#include "graphics/visualobjects.h"
#include "graphics/mesh/submesh/staticmesh.h"

class PackedMesh;

class Landscape final {
  public:
    Landscape(VisualObjects& visual, const PackedMesh& wmesh);

    const Tempest::StorageBuffer& bvh()   const { return bvhNodes; }

    // web water quality ultra high: where the swamp is (1) and the sea (0), a smooth map over the world xz
    const Tempest::Texture2d*     swampMask() const { return hasSwamp ? &swampTex : nullptr; }
    Tempest::Vec4                 swampRect() const { return swampMap; }

  private:
    using Item = VisualObjects::Item;

    struct Block {
      Item mesh;
      };

    std::vector<Block>     blocks;
    StaticMesh             mesh;
    Tempest::StorageBuffer meshletDesc;

    Tempest::StorageBuffer bvhNodes;

    void                   buildSwampMask(const PackedMesh& packed);
    Tempest::Texture2d     swampTex;
    Tempest::Vec4          swampMap = {0,0,0,0};
    bool                   hasSwamp = false;
  };
