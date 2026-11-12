#include "LedController.h"

LedController::LedController()
	: ledDataPayload{
		  .r = 0,
		  .g = 0,
		  .b = 0},
	  switchThresholdPayload{
		  .switch_threshold = 2}
{
	payloadLength = 3;
	static_assert(sizeof(ledDataPayload) == sizeof(uint8_t) * 3);
	static_assert(sizeof(switchThresholdPayload) == sizeof(uint8_t));
}

void LedController::update()
{
	lightControl();
}

void LedController::setup()
{

	Serial.println("LED CONTROLLER SETUP");

	MPS.on(
		kSubSetLightColor, [](const mps::FrameView &message, void *userContext)
		{
        auto* self = reinterpret_cast<LedController*>(userContext);
        if(!self)
          return;
        if (!self->verifyPayloadLength(message, self->payloadLength))
          return; 
        self->onSetLightColor(message); },
		this);

	MPS.on(
		kSubSetSwitchThreshold, [](const mps::FrameView &message, void *userContext)
		{
        auto* self = reinterpret_cast<LedController*>(userContext);
        if(!self)
          return;
        if (!self->verifyPayloadLength(message, 1))
          return; 
        self->onSetSwitchThreshold(message); },
		this);

	onSessionOpened();
}

void LedController::onSessionOpened()
{
	changeLedColor(ledDataPayload.color().data());
	changeSwitchThreshold(switchThresholdPayload.switch_threshold);
}

void LedController::onSetLightColor(const mps::FrameView &message)
{
	LedController::LedDataPayload p;
	if (mps::readPayloadAs(message, &p))
	{
		changeLedColor(p.color().data());
	}
	// La couleur a été Set depuis l'IHM, on met le cursor de couleur à un comme si ça si on veut rechanger de couleurs depuis le capteur, on repart du 1
	colorCursor = 1;
}

void LedController::publishPayload()
{
	MPS.publishValue(kSubStatusLightColor, ledDataPayload);
}

void LedController::changeLedColor(const uint8_t color[3])
{
	updateColorField(color);
	rgbLedWrite(LED, color[0], color[1], color[2]);
	publishPayload();
}

void LedController::lightControl()
{
	static uint32_t buttonTimer = 0;

	const bool buttonPressed = !digitalRead(G3_BUTTON);

	if (buttonPressed && !holdingButton)
	{
		buttonTimer = millis();
		holdingButton = true;
	}
	else if (buttonPressed && holdingButton)
	{
		const auto interval = millis() - buttonTimer;

		if (interval >= switchLightThresholdMs)
		{
			if (colorCursor == 0)
			{
				bool valid = false;
				colorCursor = getColorIndex(ledDataPayload.color().data(), valid);
				if (colorCursor == 0)
					colorCursor = 1;
				if (!valid)
					return;
			}
			else
			{
				colorCursor = 0;
			}
			changeLedColor(colors[colorCursor]);
			holdingButton = false;
			ignoreNextButtonRelease = true;
		}

	}
	else if (!buttonPressed && holdingButton)
	{
		const auto interval = millis() - buttonTimer;

		if (interval >= clickThreshold && colorCursor != 0 && !ignoreNextButtonRelease)
		{
			colorCursor++;
			colorCursor %= colors.size();
			if (colorCursor == 0)
				colorCursor = 1;
			changeLedColor(colors[colorCursor]);
		}
		else if (ignoreNextButtonRelease)
		{
			ignoreNextButtonRelease = false;
		}

		holdingButton = false;
	}
}

uint8_t LedController::getColorIndex(uint8_t color[3], bool &valid)
{
	auto it = std::find_if(colors.begin(), colors.end(), [&color](const auto &c)
						   { return c[0] == color[0] && c[1] == color[1] && c[2] == color[2]; });
	if (it == colors.end())
	{
		valid = false;
		return 0;
	}

	valid = true;
	return static_cast<uint8_t>(it - colors.begin());
}

void LedController::updateColorField(const uint8_t color[3])
{
	ledDataPayload.r = color[0];
	ledDataPayload.g = color[1];
	ledDataPayload.b = color[2];
}

void LedController::onSetSwitchThreshold(const mps::FrameView &message)
{
	if (mps::readPayloadAs(message, &switchThresholdPayload))
	{
		changeSwitchThreshold(switchThresholdPayload.switch_threshold);
	}
}

void LedController::changeSwitchThreshold(uint8_t threshold)
{
	switchThresholdPayload.switch_threshold = threshold;
	switchLightThresholdMs = static_cast<uint16_t>(threshold) * (uint16_t)1000;
	MPS.publishValue(kSubStatusSwitchThreshold, switchThresholdPayload);
}
