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
  };

