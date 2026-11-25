#pragma once

#include <stdint.h>
#include <array>
#include <MPS.h>

/// @brief Adresse MAC de la passerelle MPS à laquelle le nœud doit se connecter.
/// Les octets sont écrits au format hexadécimal.
static const uint8_t kGatewayMacAddress[6] = {0x88,0x56,0xA6,0x7E,0x24,0x18};

/// @brief Canal Wi-Fi utilisé par la passerelle MPS.
constexpr uint8_t kWifiChannel = 8;

/// @brief Nom du nœud enregistré auprès de la passerelle MPS.
/// Modifier cette valeur pour adapter le nom du capteur, par exemple
/// "sensor_a", "sensor_b", etc.
#define DEVICE_NAME "sensor_d"

/// @brief Broche Arduino utilisée pour le bouton matériel.
#define G3_BUTTON 3

/// @brief Broche Arduino utilisée pour piloter la LED.
#define LED 2

/// @brief Préfixe commun aux noms de variables représentant des subjects MPS.
#define PREFIX_SUBJECT "kSub"

/// @brief Broche connectée au capteur DHT.
#define DHTPIN 0

/// @brief Modèle de capteur DHT utilisé.
#define DHTTYPE DHT11

/// @brief Valeur RGB représentée par trois octets : rouge, vert et bleu.
using Color_t = uint8_t[3];

/// @brief Palette de couleurs disponibles pour la LED.
/// Chaque entrée contient les composantes RGB dans l'ordre rouge, vert, bleu.
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




