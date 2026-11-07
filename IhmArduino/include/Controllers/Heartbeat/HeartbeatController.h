#pragma once

#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"

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