#include <cmath>
#include "inventorymenu.h"

#include <Tempest/Painter>
#include <Tempest/SoundEffect>
#include <Tempest/Application>

#include "utils/string_frm.h"
#include "world/objects/npc.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/world.h"
#include "utils/gthfont.h"
#include "utils/keycodec.h"
#include "gothic.h"
#include "plugins.h"
#include "resources.h"

using namespace Tempest;

struct InventoryMenu::Page {
  Page()=default;
  Page(const Page&)=delete;
  virtual ~Page()=default;

  size_t                      size() const {
    if(is(nullptr))
      return 0;
    size_t ret = 0;
    auto it = iterator();
    while(it.isValid()) {
      ret++;
      ++it;
      }
    return ret;
    }
  Inventory::Iterator         get(size_t id) const {
    auto it = iterator();
    for(size_t i=0; i<id && it.isValid(); ++i)
      ++it;
    return it;
    }

  virtual bool                is(const Inventory* i) const { return i==nullptr; }
  virtual Inventory::Iterator iterator() const { throw std::runtime_error("index out of range");  }
  };

struct InventoryMenu::InvPage : InventoryMenu::Page {
  InvPage(const Inventory& i):inv(i){}

  bool                is(const Inventory* i) const override { return &inv==i; }
  Inventory::Iterator iterator() const override {
    return inv.iterator(Inventory::T_Inventory);
    }

  const Inventory& inv;
  };

struct InventoryMenu::TradePage : InventoryMenu::Page {
  TradePage(const Inventory& i):inv(i){}

  bool                is(const Inventory* i) const override { return &inv==i; }
  Inventory::Iterator iterator() const override {
    return inv.iterator(Inventory::T_Trade);
    }

  const Inventory& inv;
  };

struct InventoryMenu::RansackPage : InventoryMenu::Page {
  RansackPage(const Inventory& i):inv(i){}

  bool                is(const Inventory* i) const override { return &inv==i; }
  Inventory::Iterator iterator() const override {
    return inv.iterator(Inventory::T_Ransack);
    }

  const Inventory& inv;
  };

InventoryMenu::InventoryMenu(const KeyCodec& key)
  :keycodec(key) {
  slot = Resources::loadTexture("INV_SLOT.TGA");
  selT = Resources::loadTexture("INV_SLOT_HIGHLIGHTED.TGA");
  selU = Resources::loadTexture("INV_SLOT_EQUIPPED.TGA");
  tex  = Resources::loadTexture("INV_BACK.TGA"); // INV_TITEL.TGA

  int invMaxColumns = Gothic::settingsGetI("GAME","invMaxColumns");
  if(invMaxColumns>0)
    columsCount = size_t(invMaxColumns); else
    columsCount = 5;

  setFocusPolicy(NoFocus);
  setCursorShape(CursorShape::Hidden);
  takeTimer.timeout.bind(this,&InventoryMenu::onTakeStuff);
#if defined(OG_WEB_UI)
  holdTimer.timeout.bind(this,&InventoryMenu::onHoldTick);
#endif
  }

InventoryMenu::~InventoryMenu() {
  }

void InventoryMenu::close() {
#if defined(OG_WEB_UI)
  tapPg = -1;
  hold  = TouchHold();
#endif
  if(state!=State::Closed) {
    if(state==State::Trade)
      Gothic::inst().emitGlobalSound("TRADE_CLOSE"); else
      Gothic::inst().emitGlobalSound("INV_CLOSE");
    }
  renderer.reset(true);
  takeTimer.stop();
  state  = State::Closed;
  }

void InventoryMenu::open(Npc &pl) {
#if defined(OG_WEB_UI)
  tapPg = -1;
  hold  = TouchHold();
#endif
  if(pl.isDown() || pl.isMonster() || pl.isInAir() || pl.isSlide() || (pl.interactive()!=nullptr))
    return;
  if(pl.bodyStateMasked()==BS_UNCONSCIOUS || pl.bodyStateMasked()==BS_LIE)
    return;
  if(pl.weaponState()!=WeaponState::NoWeapon) {
    pl.stopAnim("");
    pl.closeWeapon(false);
    }
  state  = State::Equip;
  player = &pl;
  trader = nullptr;
  chest  = nullptr;
  page   = 0;
  pagePl .reset(new InvPage  (pl.inventory()));
  pageOth.reset();
  adjustScroll();
  update();

  Gothic::inst().emitGlobalSound("INV_OPEN");
  //Gothic::inst().emitGlobalSound("INV_CHANGE");
  }

void InventoryMenu::trade(Npc &pl, Npc &tr) {
#if defined(OG_WEB_UI)
  tapPg = -1;
  hold  = TouchHold();
#endif
  if(pl.isDown())
    return;
  state  = State::Trade;
  player = &pl;
  trader = &tr;
  chest  = nullptr;
  page   = 0;
  pagePl .reset(new InvPage  (pl.inventory()));
  pageOth.reset(new TradePage(tr.inventory()));
  adjustScroll();
  update();
  Gothic::inst().emitGlobalSound("TRADE_OPEN");
  }

bool InventoryMenu::ransack(Npc &pl, Npc &tr) {
  if(pl.isDown())
    return false;
  auto it = tr.inventory().iterator(Inventory::T_Ransack);
  if(!it.isValid())
    return false;
  if(Plugins::autoLoot.load()!=0)
    return quickLoot(pl,tr.inventory(),&tr,nullptr);
  state  = State::Ransack;
  player = &pl;
  trader = &tr;
  chest  = nullptr;
  page   = 0;
  pagePl .reset(new InvPage    (pl.inventory()));
  pageOth.reset(new RansackPage(tr.inventory()));
  adjustScroll();
  update();
  Gothic::inst().emitGlobalSound("INV_OPEN");
  return true;
  }

void InventoryMenu::open(Npc &pl, Interactive &ch) {
#if defined(OG_WEB_UI)
  tapPg = -1;
  hold  = TouchHold();
#endif
  if(pl.isDown())
    return;
  const bool needToPicklock = ch.needToLockpick(pl);
  // the plugin: a chest that opens (not a locked one) is emptied at once, without the animation and the window
  if(Plugins::autoLoot.load()!=0 && !(needToPicklock && !ch.isCracked()) && quickLoot(pl,ch.inventory(),nullptr,&ch))
    return;
  if(!pl.setInteraction(&ch))
    return;

  if(needToPicklock && !ch.isCracked()) {
    state = State::LockPicking;
    } else {
    state = State::Chest;
    }

  player = &pl;
  trader = nullptr;
  chest  = &ch;
  page   = 0;
  pagePl .reset(new InvPage(pl.inventory()));
  pageOth.reset(new InvPage(ch.inventory()));
  adjustScroll();
  update();
  }

// the plugin AutoQuickLoot: everything that can be taken from a body (from == nullptr for a chest) or a chest goes to the
// player at once; every thing is written on the screen in the colour of its category. body: the dead one, ch: the chest;
// false: nothing to take
bool InventoryMenu::quickLoot(Npc& pl, const Inventory& inv, Npc* body, Interactive* ch) {
  struct Taken { size_t cls; size_t count; std::string name; uint32_t flag; bool gold; };
  std::vector<Taken> list;
  const auto type = body!=nullptr ? Inventory::T_Ransack : Inventory::T_Inventory;
  for(auto it=inv.iterator(type); it.isValid(); ++it) {
    if(it.count()==0)
      continue;
    list.push_back({it->clsId(),it.count(),std::string(it->displayName()),uint32_t(it->mainFlag()),it->isGold()});
    }
  if(list.empty())
    return false;
  for(auto& t:list) {
    if(body!=nullptr)
      pl.addItem(t.cls,*body,t.count); else
      pl.addItem(t.cls,*ch,t.count);
    std::string txt = t.name;
    if(t.count>1)
      txt += " x" + std::to_string(t.count);
    Plugins::addLoot(std::move(txt),Plugins::lootColor(t.flag,t.gold));
    }
  Gothic::inst().emitGlobalSound("INV_CHANGE");
  return true;
  }

InventoryMenu::State InventoryMenu::isOpen() const {
  return state;
  }

bool InventoryMenu::isActive() const {
  return state!=State::Closed;
  }

void InventoryMenu::onWorldChanged() {
  close();
  player = nullptr;
  trader = nullptr;
  chest  = nullptr;
  }

void InventoryMenu::tick(uint64_t /*dt*/) {
  if(player!=nullptr && (player->isDown() || player->bodyStateMasked()==BS_UNCONSCIOUS)) {
    close();
    return;
    }

  if(state==State::LockPicking) {
    if(chest->isCracked()) {
      state = State::Chest;
      return;
      }
    }

  if(state==State::Ransack) {
    if(trader==nullptr){
      close();
      return;
      }

    if(!trader->isDown()) {
      close();
      return;
      }

    auto it = trader->inventory().iterator(Inventory::T_Ransack);
    if(!it.isValid())
      close();
    }

  if(state==State::Closed) {
    if(player!=nullptr){
      if(!player->setInteraction(nullptr))
         return;
      player = nullptr;
      chest  = nullptr;
      }

    page = 0;
    renderer.reset();
    pagePl .reset();
    pageOth.reset();
    update();
    }
  }

void InventoryMenu::processMove(KeyEvent& e) {
  auto key = keycodec.tr(e);
  if(key==KeyCodec::Forward)
    moveUp();
  else if(key==KeyCodec::Back)
    moveDown();
  else if(key==KeyCodec::Left || key==KeyCodec::RotateL)
    moveLeft(true);
  else if(key==KeyCodec::Right || key==KeyCodec::RotateR)
    moveRight(true);
  }

void InventoryMenu::moveLeft(bool usePage) {
  auto& sel = activePageSel();

  if(usePage && sel.sel%columsCount==0 && page>0)
    page--;
  else if(sel.sel>0)
    sel.sel--;
  }

void InventoryMenu::moveRight(bool usePage) {
  auto&        pg     = activePage();
  auto&        sel    = activePageSel();
  const size_t pCount = pagesCount();

  if(usePage && ((sel.sel+1u)%columsCount==0 || sel.sel+1u==pg.size() || pg.size()==0) && page+1u<pCount)
    page++;
  else if(sel.sel+1<pg.size())
    sel.sel++;
  }

void InventoryMenu::moveUp() {
  auto& sel = activePageSel();

  if(sel.sel>=columsCount)
    sel.sel -= columsCount;
  else
    moveLeft(false);
  }

void InventoryMenu::moveDown() {
  auto& pg  = activePage();
  auto& sel = activePageSel();

  if(sel.sel+columsCount<pg.size())
    sel.sel += columsCount;
  else
    moveRight(false);
  }

void InventoryMenu::keyDownEvent(KeyEvent &e) {
  if(state==State::Closed || state==State::LockPicking){
    e.ignore();
    return;
    }

  processMove(e);

  if(keycodec.tr(e)==KeyCodec::Jump) {
    lootMode = LootMode::Stack;
    takeTimer.start(200);
    onTakeStuff();
    }
  else if (keycodec.tr(e)==KeyCodec::ActionGeneric || e.key==KeyEvent::K_Return) {
    onItemAction(Item::NSLOT);
    }
  else if((KeyEvent::K_3<=e.key && e.key<=KeyEvent::K_9) || e.key==KeyEvent::K_0) {
    uint8_t slot = 10;
    if((KeyEvent::K_3<=e.key && e.key<=KeyEvent::K_9))
      slot = uint8_t(e.key-KeyEvent::K_0);
    onItemAction(slot);
    }
  else if(e.key==KeyEvent::K_ESCAPE || keycodec.tr(e)==KeyCodec::Inventory){
    close();
    }
  else if(e.key==KeyEvent::K_Space) {
    lootMode = LootMode::Normal;
    takeTimer.start(200);
    onTakeStuff();
    }
  else if(e.key==KeyEvent::K_Z) {
    lootMode = LootMode::Ten;
    takeTimer.start(200);
    onTakeStuff();
    }
  else if(e.key==KeyEvent::K_X) {
    lootMode = LootMode::Hundred;
    takeTimer.start(200);
    onTakeStuff();
    }

  adjustScroll();
  update();
  }

void InventoryMenu::keyRepeatEvent(KeyEvent& e) {
  if(state==State::LockPicking || state==State::Closed)
    return;
  processMove(e);
  adjustScroll();
  update();
  }

void InventoryMenu::keyUpEvent(KeyEvent&) {
  takeTimer.stop();
  lootMode = LootMode::Normal;
  }

void InventoryMenu::mouseDownEvent(MouseEvent &e) {
  if(player==nullptr || state==State::Closed) {
    e.ignore();
    return;
    }

  if(state==State::LockPicking)
    return;

  if (e.button==MouseEvent::ButtonLeft)
    onItemAction(Item::NSLOT);
  else if (e.button==MouseEvent::ButtonRight)
    close();

  adjustScroll();
}

void InventoryMenu::mouseUpEvent(MouseEvent&) {
  takeTimer.stop();
  takeCount=0;
  }

void InventoryMenu::mouseWheelEvent(MouseEvent &e) {
  if(state==State::Closed) {
    e.ignore();
    return;
    }

  if(state==State::LockPicking)
    return;

  scrollDelta += e.delta;
  if(scrollDelta>0) {
    for(int i=0;i<scrollDelta/120;++i)
      moveUp();
    scrollDelta %= 120;
    } else {
    for(int i=0;i<-scrollDelta/120;++i)
      moveDown();
    scrollDelta %= 120;
    }
  adjustScroll();
  }

const World *InventoryMenu::world() const {
  return Gothic::inst().world();
  }

size_t InventoryMenu::rowsCount() const {
  int iy=30+34+70;
  return size_t((h()-iy-infoHeight()-20)/slotSize().h);
  }

void InventoryMenu::paintEvent(PaintEvent &e) {
  if(player==nullptr || state==State::Closed)
    return;
  renderer.reset();

  Painter p(e);
  drawAll(p,*player,DrawPass::Back);
  }

void InventoryMenu::paintNumOverlay(PaintEvent& e) {
  if(player==nullptr || state==State::Closed)
    return;

  Painter p(e);
  drawAll(p,*player,DrawPass::Front);
  }

Size InventoryMenu::slotSize() const {
  const float scale = Gothic::interfaceScale(this);
  const float cell  = float(Gothic::options().inventoryCellSize);
  return Size(int(cell*scale),int(cell*scale));
  }

int InventoryMenu::infoHeight() const {
  const float scale = Gothic::interfaceScale(this);
  return (Item::MAX_UI_ROWS+2)*int(float(Resources::font(scale).pixelSize()))+int(scale*10)/*padding bottom*/;
  }

size_t InventoryMenu::pagesCount() const {
  if(state==State::Chest || state==State::Trade)
    return 2;
  return 1;
  }

const InventoryMenu::Page &InventoryMenu::activePage() {
  if(pageOth!=nullptr)
    return *(page==0 ? pageOth : pagePl);
  if(pagePl)
    return *pagePl;

  static Page n;
  return n;
  }

InventoryMenu::PageLocal &InventoryMenu::activePageSel() {
  if(pageOth!=nullptr)
    return (page==0 ? pageLocal[0] : pageLocal[1]);
  return pageLocal[1];
  }
  
#if defined(OG_WEB_UI)
static const uint64_t holdDropMs = 1300;   // a held slot is dropped after this
static const uint64_t holdShowMs = 150;    // the frame starts to fill after this (a tap does not flicker)

float InventoryMenu::holdFill() const {
  if(!hold.on)
    return 0;
  const uint64_t dt = Application::tickCount()-hold.t0;
  if(dt<holdShowMs)
    return 0;
  return std::min(1.f,float(dt)/float(holdDropMs));
  }

bool InventoryMenu::slotAt(int x, int y, int& pg, size_t& idx) {
  const int padd  = 43;
  const int iy    = 30+34+70;
  const int sw    = slotSize().w, sh = slotSize().h;
  const int wc    = int(columsCount), hc = int(rowsCount());
  if(sw<=0 || sh<=0 || y<iy || y>=iy+hc*sh)
    return false;
  const int row = (y-iy)/sh;

  auto test = [&](int x0, const PageLocal& sel, const Page* page, int no) {
    if(page==nullptr || x<x0 || x>=x0+wc*sw)
      return false;
    const size_t id = (sel.scroll+size_t(row))*size_t(wc) + size_t((x-x0)/sw);
    if(id>=page->size())
      return false;
    pg  = no;
    idx = id;
    return true;
    };

  if(pageOth!=nullptr && test(padd,pageLocal[0],pageOth.get(),0))
    return true;
  if(state!=State::Ransack && test(w()-padd-wc*sw,pageLocal[1],pagePl.get(),1))
    return true;
  return false;
  }

void InventoryMenu::webPointer(int type, int x, int y) {
  if(player==nullptr || state==State::Closed || state==State::LockPicking)
    return;

  int    pg  = 0;
  size_t idx = 0;
  const bool onSlot = slotAt(x,y,pg,idx);
  const int  sh     = std::max(1,slotSize().h);

  if(type==0) {
    hold   = TouchHold();
    swDown = true;
    swipe  = false;
    swY    = y;
    swAcc  = 0;
    if(!onSlot) {
      if(pageOth!=nullptr)
        page = uint8_t(x<w()/2 ? 0 : 1);   // a swipe from the empty place scrolls the list on that side
      return;
      }
    if(pageOth!=nullptr)
      page = uint8_t(pg);
    auto& sel = activePageSel();
    hold.on     = true;
    hold.wasSel = (tapPg==pg && tapIdx==idx);
    tapPg       = pg;
    tapIdx      = idx;
    hold.pg     = pg;
    hold.idx    = idx;
    hold.t0     = Application::tickCount();
    sel.sel     = idx;
    adjustScroll();
    holdTimer.start(30);
    }
  else if(type==1) {
    holdTimer.stop();
    // a tap uses the slot; released while the frame fills up it only cancels the drop
    const bool use = hold.on && hold.wasSel && onSlot && pg==hold.pg && idx==hold.idx &&
                     Application::tickCount()-hold.t0<holdShowMs && !swipe;
    hold   = TouchHold();
    swDown = false;
    swipe  = false;
    if(use)
      onItemAction(Item::NSLOT);   // the second tap in a row
    adjustScroll();
    }
  else if(type==2) {
    if(swDown) {
      const int dy = y-swY;
      if(!swipe && std::abs(dy)>=sh/3) {
        // the finger drags the list: neither use nor drop
        swipe = true;
        hold  = TouchHold();
        holdTimer.stop();
        tapPg = -1;
        }
      if(swipe) {
        swAcc += dy;
        swY    = y;
        auto&  pg2  = activePage();
        auto&  sel  = activePageSel();
        const size_t wc   = std::max<size_t>(columsCount,1);
        const size_t hc   = std::max<size_t>(rowsCount(),1);
        const size_t rows = (pg2.size()+wc-1)/wc;
        const size_t maxS = rows>hc ? rows-hc : 0;
        // the finger goes up: the list goes up, the rows below come
        while(swAcc<=-sh) { swAcc += sh; if(sel.scroll<maxS) ++sel.scroll; else swAcc = 0; }
        while(swAcc>= sh) { swAcc -= sh; if(sel.scroll>0)    --sel.scroll; else swAcc = 0; }
        // the selection stays in the visible rows
        const size_t col = sel.sel%wc, row = sel.sel/wc;
        size_t       nr  = std::min(std::max(row,sel.scroll),sel.scroll+hc-1);
        if(nr!=row) {
          size_t ns = nr*wc+col;
          if(pg2.size()>0 && ns>=pg2.size())
            ns = pg2.size()-1;
          sel.sel = ns;
          }
        }
      }
    if(hold.on && !(onSlot && pg==hold.pg && idx==hold.idx)) {
      // the finger left the slot: neither use nor drop
      hold = TouchHold();
      holdTimer.stop();
      }
    }
  else {
    hold   = TouchHold();
    swDown = false;
    swipe  = false;
    holdTimer.stop();
    }
  update();
  }

void InventoryMenu::onHoldTick() {
  if(!hold.on) {
    holdTimer.stop();
    return;
    }
  if(Application::tickCount()-hold.t0>=holdDropMs) {
    hold = TouchHold();
    holdTimer.stop();
    // the whole stack: dropped from the inventory, moved to the other side of a chest or trade
    lootMode = LootMode::Stack;
    onTakeStuff();
    lootMode = LootMode::Normal;
    takeCount = 0;
    }
  update();
  }
#endif

void InventoryMenu::onItemAction(uint8_t slotHint) {
  auto& page = activePage();
  auto& sel  = activePageSel();

  auto it = page.get(sel.sel);
  if(!it.isValid())
    return;

  if(state==State::Equip) {
    const size_t clsId = it->clsId();
    if(it.isEquipped() && slotHint==Item::NSLOT) {
      player->unequipItem(clsId);
      } else {
      player->useItem(clsId,slotHint,false);
      auto it2 = page.get(sel.sel);
      if((!it2.isValid() || it2->clsId()!=clsId) && sel.sel>0)
        --sel.sel;
      }
    }
  else if(state==State::Chest || state==State::Trade || state==State::Ransack) {
    lootMode = LootMode::Normal;
    takeTimer.start(200);
    onTakeStuff();
    }
  }

void InventoryMenu::onTakeStuff() { 
  size_t itemCount = 0;
  auto& page = activePage();
  auto& sel  = activePageSel();
  if(sel.sel >= page.size())
    return;
  auto it = page.get(sel.sel);
  if(lootMode==LootMode::Normal) {
    ++takeCount;
    itemCount = uint32_t(std::pow(10,takeCount / 10));
    if(it.count() <= itemCount) {
      itemCount = uint32_t(it.count());
      takeCount = 0;
      }
    }
  else if(lootMode==LootMode::Stack) {
    itemCount = it.count();
    }
  else if(lootMode==LootMode::Ten) {
    itemCount = 10;
    }
  else if(lootMode==LootMode::Hundred) {
    itemCount = 100;
    }

  if(it.count() < itemCount) {
    itemCount = it.count();
    }

  if(state==State::Chest) {
    if(page.is(&player->inventory())) {
      player->moveItem(it->clsId(),*chest,itemCount);
      } else {
      player->addItem (it->clsId(),*chest,itemCount);
      }
    }
  else if(state==State::Trade) {
    if(page.is(&player->inventory())) {
      player->sellItem(it->clsId(),*trader,itemCount);
      } else {
      player->buyItem (it->clsId(),*trader,itemCount);
      }
    }
  else if(state==State::Ransack) {
    if(page.is(&trader->inventory())) {
      player->addItem(it->clsId(),*trader,itemCount);
      }
    }
  else if(state==State::Equip) {
    player->dropItem(it->clsId(),itemCount);
    }
  adjustScroll();
  }

void InventoryMenu::adjustScroll() {
  auto& page = activePage();
  auto& sel  = activePageSel();
  sel.sel = std::min(sel.sel, std::max<size_t>(page.size(),1)-1);
  while(sel.sel<sel.scroll*columsCount) {
    if(sel.scroll<=1){
      sel.scroll=0;
      return;
      }
    sel.scroll-=1;
    }

  const size_t hcount=rowsCount();
  while(sel.sel>=(sel.scroll+hcount)*columsCount) {
    sel.scroll+=1;
    }
  }

void InventoryMenu::drawAll(Painter &p, Npc &player, DrawPass pass) {
  const int padd = 43;

  int iy=30+34+70;

  if(state==State::LockPicking)
    return;

  const int wcount = int(columsCount);
  const int hcount = int(rowsCount());

  if(chest!=nullptr){
    if(pass==DrawPass::Back)
      drawHeader(p,chest->displayName(),padd,70);
    drawItems(p,pass,*pageOth,pageLocal[0],padd,iy,wcount,hcount);
    }

  if(trader!=nullptr) {
    if(pass==DrawPass::Back)
      drawHeader(p,trader->displayName(),padd,70);
    drawItems(p,pass,*pageOth,pageLocal[0],padd,iy,wcount,hcount);
    }

  if(state!=State::Ransack) {
    if(pass==DrawPass::Back)
      drawGold (p,player,w()-padd-2*slotSize().w,70);
    drawItems(p,pass,*pagePl,pageLocal[1],w()-padd-wcount*slotSize().w,iy,wcount,hcount);
    }

  if(pass==DrawPass::Back)
    drawInfo(p);
  }

void InventoryMenu::drawItems(Painter &p, DrawPass pass,
                              const Page &inv, const PageLocal& sel, int x0, int y, int wcount, int hcount) {
  if(state==State::LockPicking)
    return;

  if(tex!=nullptr && pass==DrawPass::Back) {
    p.setBrush(*tex);
    p.drawRect(x0,y,slotSize().w*wcount,slotSize().h*hcount,
               0,0,tex->w(),tex->h());
    }

  auto   it = inv.iterator();
  size_t id = 0;
  for(size_t i=0; it.isValid() && i<sel.scroll*size_t(wcount); ++i) {
    ++it;
    ++id;
    }
  for(int i=0;i<hcount;++i) {
    for(int r=0;r<wcount;++r) {
      const int x = x0 + r*slotSize().w;
      if(pass==DrawPass::Back) {
        p.setBrush(*slot);
        p.drawRect(x,y,slotSize().w,slotSize().h,
                   0,0,slot->w(),slot->h());
        }
      if(it.isValid()) {
        drawSlot(p,pass, it,inv,sel, x,y, id);
        ++it;
        ++id;
        }
      }
    y += slotSize().h;
    }
  }

void InventoryMenu::drawSlot(Painter &p, DrawPass pass, const Inventory::Iterator &it,
                             const Page& page, const PageLocal &sel,
                             int x, int y, size_t id) {
  if(!slot)
    return;

  auto& active = activePage();
  const float scale = Gothic::interfaceScale(this);

  if(pass==DrawPass::Back) {
    if((!it.isValid() && id==0) || (id==sel.sel && &page==&active)){
      p.setBrush(*selT);
      p.drawRect(x,y,slotSize().w,slotSize().h,
                 0,0,selT->w(),selT->h());
      }

    if(it.isEquipped() && selU!=nullptr) {
      p.setBrush(*selU);
      p.drawRect(x,y,slotSize().w,slotSize().h,
                 0,0,selU->w(),selU->h());
      }

    const int dsz = (id==sel.sel ? 5 : 0);
    renderer.drawItem(x-dsz, y-dsz, slotSize().w+2*dsz, slotSize().h+2*dsz, *it);
    } else {
    auto fnt = Resources::font(scale);

    if(it.count()>1) {
      string_frm vint(int(it.count()));
      auto sz = fnt.textSize(vint);
      fnt.drawText(p,x+slotSize().w-sz.w-10,
                   y+slotSize().h-10,
                   vint);
      }

#if defined(OG_WEB_UI)
    if(hold.on && id==hold.idx && hold.pg==(&page==pageOth.get() ? 0 : 1)) {
      // the frame of the held slot fills up clockwise; full: the item is dropped
      const float fill = holdFill();
      if(fill>0) {
        const int sw = slotSize().w, sh = slotSize().h;
        const int t  = std::max(3,int(4*scale));
        int       len = int(fill*float(2*(sw+sh)));
        p.setBrush(Color(1.f,0.6f,0.1f,1.f));
        int l = std::min(len,sw);   len -= l;
        p.drawRect(x,y,l,t);
        l = std::min(len,sh);       len -= l;
        p.drawRect(x+sw-t,y,t,l);
        l = std::min(len,sw);       len -= l;
        p.drawRect(x+sw-l,y+sh-t,l,t);
        l = std::min(len,sh);
        p.drawRect(x,y+sh-l,t,l);
        }
      }
#endif

    if(it.slot()!=Item::NSLOT) {
      fnt = Resources::font(Resources::FontType::Red, scale);

      string_frm vint(int(it.slot()));
      auto sz = fnt.textSize(vint);
      fnt.drawText(p,x+10,
                   y+slotSize().h/2+sz.h/2,
                   vint);
      }
    }
  }

void InventoryMenu::drawGold(Painter &p, Npc &player, int x, int y) {
  if(!slot)
    return;
  auto           w    = world();
  auto           txt  = w ? w->script().currencyName() : "";
  const size_t   gold = player.inventory().goldCount();
  if(txt.empty())
    txt="Gold";

  string_frm vint(txt," : ",int(gold));
  drawHeader(p,vint,x,y);
  }

void InventoryMenu::drawHeader(Painter &p, std::string_view title, int x, int y) {
  const float scale = Gothic::interfaceScale(this);
  auto&       fnt   = Resources::font(scale);

  const int   tw    = fnt.textSize(title).w;
  const int   th    = fnt.textSize(title).h;
  const int   padd  = int(8*scale);
  const int   dw    = std::max(slotSize().w*2, tw+padd*2);
  const int   dh    = int(34*scale);

  if(tex) {
    p.setBrush(*tex);
    p.drawRect(x,y,dw,dh, 0,0,tex->w(),tex->h());
    }
  if(slot) {
    p.setBrush(*slot);
    p.drawRect(x,y,dw,dh, 0,0,slot->w(),slot->h());
    }

  fnt.drawText(p,x+(dw-tw)/2,y+dh/2+th/2,title);
  }

void InventoryMenu::drawInfo(Painter &p) {
  const float scale = Gothic::interfaceScale(this);
  const int   dw    = std::min(w(), int(720*scale));
  const int   dh    = infoHeight();
  const int   x     = (w()-dw)/2;
  const int   y     = h()-dh-20;

  auto& pg  = activePage();
  auto& sel = activePageSel();

  auto it = pg.get(sel.sel);
  if(!it.isValid())
    return;

  auto& r = *pg.get(sel.sel);
  if(tex) {
    p.setBrush(*tex);
    p.drawRect(x,y,dw,dh,
               0,0,tex->w(),tex->h());
    }

  auto& fnt = Resources::font(scale);
  auto  desc = r.description();
  int   tw   = fnt.textSize(desc).w;

  fnt.drawText(p,x+(dw-tw)/2,y+int(fnt.pixelSize()),desc);

  for(size_t i=0;i<Item::MAX_UI_ROWS;++i){
    auto    txt = r.uiText(i);
    int32_t val = r.uiValue(i);

    if(txt.empty())
      continue;

    if(i+1==Item::MAX_UI_ROWS && state==State::Trade && player!=nullptr && pg.is(&player->inventory())){
      val = r.sellCost();
      }

    string_frm vint(val);
    int tw = fnt.textSize(vint).w;

    fnt.drawText(p, x+20,  y+int(i+2)*fnt.pixelSize(), txt);
    if(val!=0)
      fnt.drawText(p,x+dw-tw-20,y+int(i+2)*fnt.pixelSize(),vint);
    }

  const int sz = dh;
  renderer.drawItem(x+dw-sz-sz/2,y,sz,sz,r);
  }

void InventoryMenu::draw(Tempest::Encoder<CommandBuffer>& cmd, float sx, float sy) {
  renderer.draw(cmd,sx,sy);
#if defined(OG_WEB_UI)
  if(quickVisible())
    quickRenderer.draw(cmd,sx,sy);
#endif
  }

#if defined(OG_WEB_UI)
// the quick panel: the weapons and spells (runes, scrolls) the player put on the number keys 1..0 (melee 1,
// ranged 2, magic 3..0), a tap presses the key. The slots have their place in window pixels; the page puts its buttons on top of the same rectangles
// (og_quickslots), the icons are drawn here
void InventoryMenu::quickUpdate() {
  auto pl = Gothic::inst().player();
  std::vector<QuickSlot> all;
  if(pl!=nullptr && Gothic::inst().world()!=nullptr) {
    // the ring: the ten number keys 1..9,0 in a row, the empty ones too
    const auto ws = pl->weaponState();
    const uint8_t spell = pl->inventory().currentSpellSlot();
    auto add = [&](int key, const Item* it, bool on) {
      QuickSlot s;
      s.key  = key;
      s.item = it;
      s.cls  = it!=nullptr ? it->clsId() : 0;
      s.on   = on && it!=nullptr;
      if(it!=nullptr) {
        if(key==2) {
          const int32_t munition = it->handle().munition;
          s.n = munition>=0 ? int(pl->itemCount(size_t(munition))) : 0; // arrows or bolts
          }
        else if(key>2 && it->isSpell())
          s.n = int(it->count());                                         // scrolls (runes are not used up)
        }
      all.push_back(s);
      };
    add(1, pl->currentMeleeWeapon(),  ws==WeaponState::Fist || ws==WeaponState::W1H || ws==WeaponState::W2H);
    add(2, pl->currentRangedWeapon(), ws==WeaponState::Bow  || ws==WeaponState::CBow);
    for(int i=3; i<=10; ++i)
      add(i%10, pl->inventory().currentSpell(uint8_t(i-3)), ws==WeaponState::Mage && spell==i); // numslot[0..7]: keys 3..0
    }

  quickAll = "[";
  for(size_t i=0; i<all.size(); ++i) {
    if(i>0)
      quickAll += ',';
    quickAll += "[" + std::to_string(all[i].key) + "," + (all[i].item!=nullptr ? "1" : "0") + "," + (all[i].on ? "1" : "0") + "," + std::to_string(all[i].n) + "]";
    }
  quickAll += "]";

  // 4..10 visible slots (the layout editor of the page; by default 4 in a row at the bottom, in the middle: clear of the
  // stick and the buttons), in a row or a column, in one line or two (8 -> 4 + 4). The used items are a ring: the page
  // turns it round by `quickPos` (fractions of a slot while it scrolls, every line shows one more slot then); a line
  // shows the ring from its own place (the second one goes on after the first). All ten visible: no ring
  const int ww = w(), hh = h();
  const float k = float(std::clamp(quickCfgK.load(), 30, 300))/100.f;
  const int sz = std::max(24, int(float(hh)*0.12f*k));
  const int gp = sz/6;
  quickTotal = int(all.size());
  const int cols = std::clamp(quickCfgCols.load(), 1, std::max(1, quickTotal>0 ? quickTotal : 10));
  const bool ring = quickTotal>cols;
  const bool vert = quickCfgVert.load()!=0;
  const int lines = (quickCfgSplit.load()!=0 && cols>=2) ? 2 : 1;
  const int per   = (cols+lines-1)/lines;
  quickCols  = cols;
  float pos = 0;
  if(quickTotal>0 && ring) {
    pos = std::fmod(quickPos, float(quickTotal));
    if(pos<0)
      pos += float(quickTotal);
    }
  const int   base = int(pos);
  const float frac = pos-float(base);
  quickFirst = quickTotal>0 ? (base + (frac>=0.5f ? 1 : 0)) % quickTotal : 0;
  const int pitch = sz+gp;
  const int lenMain  = per*sz + (per-1)*gp;
  const int lenCross = lines*sz + (lines-1)*gp;
  const int boxW = vert ? lenCross : lenMain;
  const int boxH = vert ? lenMain  : lenCross;
  // the inventory block: right of the first row, or under the first column
  const int invW = vert ? 0 : pitch, invH = vert ? pitch : 0;
  const int cx = quickCfgX.load(), cy = quickCfgY.load();
  int x0 = 0, y = 0;
  if(cx<0 || cy<0) {
    if(!vert) {
      x0 = (ww - boxW)/2;
      y  = hh - boxH - int(float(hh)*0.035f);
      } else {
      x0 = int(float(hh)*0.035f);
      y  = (hh - boxH)/2;
      }
    }
  if(cx>=0)
    x0 = int(float(cx)/1000.f*float(ww)) - boxW/2;
  if(cy>=0)
    y  = int(float(cy)/1000.f*float(hh)) - boxH/2;
  x0 = std::clamp(x0, 0, std::max(0, ww-boxW-invW));
  y  = std::clamp(y,  0, std::max(0, hh-boxH-invH));
  quickX0 = x0; quickY = y; quickSz = sz; quickPitch = pitch; quickRowW = lenMain;
  quickVert = vert; quickLines = lines; quickPer = per; quickBoxW = boxW; quickBoxH = boxH;
  std::vector<QuickSlot> slots;
  for(int ln=0; ln<lines; ++ln) {
    const int off = ln*per;
    const int cnt = std::min(per, cols-off) + (ring ? 1 : 0);
    for(int j=0; j<cnt; ++j) {
      QuickSlot s;
      if(quickTotal>0) {
        auto& e = all[size_t((base+off+j)%quickTotal)];
        s.key   = e.key;
        s.item  = e.item;
        s.cls   = e.cls;
        s.on    = e.on;
        s.n     = e.n;
        }
      const int main  = int(float(j)*float(pitch) - frac*float(pitch) + 0.5f);
      const int cross = ln*pitch;
      s.x    = x0 + (vert ? cross : main);
      s.y    = y  + (vert ? main  : cross);
      s.s    = sz;
      s.line = ln;
      slots.push_back(s);
      }
    }

  std::vector<size_t> sig;
  for(auto& s:slots)
    sig.push_back(s.item!=nullptr ? s.cls*16u + size_t(s.key+1) : 0); // the count is not here: the icons do not change with it
  std::vector<int> geo = {x0, y, sz, cols, vert ? 1 : 0, lines};
  const bool changed = (sig!=quickSig || ww!=quickW || hh!=quickH || geo!=quickGeo);
  if(quickCfgBack.load()!=0) {
    // the backing is in the ui layer: painted again when the slots move
    bool moved = changed || slots.size()!=quickSlots.size();
    for(size_t i=0; !moved && i<slots.size(); ++i)
      moved = slots[i].x!=quickSlots[i].x || slots[i].y!=quickSlots[i].y || (slots[i].item!=nullptr)!=(quickSlots[i].item!=nullptr);
    if(moved)
      quickBackMoved = true;
    }
  quickGeo   = std::move(geo);
  quickSlots = std::move(slots);
  {
  // switched on / off, the bar shown / hidden: the backing is painted again (or wiped)
  const int bs = (quickCfgBack.load()!=0 ? 1 : 0) | (quickVisible() ? 2 : 0) | (state==State::Closed ? 4 : 0);
  if(bs!=quickBackState) {
    quickBackState = bs;
    quickBackMoved = true;
    }
  }
  quickRenderer.setClip(x0, y, boxW, boxH);
  if(!changed) {
    size_t k = 0;   // the same icons: they only move
    for(auto& s:quickSlots)
      if(s.item!=nullptr)
        quickRenderer.setItemPos(k++, s.x, s.y);
    return;
    }
  quickSig = std::move(sig);
  quickW   = ww;
  quickH   = hh;
  quickRenderer.reset();
  size_t idx = 0;
  for(auto& s:quickSlots)
    if(s.item!=nullptr) {
      quickRenderer.drawItem(s.x,s.y,s.s,s.s,*s.item);
      int bx, by, bw, bh;
      quickLineBox(s.line, bx, by, bw, bh);
      quickRenderer.setItemClip(idx++, bx, by, bw, bh);
      }
  }

void InventoryMenu::quickLineBox(int line, int& x, int& y, int& w, int& h) const {
  const int cnt   = std::max(1, std::min(quickPer, quickCols - line*quickPer));
  const int len   = cnt*quickSz + (cnt-1)*(quickPitch-quickSz);
  const int cross = line*quickPitch;
  x = quickX0 + (quickVert ? cross : 0);
  y = quickY  + (quickVert ? 0 : cross);
  w = quickVert ? quickSz : len;
  h = quickVert ? len : quickSz;
  }

std::atomic<int> InventoryMenu::quickCfgCols{4};
std::atomic<int> InventoryMenu::quickCfgX{-1};
std::atomic<int> InventoryMenu::quickCfgY{-1};
std::atomic<int> InventoryMenu::quickCfgK{100};
std::atomic<int> InventoryMenu::quickCfgBack{0};
std::atomic<int> InventoryMenu::quickCfgVert{0};
std::atomic<int> InventoryMenu::quickCfgSplit{0};

void InventoryMenu::paintQuickBack(Tempest::Painter& p) const {
  if(quickCfgBack.load()==0 || !quickVisible() || state!=State::Closed)
    return;
  // a dark square under every slot: it covers the whole slot, up to the frame (the corners too: the frame's line is drawn over
  // it), so the icons stand out of a bright or busy picture. The middle is darker than the edge (three layers inset into the
  // slot). Each slot is cut by the box of its row (the ring scrolls)
  const int e = std::max(2, quickSz/10);
  const struct { int in; float a; } layer[] = {{0, 0.30f}, {e, 0.18f}, {2*e, 0.14f}};
  for(auto& s:quickSlots) {
    int bx, by, bw, bh;
    quickLineBox(s.line, bx, by, bw, bh);
    for(auto& ly:layer) {
      const int x0 = std::max(s.x + ly.in, bx), y0 = std::max(s.y + ly.in, by);
      const int x1 = std::min(s.x + s.s - ly.in, bx+bw), y1 = std::min(s.y + s.s - ly.in, by+bh);
      if(x1<=x0 || y1<=y0)
        continue;
      p.setBrush(Tempest::Color(0.f, 0.f, 0.f, ly.a));
      p.drawRect(x0, y0, x1-x0, y1-y0);
      }
    }
  }

std::string InventoryMenu::quickJson() const {
  // the page draws the frames itself (it scrolls them every frame): the place of the row and the ten keys "a"
  return "{\"w\":" + std::to_string(w()) + ",\"h\":" + std::to_string(h()) +
         ",\"f\":" + std::to_string(quickFirst) + ",\"c\":" + std::to_string(quickCols) +
         ",\"t\":" + std::to_string(quickTotal) + ",\"x\":" + std::to_string(quickX0) +
         ",\"y\":" + std::to_string(quickY) + ",\"b\":" + std::to_string(quickSz) +
         ",\"p\":" + std::to_string(quickPitch) + ",\"v\":" + (quickVert ? "1" : "0") +
         ",\"l\":" + std::to_string(quickLines) + ",\"n\":" + std::to_string(quickPer) +
         ",\"a\":" + quickAll + "}";
  }
#endif
