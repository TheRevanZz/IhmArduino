#pragma once

#include <stdint.h>

static const uint8_t kGatewayMacAddress[6] = {0x90,0xDA,0x72,0x48,0x53,0x5C}; // Adresse de la passerelle (écriture hexadécimale)
constexpr uint8_t kWifiChannel = 5; // Canal wifi de la passerelle

#define DEVICE_NAME "sensor_d" // modifiez cette valeur avec le nom de votre capteur (sensor_<groupe>), ex : "sensor_a"