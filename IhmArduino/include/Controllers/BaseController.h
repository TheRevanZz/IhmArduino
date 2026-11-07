#pragma once

#include <MPS.h>

class BaseController 
{

protected:
	BaseController() {};

protected: 
  bool VerifyPayloadLength(const mps::FrameView& , uint8_t length);

  uint8_t payload_length = 4;

public:
  virtual void OnSessionOpened(){}
  virtual void Setup() = 0;
  virtual void Update() = 0;
  virtual void Reset(){}
  
};