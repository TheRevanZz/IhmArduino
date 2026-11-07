#pragma once

#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"

CREATE_SUBJECT(kSubHb,)

class HeartbeatController : public BaseController {

private:
  void HandleHeartbeat();
  void publishHeartbeat();

public:
  virtual void Update() override;
  virtual void Setup() override {};
  virtual void OnSessionOpened() { publishHeartbeat(); }

};