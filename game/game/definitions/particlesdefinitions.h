#pragma once

#include <zenkit/DaedalusVm.hh>
#include <zenkit/addon/daedalus.hh>

#include <unordered_map>
#include <memory>
#include <mutex>

#include "graphics/visualfx.h"

class ParticleFx;

class ParticlesDefinitions final {
  public:
    ParticlesDefinitions();
    ~ParticlesDefinitions();

    const ParticleFx* get(std::string_view name, bool relaxed);
    const ParticleFx* get(const ParticleFx* base, const VisualFx::Key* key);
    // blood variants (cached copies): 1 = big splash (more and larger particles, flies further), 2 = long bleeding (1.5 s)
    const ParticleFx* getBloodVariant(const ParticleFx* base, int kind);

  private:
    std::recursive_mutex                sync;
    std::unique_ptr<zenkit::DaedalusVm> vm;

    std::unordered_map<std::string,          std::unique_ptr<ParticleFx>> pfx;
    std::unordered_map<const VisualFx::Key*, std::unique_ptr<ParticleFx>> pfxKey;
    std::unordered_map<uint64_t,             std::unique_ptr<ParticleFx>> pfxBlood;

    const ParticleFx* implGet(std::string_view name, bool relaxed);
    const ParticleFx* implGet(const ParticleFx& base, const VisualFx::Key& key);

    std::shared_ptr<zenkit::IParticleEffect> implGetDirect(std::string_view name, bool relaxed);
  };
