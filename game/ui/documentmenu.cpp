#include "documentmenu.h"

#include "world/objects/interactive.h"
#include "world/objects/npc.h"
#include "utils/gthfont.h"
#include "utils/keycodec.h"
#include "gothic.h"
#include "plugins.h"

using namespace Tempest;

DocumentMenu::DocumentMenu(const KeyCodec& key)
  :keycodec(key) {
  setFocusPolicy(NoFocus);
  cursor = Resources::loadTexture("U.TGA");
  }

void DocumentMenu::show(const DocumentMenu::Show &doc) {
  const bool wantMini = Plugins::miniMap.load()!=0 && doc.showPlayer && !doc.pages.empty();
  // the key of the map pressed again closes the small map
  if(active && mini && wantMini) {
    close();
    return;
    }
  document = doc;
  active   = true;
  mini     = wantMini;
  update();
  }

void DocumentMenu::close() {
  if(!active)
    return;
  active=false;
  mini  =false;
  Plugins::miniCross.store(0);
  update();

  if(auto pl = Gothic::inst().player())
    pl->setInteraction(nullptr);
  }

bool DocumentMenu::miniCrossHit(const Tempest::Point& pos) const {
  return active && mini && crossRect.contains(pos);
  }

void DocumentMenu::keyDownEvent(KeyEvent &e) {
  if(!active){
    e.ignore();
    return;
    }

  if(e.key!=Event::K_ESCAPE && keycodec.tr(e)!=KeyCodec::Inventory){
    e.ignore();
    return;
    }
  close();
  }

void DocumentMenu::keyUpEvent(KeyEvent&) {
  }

void DocumentMenu::paintEvent(PaintEvent &e) {
  if(!active)
    return;

  const float scale = Gothic::interfaceScale(this);
  float mw = 0, mh = 0;
  for(auto& i:document.pages){
    auto back = Resources::loadTexture((i.flg&F_Backgr) ? i.img : document.img);
    if(!back)
      continue;
    mw += float(back->w())*scale;
    mh = std::max(mh,float(back->h())*scale);
    }

  float k  = std::min(1.f,float(800.f+float(document.margins.xMargin()))*scale/std::max(mw,1.f));

  float ms = 1.f;   // the small map: a part of the window, in the top right corner, under the page's top bar
  int   x  = (w()-int(k*mw))/2, y = (h()-int(mh))/2;
  if(mini) {
    ms = std::min(0.27f*float(w())/std::max(k*mw,1.f), 0.5f*float(h())/std::max(mh,1.f));
    x  = w()-int(k*mw*ms)-int(0.08f*float(w()));   // left of the gear of the settings
    y  = int(0.12f*float(h()));   // the bar of the player (FPS, the gear) is over the top of the window
    }
  const int x0 = x;
  auto  pl = Gothic::inst().player();

  Painter p(e);
  for(auto& i:document.pages) {
    const GthFont* fnt = nullptr;
    if(i.flg&F_Font)
      fnt = &Resources::font(i.font, Resources::FontType::Normal, scale); else
      fnt = &Resources::font(document.font, Resources::FontType::Normal, scale);
    if(fnt==nullptr)
      fnt = &Resources::font(scale);
    auto back = Resources::loadTexture((i.flg&F_Backgr) ? i.img : document.img);
    if(!back)
      continue;

    auto mgr = (i.flg&F_Margin) ? i.margins : document.margins;
    mgr.left   = int(float(mgr.left)  *scale);
    mgr.right  = int(float(mgr.right) *scale);
    mgr.top    = int(float(mgr.top)   *scale);
    mgr.bottom = int(float(mgr.bottom)*scale);

    const int w = int(k*scale*ms*float(back->w()));
    const int h = int(  scale*ms*float(back->h()));

    p.setBrush(*back);
    p.drawRect(x,y,w,h,
               0,0,back->w(),back->h());

    p.setBrush(Color(0.04f,0.04f,0.04f,1));

    if(!mini)
      fnt->drawText(p,x+mgr.left,
                    y+mgr.top+fnt->pixelSize(),
                    w - mgr.xMargin(),
                    h - mgr.yMargin(),
                    i.text, Tempest::AlignLeft);

    if(document.showPlayer && cursor!=nullptr && pl!=nullptr) {
      auto  pos = pl->position();
      float wx  = (pos.x-float(document.wbounds.x))/float(document.wbounds.w);
      float wy  = (pos.z-float(document.wbounds.y))/float(document.wbounds.h);

      p.setBrush(*cursor);
      int cx = x+int(wx*float(w));
      int cy = y+int(wy*float(h));

      p.pushState();
      p.translate(cx,cy);
      p.rotate(-pl->rotation());
      // on the small map the arrow shrinks with it: a tenth of the map's height, never bigger than the native size
      const float ck = mini ? std::min(1.f,0.1f*float(h)/float(std::max(cursor->h(),1))) : 1.f;
      const int cw = int(float(cursor->w())*ck);
      const int ch = int(float(cursor->h())*ck);
      p.drawRect(-cw/2,-ch/2, cw,ch, 0,0,cursor->w(),cursor->h());
      p.popState();
      }
    x+=w;
    }

  if(mini) {
    // the cross in the top left corner of the small map (clear of the buttons at the right edge): a click
    // (the page puts a touch button over it) closes it
    const int sz = std::max(24,int(0.06f*float(h())));
    const int cx = x0, cy = y;
    crossRect = Rect(cx,cy,sz,sz);
    p.setBrush(Color(0.f,0.f,0.f,0.65f));
    p.drawRect(cx,cy,sz,sz);
    p.setBrush(Color(1.f,1.f,1.f,1.f));
    const int t = std::max(2,sz/12), m = sz/4;
    for(int i=0; i<sz-2*m; ++i) {
      p.drawRect(cx+m+i-t/2,cy+m+i-t/2,t,t);
      p.drawRect(cx+sz-m-i-t/2,cy+m+i-t/2,t,t);
      }
    Plugins::miniCross.store(std::max(1,(cx+sz/2)*1000/std::max(w(),1)) | (std::max(1,(cy+sz/2)*1000/std::max(h(),1))<<10));
    }
  }
