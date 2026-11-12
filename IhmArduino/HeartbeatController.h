#pragma once

#include "BaseController.h"
#include "SubjectHelper.h"

CREATE_SUBJECT(kSubHb,)

class HeartbeatController : public BaseController {

private:
  void handleHeartbeat();
  void publishHeartbeat();

public:
  virtual void update() override;
  virtual void setup() override {};
  virtual void onSessionOpened() { publishHeartbeat(); }

};