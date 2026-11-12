#pragma once

#include <array>
#include "SubjectHelper.h"
#include "BaseController.h"

//Subject permettant de gérer la couleur de la led. Quand celle-ci est en noir, la led s'éteint
CREATE_SUBJECT(kSubSetLightColor, light_color)
//Status permettant d'informer Godot sur l'état de la lumière
CREATE_SUBJECT(kSubStatusLightColor, light_color)

//Subject Set permettant de gérer le nombre de secondes a maintenir le bouton pour allumer/éteindre la led
CREATE_SUBJECT(kSubSetSwitchThreshold, switch_threshold)
CREATE_SUBJECT(kSubStatusSwitchThreshold, switch_threshold)

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

  uint8_t colorCursor = 0;
  bool holdingButton = false;
  uint8_t clickThreshold = 100;
  //En Millisecondes, le temps qu'il faut maintenir le bouton pour allumer/éteindre la led
  uint16_t switchLightThresholdMs = 5000;
  bool ignoreNextButtonRelease = false;
  
  void onSetLightColor(const mps::FrameView& message);
  void onSetSwitchThreshold(const mps::FrameView& message);
  void publishPayload();
  void changeLedColor(const uint8_t color[3]);
  void changeSwitchThreshold(uint8_t threshold);

  void lightControl();
  uint8_t getColorIndex(uint8_t color[3], bool& valid);
  void updateColorField(const uint8_t color[3]);

public:
  virtual void update() override;
  virtual void setup() override;
  virtual void onSessionOpened() override;
};