#include "Controllers/BaseController.h"

bool BaseController::VerifyPayloadLength(const mps::FrameView& message, const uint8_t length)
{
	return message.payloadLength == length;
}