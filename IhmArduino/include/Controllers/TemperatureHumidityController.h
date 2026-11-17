#pragma once

#include <DHT.h>
#include <DHT_U.h>
#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"
#include "config.h"

// Status utilisé pour indiqué à l'IHM que le compteur a changer
CREATE_SUBJECT(kSubStatusTemperatureHumidity, temperature_humidity)

inline DHT dht(DHTPIN, DHTTYPE);

class TemperatureHumidityController : public BaseController
{

public:
    struct __attribute__((packed)) TemperatureHumidityPayload
    {
        float temperature;
        float humidity;
    } tempHum = {0, 0};

    TemperatureHumidityController();

private:
    void publishTemperatureHumidity();
    void handleTemperatureHumidityTimer();
    float temp = 0;
    float hum = 0;


public:
    virtual void update() override;
    virtual void setup() override;
    virtual void onSessionOpened() override;
};