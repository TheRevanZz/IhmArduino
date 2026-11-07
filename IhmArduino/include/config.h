#pragma once

#include <stdint.h>
#include <array>
#include <MPS.h>

static const uint8_t kGatewayMacAddress[6] = {0xF8,0x5B,0x1B,0x05,0x71,0xB4}; // Adresse de la passerelle (écriture hexadécimale)
constexpr uint8_t kWifiChannel = 7; // Canal wifi de la passerelle

#define DEVICE_NAME "sensor_d" // modifiez cette valeur avec le nom de votre capteur (sensor_<groupe>), ex : "sensor_a"

#define G3_BUTTON 3
#define LED 2

#define PREFIX_SUBJECT "kSub"

using Color_t = uint8_t[3];

inline std::array<Color_t,33> colors = {{
    {0,0,0},
    {255,   66,   0}, // Rou3ge
    {  0, 255,   0}, // Vert
    {  0,   0, 255}, // Bleu
    {255, 255,   0}, // Jaune
    {255,   0, 255}, // Magenta
    {  0, 255, 255}, // Cyan
    {255, 128,   0}, // Orange
    {128,   0, 255}, // Violet

    {255, 128, 128}, // Rose clair
    {128, 255, 128}, // Vert clair
    {128, 128, 255}, // Bleu clair
    {255, 255, 128}, // Jaune clair
    {255, 128, 255}, // Rose
    {128, 255, 255}, // Cyan clair
    {128,  64,   0}, // Marron
    {128, 128, 128}, // Gris

    { 64,   0,   0}, // Rouge foncé
    {  0,  64,   0}, // Vert foncé
    {  0,   0,  64}, // Bleu foncé
    { 64,  64,   0}, // Olive
    { 64,   0,  64}, // Prune
    {  0,  64,  64}, // Sarcelle
    {192,  64,  64}, // Rouge brique
    { 64, 192,  64}, // Vert printemps

    { 64, 128, 192}, // Bleu acier
    {192, 128,  64}, // Ocre
    {192,  64, 192}, // Fuchsia
    { 64, 192, 192}, // Turquoise
    {255, 192, 128}, // Pêche
    {192, 192, 192}, // Argent
    { 32,  32,  32}, // Gris foncé
    {240, 240, 240}  // Blanc cassé
}};




