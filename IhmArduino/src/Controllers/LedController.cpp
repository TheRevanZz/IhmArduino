#include "Controllers/Led/LedController.h"

LedController::LedController()
	: ledDataPayload{
		  .r = 0,
		  .g = 0,
		  .b = 0},
	  switchThresholdPayload{
		  .switch_threshold = 2}

{
	payload_length = 3;
	static_assert(sizeof(ledDataPayload) == 3);
	static_assert(sizeof(switchThresholdPayload) == 1);
}

void LedController::Update()
{
	LightControl();
}

void LedController::Setup()
{

	Serial.println("LED CONTROLLER SETUP");

	MPS.on(
		kSubSetLightColor, [](const mps::FrameView &message, void *userContext)
		{
        auto* self = reinterpret_cast<LedController*>(userContext);
        if(!self)
          return;
        if (!self->VerifyPayloadLength(message, self->payload_length))
          return; 
        self->OnSetLightColor(message); },
		this);

	MPS.on(
		kSubSetSwitchThreshold, [](const mps::FrameView &message, void *userContext)
		{
        auto* self = reinterpret_cast<LedController*>(userContext);
        if(!self)
          return;
        if (!self->VerifyPayloadLength(message, 1))
          return; 
        self->OnSetSwitchThreshold(message); },
		this);

	OnSessionOpened();
}

void LedController::OnSessionOpened()
{
	ChangeLedColor(ledDataPayload.color().data());
	ChangeSwitchThreshold(switchThresholdPayload.switch_threshold);
}

void LedController::OnSetLightColor(const mps::FrameView &message)
{
	LedController::LedDataPayload p;
	if (mps::readPayloadAs(message, &p))
	{
		ChangeLedColor(p.color().data());
	}
	// La couleur a été Set depuis l'IHM, on met le cursor de couleur à un comme si ça si on veut rechanger de couleurs depuis le capteur, on repart du 1
	color_cursor = 1;
}

void LedController::PublishPayload()
{
	MPS.publishValue(kSubStatusLightColor, ledDataPayload);
}

void LedController::ChangeLedColor(const uint8_t color[3])
{
	UpdateColorField(color);
	rgbLedWrite(LED, color[0], color[1], color[2]);
	PublishPayload();
}

void LedController::LightControl()
{
	static uint32_t buttonTimer = 0;

	const bool buttonPressed = !digitalRead(G3_BUTTON);

	if (buttonPressed && !holding_button)
	{
		buttonTimer = millis();
		holding_button = true;
	}
	else if (buttonPressed && holding_button)
	{
		const auto interval = millis() - buttonTimer;

		if (interval >= switch_light_threshold_ms)
		{
			if (color_cursor == 0)
			{
				bool valid = false;
				color_cursor = GetColorIndex(ledDataPayload.color().data(), valid);
				if (color_cursor == 0)
					color_cursor = 1;
				if (!valid)
					return;
			}
			else
			{
				color_cursor = 0;
			}
			ChangeLedColor(Colors[color_cursor]);
			holding_button = false;
			ignore_next_button_release = true;
		}

	}
	else if (!buttonPressed && holding_button)
	{
		const auto interval = millis() - buttonTimer;

		if (interval >= click_threshold && color_cursor != 0 && !ignore_next_button_release)
		{
			color_cursor++;
			color_cursor %= Colors.size();
			if (color_cursor == 0)
				color_cursor = 1;
			ChangeLedColor(Colors[color_cursor]);
		}
		else if (ignore_next_button_release)
		{
			ignore_next_button_release = false;
		}

		holding_button = false;
	}
}

uint8_t LedController::GetColorIndex(uint8_t color[3], bool &valid)
{
	auto it = std::find_if(Colors.begin(), Colors.end(), [&color](const auto &c)
						   { return c[0] == color[0] && c[1] == color[1] && c[2] == color[2]; });
	if (it == Colors.end())
	{
		valid = false;
		return 0;
	}

	valid = true;
	return static_cast<uint8_t>(it - Colors.begin());
}

void LedController::UpdateColorField(const uint8_t color[3])
{
	ledDataPayload.r = color[0];
	ledDataPayload.g = color[1];
	ledDataPayload.b = color[2];
}

void LedController::OnSetSwitchThreshold(const mps::FrameView &message)
{
	if (mps::readPayloadAs(message, &switchThresholdPayload))
	{
		ChangeSwitchThreshold(switchThresholdPayload.switch_threshold);
	}
}

void LedController::ChangeSwitchThreshold(uint8_t threshold)
{
	switchThresholdPayload.switch_threshold = threshold;
	switch_light_threshold_ms = static_cast<uint16_t>(threshold) * (uint16_t)1000;
	MPS.publishValue(kSubStatusSwitchThreshold, switchThresholdPayload);
}
