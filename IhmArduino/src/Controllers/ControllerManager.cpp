#include "Controllers/Manager/ControllerManager.h"

void ControllerManager::Setup()
{
	for (BaseController* const controller : controllers)
	{
		controller->Setup();
	}

	MPS.on(kSubCmdReset, [](const mps::FrameView& message, void* userContext) {
		if (message.payloadLength != 0)
			return;

		auto* self = reinterpret_cast<ControllerManager*>(userContext);
		if (!self)
			return;

		self->Reset();
	}, this);
}

void ControllerManager::OnSessionOpened()
{
	for (BaseController* const controller : controllers)
	{
		controller->OnSessionOpened();
	}
}

void ControllerManager::Loop()
{
	for (BaseController* const controller : controllers)
	{
		controller->Update();
	}
}

void ControllerManager::Reset()
{
	for (BaseController* const controller : controllers)
	{
		controller->Reset();
	}
}