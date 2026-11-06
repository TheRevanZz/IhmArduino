#pragma once
#include "BaseController.h"
#include "Macro.h"
#include <MPS.h>

MAKE_SUBJECT_NO_DATA_SEND(Hb)

class HeartbeatController : public BaseController {

private:
  void HandleHeartbeat();
  void publishHeartbeat();
public:
  virtual void Update() override;
  virtual void Setup() override {};
  virtual void OnSessionOpened() { publishHeartbeat(); }

};