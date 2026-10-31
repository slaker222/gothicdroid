#pragma once

#include <string>
#include <memory>
#include <thread>
#include <vector>
#include <cmath>
#include <algorithm>

#include <Tempest/Signal>
#include <Tempest/Dir>

#include <zenkit/DaedalusVm.hh>

#include "game/gamesession.h"
#include "world/world.h"
#include "ui/documentmenu.h"
#include "ui/chapterscreen.h"
#include "utils/versioninfo.h"
#include "sound/soundfx.h"

class VersionInfo;
class CameraDefinitions;
class SoundDefinitions;
class VisualFxDefinitions;
class ParticlesDefinitions;
class MusicDefinitions;
class FightAi;
class IniFile;

class Gothic final {
  public:
    Gothic();
    ~Gothic();

    static Gothic& inst();

    enum class LoadState:int {
      Idle       = 0,
      Loading    = 1,
      Saving     = 2,
      Finalize   = 3,
      FailedLoad = 4,
      FailedSave = 5
      };

    struct Options {
      bool     doRayQuery        = false;
      bool     doRtGi            = false;
      bool     doMeshShading     = false;
      bool     doBindless        = false;
      bool     doVirtualShadow   = false;
      bool     doSoftwareShadow  = false;
      bool     doSoftwareRT      = false;
      uint32_t swRenderingPreset = 0;

      uint32_t aaPreset          = 0;

      bool     hideFocus         = false;
      float    cameraFov         = 67.5f;
      int      fpsLimit          = 0;
      float    interfaceScale    = 1;
      int      inventoryCellSize = 70;

      Tempest::Size newChapterSize = {800, 600};

      Tempest::Size saveGameImageSize = {0, 0};

      bool     showHealthBar     = true;
      uint8_t  showManaBar       = 2;
      uint8_t  showSwimBar       = 1;
      };

    auto         version() const -> const VersionInfo&;

    bool         isInGame() const;
    bool         isInGameAndAlive() const;

    std::string_view defaultWorld()       const;
    std::string_view defaultPlayer()      const;
    std::string_view defaultSave()        const;
    std::string_view defaultGameDatFile() const;
    std::string_view defaultOutputUnits() const;

    std::string_view menuMain() const;

    void         setGame(std::unique_ptr<GameSession> &&w);
    auto         clearGame() -> std::unique_ptr<GameSession>;

    auto         gameSession() const -> const GameSession*;
    auto         gameSession() -> GameSession*;
    const World* world() const;
    World*       world();
    WorldView*   worldView() const;
    Npc*         player();
    Camera*      camera();
    auto         questLog() const -> const QuestLog*;

    void         setupGlobalScripts();

    auto         loadingBanner() const -> const Tempest::Texture2d*;
    int          loadingProgress() const;
    void         setLoadingProgress(int v);

    SoundFx*     loadSoundFx   (std::string_view name);
    SoundFx*     loadSoundWavFx(std::string_view name);

    auto         loadParticleFx(std::string_view name, bool relaxed=false) -> const ParticleFx*;
    auto         loadParticleFx(const ParticleFx* base, const VisualFx::Key* key) -> const ParticleFx*;
    auto         loadVisualFx  (std::string_view name) -> const VisualFx*;

    void         emitGlobalSound(std::string_view   sfx);
    void         emitGlobalSound(const SoundFx*     sfx);
    void         emitGlobalSound(const Tempest::Sound& sfx);

    void         emitGlobalSoundWav(std::string_view wav, float volume = 1.f);

    static auto  invCatOrder()  -> const std::vector<ItmFlags>&;

    void         pushPause();
    void         popPause();
    bool         isPause() const;

    bool         isMarvinEnabled() const;
    void         setMarvinEnabled(bool m);

    static auto  options() -> const Options&;
    static float interfaceScale(const Tempest::Widget* w);

    bool         isGodMode() const { return godMode; }
    void         setGodMode(bool g) { godMode = g; }

    void         toggleDesktop() { desktop = !desktop; }
    bool         isDesktop() { return desktop; }

    bool         doFrate() const { return showFpsCounter; }
    void         setFRate(bool f) { showFpsCounter = f; }

    bool         doClock() const { return showTime; }
    void         setClock(bool t) { showTime = t; }

    bool         doVobBox() const { return vobBox; }
    void         setVobBox(bool v) { vobBox = v; }

    bool         isBenchmarkMode() const;
    bool         isBenchmarkModeCi() const;
    void         setBenchmarkMode(Benchmark b);

    Tempest::Signal<void()> toggleGi, toggleVsm, toggleRtsm;

    LoadState    checkLoading() const;
    bool         finishLoading();
    void         startLoad(std::string_view banner, const std::function<std::unique_ptr<GameSession>(std::unique_ptr<GameSession>&&)> f);
    void         startSave(Tempest::Texture2d&& tex, const std::function<std::unique_ptr<GameSession>(std::unique_ptr<GameSession>&&)> f);
    void         cancelLoading();

    void         tick(uint64_t dt);

    void         updateAnimation(uint64_t dt);
    void         quickSave();
    void         quickLoad();
    void         save(std::string_view slot, std::string_view usrName);
    void         load(std::string_view slot);

    auto         updateDialog(const GameScript::DlgChoice& dlg, Npc& player, Npc& npc) -> std::vector<GameScript::DlgChoice>;
    void         dialogExec  (const GameScript::DlgChoice& dlg, Npc& player, Npc& npc);

    void         openDialogPipe (Npc& player, Npc& npc, AiOuputPipe*& pipe);
    bool         isNpcInDialog(const Npc& npc) const;
    bool         isInDialog() const;

    Tempest::Signal<void(std::string_view)>                             onStartGame;
    Tempest::Signal<void(std::string_view)>                             onLoadGame;
    Tempest::Signal<void(std::string_view,std::string_view)>            onSaveGame;

    Tempest::Signal<void(Npc&,Npc&,AiOuputPipe*&)>                      onDialogPipe;
    std::function<bool(const Npc*)>                                     isNpcInDialogFn;

    Tempest::Signal<void(std::string_view,int,int,int,const GthFont&)>  onPrintScreen;
    Tempest::Signal<void(std::string_view)>                             onPrint;
    Tempest::Signal<void(std::string_view)>                             onVideo;

    Tempest::Signal<void(const ChapterScreen::Show&)>                   onIntroChapter;
    Tempest::Signal<void(const DocumentMenu::Show&)>                    onShowDocument;
    Tempest::Signal<void()>                                             onWorldLoaded;
    Tempest::Signal<void()>                                             onStartLoading;
    Tempest::Signal<void()>                                             onSessionExit;
    Tempest::Signal<void()>                                             onSettingsChanged;

    Tempest::Signal<void()>                                             onBenchmarkFinished;

    std::string_view                      messageFromSvm(std::string_view id, int voice) const;
    std::string_view                      messageByName (std::string_view id) const;
    uint32_t                              messageTime   (std::string_view id) const;

    static std::u16string                 nestedPath(const std::initializer_list<const char16_t*> &name, Tempest::Dir::FileType type);
    std::unique_ptr<zenkit::DaedalusVm>   createPhoenixVm(std::string_view datFile, const ScriptLang lang = ScriptLang::NONE);
    zenkit::DaedalusScript                loadScript(std::string_view datFile, const ScriptLang lang);
    void                                  setupCommonScriptClasses(zenkit::DaedalusScript &sc);
    void                                  setupVmCommonApi(zenkit::DaedalusVm &vm);

    static const FightAi&                 fai();
    static const SoundDefinitions&        sfx();
    static const MusicDefinitions&        musicDef();
    static const CameraDefinitions&       cameraDef();

    static bool                           settingsHasSection(std::string_view sec);
    static int                            settingsGetI(std::string_view sec, std::string_view name);
    static void                           settingsSetI(std::string_view sec, std::string_view name, int val);
    static std::string_view               settingsGetS(std::string_view sec, std::string_view name);
    static void                           settingsSetS(std::string_view sec, std::string_view name, std::string_view val);
    static float                          settingsGetF(std::string_view sec, std::string_view name);
    static void                           settingsSetF(std::string_view sec, std::string_view name, float val);
    static void                           flushSettings();
    // several settings at once: onSettingsChanged is emitted once, at the end
    static void                           settingsBeginBatch();
    static void                           settingsEndBatch();

    // web shadows "ultra low": a translucent circle under every npc instead of shadow maps
    bool                                  blobShadows() const { return blobShadow; }
    // web water quality ultra high: the swamp water shading
    bool                                  webSwampWater() const { return swampWater; }
    // web water physics: rings on the water around a wading / swimming / landing character
    bool                                  webWaterPhysics() const { return waterPhysics; }
    // web: blood from the hit NPC (the emitter of its guild from the script)
    bool                                  webBlood() const { return blood; }
    // web fog setting: <0 = off, >0 = soft haze (1.6) / dense haze (3); 0 = the engine fog (other platforms)
    float                                 webFogAmount() const { return fogAmount; }
    // fog distances relative to the unlimited world (1), smaller for a shorter world draw distance
    float                                 webFogScale() const { return fogScale; }
    // web sun setting: 0 = disc with halo and light shafts, 1 = the same without the shafts, 2 = no sun
    int                                   webSunMode() const { return sunMode; }
    // web barrier setting: 0 = classic (45 s at the start, again every 36 minutes), 1 = always, 2 = off; sound volume 0.2..1
    int                                   webBarrierMode()   const { return barrierMode; }
    float                                 webBarrierVolume() const { return barrierVolume; }

    // lethal zones of the barrier (the game data has none): boxes by two corners, any height. [INTERNAL] webBarrierZones
    // a convex quadrangle in the ground plane (counter-clockwise), any height
    struct BarrierZone {
      float x[4], z[4];
      bool  contains(float px, float pz) const {
        for(int i=0; i<4; ++i) {
          const int j = (i+1)&3;
          if((x[j]-x[i])*(pz-z[i]) - (z[j]-z[i])*(px-x[i]) < 0)
            return false;
          }
        return true;
        }
      // distance to the zone in the ground plane, 0 inside
      float distance(float px, float pz) const {
        if(contains(px,pz))
          return 0;
        float best = 1e30f;
        for(int i=0; i<4; ++i) {
          const int   j  = (i+1)&3;
          const float ex = x[j]-x[i], ez = z[j]-z[i];
          const float l2 = ex*ex+ez*ez;
          float       t  = l2>0 ? ((px-x[i])*ex+(pz-z[i])*ez)/l2 : 0;
          t = t<0 ? 0 : (t>1 ? 1 : t);
          const float dx = px-(x[i]+ex*t), dz = pz-(z[i]+ez*t);
          best = std::min(best,dx*dx+dz*dz);
          }
        return std::sqrt(best);
        }
      // the nearest point of the border and the unit direction out of the zone there (from the point to the outside)
      void  nearest(float px, float pz, float& qx, float& qz, float& ox, float& oz) const {
        float best = 1e30f;
        qx = px; qz = pz; ox = 0; oz = 1;
        for(int i=0; i<4; ++i) {
          const int   j  = (i+1)&3;
          const float ex = x[j]-x[i], ez = z[j]-z[i];
          const float l2 = ex*ex+ez*ez;
          if(l2<1)
            continue;
          float t = ((px-x[i])*ex+(pz-z[i])*ez)/l2;
          t = t<0 ? 0 : (t>1 ? 1 : t);
          const float cx = x[i]+ex*t, cz = z[i]+ez*t;
          const float d2 = (px-cx)*(px-cx)+(pz-cz)*(pz-cz);
          if(d2>=best)
            continue;
          best = d2;
          qx = cx; qz = cz;
          const float l = std::sqrt(l2);
          ox = ez/l;  // outward normal of a counter-clockwise polygon
          oz = -ex/l;
          if(d2>1) { // outside: from the border to the point
            const float d = std::sqrt(d2);
            ox = (px-cx)/d;
            oz = (pz-cz)/d;
            }
          }
        }
      // direction out of the zone through the nearest side (unit length), for a point inside
      void  outward(float px, float pz, float& ox, float& oz) const {
        float best = 1e30f;
        ox = 0; oz = 1;
        for(int i=0; i<4; ++i) {
          const int   j  = (i+1)&3;
          const float ex = x[j]-x[i], ez = z[j]-z[i];
          const float l  = std::sqrt(ex*ex+ez*ez);
          if(l<1)
            continue;
          const float d = ((x[j]-x[i])*(pz-z[i]) - (z[j]-z[i])*(px-x[i]))/l; // distance to the side, positive inside
          if(d<best) {
            best = d;
            ox   = ez/l;   // the outward normal of a counter-clockwise polygon
            oz   = -ex/l;
            }
          }
        }
      };
    // n = 3 or 4 points in any order to a zone (the points go around the centre)
    static BarrierZone                    webBarrierMakeZone(const float* x, const float* z, int n);
    const std::vector<BarrierZone>&       webBarrierZones() const { return barrierZones; }
    // each call stores a corner (returns the number of corners so far), the fourth one closes the zone (returns 4)
    int                                   webBarrierMark(float x, float z);
    // game time (ms) of the last strike of the barrier on the player, 0 - none yet
    // 0..1: how close the player is to a zone (the blue glow), set by the world every tick
    float                                 webBarrierWarn() const { return barrierWarn; }
    void                                  webBarrierWarn(float v) { barrierWarn = v; }
    uint64_t                              webBarrierHitAt() const { return barrierHitAt; }
    void                                  webBarrierHit(uint64_t tick) { barrierHitAt = tick; }
    bool                                  webBarrierZonesShown() const { return barrierZoneShow; }
    // corners of the zone being marked; returns their number
    int                                   webBarrierPending(float* x, float* z) const {
      for(int i=0; i<barrierCorners; ++i) { x[i] = barrierCornerX[i]; z[i] = barrierCornerZ[i]; }
      return barrierCorners;
      }
    void                                  webBarrierUndo();
    void                                  webBarrierClear();

  private:
    VersionInfo                             vinfo;
    Options                                 opts;
    std::mt19937                            randGen;
    uint16_t                                pauseSum=0;
    bool                                    isMarvin       = false;
    bool                                    godMode        = false;
    bool                                    desktop        = false;
    bool                                    showFpsCounter = false;
    int                                     settingsBatch  = 0;
    bool                                    settingsBatchDirty = false;
    bool                                    blobShadow     = false;
    bool                                    swampWater     = false;
    bool                                    waterPhysics   = false;
    bool                                    blood          = false;
    float                                   fogAmount      = 0;
    float                                   fogScale       = 1;
    int                                     sunMode        = 0;
    int                                     barrierMode    = 1;
    float                                   barrierVolume  = 1;
    std::vector<BarrierZone>                barrierZones;
    bool                                    barrierZoneShow = false;
    uint64_t                                barrierHitAt   = 0;
    float                                   barrierWarn    = 0;
    int                                     barrierCorners = 0;
    float                                   barrierCornerX[4] = {}, barrierCornerZ[4] = {};
    void                                    saveBarrierZones();
    bool                                    showTime       = false;
    bool                                    vobBox         = false;
    Benchmark                               isBenchmark    = Benchmark::None;

    std::string                             wrldDef, plDef, gameDatDef, ouDef;

    std::unique_ptr<IniFile>                defaults;
    std::unique_ptr<IniFile>                baseIniFile;
    std::unique_ptr<IniFile>                iniFile;
    std::unique_ptr<IniFile>                modFile;
    std::unique_ptr<IniFile>                systemPackIniFile;

    Tempest::Texture2d                      loadTex;
    Tempest::Texture2d                      saveTex;
    std::atomic_int                         loadProgress{0};
    std::thread                             loaderTh;
    std::atomic<LoadState>                  loadingFlag{LoadState::Idle};

    std::unique_ptr<GameSession>            game, pendingGame;
    std::unique_ptr<FightAi>                fight;
    std::unique_ptr<CameraDefinitions>      camDef;
    std::unique_ptr<SoundDefinitions>       soundDef;
    std::unique_ptr<VisualFxDefinitions>    vfxDef;
    std::unique_ptr<ParticlesDefinitions>   particleDef;
    std::unique_ptr<MusicDefinitions>       music;

    std::mutex                              syncSnd;
    Tempest::SoundDevice                    sndDev;
    std::unordered_map<std::string,SoundFx> sndFxCache;
    std::unordered_map<std::string,SoundFx> sndWavCache;
    std::vector<Tempest::SoundEffect>       sndStorage;

    std::vector<std::unique_ptr<DocumentMenu::Show>> documents;
    ChapterScreen::Show                     chapter;
    bool                                    pendingChapter=false;

    std::vector<ItmFlags>                   inventoryOrder;

    static Gothic*                          instance;

#if defined(__EMSCRIPTEN__)
    void                                    webRunLoadSave(const std::function<std::unique_ptr<GameSession>(std::unique_ptr<GameSession>&&)>& f,
                                                           GameSession* g, LoadState one);
#endif
    void                                    implStartLoadSave(std::string_view banner,
                                                              bool load,
                                                              const std::function<std::unique_ptr<GameSession>(std::unique_ptr<GameSession>&&)> f);

    void                                    detectGothicVersion();
    void                                    setupSettings();

    auto                                    getDocument(int id) -> std::unique_ptr<DocumentMenu::Show>&;

    static void                             notImplementedRoutine(std::string_view fn);

    static std::string                      concatstrings     (std::string_view a, std::string_view b);
    static std::string                      inttostring       (int i);
    static std::string                      floattostring     (float f);
    static int                              floattoint        (float f);
    static float                            inttofloat        (int i);

    static bool                             hlp_strcmp        (std::string_view a, std::string_view b);
    int                                     hlp_random        (int max);

    void                                    introducechapter  (std::string_view title, std::string_view subtitle, std::string_view img, std::string_view sound, int time);
    bool                                    playvideo         (std::string_view name);
    bool                                    playvideoex       (std::string_view name, bool screenBlend, bool exitSession);
    bool                                    printscreen       (std::string_view msg, int posx, int posy, std::string_view font, int timesec);
    bool                                    printdialog       (int dialognr, std::string_view msg, int posx, int posy, std::string_view font, int timesec);
    void                                    print             (std::string_view msg);

    int                                     doc_create        ();
    int                                     doc_createmap     ();
    void                                    doc_setpage       (int handle, int page, std::string_view img, int scale);
    void                                    doc_setpages      (int handle, int count);
    void                                    doc_printline     (int handle, int page, std::string_view text);
    void                                    doc_printlines    (int handle, int page, std::string_view text);
    void                                    doc_setmargins    (int handle, int page, int left, int top, int right, int bottom, int mul);
    void                                    doc_setfont       (int handle, int page, std::string_view font);
    void                                    doc_setlevel      (int handle, std::string_view level);
    void                                    doc_setlevelcoords(int handle, int left, int top, int right, int bottom);
    void                                    doc_show          (int handle);

    void                                    exitgame          ();

    void                                    printdebug        (std::string_view msg);
    void                                    printdebugch      (int ch, std::string_view msg);
    void                                    printdebuginst    (std::string_view msg);
    void                                    printdebuginstch  (int ch, std::string_view msg);
  };
