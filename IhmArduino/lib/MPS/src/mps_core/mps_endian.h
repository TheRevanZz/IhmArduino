/**
 * @file mps_endian.h
 * @brief Lecture et écriture d'entiers little-endian, indépendantes de la cible (§11.2).
 *
 * @details Tout champ multi-octets du protocole est en little-endian. Ces fonctions assemblent
 * les octets un par un : elles évitent les accès non alignés et ne supposent rien sur le
 * boutisme du processeur (l'attribut @c packed ne convertit pas le boutisme).
 */
#pragma once

#include <stdint.h>

namespace mps {

/**
 * @brief Lit un entier non signé de 16 bits stocké en little-endian.
 * @param[in] bytes Pointeur vers les 2 octets à lire.
 * @return La valeur décodée.
 */
inline uint16_t readUint16LittleEndian(const uint8_t* bytes) {
  return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

/**
 * @brief Lit un entier non signé de 32 bits stocké en little-endian.
 * @param[in] bytes Pointeur vers les 4 octets à lire.
 * @return La valeur décodée.
 */
inline uint32_t readUint32LittleEndian(const uint8_t* bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[3] << 24);
}

/**
 * @brief Écrit un entier non signé de 16 bits en little-endian.
 * @param[out] destination Pointeur vers les 2 octets à écrire.
 * @param[in]  value       Valeur à encoder.
 */
inline void writeUint16LittleEndian(uint8_t* destination, uint16_t value) {
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
}

/**
 * @brief Écrit un entier non signé de 32 bits en little-endian.
 * @param[out] destination Pointeur vers les 4 octets à écrire.
 * @param[in]  value       Valeur à encoder.
 */
inline void writeUint32LittleEndian(uint8_t* destination, uint32_t value) {
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
  destination[2] = (uint8_t)(value >> 16);
  destination[3] = (uint8_t)(value >> 24);
}

}  // namespace mps
