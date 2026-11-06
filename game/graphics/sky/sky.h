#pragma once

#include <Tempest/Matrix4x4>
#include <Tempest/Texture2d>

#include "graphics/lightsource.h"
#include "graphics/sceneglobals.h"
#include "resources.h"

class World;

class Sky final {
  public:
    struct State final {
      const Tempest::Texture2d* lay[2] = {};
      };

    Sky(const SceneGlobals& scene, const World& world);
    ~Sky();

    void updateLight(const int64_t now);
    void reload() { loadTextures(); }   // the setting webSky changed: the textures again, the world stays

    const LightSource&        sunLight()         const { return sun;     }
    const Tempest::Vec3&      ambientLight()     const { return ambient; }
    float                     sunIntensity()     const { return GSunIntensity;  }
    float                     moonIntensity()    const { return GMoonIntensity; }

    const Tempest::Texture2d& sunImage()  const { return *sunImg;  }
    const Tempest::Texture2d& moonImage() const { return *moonImg; }

    const Tempest::Texture2d* barrierImage() const { return barrierImg; }

    const State&              cloudsDay()   const { return clouds[0]; }
    const State&              cloudsNight() const { return clouds[1]; }
    Tempest::Vec2             cloudsOffset(int layer) const;
    float                     isNight() const;

  private:
    void                          loadTextures();
    const Tempest::Texture2d*     skyTexture(std::string_view name, bool day, size_t id);
    const Tempest::Texture2d*     implSkyTexture(std::string_view name, bool day, size_t id, bool fromOther = false);

    LightSource                   sun;
    Tempest::Vec3                 ambient;

    const SceneGlobals&           scene;
    std::string                   worldName;
    int                           skyGame = 0;   // 1 / 2: whose sky
    bool                          blue  = false; // webSky 3: the layers of the New World of Gothic 2 over any world
    bool                          other = false; // the sky is taken from the other game's folder (setting webSky)
    State                         clouds[2]; //day, night;

    const Tempest::Texture2d*     sunImg   = &Resources::fallbackBlack();
    const Tempest::Texture2d*     moonImg  = &Resources::fallbackBlack();

    const Tempest::Texture2d*     barrierImg = nullptr; // Gothic 1 only

    float                         GSunIntensity  = 0;
    float                         GMoonIntensity = 0;
  };
