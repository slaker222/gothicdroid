#pragma once

#include <Tempest/Widget>
#include <Tempest/Point>
#include <array>

class PlayerControl;

class TouchInput : public Tempest::Widget {
  public:
    explicit TouchInput(PlayerControl& ctrl);
    ~TouchInput() override;

    bool onTouch(int action, float x, float y, int pointerId);

    void paintEvent(Tempest::PaintEvent& e) override;
    void mouseDownEvent(Tempest::MouseEvent& e) override;
    void mouseDragEvent(Tempest::MouseEvent& e) override;
    void mouseUpEvent(Tempest::MouseEvent& e) override;

  private:
    enum ButtonId {
      BtnLMB = 0,
      BtnRMB,
      BtnUp,
      BtnDown,
      BtnEnter,
      BtnJump,
      BtnWeapon,
      BtnInventory,
      BtnLog,
      BtnMenu,
      BtnSneak,
      BtnSkip,
      BtnCount
    };

    enum MenuButtonId {
      MenuBtnUp = 0,
      MenuBtnDown,
      MenuBtnEnter,
      MenuBtnEsc,
      MenuBtnCount
    };

    struct TouchBtn {
      float x = 0;
      float y = 0;
      float r = 0;
      const char* label = "";
      int pointerId = -1;
      bool pressed = false;
    };

    PlayerControl& ctrl;

    int   joyPointerId = -1;
    float joyCenterX = 0;
    float joyCenterY = 0;
    float joyThumbX = 0;
    float joyThumbY = 0;
    float joyRadius = 0;
    float defaultJoyX = 0;
    float defaultJoyY = 0;
    bool  joyActive = false;

    int   camPointerId = -1;
    float camLastX = 0;
    float camLastY = 0;

    std::array<TouchBtn, BtnCount>     buttons;
    std::array<TouchBtn, MenuBtnCount> menuButtons;

    void updateLayout();
    void updateMenuLayout();
    void releaseAll();
    void processJoystick(float dx, float dy);
    void triggerButtonPress(ButtonId id);
    void triggerButtonRelease(ButtonId id);
    void triggerMenuButtonPress(MenuButtonId id);
    void triggerMenuButtonRelease(MenuButtonId id);

    static TouchInput* instance;
    static bool touchHook(int action, float x, float y, int pointerId);
  };
