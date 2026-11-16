#pragma once

#include <MPS.h>

class BaseController 
{

protected:
	BaseController() {};

protected: 
  bool verifyPayloadLength(const mps::FrameView& , uint8_t length);

  uint8_t payloadLength = 4;

public:
  virtual void onSessionOpened(){}
  virtual void setup() = 0;
  virtual void update() = 0;
  virtual void reset(){}
  
};