/**
 * @file mps_crc16.h
 * @brief CRC-16/CCITT-FALSE des trames série (§5.2).
 *
 * @details Paramètres : polynôme 0x1021, valeur initiale 0xFFFF, sans réflexion des bits en entrée
 * ni en sortie, XOR final nul. Le CRC de la chaîne ASCII « 123456789 » vaut 0x29B1.
 *
 * @warning Ne pas utiliser les fonctions CRC de la ROM de l'ESP32 : leur convention d'inversion
 *          des bits diffère et donne un résultat incompatible.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mps {

/**
 * @brief Calcule le CRC-16/CCITT-FALSE d'un bloc d'octets.
 *
 * @details Version bit à bit, sans table de 256 entrées : aucun octet de mémoire constante en
 * plus, et largement assez rapide pour des trames de 250 octets.
 *
 * @param[in] data   Octets à protéger (en-tête et payload de la trame MPS).
 * @param[in] length Nombre d'octets de @p data.
 * @return Le CRC de 16 bits.
 */
inline uint16_t computeCrc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t byteIndex = 0; byteIndex < length; ++byteIndex) {
    crc ^= (uint16_t)((uint16_t)data[byteIndex] << 8);
    for (int bitIndex = 0; bitIndex < 8; ++bitIndex) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

}  // namespace mps
