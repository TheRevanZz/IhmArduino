#pragma once

#include <array>
#include "Helpers/SubjectHelper.h"
#include "Controllers/BaseController.h"

//Subject permettant de gérer la couleur de la led. Quand celle-ci est en noir, la led s'éteint
MAKE_SUBJECT_BIS(Set, LightColor, light_color)
//Status permettant d'informer Godot sur l'état de la lumière
MAKE_SUBJECT_BIS(Status, LightColor, light_color)

//Subject Set permettant de gérer le nombre de secondes a maintenir le bouton pour allumer/éteindre la led
MAKE_SUBJECT_BIS(Set,SwitchThreshold, switch_threshold)
MAKE_SUBJECT_BIS(Status,SwitchThreshold, switch_threshold)

class LedController : public BaseController {

public:

  LedController();

  //Contient les données concernant la lumière
  struct __attribute__((packed)) LedDataPayload {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    std::array<uint8_t,3> color() const {
      std::array<uint8_t,3> result{r,g,b};
      return result;
    }
  } ledDataPayload;

  //Contient le nombre de secondes a maintenir le bouton pour allumer/éteindre la led
  struct __attribute__((packed)) LedSwitchThresholdPayload {
    uint8_t switch_threshold = 5;
  } switchThresholdPayload;

private:

  uint8_t color_cursor = 0;
  bool holding_button = false;
  uint8_t click_threshold = 100;
  //En Millisecondes, le temps pour maintenir le bouton pour allumer/éteindre la led
  uint16_t switch_light_threshold_ms = 5000;
  bool ignore_next_button_release = false;
  
  void OnSetLightColor(const mps::FrameView& message);
  void OnSetSwitchThreshold(const mps::FrameView& message);
  void PublishPayload();
  void ChangeLedColor(const uint8_t color[3]);
  void ChangeSwitchThreshold(uint8_t threshold);

  void LightControl();
  uint8_t GetColorIndex(uint8_t color[3], bool& valid);
  void UpdateColorField(const uint8_t color[3]);

public:
  virtual void Update() override;
  virtual void Setup() override;
  virtual void OnSessionOpened() override;
};