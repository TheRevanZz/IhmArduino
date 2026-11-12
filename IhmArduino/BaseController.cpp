#include "BaseController.h"

bool BaseController::verifyPayloadLength(const mps::FrameView& message, const uint8_t length)
{
	return message.payloadLength == length;
}