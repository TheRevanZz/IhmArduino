#include "ControllerManager.h"

void ControllerManager::setup()
{
	for (BaseController* const controller : controllers)
	{
		controller->setup();
	}

	MPS.on(kSubCmdReset, [](const mps::FrameView& message, void* userContext) {
		if (message.payloadLength != 0)
			return;

		auto* self = reinterpret_cast<ControllerManager*>(userContext);
		if (!self)
			return;

		self->reset();
	}, this);
}

void ControllerManager::onSessionOpened()
{
	for (BaseController* const controller : controllers)
	{
		controller->onSessionOpened();
	}
}

void ControllerManager::loop()
{
	for (BaseController* const controller : controllers)
	{
		controller->update();
	}
}

void ControllerManager::reset()
{
	for (BaseController* const controller : controllers)
	{
		controller->reset();
	}
}