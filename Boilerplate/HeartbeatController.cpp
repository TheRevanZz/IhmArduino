#include "HeartbeatController.h"

void HeartbeatController::HandleHeartbeat() {
    static uint32_t lastHeartbeatTimeMs = 0;
    static const uint16_t kHbPeriod = 1000;
    if (millis() - lastHeartbeatTimeMs >= kHbPeriod) {
        lastHeartbeatTimeMs = millis();

        // L'envoi de heartbeats depuis loop() assure au récepteur (IHM) que le code "métier" n'est pas planté
        publishHeartbeat();
    }
}

void HeartbeatController::publishHeartbeat()
{
    MPS.publishHeartbeat(kSubHb);
}

void HeartbeatController::Update()
{
    HandleHeartbeat();
}
