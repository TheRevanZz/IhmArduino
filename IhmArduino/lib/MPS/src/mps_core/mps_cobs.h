/**
 * @file mps_cobs.h
 * @brief Encodage COBS (Consistent Overhead Byte Stuffing) du cadrage série (§5.3).
 *
 * @details COBS remplace chaque octet 0x00 des données par un code de saut : le résultat ne
 * contient plus aucun 0x00, ce qui permet d'utiliser cet octet comme délimiteur de trame et
 * garantit la resynchronisation quel que soit le contenu. Le surcoût est d'un octet, plus un
 * octet par bloc de 254 octets.
 *
 * Ces fonctions n'allouent rien : l'appelant fournit les tampons. Le délimiteur 0x00 final n'est
 * PAS ajouté ici (voir encodeSerialFrame()).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mps {

/**
 * @brief Taille maximale de l'encodage COBS de @p inputLength octets.
 * @param inputLength Nombre d'octets à encoder.
 * @return Le pire cas : @c inputLength + @c inputLength/254 + 1.
 */
constexpr size_t cobsMaxEncodedSize(size_t inputLength) { return inputLength + inputLength / 254 + 1; }

/**
 * @brief Encode des données en COBS.
 *
 * @param[in]  input          Octets à encoder (peut contenir des 0x00).
 * @param[in]  inputLength    Nombre d'octets de @p input.
 * @param[out] output         Tampon de sortie.
 * @param[in]  outputCapacity Capacité de @p output ; doit valoir au moins cobsMaxEncodedSize().
 * @return Le nombre d'octets écrits, ou 0 si la capacité est insuffisante.
 *         L'encodage de 0 octet occupe 1 octet (0x01).
 */
inline size_t cobsEncode(const uint8_t* input, size_t inputLength, uint8_t* output, size_t outputCapacity) {
  if (outputCapacity < cobsMaxEncodedSize(inputLength)) return 0;
  size_t  writeIndex = 1;      // prochain octet à écrire (l'octet 0 est le code du premier bloc)
  size_t  codeIndex  = 0;      // position du code du bloc en cours, renseigné à la fin du bloc
  uint8_t blockCode  = 1;      // 1 + nombre d'octets non nuls du bloc en cours
  for (size_t inputIndex = 0; inputIndex < inputLength; ++inputIndex) {
    if (input[inputIndex] == 0) {
      output[codeIndex] = blockCode;
      codeIndex         = writeIndex++;
      blockCode         = 1;
    } else {
      output[writeIndex++] = input[inputIndex];
      if (++blockCode == 0xFF) {  // bloc plein : 254 octets non nuls, sans zéro implicite après
        output[codeIndex] = blockCode;
        codeIndex         = writeIndex++;
        blockCode         = 1;
      }
    }
  }
  output[codeIndex] = blockCode;
  return writeIndex;
}

/**
 * @brief Décode un bloc COBS reçu (sans le délimiteur 0x00 final).
 *
 * @details Le décodage peut se faire en place (@p output égal à @p input) : l'écriture ne
 * dépasse jamais la lecture.
 *
 * @param[in]  input          Bloc encodé.
 * @param[in]  inputLength    Nombre d'octets de @p input.
 * @param[out] output         Tampon de sortie.
 * @param[in]  outputCapacity Capacité de @p output.
 * @return Le nombre d'octets décodés, ou -1 si le bloc est invalide (bloc vide, code 0x00, saut
 *         au-delà de la fin des données) ou si la capacité est insuffisante.
 */
inline int cobsDecode(const uint8_t* input, size_t inputLength, uint8_t* output, size_t outputCapacity) {
  if (inputLength == 0) return -1;
  size_t readIndex  = 0;
  size_t writeIndex = 0;
  while (readIndex < inputLength) {
    const uint8_t blockCode = input[readIndex++];
    if (blockCode == 0) return -1;
    const size_t dataBytesInBlock = (size_t)blockCode - 1;
    if (dataBytesInBlock > inputLength - readIndex) return -1;  // le saut dépasse la fin des données
    if (writeIndex + dataBytesInBlock > outputCapacity) return -1;
    for (size_t copied = 0; copied < dataBytesInBlock; ++copied) output[writeIndex++] = input[readIndex++];
    // Un code < 0xFF encode un 0x00 d'origine, sauf après le dernier bloc.
    if (blockCode < 0xFF && readIndex < inputLength) {
      if (writeIndex + 1 > outputCapacity) return -1;
      output[writeIndex++] = 0;
    }
  }
  return (int)writeIndex;
}

}  // namespace mps
