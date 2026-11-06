#pragma once

#include <MPS.h>
#include "config.h"
#include <array>
#include "Macro.h"
#include "BaseController.h"

//Subject permettant de gérer la couleur de la led. Quand celle-ci est en noir, la led s'éteint
MAKE_SUBJECT_BIS(Set, LightColor, light_color)
//Status permettant d'informer Godot sur l'état de la lumière
MAKE_SUBJECT_BIS(Status, LightColor, light_color)

class LedController : public BaseController {

public:

  LedController();

  //Contient les données concernant la lumière
  struct __attribute__((packed)) Payload {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    std::array<uint8_t,3> color() const { return {r,g,b}; }
  };

  Payload payload;


private:

  uint8_t color_cursor = 0;
  bool holding_button = false;
  uint8_t click_threshold = 100;
  uint16_t switch_light_threshold = 5000;
  bool action_done_switch_power = false;
  bool action_done_change_color = false;
  
  void OnSetLightColor(const mps::FrameView& message);
  void PublishPayload();
  void ChangeLedColor(const uint8_t color[3]);

  void LightControl();
  uint8_t GetColorIndex(uint8_t color[3], bool& valid);
  void UpdateColorField(const uint8_t color[3]);

public:
  virtual void Update() override;
  virtual void Setup() override;
  virtual void OnSessionOpened() override;
};