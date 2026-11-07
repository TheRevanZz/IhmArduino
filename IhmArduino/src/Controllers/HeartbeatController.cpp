#include "Controllers/HeartbeatController.h"

void HeartbeatController::handleHeartbeat() {
    static uint32_t lastHeartbeatTimeMs = 0;
    static const uint16_t kHbPeriod = 1000;
    if (millis() - lastHeartbeatTimeMs >= kHbPeriod) {
        lastHeartbeatTimeMs = millis();

        // L'envoi de heartbeats depuis loop() assure au r�cepteur (IHM) que le code "m�tier" n'est pas plant�
        publishHeartbeat();
    }
}

void HeartbeatController::publishHeartbeat()
{
    MPS.publishHeartbeat(kSubHb);
}

void HeartbeatController::update()
{
    handleHeartbeat();
}
