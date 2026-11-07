#pragma once

#include <vector>
#include <cstdint>

#include <zenkit/Material.hh>

#include "physics/physics.h"
#include "resources.h"

class PackedMesh;

class PhysicVbo : public btTriangleIndexVertexArray {
  public:
    using Vertex = Resources::Vertex;

    PhysicVbo(PackedMesh&& packed);
    PhysicVbo(const std::vector<btVector3>* v);

    PhysicVbo(const PhysicVbo&)=delete;
    PhysicVbo(PhysicVbo&&)=delete;

    void                    addIndex(const std::vector<uint32_t>& index, size_t iboOff, size_t iboLen, zenkit::MaterialGroup material);
    void                    addIndex(const std::vector<uint32_t>& index, size_t iboOff, size_t iboLen, zenkit::MaterialGroup material, const char* sector);
    // part and triangle as Bullet reports them (LocalShapeInfo m_shapePart/m_triangleIndex, manifold m_partId/m_index)
    zenkit::MaterialGroup   materialId(int part, int triangle) const;
    auto                    sectorName(int part, int triangle) const -> const char*;
    bool                    useQuantization() const;
    bool                    isEmpty() const;

    void                    adjustMesh();

    std::string_view        validateSectorName(std::string_view name) const;

  private:
    PhysicVbo(const std::vector<Vertex>& v);

    void addSegment(size_t indexSize,size_t offset, zenkit::MaterialGroup material, const char* sector);

    struct Segment {
      size_t                off;
      int                   size;
      zenkit::MaterialGroup mat;
      const char*           sector=nullptr;
      int                   triInPart = 0; // the first triangle of the segment in its part
      };

    // Bullet parts: runs of segments that follow each other in the index buffer, one btIndexedMesh each.
    // A part per segment made more than 1024 parts in a big world (Archolos), and then the bvh can not be
    // quantized (10 bits for the part in a node): 64 instead of 16 bytes per node, 120 MB for the land
    struct Part {
      size_t                first = 0;     // segments [first, first+count)
      size_t                count = 0;
      size_t                off   = 0;     // in id
      int                   size  = 0;     // triangles
      };
    void                    rebuildParts();
    const Segment*          findSegment(int part, int triangle) const;
    std::vector<Part>             parts;

    std::vector<btVector3>        vStorage;
    const std::vector<btVector3>& vert;
    std::vector<uint32_t>         id;
    std::vector<Segment>          segments;
  };
