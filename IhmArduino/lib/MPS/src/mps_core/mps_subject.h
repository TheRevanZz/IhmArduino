/**
 * @file mps_subject.h
 * @brief Noms de subjects : validation (§3.2), hachage FNV-1a 32 bits (§3.3) et subjects réservés (§3.4).
 *
 * @details Le développeur écrit un nom lisible (@c set_sensor_a_led_state) ; la trame ne transporte
 * que son identifiant de 32 bits, calculé par FNV-1a. Les fonctions de ce fichier sont
 * @c constexpr : le calcul se fait à la compilation pour un littéral, sans coût à l'exécution.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mps_protocol.h"

namespace mps {

/**
 * @brief Longueur d'une chaîne terminée par 0, utilisable dans une expression constante.
 * @param text Chaîne terminée par 0.
 * @return Le nombre de caractères avant le terminateur.
 */
constexpr size_t stringLength(const char* text) {
  size_t length = 0;
  while (text[length] != '\0') ++length;
  return length;
}

/**
 * @brief Hache un texte avec FNV-1a 32 bits (§3.3).
 *
 * @details Algorithme : @c hash = 0x811C9DC5 ; pour chaque octet, @c hash ^= octet puis
 * @c hash *= 0x01000193 (modulo 2^32, implicite avec un @c uint32_t).
 *
 * @param text   Octets à hacher (les caractères du nom, sans terminateur).
 * @param length Nombre d'octets.
 * @return Le hachage de 32 bits, utilisé comme @c subjectId.
 */
constexpr uint32_t fnv1aHash(const char* text, size_t length) {
  uint32_t hash = 0x811C9DC5u;
  for (size_t index = 0; index < length; ++index) {
    hash ^= (uint8_t)text[index];
    hash *= 0x01000193u;
  }
  return hash;
}

/**
 * @brief Hache une chaîne terminée par 0 avec FNV-1a 32 bits.
 * @param text Chaîne terminée par 0.
 * @return Le hachage de 32 bits.
 * @see fnv1aHash(const char*, size_t)
 */
constexpr uint32_t fnv1aHash(const char* text) { return fnv1aHash(text, stringLength(text)); }

/**
 * @brief Vérifie qu'un nom respecte la grammaire des subjects (§3.2).
 *
 * @details Grammaire : @c mot *( "_" mot ) avec @c mot = 1*( a-z / 0-9 ), premier caractère une
 * lettre, 1 à 64 caractères. Sont refusés : majuscule, espace, @c _ initial, final ou double. Sans
 * ce contrôle, une casse ou un espace parasite donnerait un autre identifiant sans erreur visible.
 *
 * @param text   Nom à vérifier.
 * @param length Longueur du nom.
 * @return @c true si le nom est valide.
 */
constexpr bool isValidSubjectName(const char* text, size_t length) {
  if (length < 1 || length > kMaxSubjectNameLength) return false;
  if (!(text[0] >= 'a' && text[0] <= 'z')) return false;
  if (text[length - 1] == '_') return false;
  bool previousWasUnderscore = false;
  for (size_t index = 0; index < length; ++index) {
    const char character = text[index];
    if (character == '_') {
      if (previousWasUnderscore) return false;
      previousWasUnderscore = true;
    } else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) {
      previousWasUnderscore = false;
    } else {
      return false;
    }
  }
  return true;
}

/**
 * @brief Vérifie qu'une chaîne terminée par 0 respecte la grammaire des subjects.
 * @param text Nom à vérifier.
 * @return @c true si le nom est valide.
 * @see isValidSubjectName(const char*, size_t)
 */
constexpr bool isValidSubjectName(const char* text) { return isValidSubjectName(text, stringLength(text)); }

/**
 * @brief Indique si un identifiant est interdit (0x00000000 et 0xFFFFFFFF, §3.3).
 * @param subjectId Identifiant à tester.
 * @return @c true si l'identifiant est réservé : le développeur doit choisir un autre nom.
 */
constexpr bool isReservedSubjectId(uint32_t subjectId) {
  return subjectId == kReservedSubjectIdZero || subjectId == kReservedSubjectIdMax;
}

/**
 * @brief Indique si un nom commence par le mot réservé @c sys (§3.4).
 * @details Seule la passerelle publie des subjects @c sys_ ; la bibliothèque refuse d'en publier.
 * @param text   Nom à tester.
 * @param length Longueur du nom.
 * @return @c true pour @c "sys" ou un nom commençant par @c "sys_".
 */
constexpr bool hasReservedSystemPrefix(const char* text, size_t length) {
  return (length == 3 && text[0] == 's' && text[1] == 'y' && text[2] == 's') ||
         (length >= 4 && text[0] == 's' && text[1] == 'y' && text[2] == 's' && text[3] == '_');
}

/**
 * @brief Compare le premier mot d'un nom avec un mot donné.
 * @param text            Nom complet.
 * @param length          Longueur du nom.
 * @param firstWordLength Longueur du premier mot du nom (jusqu'au premier @c _).
 * @param word            Mot à comparer.
 * @return @c true si le premier mot est exactement @p word.
 */
constexpr bool firstWordEquals(const char* text, size_t length, size_t firstWordLength, const char* word) {
  if (firstWordLength != stringLength(word)) return false;
  for (size_t index = 0; index < firstWordLength && index < length; ++index)
    if (text[index] != word[index]) return false;
  return true;
}

/**
 * @brief Indique si le premier mot du nom est l'un des types de message de la convention (§3.2).
 *
 * @details Types attendus : @c get, @c set, @c cmd, @c status, @c evt, @c hb, plus @c sys. Un nom
 * qui échoue ce test reste valide : la bibliothèque ne fait qu'avertir le développeur.
 *
 * @param text   Nom à examiner.
 * @param length Longueur du nom.
 * @return @c true si le premier mot est un type connu.
 */
constexpr bool hasKnownMessageVerb(const char* text, size_t length) {
  size_t firstWordLength = 0;
  while (firstWordLength < length && text[firstWordLength] != '_') ++firstWordLength;
  return firstWordEquals(text, length, firstWordLength, "get") ||
         firstWordEquals(text, length, firstWordLength, "set") ||
         firstWordEquals(text, length, firstWordLength, "cmd") ||
         firstWordEquals(text, length, firstWordLength, "status") ||
         firstWordEquals(text, length, firstWordLength, "evt") ||
         firstWordEquals(text, length, firstWordLength, "hb") ||
         firstWordEquals(text, length, firstWordLength, "sys");
}

/// @name Subjects réservés publiés par la passerelle (§3.4)
/// @{
constexpr uint32_t kSubjectIdGatewayBoot = 0x412CF2E6u;  ///< @c sys_gw_boot : envoyé à l'hôte au démarrage.
constexpr uint32_t kSubjectIdNodeOnline  = 0x9F54FAD7u;  ///< @c sys_node_online : à chaque HELLO accepté.
constexpr uint32_t kSubjectIdNodeOffline = 0xF9ED3FABu;  ///< @c sys_node_offline : quand un nœud disparaît.
constexpr uint32_t kSubjectIdGatewayLog  = 0x055136E8u;  ///< @c sys_gw_log : journal texte facultatif.
/// @}

// Vérification à la compilation des vecteurs de test de la spec (§3.3) et des subjects réservés.
static_assert(fnv1aHash("") == 0x811C9DC5u, "FNV-1a de la chaine vide");
static_assert(fnv1aHash("a") == 0xE40C292Cu, "FNV-1a de 'a'");
static_assert(fnv1aHash("foobar") == 0xBF9CF968u, "FNV-1a de 'foobar'");
static_assert(fnv1aHash("sys_gw_boot") == kSubjectIdGatewayBoot, "sys_gw_boot");
static_assert(fnv1aHash("sys_node_online") == kSubjectIdNodeOnline, "sys_node_online");
static_assert(fnv1aHash("sys_node_offline") == kSubjectIdNodeOffline, "sys_node_offline");
static_assert(fnv1aHash("sys_gw_log") == kSubjectIdGatewayLog, "sys_gw_log");

}  // namespace mps
