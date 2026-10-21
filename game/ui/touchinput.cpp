#include "touchinput.h"

#include <Tempest/Painter>
#include <Tempest/Application>
#include <Tempest/Log>
#include <cmath>

#include "game/playercontrol.h"
#include "camera.h"
#include "gothic.h"
#include "resources.h"
#include "utils/gthfont.h"

#if defined(__ANDROID__)
#include "system/api/androidapi.h"
#endif

using namespace Tempest;

TouchInput* TouchInput::instance = nullptr;

static void drawCircle(Painter& p, float cx, float cy, float r, const Color& color) {
  p.setBrush(color);
  const int segments = 18;
  for(int i = 0; i < segments; ++i) {
    float a1 = float(i) * 2.0f * float(M_PI) / float(segments);
    float a2 = float(i + 1) * 2.0f * float(M_PI) / float(segments);
    p.drawTriangle(cx, cy, 0, 0,
                   cx + r * std::cos(a1), cy + r * std::sin(a1), 0, 0,
                   cx + r * std::cos(a2), cy + r * std::sin(a2), 0, 0);
  }
}

static void drawRing(Painter& p, float cx, float cy, float r, float thickness, const Color& color) {
  p.setPen(Pen(color, PaintDevice::Alpha, thickness));
  const int segments = 24;
  for(int i = 0; i < segments; ++i) {
    float a1 = float(i) * 2.0f * float(M_PI) / float(segments);
    float a2 = float(i + 1) * 2.0f * float(M_PI) / float(segments);
    p.drawLine(int(cx + r * std::cos(a1)), int(cy + r * std::sin(a1)),
               int(cx + r * std::cos(a2)), int(cy + r * std::sin(a2)));
  }
}

static void drawDirectionArrows(Painter& p, float cx, float cy, float r) {
  p.setBrush(Color(1.0f, 1.0f, 1.0f, 0.35f));
  float d = r * 0.70f;
  float s = r * 0.12f;
  // Up
  p.drawTriangle(cx, cy - d - s, 0, 0, cx - s, cy - d + s, 0, 0, cx + s, cy - d + s, 0, 0);
  // Down
  p.drawTriangle(cx, cy + d + s, 0, 0, cx - s, cy + d - s, 0, 0, cx + s, cy + d - s, 0, 0);
  // Left
  p.drawTriangle(cx - d - s, cy, 0, 0, cx - d + s, cy - s, 0, 0, cx - d + s, cy + s, 0, 0);
  // Right
  p.drawTriangle(cx + d + s, cy, 0, 0, cx + d - s, cy - s, 0, 0, cx + d - s, cy + s, 0, 0);
}

TouchInput::TouchInput(PlayerControl& ctrl) : ctrl(ctrl) {
  instance = this;
#if defined(__ANDROID__)
  AndroidApi::setTouchHook(&TouchInput::touchHook);
#endif
}

TouchInput::~TouchInput() {
#if defined(__ANDROID__)
  AndroidApi::setTouchHook(nullptr);
#endif
  if(instance == this)
    instance = nullptr;
}

bool TouchInput::touchHook(int action, float x, float y, int pointerId) {
  if(instance != nullptr)
    return instance->onTouch(action, x, y, pointerId);
  return false;
}

void TouchInput::updateLayout() {
  const float scale = Gothic::interfaceScale(this);
  const float W = float(w());
  const float H = float(h());

  joyRadius   = 90.0f * scale;
  defaultJoyX = 140.0f * scale;
  defaultJoyY = H - 140.0f * scale;

  if(!joyActive) {
    joyCenterX = defaultJoyX;
    joyCenterY = defaultJoyY;
  }

  // Action / Attack (Main big button)
  buttons[BtnAction]    = { W - 140.0f * scale, H - 130.0f * scale, 52.0f * scale, "ATK",    buttons[BtnAction].pointerId,    buttons[BtnAction].pressed };
  // Jump / Climb
  buttons[BtnJump]      = { W - 65.0f * scale,  H - 220.0f * scale, 40.0f * scale, "JUMP",   buttons[BtnJump].pointerId,      buttons[BtnJump].pressed };
  // Block / Parry
  buttons[BtnParade]    = { W - 220.0f * scale, H - 210.0f * scale, 40.0f * scale, "BLOCK",  buttons[BtnParade].pointerId,    buttons[BtnParade].pressed };
  // Weapon draw/sheathe
  buttons[BtnWeapon]    = { W - 140.0f * scale, H - 260.0f * scale, 40.0f * scale, "WEAPON", buttons[BtnWeapon].pointerId,    buttons[BtnWeapon].pressed };
  // Inventory
  buttons[BtnInventory] = { W - 65.0f * scale,  65.0f * scale,      36.0f * scale, "INV",    buttons[BtnInventory].pointerId, buttons[BtnInventory].pressed };
  // Quest Log
  buttons[BtnLog]       = { W - 150.0f * scale, 65.0f * scale,      36.0f * scale, "LOG",    buttons[BtnLog].pointerId,       buttons[BtnLog].pressed };
  // Pause / Menu (ESC)
  buttons[BtnMenu]      = { 65.0f * scale,       65.0f * scale,      36.0f * scale, "ESC",    buttons[BtnMenu].pointerId,      buttons[BtnMenu].pressed };
  // Sneak
  buttons[BtnSneak]     = { 260.0f * scale,     H - 100.0f * scale, 34.0f * scale, "SNEAK",  buttons[BtnSneak].pointerId,     buttons[BtnSneak].pressed };
}

void TouchInput::updateMenuLayout() {
  const float scale = Gothic::interfaceScale(this);
  const float W = float(w());
  const float H = float(h());

  // In Menu: UP, DOWN on left; ENTER, ESC on right
  menuButtons[MenuBtnUp]   = { 120.0f * scale,     H - 180.0f * scale, 44.0f * scale, "UP",    menuButtons[MenuBtnUp].pointerId,   menuButtons[MenuBtnUp].pressed };
  menuButtons[MenuBtnDown] = { 120.0f * scale,     H - 80.0f * scale,  44.0f * scale, "DOWN",  menuButtons[MenuBtnDown].pointerId, menuButtons[MenuBtnDown].pressed };
  menuButtons[MenuBtnEnter]= { W - 130.0f * scale, H - 110.0f * scale, 52.0f * scale, "ENTER", menuButtons[MenuBtnEnter].pointerId,menuButtons[MenuBtnEnter].pressed };
  menuButtons[MenuBtnEsc]  = { W - 65.0f * scale,  65.0f * scale,      38.0f * scale, "ESC",   menuButtons[MenuBtnEsc].pointerId,  menuButtons[MenuBtnEsc].pressed };
}

void TouchInput::releaseAll() {
  if(joyActive) {
    joyActive = false;
    joyPointerId = -1;
    ctrl.onKeyReleased(KeyCodec::Forward);
    ctrl.onKeyReleased(KeyCodec::Back);
    ctrl.onKeyReleased(KeyCodec::RotateL);
    ctrl.onKeyReleased(KeyCodec::RotateR);
  }
  camPointerId = -1;
  for(size_t i = 0; i < buttons.size(); ++i) {
    if(buttons[i].pressed) {
      buttons[i].pressed = false;
      buttons[i].pointerId = -1;
      triggerButtonRelease(ButtonId(i));
    }
  }
  for(size_t i = 0; i < menuButtons.size(); ++i) {
    if(menuButtons[i].pressed) {
      menuButtons[i].pressed = false;
      menuButtons[i].pointerId = -1;
      triggerMenuButtonRelease(MenuButtonId(i));
    }
  }
}

void TouchInput::processJoystick(float dx, float dy) {
  float dist = std::hypot(dx, dy);
  float maxR = joyRadius;

  if(dist > maxR && dist > 0.001f) {
    joyThumbX = joyCenterX + (dx / dist) * maxR;
    joyThumbY = joyCenterY + (dy / dist) * maxR;
  }

  const float deadzone = maxR * 0.22f;
  if(dist < deadzone) {
    ctrl.onKeyReleased(KeyCodec::Forward);
    ctrl.onKeyReleased(KeyCodec::Back);
    ctrl.onKeyReleased(KeyCodec::RotateL);
    ctrl.onKeyReleased(KeyCodec::RotateR);
    return;
  }

  // Vertical movement
  if(dy < -deadzone) {
    ctrl.onKeyPressed(KeyCodec::Forward, KeyEvent::K_NoKey);
    ctrl.onKeyReleased(KeyCodec::Back);
  } else if(dy > deadzone) {
    ctrl.onKeyPressed(KeyCodec::Back, KeyEvent::K_NoKey);
    ctrl.onKeyReleased(KeyCodec::Forward);
  } else {
    ctrl.onKeyReleased(KeyCodec::Forward);
    ctrl.onKeyReleased(KeyCodec::Back);
  }

  // Horizontal movement
  if(dx < -deadzone) {
    ctrl.onKeyPressed(KeyCodec::RotateL, KeyEvent::K_NoKey);
    ctrl.onKeyReleased(KeyCodec::RotateR);
  } else if(dx > deadzone) {
    ctrl.onKeyPressed(KeyCodec::RotateR, KeyEvent::K_NoKey);
    ctrl.onKeyReleased(KeyCodec::RotateL);
  } else {
    ctrl.onKeyReleased(KeyCodec::RotateL);
    ctrl.onKeyReleased(KeyCodec::RotateR);
  }
}

void TouchInput::triggerButtonPress(ButtonId id) {
  switch(id) {
    case BtnAction:
      ctrl.onKeyPressed(KeyCodec::ActionGeneric, KeyEvent::K_NoKey);
      break;
    case BtnJump:
      ctrl.onKeyPressed(KeyCodec::Jump, KeyEvent::K_NoKey);
      break;
    case BtnParade:
      ctrl.onKeyPressed(KeyCodec::Parade, KeyEvent::K_NoKey);
      break;
    case BtnWeapon:
      ctrl.onKeyPressed(KeyCodec::Weapon, KeyEvent::K_NoKey);
      break;
    case BtnInventory:
      ctrl.onKeyPressed(KeyCodec::Inventory, KeyEvent::K_NoKey);
      break;
    case BtnLog:
      ctrl.onKeyPressed(KeyCodec::Log, KeyEvent::K_NoKey);
      break;
    case BtnMenu:
      ctrl.onKeyPressed(KeyCodec::Escape, KeyEvent::K_NoKey);
      break;
    case BtnSneak:
      ctrl.onKeyPressed(KeyCodec::Sneak, KeyEvent::K_NoKey);
      break;
    default:
      break;
  }
}

void TouchInput::triggerButtonRelease(ButtonId id) {
  switch(id) {
    case BtnAction:
      ctrl.onKeyReleased(KeyCodec::ActionGeneric);
      break;
    case BtnJump:
      ctrl.onKeyReleased(KeyCodec::Jump);
      break;
    case BtnParade:
      ctrl.onKeyReleased(KeyCodec::Parade);
      break;
    case BtnWeapon:
      ctrl.onKeyReleased(KeyCodec::Weapon);
      break;
    case BtnInventory:
      ctrl.onKeyReleased(KeyCodec::Inventory);
      break;
    case BtnLog:
      ctrl.onKeyReleased(KeyCodec::Log);
      break;
    case BtnMenu:
      ctrl.onKeyReleased(KeyCodec::Escape);
      break;
    case BtnSneak:
      ctrl.onKeyReleased(KeyCodec::Sneak);
      break;
    default:
      break;
  }
}

void TouchInput::triggerMenuButtonPress(MenuButtonId id) {
#if defined(__ANDROID__)
  int code = 0;
  switch(id) {
    case MenuBtnUp:    code = 19; break; // KEYCODE_DPAD_UP
    case MenuBtnDown:  code = 20; break; // KEYCODE_DPAD_DOWN
    case MenuBtnEnter: code = 66; break; // KEYCODE_ENTER
    case MenuBtnEsc:   code = 4;  break; // KEYCODE_BACK
    default: break;
  }
  if(code != 0)
    AndroidApi::pushKeyEvent(0, code);
#else
  (void)id;
#endif
}

void TouchInput::triggerMenuButtonRelease(MenuButtonId id) {
#if defined(__ANDROID__)
  int code = 0;
  switch(id) {
    case MenuBtnUp:    code = 19; break; // KEYCODE_DPAD_UP
    case MenuBtnDown:  code = 20; break; // KEYCODE_DPAD_DOWN
    case MenuBtnEnter: code = 66; break; // KEYCODE_ENTER
    case MenuBtnEsc:   code = 4;  break; // KEYCODE_BACK
    default: break;
  }
  if(code != 0)
    AndroidApi::pushKeyEvent(1, code);
#else
  (void)id;
#endif
}

bool TouchInput::onTouch(int action, float x, float y, int pointerId) {
  bool inGame = Gothic::inst().isInGame() &&
                Gothic::inst().world() != nullptr &&
                Gothic::inst().checkLoading() == Gothic::LoadState::Idle;

  auto c = Gothic::inst().camera();
  if(c && c->isCutscene()) {
    releaseAll();
    return false;
  }

  // --- MENU TOUCH CONTROLS ---
  if(!inGame) {
    updateMenuLayout();
    if(action == 0 || action == 5) {
      for(size_t i = 0; i < menuButtons.size(); ++i) {
        auto& btn = menuButtons[i];
        float dx = x - btn.x;
        float dy = y - btn.y;
        float hitDist = btn.r * 1.35f;
        if(dx * dx + dy * dy <= hitDist * hitDist) {
          btn.pointerId = pointerId;
          btn.pressed = true;
          triggerMenuButtonPress(MenuButtonId(i));
          update();
          return true;
        }
      }
      return false; // let tap fall through to menu item clicks
    }
    if(action == 1 || action == 6 || action == 3) {
      for(size_t i = 0; i < menuButtons.size(); ++i) {
        auto& btn = menuButtons[i];
        if(btn.pointerId == pointerId) {
          btn.pointerId = -1;
          btn.pressed = false;
          triggerMenuButtonRelease(MenuButtonId(i));
          update();
          return true;
        }
      }
      return false;
    }
    return false;
  }

  // --- IN-GAME TOUCH CONTROLS ---
  updateLayout();

  // 1. DOWN (action == 0 or 5)
  if(action == 0 || action == 5) {
    // Check buttons first
    for(size_t i = 0; i < buttons.size(); ++i) {
      auto& btn = buttons[i];
      float dx = x - btn.x;
      float dy = y - btn.y;
      float hitDist = btn.r * 1.35f;
      if(dx * dx + dy * dy <= hitDist * hitDist) {
        btn.pointerId = pointerId;
        btn.pressed = true;
        triggerButtonPress(ButtonId(i));
        update();
        return true;
      }
    }

    // Check Joystick area (bottom-left 40% of width, bottom 65% of height)
    if(joyPointerId == -1 && x < float(w()) * 0.40f && y > float(h()) * 0.35f) {
      joyPointerId = pointerId;
      joyActive = true;
      float baseDist = std::hypot(x - defaultJoyX, y - defaultJoyY);
      if(baseDist < joyRadius * 1.5f) {
        joyCenterX = defaultJoyX;
        joyCenterY = defaultJoyY;
      } else {
        joyCenterX = x;
        joyCenterY = y;
      }
      joyThumbX = x;
      joyThumbY = y;
      processJoystick(x - joyCenterX, y - joyCenterY);
      update();
      return true;
    }

    // Camera swipe area (right 65% of screen, outside buttons)
    if(camPointerId == -1 && x >= float(w()) * 0.35f) {
      camPointerId = pointerId;
      camLastX = x;
      camLastY = y;
      return true;
    }

    return false;
  }

  // 2. MOVE (action == 2)
  if(action == 2) {
    if(pointerId == joyPointerId) {
      joyThumbX = x;
      joyThumbY = y;
      processJoystick(x - joyCenterX, y - joyCenterY);
      update();
      return true;
    }

    if(pointerId == camPointerId) {
      float dx = x - camLastX;
      float dy = y - camLastY;
      camLastX = x;
      camLastY = y;
      const float rotSpeed = 2.5f;
      ctrl.onRotateMouse(-dx * rotSpeed, dy * rotSpeed);
      return true;
    }

    for(const auto& btn : buttons) {
      if(btn.pointerId == pointerId)
        return true;
    }

    return false;
  }

  // 3. UP / CANCEL (action == 1, 6, or 3)
  if(action == 1 || action == 6 || action == 3) {
    if(pointerId == joyPointerId) {
      joyPointerId = -1;
      joyActive = false;
      ctrl.onKeyReleased(KeyCodec::Forward);
      ctrl.onKeyReleased(KeyCodec::Back);
      ctrl.onKeyReleased(KeyCodec::RotateL);
      ctrl.onKeyReleased(KeyCodec::RotateR);
      update();
      return true;
    }

    if(pointerId == camPointerId) {
      camPointerId = -1;
      return true;
    }

    for(size_t i = 0; i < buttons.size(); ++i) {
      auto& btn = buttons[i];
      if(btn.pointerId == pointerId) {
        btn.pointerId = -1;
        btn.pressed = false;
        triggerButtonRelease(ButtonId(i));
        update();
        return true;
      }
    }
  }

  return false;
}

void TouchInput::paintEvent(PaintEvent& e) {
  bool inGame = Gothic::inst().isInGame() &&
                Gothic::inst().world() != nullptr &&
                Gothic::inst().checkLoading() == Gothic::LoadState::Idle;

  auto c = Gothic::inst().camera();
  if(c && c->isCutscene())
    return;

  Painter p(e);
  const float scale = Gothic::interfaceScale(this);
  auto& fnt = Resources::font(scale);

  // --- PAINT MENU BUTTONS ---
  if(!inGame) {
    updateMenuLayout();
    for(const auto& btn : menuButtons) {
      Color bg     = btn.pressed ? Color(0.95f, 0.75f, 0.15f, 0.80f) : Color(0.12f, 0.15f, 0.18f, 0.55f);
      Color border = btn.pressed ? Color(1.0f, 0.95f, 0.60f, 0.95f) : Color(0.85f, 0.90f, 0.95f, 0.65f);
      drawCircle(p, btn.x, btn.y, btn.r, bg);
      drawRing(p, btn.x, btn.y, btn.r, 2.0f, border);

      int bw = int(btn.r * 2);
      int bh = int(btn.r * 2);
      fnt.drawText(p, int(btn.x - btn.r), int(btn.y - btn.r), bw, bh, btn.label, AlignHCenter | AlignVCenter);
    }
    return;
  }

  // --- PAINT IN-GAME BUTTONS & JOYSTICK ---
  updateLayout();

  // 1. Joystick
  float jBaseR = joyRadius;
  drawCircle(p, joyCenterX, joyCenterY, jBaseR, Color(0.10f, 0.12f, 0.15f, 0.40f));
  drawRing(p, joyCenterX, joyCenterY, jBaseR, 2.0f, Color(0.85f, 0.90f, 0.95f, 0.50f));
  drawDirectionArrows(p, joyCenterX, joyCenterY, jBaseR);

  // Thumb stick
  float thumbR = jBaseR * 0.42f;
  float tx = joyActive ? joyThumbX : joyCenterX;
  float ty = joyActive ? joyThumbY : joyCenterY;
  drawCircle(p, tx, ty, thumbR, joyActive ? Color(0.95f, 0.80f, 0.20f, 0.75f) : Color(0.55f, 0.60f, 0.65f, 0.50f));
  drawRing(p, tx, ty, thumbR, 2.0f, Color(1.0f, 1.0f, 1.0f, 0.75f));

  // 2. Action buttons
  for(const auto& btn : buttons) {
    Color bg     = btn.pressed ? Color(0.95f, 0.75f, 0.15f, 0.80f) : Color(0.12f, 0.15f, 0.18f, 0.50f);
    Color border = btn.pressed ? Color(1.0f, 0.95f, 0.60f, 0.95f) : Color(0.85f, 0.90f, 0.95f, 0.60f);
    drawCircle(p, btn.x, btn.y, btn.r, bg);
    drawRing(p, btn.x, btn.y, btn.r, 2.0f, border);

    int bw = int(btn.r * 2);
    int bh = int(btn.r * 2);
    fnt.drawText(p, int(btn.x - btn.r), int(btn.y - btn.r), bw, bh, btn.label, AlignHCenter | AlignVCenter);
  }
}

void TouchInput::mouseDownEvent(MouseEvent& e) {
  if(!onTouch(0, float(e.x), float(e.y), e.mouseID))
    e.ignore();
}

void TouchInput::mouseDragEvent(MouseEvent& e) {
  if(!onTouch(2, float(e.x), float(e.y), e.mouseID))
    e.ignore();
}

void TouchInput::mouseUpEvent(MouseEvent& e) {
  if(!onTouch(1, float(e.x), float(e.y), e.mouseID))
    e.ignore();
}
