#include "particlesdefinitions.h"

#include <Tempest/Log>

#include "graphics/pfx/particlefx.h"
#include "utils/fileext.h"
#include "gothic.h"

using namespace Tempest;

ParticlesDefinitions::ParticlesDefinitions() {
  vm = Gothic::inst().createPhoenixVm("ParticleFx.dat");
  }

ParticlesDefinitions::~ParticlesDefinitions() {
  }

const ParticleFx* ParticlesDefinitions::get(std::string_view name, bool relaxed) {
  if(name.empty())
    return nullptr;

  while(FileExt::hasExt(name,"PFX"))
    name = name.substr(0,name.size()-4);

  std::lock_guard<std::recursive_mutex> guard(sync);
  return implGet(name,relaxed);
  }

const ParticleFx* ParticlesDefinitions::get(const ParticleFx* base, const VisualFx::Key* key) {
  if(base==nullptr || key==nullptr)
    return base;
  std::lock_guard<std::recursive_mutex> guard(sync);
  return implGet(*base,*key);
  }

const ParticleFx* ParticlesDefinitions::getBloodVariant(const ParticleFx* base, int kind) {
  if(base==nullptr || kind<=0)
    return base;
  std::lock_guard<std::recursive_mutex> guard(sync);
  const uint64_t k = (uint64_t(reinterpret_cast<uintptr_t>(base))<<2) | uint64_t(kind&3);
  auto it = pfxBlood.find(k);
  if(it!=pfxBlood.end())
    return it->second.get();

  std::unique_ptr<ParticleFx> p{new ParticleFx(*base)};
  if(kind==1) {
    p->ppsValue    *= 2.5f;
    p->visSizeStart = p->visSizeStart*1.7f;
    p->velAvg      *= 1.35f;
    p->velVar      *= 1.35f;
    } else {
    // bleeding: the same particles, but the emitter lives 1.5 s with a fading rate
    const float burst = base->prefferedTime==0 ? base->ppsValue*1000.f : base->maxPps();
    p->ppsValue        = burst*0.8f;
    p->ppsScaleKeys    = {1.f,0.8f,0.6f,0.4f,0.25f,0.1f};
    p->ppsFPS          = 4.f;
    p->ppsIsLooping    = false;
    p->ppsIsSmooth     = true;
    p->prefferedTime   = 1500;
    p->visSizeStart    = p->visSizeStart*1.3f;
    }
  auto elt = pfxBlood.insert(std::make_pair(k,std::move(p)));
  return elt.first->second.get();
  }

const ParticleFx* ParticlesDefinitions::implGet(std::string_view name, bool relaxed) {
  auto cname = std::string(name);
  auto it    = pfx.find(cname);
  if(it!=pfx.end())
    return it->second.get();
  auto decl=implGetDirect(name, relaxed);
  if(!decl)
    return nullptr;
  std::unique_ptr<ParticleFx> p{new ParticleFx(*decl,name)};
  auto elt = pfx.insert(std::make_pair(std::move(cname),std::move(p)));

  return elt.first->second.get();
  }

const ParticleFx* ParticlesDefinitions::implGet(const ParticleFx& base, const VisualFx::Key& key) {
  auto it = pfxKey.find(&key);
  if(it!=pfxKey.end())
    return it->second.get();

  std::unique_ptr<ParticleFx> p{new ParticleFx(base,key)};
  auto elt = pfxKey.insert(std::make_pair(&key,std::move(p)));

  return elt.first->second.get();
  }

std::shared_ptr<zenkit::IParticleEffect> ParticlesDefinitions::implGetDirect(std::string_view name, bool relaxed) {
  if(!vm || name.empty())
    return nullptr;

  auto id = vm->find_symbol_by_name(name);
  if(id==nullptr) {
    if(!relaxed)
      Log::e("invalid particle system: \"",name,"\"");
    return nullptr;
    }

  auto ret = std::make_shared<zenkit::IParticleEffect>();
  ret->vis_tex_is_quadpoly = 1; // seem to be default
  vm->init_instance(ret, id);
  return ret;
  }
