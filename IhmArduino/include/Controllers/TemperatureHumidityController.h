#pragma once

#include <DHT.h>
#include <DHT_U.h>
#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"
#include "config.h"

// Subject permettant d'envoyer la température et l'humidité à Godot
CREATE_SUBJECT(kSubStatusTemperatureHumidity, temperature_humidity)

inline DHT dht(DHTPIN, DHTTYPE);

class TemperatureHumidityController : public BaseController
{

public:
    // Contient les données concernant la température et l'humidité
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