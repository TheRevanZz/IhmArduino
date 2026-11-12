#include "CounterController.h"


CounterController::CounterController() {
  payloadLength = 4;
  static_assert(sizeof(Payload) == sizeof(uint32_t), "Payload = 4 octets");
}

void CounterController::onCmdReset(const mps::FrameView& message) {
  if (!verifyPayloadLength(message, 0))
    return;
  counter.value = 0;
  MPS.publishValue(kSubStatusCounter, counter);  // l'IHM voit le r�sultat r�el
}

void CounterController::handleCounterTimer() {
  static uint32_t lastCounterTimeMs = 0;
  if (millis() - lastCounterTimeMs >= 2000) {
    lastCounterTimeMs = millis();
    counter.value++;
    publishCounter();
  }
}

void CounterController::publishCounter()
{
    MPS.publishValue(kSubStatusCounter, counter);
}

void CounterController::update()
{
    handleCounterTimer();
}

void CounterController::setup()
{
    MPS.on(
        kSubCmdResetCounter, [](const mps::FrameView& message, void* userContext) {
            auto* self = static_cast<CounterController*>(userContext);
            self->onCmdReset(message);
        },
        this);
}

void CounterController::onSessionOpened()
{
    publishCounter();
}
