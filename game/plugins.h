#pragma once

// A parody of the Union plugins (Hedin's AutoQuickLoot, ScreenInfo): switched on / off and set from the page
// (the green "Plugins" button of the touch controls, the "More" panel). The values are set from the page's thread and
// read by the game thread.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>

#include <Tempest/Color>
#include <Tempest/Painter>

#include "game/constants.h"
#include "utils/gthfont.h"

struct Plugins final {
  static inline std::atomic<int> autoLoot   {0};     // take the loot of a body / a chest at once, no animation, no window
  static inline std::atomic<int> autoPick   {0};     // ... and without a button: things and bodies near the player are taken by themselves
  static inline std::atomic<int> autoRadius {2};     // metres
  static inline std::atomic<int> steal      {0};     // auto also takes what belongs to somebody (a theft); off: such things are left
  static inline std::atomic<int> takeWeapons{0};     // the weapons a dead one holds (equipped, the game does not offer them) are taken too
  static inline std::atomic<int> miniMap    {0};     // the plugin MiniMap: a map opens as a small window in the top right corner, the game goes on
  static inline std::atomic<int> miniClose  {0};     // set by the page (the cross over the small map): closes it
  static inline std::atomic<int> miniCross  {0};     // where the cross is (the centre, permille of the window: x | y<<10), 0 - no small map
  static inline std::atomic<int> screenInfo {0};     // the day and the time in the corner, the numbers on the bars
  static inline std::atomic<int> timeScale  {100};   // the speed of the game time, percent (x1 = 100)

  struct LootLine final {
    std::string    text;
    Tempest::Color color;
    float          ttl = 0;
    };

  // the colour of the line of a taken item: by its category
  static Tempest::Color lootColor(uint32_t flag, bool gold) {
    if(gold)
      return Tempest::Color(1.00f,0.85f,0.20f,1.f);
    if(flag & (ITM_CAT_NF|ITM_CAT_FF|ITM_CAT_MUN))
      return Tempest::Color(1.00f,0.45f,0.35f,1.f);   // weapons and ammunition: red
    if(flag & ITM_CAT_ARMOR)
      return Tempest::Color(0.45f,0.72f,1.00f,1.f);   // armour: blue
    if(flag & ITM_CAT_FOOD)
      return Tempest::Color(0.55f,0.90f,0.40f,1.f);   // food: green
    if(flag & ITM_CAT_POTION)
      return Tempest::Color(0.95f,0.50f,0.95f,1.f);   // potions: pink
    if(flag & (ITM_CAT_RUNE))
      return Tempest::Color(0.65f,0.55f,1.00f,1.f);   // runes and scrolls: violet
    if(flag & ITM_CAT_MAGIC)
      return Tempest::Color(0.40f,0.90f,0.90f,1.f);   // rings and amulets: cyan
    if(flag & ITM_CAT_DOCS)
      return Tempest::Color(0.92f,0.80f,0.55f,1.f);   // documents, maps: parchment
    if(flag & ITM_CAT_LIGHT)
      return Tempest::Color(1.00f,0.65f,0.20f,1.f);   // torches: orange
    return Tempest::Color(0.85f,0.85f,0.85f,1.f);     // the rest: grey
    }

  static void addLoot(std::string text, Tempest::Color c) {
    std::lock_guard<std::mutex> lk(mutex());
    auto& v = lines();
    v.push_back({std::move(text),c,5.f});
    if(v.size()>10)
      v.erase(v.begin());
    }

  // dt in ms; true while there is something to draw
  static bool tick(uint64_t dt) {
    std::lock_guard<std::mutex> lk(mutex());
    auto& v = lines();
    if(v.empty())
      return false;
    for(auto& l:v)
      l.ttl -= float(dt)/1000.f;
    v.erase(std::remove_if(v.begin(),v.end(),[](const LootLine& l){ return l.ttl<=0; }),v.end());
    return true;
    }

  // the taken things, newest at the bottom, at the left edge; they fade out in the last second
  static void paintLoot(Tempest::Painter& p, const GthFont& fnt, int /*w*/, int y) {
    std::lock_guard<std::mutex> lk(mutex());
    for(auto& l:lines()) {
      auto  c  = l.color;
      float a  = std::min(1.f,l.ttl);
      auto  sz = fnt.textSize(l.text);
      int   x  = 10;
      fnt.tinted(Tempest::Color(0.f,0.f,0.f,a*0.8f)).drawText(p,x+1,y+1,l.text);
      fnt.tinted(Tempest::Color(c.r(),c.g(),c.b(),a)).drawText(p,x,y,l.text);
      y += sz.h+2;
      }
    }

  private:
    static std::vector<LootLine>& lines() { static std::vector<LootLine> v; return v; }
    static std::mutex&            mutex() { static std::mutex m; return m; }
  };
