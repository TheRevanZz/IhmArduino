#include "LedController.h"


LedController::LedController() {
  payload_length = 3;
  static_assert(sizeof(payload) == 3);
  UpdateColorField(Colors[color_cursor]);
}

void LedController::Update() {
  LightControl();
}

void LedController::Setup() {

  MPS.on(
    kSubSetLightColor, [](const mps::FrameView& message, void* userContext) {
      auto* self = reinterpret_cast<LedController*>(userContext);
      if(!self)
        return;
      self->OnSetLightColor(message);
    },
    this);
    ChangeLedColor(payload.color().data());
}

void LedController::OnSessionOpened() {
  ChangeLedColor(payload.color().data());
}

void LedController::OnSetLightColor(const mps::FrameView& message) {
  //if (!VerifyPayloadLength(message, payload_length))
  //  return;

  LedController::Payload p;
  if (mps::readPayloadAs(message, &p)) {
    ChangeLedColor(p.color().data());
  }
  else
  {
      uint8_t c[] = { 0,255,0 };
      ChangeLedColor(c);
  }
  color_cursor = 1;
}

void LedController::PublishPayload() {
  MPS.publishValue(kSubStatusLightColor, payload);
}

void LedController::ChangeLedColor(const uint8_t color[3]) {
  UpdateColorField(color);
  rgbLedWrite(LED, color[0], color[1], color[2]);
  PublishPayload();
}

void LedController::LightControl() {
  static uint32_t buttonTimer = 0;

  if (!digitalRead(G3_BUTTON)) {


    const auto interval = millis() - buttonTimer;

    if (interval >= click_threshold && interval <= click_threshold * 1.2f && !action_done_change_color && color_cursor != 0) {
      action_done_change_color = true;
      color_cursor++;
      color_cursor %= Colors.size();
      if (color_cursor == 0)
        color_cursor = 1;
      ChangeLedColor(Colors[color_cursor]);
    }

    else if (interval >= switch_light_threshold && interval <= switch_light_threshold * 1.2f && action_done_switch_power == false) {
      action_done_switch_power = true;
      if (color_cursor == 0) {
        bool valid = true;
        color_cursor = GetColorIndex(payload.color().data(), valid);
        if (color_cursor == 0)
          color_cursor = 1;
        if (!valid)
          return;
      } else {
        color_cursor = 0;
        uint8_t c[] = {0,0,0};
        ChangeLedColor(c);
      }
      ChangeLedColor(Colors[color_cursor]);
    }

    holding_button = true;

  } else {
    action_done_switch_power = false;
    action_done_change_color = false;
    buttonTimer = millis();
    holding_button = false;
  }
}

uint8_t LedController::GetColorIndex(uint8_t color[3], bool& valid) {
  auto it = std::find_if(Colors.begin(), Colors.end(), [&color](const auto& c) {
    return c[0] == color[0]
           && c[1] == color[1]
           && c[2] == color[2];
  });
  if (it == Colors.end()) {
    valid = false;
    return 0;
  }

  valid = true;
  return static_cast<uint8_t>(it - Colors.begin());
}

void LedController::UpdateColorField(const uint8_t color[3]) {
    payload.r = color[0];
    payload.g = color[1];
    payload.b = color[2];
}
