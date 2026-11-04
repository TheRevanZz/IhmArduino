#pragma once

#include <stdint.h>
#include <vector>

static const uint8_t kGatewayMacAddress[6] = {0x90,0xDA,0x72,0x52,0xEE,0x1C}; // Adresse de la passerelle (écriture hexadécimale)
constexpr uint8_t kWifiChannel = 5; // Canal wifi de la passerelle

#define DEVICE_NAME "sensor_d" // modifiez cette valeur avec le nom de votre capteur (sensor_<groupe>), ex : "sensor_a"

#define G3_BUTTON 3
#define LED 2

std::array<std::array<uint8_t,3>,33> Colors = {{
    {0,0,0},
    {255,   0,   0}, // Rouge
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

inline void ChangeLedColor(const std::array<uint8_t,3>& color)
{
  rgbLedWrite(LED, color[0], color[1], color[2]);
}


