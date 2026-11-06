#pragma once

#include <MPS.h>
#include "config.h"
#include "BaseController.h"
#include "Macro.h"

MAKE_SUBJECT(Status,Counter)
MAKE_SUBJECT_BIS(Cmd,ResetCounter,reset_counter)

class CounterController : public BaseController {

public:
  struct __attribute__((packed)) Payload {
    uint32_t value;
  } counter = { 0 };

private:
  void OnCmdReset(const mps::FrameView& message);

  void handleCounterTimer();
  void publishCounter();

public:

  CounterController();

  virtual void Update() override;
  virtual void Setup() override;
  virtual void OnSessionOpened() override;
};