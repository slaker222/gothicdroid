#pragma once

#include <Tempest/Widget>
#include <atomic>
#include <Tempest/Texture2d>
#include <Tempest/Timer>

#include "graphics/inventoryrenderer.h"
#include "game/inventory.h"

class Npc;
class Item;
class Inventory;
class Interactive;
class World;
class KeyCodec;

class InventoryMenu : public Tempest::Widget {
  public:
    InventoryMenu(const KeyCodec& key);
    ~InventoryMenu();

    enum class State:uint8_t {
      Closed=0,
      Equip,
      Chest,
      Trade,
      Ransack,
      LockPicking
      };

    enum class LootMode:uint8_t {
      Normal=0,
      Stack,
      Ten,
      Hundred
      };

    enum class DrawPass:uint8_t {
      Back,
      Front
      };

    void  close();
    void  open(Npc& pl);
    void  trade(Npc& pl,Npc& tr);
    bool  ransack(Npc& pl,Npc& tr);
    void  open(Npc& pl,Interactive& chest);
    State isOpen() const;
    bool  quickLoot(Npc& pl, const Inventory& inv, Npc* body, Interactive* ch);
    bool  isActive() const;
    void  onWorldChanged();

    void  tick(uint64_t dt);
    void  draw(Tempest::Encoder<Tempest::CommandBuffer>& cmd, float sx = 1.f, float sy = 1.f);
#if defined(OG_WEB_UI)
    // web touch controls: the quick panel under the world view - the melee and ranged weapon and the
    // spells of the number keys 1..0, drawn with the same 3D item icons as the inventory
    bool  quickVisible() const { return quickOpen && !quickSlots.empty() && quickTotal>0; }
    // pos: the ring's position in thousandths of a slot (the page scrolls it smoothly)
    void  quickSetOpen(bool on, int pos) { quickOpen = on; quickPos = float(pos)/1000.f; }
    void  quickUpdate();
    std::string quickJson() const;
    // the dark soft backing under the slots (the page's option), painted in the ui layer under the icons
    void  paintQuickBack(Tempest::Painter& p) const;
    bool  quickBackDirty() { bool d = quickBackMoved; quickBackMoved = false; return d; }
    // the layout of the row from the page (layout editor): visible slots 1..10, the centre of the row in 1/1000 of the
    // window (-1: the default place, bottom middle), the size in % of the default one, the backing on / off.
    // Set from the page's thread (Android), read by the game thread
    // vert: the slots go down instead of to the right; split: two rows (columns when vertical), 8 -> 4 + 4
    static std::atomic<int> quickCfgCols, quickCfgX, quickCfgY, quickCfgK, quickCfgBack, quickCfgVert, quickCfgSplit;
    // a finger on the inventory (the page forwards the touches): 0 down, 1 up, 2 move, 3 cancelled; x, y in
    // the window. A slot is selected by a touch, the second tap in a row uses it, holding it drops it
    void  webPointer(int type, int x, int y);
#endif
    void  paintNumOverlay(Tempest::PaintEvent& e);

    void  keyDownEvent  (Tempest::KeyEvent&   e) override;
    void  keyRepeatEvent(Tempest::KeyEvent&   e) override;
    void  keyUpEvent    (Tempest::KeyEvent&   e) override;

  protected:
    void  paintEvent     (Tempest::PaintEvent& e) override;

    void  mouseDownEvent (Tempest::MouseEvent& event) override;
    void  mouseUpEvent   (Tempest::MouseEvent& event) override;
    void  mouseWheelEvent(Tempest::MouseEvent& event) override;

  private:
    struct Page;
    struct InvPage;
    struct TradePage;
    struct RansackPage;

    struct PageLocal final {
      size_t                  sel    = 0;
      size_t                  scroll = 0;
      };

    const KeyCodec&           keycodec;

    const Tempest::Texture2d* tex =nullptr;
    const Tempest::Texture2d* slot=nullptr;
    const Tempest::Texture2d* selT=nullptr;
    const Tempest::Texture2d* selU=nullptr;

    State                     state      =State::Closed;
    Npc*                      player     =nullptr;
    Npc*                      trader     =nullptr;
    Interactive*              chest      =nullptr;

    std::unique_ptr<Page>     pageOth, pagePl;
    PageLocal                 pageLocal[2];

    uint8_t                   page       =0;
    Tempest::Timer            takeTimer;
    size_t                    takeCount  =0;
    LootMode                  lootMode   =LootMode::Normal;
    InventoryRenderer         renderer;
#if defined(OG_WEB_UI)
    // key: the number key (1..0) of a weapon or spell, -1: an item of the inventory (cls), used by a tap
    struct QuickSlot { int key=-1, x=0, y=0, s=0, line=0; bool on=false; size_t cls=0; uint32_t count=1; int n=-1; const Item* item=nullptr; }; // n: arrows / scrolls left, -1 - not counted
    InventoryRenderer         quickRenderer;
    std::vector<QuickSlot>    quickSlots;
    std::vector<size_t>       quickSig;
    bool                      quickOpen = false;
    int                       quickW = 0, quickH = 0;
    int                       quickFirst = 0, quickCols = 1, quickTotal = 0;
    float                     quickPos = 0;
    int                       quickX0 = 0, quickY = 0, quickSz = 0, quickPitch = 0;
    int                       quickRowW = 0;
    // the layout: vertical or not, rows (1..2), slots in the first row; the box of the slots (without the inventory block)
    bool                      quickVert = false;
    int                       quickLines = 1, quickPer = 4, quickBoxW = 0, quickBoxH = 0;
    // the box of a row (it cuts the slots scrolling out of it)
    void  quickLineBox(int line, int& x, int& y, int& w, int& h) const;
    std::vector<int>          quickGeo;           // the place of the row and its slots: the icons are made again when it changes
    bool                      quickBackMoved = false;
    int                       quickBackState = 0;
    std::string               quickAll = "[]"; // all ten keys: [[key, has an item, in use], ...]

    struct TouchHold { bool on=false, wasSel=false; int pg=0; size_t idx=0; uint64_t t0=0; };
    TouchHold                 hold;
    bool                      swDown = false, swipe = false;   // a finger is down / it scrolls the list
    int                       swY = 0, swAcc = 0;
    int                       tapPg = -1;  // the slot selected by the last tap (the one a second tap uses)
    size_t                    tapIdx = 0;
    Tempest::Timer            holdTimer;   // repaints the filling frame, drops the item when it is full
    bool                      slotAt(int x, int y, int& pg, size_t& idx);
    void                      onHoldTick();
    float                     holdFill() const;
#endif

    size_t                    columsCount = 5;
    int32_t                   scrollDelta = 0;

    size_t                    rowsCount() const;

    Tempest::Size             slotSize() const;
    int                       infoHeight() const;
    size_t                    pagesCount() const;

    const Page&               activePage();
    PageLocal&                activePageSel();
    const World*              world() const;

    void          processMove(Tempest::KeyEvent& e);
    void          moveLeft(bool usePage);
    void          moveRight(bool usePage);
    void          moveUp();
    void          moveDown();

    void          onItemAction(uint8_t slotHint);
    void          onTakeStuff();
    void          adjustScroll();
    void          drawAll   (Tempest::Painter& p, Npc& player, DrawPass pass);
    void          drawItems (Tempest::Painter& p, DrawPass pass, const Page &inv, const PageLocal &sel, int x, int y, int wcount, int hcount);
    void          drawSlot  (Tempest::Painter& p, DrawPass pass, const Inventory::Iterator& it,
                             const Page& page, const PageLocal &sel, int x, int y, size_t id);
    void          drawGold  (Tempest::Painter& p, Npc &player, int x, int y);
    void          drawHeader(Tempest::Painter& p, std::string_view title, int x, int y);
    void          drawInfo  (Tempest::Painter& p);
  };
