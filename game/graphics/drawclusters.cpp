#include "drawclusters.h"
#include "shaders.h"

#include "graphics/mesh/submesh/staticmesh.h"
#include "graphics/mesh/submesh/animmesh.h"

#include <limits>
#include <algorithm>

using namespace Tempest;

DrawClusters::DrawClusters() {
  scratch.header.reserve(1024);
  scratch.patch .reserve(1024);
  }

DrawClusters::~DrawClusters() {
  }

uint32_t DrawClusters::alloc(const PackedMesh::Cluster* cx, size_t firstMeshlet, size_t meshletCount, uint16_t bucketId, uint16_t commandId) {
  if(commandId==uint16_t(-1))
    return uint32_t(-1);

  const auto ret = implAlloc(meshletCount);
  for(size_t i=0; i<meshletCount; ++i) {
    Cluster c;
    c.pos          = cx[i].pos;
    c.r            = cx[i].r;
    c.bucketId     = bucketId;
    c.commandId    = commandId;
    c.firstMeshlet = uint32_t(firstMeshlet + i);
    c.meshletCount = 1;
    c.instanceId   = uint32_t(-1);

    clusters[ret+i] = c;
    }

  clustersDurty.resize((clusters.size() + 32 - 1)/32);
  markClusters(ret, meshletCount);
  return uint32_t(ret);
  }

uint32_t DrawClusters::alloc(const Bucket& bucket, size_t firstMeshlet, size_t meshletCount, uint16_t bucketId, uint16_t commandId) {
  const auto ret = implAlloc(1);

  Cluster& c = clusters[ret];
  if(bucket.staticMesh!=nullptr)
    c.r = bucket.staticMesh->bbox.rConservative + bucket.mat.waveMaxAmplitude; else
    c.r = bucket.animMesh->bbox.rConservative;
  c.bucketId     = bucketId;
  c.commandId    = commandId;
  c.firstMeshlet = uint32_t(firstMeshlet);
  c.meshletCount = uint32_t(meshletCount);
  c.instanceId   = uint32_t(-1);

  clustersDurty.resize((clusters.size() + 32 - 1)/32);
  markClusters(ret, 1);
  return uint32_t(ret);
  }

void DrawClusters::free(uint32_t id, uint32_t numCluster) {
  for(size_t i=0; i<numCluster; ++i) {
    clusters[id + i]              = Cluster();
    clusters[id + i].r            = -1;
    clusters[id + i].meshletCount = 0;
    markClusters(id + i);
    }

  Range r = {id, id+numCluster};
  auto at = std::lower_bound(freeList.begin(),freeList.end(),r,[](const Range& l, const Range& r){
    return l.begin<r.begin;
    });
  at = freeList.insert(at,r);
  auto next = at+1;
  if(next!=freeList.end() && at->end==next->begin) {
    next->begin = at->begin;
    at = freeList.erase(at);
    }
  if(at!=freeList.begin() && at->begin==(at-1)->end) {
    auto prev = (at-1);
    prev->end = at->end;
    at = freeList.erase(at);
    }
  }

bool DrawClusters::commit(Encoder<CommandBuffer>& cmd, uint8_t fId) {
  if(!clustersDurtyBit) {
    uploadGroups(fId);
    return false;
    }
  clustersDurtyBit = false;

  std::atomic_thread_fence(std::memory_order_acquire);
  size_t csize = clusters.size()*sizeof(clusters[0]);
  csize = (csize + 0xFFF) & size_t(~0xFFF);

  auto& device = Resources::device();
  if(clustersGpu.byteSize() == csize) {
    updateGroups(false);
    uploadGroups(fId);
    patchClusters(cmd, fId);
    return false;
    }

  Resources::recycle(std::move(clustersGpu));
  clustersGpu  = device.ssbo(Tempest::Uninitialized, csize);
  clustersGpu.update(clusters);
  updateGroups(true);
  uploadGroups(fId);
  std::fill(clustersDurty.begin(), clustersDurty.end(), 0x0);
  return true;
  }

void DrawClusters::updateGroups(bool all) {
  // the dirty bits of the clusters are still set here: the groups of the changed clusters are made again
  const size_t gCount = (clusters.size() + GroupSize - 1)/GroupSize;
  if(groups.size()!=gCount) {
    groups.resize(gCount);
    all = true;
    }

  auto build = [this](size_t g) {
    const float inf = std::numeric_limits<float>::infinity();
    Vec3 mn = Vec3( inf, inf, inf), mx = Vec3(-inf,-inf,-inf);
    const size_t b = g*GroupSize, e = std::min(b+GroupSize, clusters.size());
    for(size_t i=b; i<e; ++i) {
      auto& c = clusters[i];
      if(!(c.r>0.f))
        continue; // disabled or deleted: the shader skips it
      mn = Vec3(std::min(mn.x,c.pos.x-c.r), std::min(mn.y,c.pos.y-c.r), std::min(mn.z,c.pos.z-c.r));
      mx = Vec3(std::max(mx.x,c.pos.x+c.r), std::max(mx.y,c.pos.y+c.r), std::max(mx.z,c.pos.z+c.r));
      }
    groups[g].mn = Vec4(mn.x,mn.y,mn.z,0);
    groups[g].mx = Vec4(mx.x,mx.y,mx.z,0);
    };

  if(all) {
    for(size_t g=0; g<groups.size(); ++g)
      build(g);
    groupGen++;
    return;
    }

  static_assert(GroupSize%32==0);
  constexpr size_t wordsPerGroup = GroupSize/32;
  bool changed = false;
  for(size_t i=0; i<clustersDurty.size(); ++i) {
    if(clustersDurty[i]==0x0)
      continue;
    const size_t g = i/wordsPerGroup;
    build(g);
    i = (g+1)*wordsPerGroup - 1; // the group is done
    changed = true;
    }
  if(changed)
    groupGen++;
  }

void DrawClusters::uploadGroups(uint8_t fId) {
  groupFId = fId;
  if(groupGpuGen[fId]==groupGen && !groupGpu[fId].isEmpty())
    return;
  auto& device = Resources::device();
  static const GroupBox none = {Vec4(1,1,1,0), Vec4(-1,-1,-1,0)}; // empty: no buffer of zero size
  const size_t sz = std::max<size_t>(groups.size(),1)*sizeof(GroupBox);
  if(groupGpu[fId].byteSize()<sz) {
    groupGpu[fId] = groups.empty() ? device.ssbo(BufferHeap::Upload, &none, sizeof(none))
                                   : device.ssbo(BufferHeap::Upload, groups.data(), groups.size()*sizeof(GroupBox));
    } else if(!groups.empty()) {
    groupGpu[fId].update(groups.data(), 0, groups.size()*sizeof(GroupBox));
    }
  groupGpuGen[fId] = groupGen;
  }

size_t DrawClusters::implAlloc(size_t count) {
  size_t bestFit = size_t(-1), bfDiff = size_t(-1);
  for(size_t i=0; i<freeList.size(); ++i) {
    auto f  = freeList[i];
    auto sz = (f.end - f.begin);
    if(sz==count) {
      freeList.erase(freeList.begin()+int(i));
      return f.begin;
      }
    if(sz<count)
      continue;
    if(sz-count < bfDiff)
      bestFit = i;
    }

  if(bestFit != size_t(-1)) {
    auto& f = freeList[bestFit];
    f.end -= count;
    return f.end;
    }

  size_t ret = clusters.size();
  clusters.resize(clusters.size() + count);
  return ret;
  }

void DrawClusters::patchClusters(Encoder<CommandBuffer>& cmd, uint8_t fId) {
  std::vector<uint32_t>& header = scratch.header;
  std::vector<Cluster>&  patch  = scratch.patch;

  header.clear();
  patch.clear();

  for(size_t i=0; i<clustersDurty.size(); ++i) {
    if(clustersDurty[i]==0x0)
      continue;
    const uint32_t mask = clustersDurty[i];
    clustersDurty[i] = 0x0;

    for(size_t r=0; r<32; ++r) {
      if((mask & (1u<<r))==0)
        continue;
      size_t idx = i*32 + r;
      if(idx>=clusters.size())
        continue;
      patch.push_back(clusters[idx]);
      header.push_back(uint32_t(idx));
      }
    }

  if(header.empty())
    return;

  auto& device = Resources::device();
  auto& p = this->patch[fId];

  if(header.size()*sizeof(header[0]) < p.indices.byteSize()) {
    p.indices.update(header);
    } else {
    p.indices = device.ssbo(BufferHeap::Upload, header);
    }

  if(patch.size()*sizeof(patch[0]) < p.data.byteSize()) {
    p.data.update(patch);
    } else {
    p.data = device.ssbo(BufferHeap::Upload, patch);
    }

  const uint32_t count = uint32_t(header.size());
  cmd.setFramebuffer({});
  cmd.setBinding(0, clustersGpu);
  cmd.setBinding(1, p.data);
  cmd.setBinding(2, p.indices);
  cmd.setPushData(count);
  cmd.setPipeline(Shaders::inst().clusterPatch);
  cmd.dispatchThreads(count);
  }

void DrawClusters::markClusters(size_t id, size_t count) {
  for(size_t i=0; i<count; ++i) {
    static_assert(sizeof(std::atomic<uint32_t>)==sizeof(uint32_t));
    auto& bits = clustersDurty[id/32];
    reinterpret_cast<std::atomic<uint32_t>&>(bits).fetch_or(1u << (id%32), std::memory_order_relaxed);
    id++;
    }
  clustersDurtyBit.store(true);
  }
