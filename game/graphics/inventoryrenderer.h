#pragma once

#include <Tempest/Device>
#include <Tempest/Matrix4x4>

#include "meshobjects.h"
#include "sceneglobals.h"
#include "visualobjects.h"

class Item;

class InventoryRenderer {
  public:
    InventoryRenderer();

    // sx, sy: framebuffer pixels per window pixel (they differ when the swapchain is not the size of the window)
    void draw(Tempest::Encoder<Tempest::CommandBuffer>& cmd, float sx = 1.f, float sy = 1.f);

    void reset(bool full=false);
    void drawItem(int x, int y, int w, int h, const Item &item);
    // moves a drawn item (the quick row scrolls); the clip rectangle (window pixels) cuts what is outside, w=0: none
    void setItemX(size_t idx, int x) { if(idx<items.size()) items[idx].x = x; }
    void setClip(int x, int y, int w, int h) { clipX=x; clipY=y; clipW=w; clipH=h; }

  private:
    struct PerFrame {
      Tempest::Matrix4x4 mvp;
      };

    struct Itm {
      MeshObjects::Mesh  mesh;
      Tempest::Matrix4x4 viewMat;
      int x=0, y=0, w=0, h=0;
      };

    SceneGlobals     scene;
    VisualObjects    visual;
    MeshObjects      itmGroup;
    std::vector<Itm> items;
    int              clipX=0, clipY=0, clipW=0, clipH=0;
  };

