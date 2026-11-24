#include "Controllers/TemperatureHumidityController.h"

TemperatureHumidityController::TemperatureHumidityController()
{
    payloadLength = sizeof(float) * 2;
    static_assert(sizeof(TemperatureHumidityPayload) == sizeof(float) * 2, "TemperatureHumidityPayload = 8 octets");
}

void TemperatureHumidityController::publishTemperatureHumidity()
{
    MPS.publishValue(kSubStatusTemperatureHumidity, TemperatureHumidityPayload{temp, hum});
}

void TemperatureHumidityController::handleTemperatureHumidityTimer()
{
    static uint32_t lastCounterTimeMs = 0;
    temp = dht.readTemperature();
    hum = dht.readHumidity();
    if (std::isnan(temp) || std::isnan(hum))
    {
        Serial.print(F("Impossible de lire la température ou l'humidité"));
        return;
    }
    if (millis() - lastCounterTimeMs >= 500)
    {
        lastCounterTimeMs = millis();
        publishTemperatureHumidity();
    }
}

void TemperatureHumidityController::update()
{
    handleTemperatureHumidityTimer();
}

void TemperatureHumidityController::setup()
{
    Serial.println(F("DHT11 test!"));
    dht.begin();
}

void TemperatureHumidityController::onSessionOpened()
{
    temp = dht.readTemperature();
    hum = dht.readHumidity();
    publishTemperatureHumidity();
}
