#include "landscape.h"

#include <Tempest/Log>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_map>

#include "graphics/mesh/submesh/packedmesh.h"
#include "gothic.h"

using namespace Tempest;

Landscape::Landscape(VisualObjects& visual, const PackedMesh &packed)
  :mesh(packed) {
  auto& device = Resources::device();

  meshletDesc = Resources::ssbo(packed.meshletBounds.data(), packed.meshletBounds.size()*sizeof(packed.meshletBounds[0]));
  bvhNodes    = Resources::ssbo(packed.bvhNodes.data(),  packed.bvhNodes.size()*sizeof(packed.bvhNodes[0]));
  //bvhNodes    = Resources::ssbo(packed.bvh8Nodes.data(), packed.bvh8Nodes.size()*sizeof(packed.bvh8Nodes[0]));

#if defined(__EMSCRIPTEN__)
  buildSwampMask(packed);
#endif

  blocks.reserve(packed.subMeshes.size());
  for(size_t i=0; i<packed.subMeshes.size(); ++i) {
    auto& sub      = packed.subMeshes[i];
    auto  id       = uint32_t(sub.iboOffset/PackedMesh::MaxInd);
    auto  material = Resources::loadMaterial(sub.material,true);

    if(material.alpha==Material::AdditiveLight || sub.iboLength==0) {
      continue;
      }

    if(Gothic::options().doRayQuery) {
      mesh.sub[i].blas = device.blas(mesh.vbo,mesh.ibo,sub.iboOffset,sub.iboLength);
      }

    Block b;
    b.mesh = visual.get(mesh,material,sub.iboOffset,sub.iboLength,&packed.meshletBounds[id],DrawCommands::Landscape);
    b.mesh.setObjMatrix(Matrix4x4::mkIdentity());
    blocks.emplace_back(std::move(b));
    }
  }

// The swamp of Gothic 1 is separate polygons (the swamp texture) with strips of a gradient texture between them and the
// sea. A map over the world is drawn from them (the strips by the gradient of their texture), blurred: the water shader
// reads it by the world position, so the border between the swamp and the water is smooth and does not follow the mesh.
void Landscape::buildSwampMask(const PackedMesh& packed) {
  auto up = [](std::string s) { for(auto& c:s) c = char(std::toupper((unsigned char)c)); return s; };

  // 0 not water, 1 swamp, 2 strip, 3 plain water
  std::vector<int> kind(packed.subMeshes.size(),0);
  bool any = false;
  for(size_t i=0; i<packed.subMeshes.size(); ++i) {
    const auto tex = up(packed.subMeshes[i].material.texture);
    if(tex.find("SEA2SWAMP")!=std::string::npos)
      kind[i] = 2;
    else if(tex.rfind("SWAMP_",0)==0)
      kind[i] = 1;
    else if(packed.subMeshes[i].material.group==zenkit::MaterialGroup::WATER)
      kind[i] = 3;
    any |= kind[i]==1;
    }
  if(!any)
    return;

  const auto bb = packed.bbox();
  const float minX = bb.first.x, minZ = bb.first.z;
  const float sx   = std::max(bb.second.x - bb.first.x, 1.f);
  const float sz   = std::max(bb.second.z - bb.first.z, 1.f);
  const int   N    = 2048;

  // classes of the pixels: 1 swamp, 2 plain water, 4 strip between them
  std::vector<uint8_t> cls(size_t(N)*N, 0);
  for(size_t s=0; s<packed.subMeshes.size(); ++s) {
    if(kind[s]==0)
      continue;
    const uint8_t bit = (kind[s]==1 ? 1 : (kind[s]==3 ? 2 : 4));
    auto& sub = packed.subMeshes[s];
    for(size_t k=0; k+2<sub.iboLength; k+=3) {
      const uint32_t id[3] = {packed.indices[sub.iboOffset+k], packed.indices[sub.iboOffset+k+1], packed.indices[sub.iboOffset+k+2]};
      if(id[0]>=packed.vertices.size() || id[1]>=packed.vertices.size() || id[2]>=packed.vertices.size())
        continue;
      float px[3], pz[3];
      for(int i=0; i<3; ++i) {
        const auto& v = packed.vertices[id[i]];
        px[i] = (v.pos[0]-minX)/sx*float(N);
        pz[i] = (v.pos[2]-minZ)/sz*float(N);
        }
      const float area = (px[1]-px[0])*(pz[2]-pz[0]) - (px[2]-px[0])*(pz[1]-pz[0]);
      if(std::fabs(area)<1e-6f)
        continue;
      const int x0 = std::max(int(std::floor(std::min({px[0],px[1],px[2]}))), 0);
      const int x1 = std::min(int(std::ceil (std::max({px[0],px[1],px[2]}))), N-1);
      const int z0 = std::max(int(std::floor(std::min({pz[0],pz[1],pz[2]}))), 0);
      const int z1 = std::min(int(std::ceil (std::max({pz[0],pz[1],pz[2]}))), N-1);
      for(int z=z0; z<=z1; ++z)
        for(int x=x0; x<=x1; ++x) {
          const float fx = float(x)+0.5f, fz = float(z)+0.5f;
          const float w1 = ((fx-px[0])*(pz[2]-pz[0]) - (px[2]-px[0])*(fz-pz[0]))/area;
          const float w2 = ((px[1]-px[0])*(fz-pz[0]) - (fx-px[0])*(pz[1]-pz[0]))/area;
          const float w0 = 1.f-w1-w2;
          const float e  = -0.5f/std::max(std::sqrt(std::fabs(area)),1.f);
          if(w0<e || w1<e || w2<e)
            continue;
          cls[size_t(z)*N+x] |= bit;
          }
      }
    }

  // distance (in pixels) to the nearest swamp pixel and to the nearest plain water pixel, a two pass chamfer
  auto distance = [&](uint8_t mask, std::vector<float>& d) {
    const float inf = 1e9f;
    d.assign(size_t(N)*N, inf);
    for(size_t i=0; i<d.size(); ++i)
      if(cls[i] & mask)
        d[i] = 0;
    auto at = [&](int x, int z) -> float& { return d[size_t(z)*N+x]; };
    for(int z=0; z<N; ++z)
      for(int x=0; x<N; ++x) {
        float v = at(x,z);
        if(x>0)          v = std::min(v, at(x-1,z)   + 1.f);
        if(z>0)          v = std::min(v, at(x,z-1)   + 1.f);
        if(x>0 && z>0)   v = std::min(v, at(x-1,z-1) + 1.414f);
        if(x<N-1 && z>0) v = std::min(v, at(x+1,z-1) + 1.414f);
        at(x,z) = v;
        }
    for(int z=N-1; z>=0; --z)
      for(int x=N-1; x>=0; --x) {
        float v = at(x,z);
        if(x<N-1)          v = std::min(v, at(x+1,z)   + 1.f);
        if(z<N-1)          v = std::min(v, at(x,z+1)   + 1.f);
        if(x<N-1 && z<N-1) v = std::min(v, at(x+1,z+1) + 1.414f);
        if(x>0 && z<N-1)   v = std::min(v, at(x-1,z+1) + 1.414f);
        at(x,z) = v;
        }
    };
  std::vector<float> dSwamp, dWater;
  distance(1, dSwamp);
  distance(2, dWater);

  // swamp 1, water 0; in the strips: by the distances to both sides
  std::vector<float> map(size_t(N)*N, 0.f);
  for(size_t i=0; i<map.size(); ++i) {
    if(cls[i] & 1)
      map[i] = 1.f;
    else if(cls[i] & 2)
      map[i] = 0.f;
    else if(cls[i] & 4) {
      const float a = dSwamp[i], b = dWater[i];
      const float t = (a>=1e8f && b>=1e8f) ? 1.f : b/(a+b);
      map[i] = t*t*(3.f - 2.f*t);
      }
    }
  dSwamp.clear(); dWater.clear(); cls.clear();

  // blur: a box blur three times (close to a gauss), about 3 pixels each
  std::vector<float> tmp(map.size());
  const int R = 3;
  for(int pass=0; pass<3; ++pass) {
    for(int z=0; z<N; ++z) {
      float acc = 0;
      for(int x=-R; x<=R; ++x)
        acc += map[size_t(z)*N + std::clamp(x,0,N-1)];
      for(int x=0; x<N; ++x) {
        tmp[size_t(z)*N+x] = acc/float(2*R+1);
        acc += map[size_t(z)*N + std::min(x+R+1,N-1)] - map[size_t(z)*N + std::max(x-R,0)];
        }
      }
    for(int x=0; x<N; ++x) {
      float acc = 0;
      for(int z=-R; z<=R; ++z)
        acc += tmp[size_t(std::clamp(z,0,N-1))*N+x];
      for(int z=0; z<N; ++z) {
        map[size_t(z)*N+x] = acc/float(2*R+1);
        acc += tmp[size_t(std::min(z+R+1,N-1))*N+x] - tmp[size_t(std::max(z-R,0))*N+x];
        }
      }
    }

  Pixmap pm(uint32_t(N), uint32_t(N), TextureFormat::R8);
  auto* dst = reinterpret_cast<uint8_t*>(pm.data());
  for(size_t i=0; i<map.size(); ++i)
    dst[i] = uint8_t(std::clamp(map[i], 0.f, 1.f)*255.f + 0.5f);

  swampTex = Resources::loadTexturePm(pm);
  swampMap = Vec4(minX, minZ, 1.f/sx, 1.f/sz);
  hasSwamp = true;
  Log::i("[Swamp] mask built");
  }
