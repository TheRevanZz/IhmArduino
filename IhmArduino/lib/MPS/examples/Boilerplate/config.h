#pragma once

#include <stdint.h>

static const uint8_t kGatewayMacAddress[6] = {0x00,0x00,0x00,0x00,0x00,0x00}; // Adresse de la passerelle (écriture hexadécimale)
constexpr uint8_t kWifiChannel = 1; // Canal wifi de la passerelle

#define DEVICE_NAME "sensor_boiler" // modifiez cette valeur avec le nom de votre capteur (sensor_<groupe>), ex : "sensor_a"