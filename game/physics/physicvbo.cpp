#include "physicvbo.h"

#include <algorithm>

#include "graphics/mesh/submesh/packedmesh.h"
#include "collisionworld.h"

PhysicVbo::PhysicVbo(PackedMesh&& packed)
  :PhysicVbo(packed.vertices) {
  id = std::move(packed.indices);
  for(auto& i:packed.subMeshes)
    if(!i.material.disable_collision && i.iboLength>0) {
      addSegment(i.iboLength,i.iboOffset,i.material.group,nullptr);
      }
  for(size_t i=0;i<id.size();i+=3){
    std::swap(id[i+1],id[i+2]);
    }
  adjustMesh();
  }

PhysicVbo::PhysicVbo(const std::vector<Vertex>& v)
  :vStorage(v.size()), vert(vStorage) {
  for(size_t i=0;i<v.size();++i){
    vStorage[i] = CollisionWorld::toMeters(Tempest::Vec3(v[i].pos[0],v[i].pos[1],v[i].pos[2]));
    }
  }

PhysicVbo::PhysicVbo(const std::vector<btVector3>* v)
  :vert(*v) {
  }

void PhysicVbo::addIndex(const std::vector<uint32_t>& index, size_t iboOff, size_t iboLen, zenkit::MaterialGroup material) {
  addIndex(index,iboOff,iboLen,material,nullptr);
  }

void PhysicVbo::addIndex(const std::vector<uint32_t>& index, size_t iboOff, size_t iboLen, zenkit::MaterialGroup material,
                         const char* sector) {
  if(iboLen==0)
    return;

  size_t off = id.size();

  id.resize(off+iboLen);
  for(size_t i=0; i<iboLen; i+=3){
    id[off+i+0] = index[iboOff+i+0];
    id[off+i+1] = index[iboOff+i+2];
    id[off+i+2] = index[iboOff+i+1];
    }

  addSegment(iboLen,off,material,sector);
  adjustMesh();
  }

void PhysicVbo::addSegment(size_t indexSize, size_t offset, zenkit::MaterialGroup material, const char* sector) {
  Segment sgm;
  sgm.off    = offset;
  sgm.size   = int(indexSize/3);
  sgm.mat    = material;
  sgm.sector = sector;
  segments.push_back(sgm);
  rebuildParts();
  }

void PhysicVbo::rebuildParts() {
  // the triangle index of a quantized bvh node has 31-MAX_NUM_PARTS_IN_BITS bits
  constexpr int maxTri = (1 << (31 - MAX_NUM_PARTS_IN_BITS)) - 1;
  parts.clear();
  for(size_t i=0; i<segments.size(); ++i) {
    auto& sg = segments[i];
    if(!parts.empty()) {
      auto& p = parts.back();
      if(p.off + size_t(p.size)*3==sg.off && p.size + sg.size<=maxTri) {
        sg.triInPart = p.size;
        p.size  += sg.size;
        p.count += 1;
        continue;
        }
      }
    Part p;
    p.first = i;
    p.count = 1;
    p.off   = sg.off;
    p.size  = sg.size;
    sg.triInPart = 0;
    parts.push_back(p);
    }

  m_indexedMeshes.resize(int(parts.size()));
  for(size_t i=0; i<parts.size(); ++i) {
    btIndexedMesh meshIndex={};
    meshIndex.m_numTriangles        = parts[i].size;
    meshIndex.m_numVertices         = int32_t(vert.size());
    meshIndex.m_indexType           = PHY_INTEGER;
    meshIndex.m_triangleIndexBase   = reinterpret_cast<const uint8_t*>(id.data() + parts[i].off);
    meshIndex.m_triangleIndexStride = 3 * sizeof(id[0]);
    meshIndex.m_vertexBase          = reinterpret_cast<const uint8_t*>(vert.data());
    meshIndex.m_vertexStride        = sizeof(btVector3);
    m_indexedMeshes[int(i)] = meshIndex;
    }
  }

const PhysicVbo::Segment* PhysicVbo::findSegment(int part, int triangle) const {
  if(part<0 || size_t(part)>=parts.size())
    return nullptr;
  auto& p = parts[size_t(part)];
  auto  b = segments.begin() + std::ptrdiff_t(p.first);
  auto  e = b + std::ptrdiff_t(p.count);
  // the last segment that starts at or before the triangle
  auto  it = std::upper_bound(b, e, triangle, [](int t, const Segment& s){ return t<s.triInPart; });
  if(it==b)
    return nullptr;
  return &*(it-1);
  }

zenkit::MaterialGroup PhysicVbo::materialId(int part, int triangle) const {
  if(auto sg = findSegment(part,triangle))
    return sg->mat;
  return zenkit::MaterialGroup::UNDEFINED;
  }

const char* PhysicVbo::sectorName(int part, int triangle) const {
  if(auto sg = findSegment(part,triangle))
    return sg->sector;
  return nullptr;
  }

bool PhysicVbo::useQuantization() const {
  constexpr int maxParts = (1 << MAX_NUM_PARTS_IN_BITS);
  constexpr int maxTri   = (1 << (31 - MAX_NUM_PARTS_IN_BITS));
  if(parts.size()>maxParts)
    return false;
  for(auto& i:parts)
    if(i.size>=maxTri)
      return false;
  return true;
  }

bool PhysicVbo::isEmpty() const {
  return segments.size()==0;
  }

void PhysicVbo::adjustMesh(){
  // id may have been reallocated
  for(size_t i=0; i<parts.size(); ++i)
    m_indexedMeshes[int(i)].m_triangleIndexBase = reinterpret_cast<const uint8_t*>(id.data() + parts[i].off);
  }

std::string_view PhysicVbo::validateSectorName(std::string_view name) const {
  if(name.empty())
    return "";
  for(auto& i:segments)
    if(name==i.sector)
      return i.sector;
  return "";
  }
