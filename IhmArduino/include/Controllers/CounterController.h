#pragma once

#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"

//Status utilisé pour indiqué à l'IHM que le compteur a changer
CREATE_SUBJECT(kSubStatusCounter,Counter)

//Commande permettant de reinitialiser le compteur
CREATE_SUBJECT(kSubCmdResetCounter,reset_counter)


class CounterController : public BaseController {

public:
  struct __attribute__((packed)) Payload {
    uint32_t value;
  } counter = { 0 };

  CounterController();  

private:
  void onCmdReset(const mps::FrameView& message);

  void handleCounterTimer();
  void publishCounter();

public:
  virtual void update() override;
  virtual void setup() override;
  virtual void onSessionOpened() override;
};