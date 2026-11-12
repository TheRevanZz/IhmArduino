#pragma once
#include <initializer_list>
#include "BaseController.h"
#include "SubjectHelper.h"
#include <vector>

// MAKE_SUBJECT(Cmd, Reset)
CREATE_SUBJECT(kSubCmdReset, Reset)

class ControllerManager {

public:
  ControllerManager(std::initializer_list<BaseController*> _controllers) : 
    controllers{_controllers.begin(), _controllers.end()} {}

  void setup();
  void onSessionOpened();
  void loop();
  void reset();  

private:
  std::vector<BaseController*> controllers;
  
};