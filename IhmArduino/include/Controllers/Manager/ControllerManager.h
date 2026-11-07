#pragma once
#include <initializer_list>
#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"
#include <vector>

MAKE_SUBJECT(Cmd, Reset)

class ControllerManager {

public:
  ControllerManager(std::initializer_list<BaseController*> _controllers) : 
    controllers{_controllers.begin(), _controllers.end()} {}

  void Setup();
  void OnSessionOpened();
  void Loop();
  void Reset();  

private:
  std::vector<BaseController*> controllers;
  
};