/**
 * @file mps_duplicate_window.h
 * @brief Détection des trames en double par une fenêtre glissante de 32 numéros de séquence (§7.1).
 *
 * @details Un émetteur retransmet une trame avec le même numéro de séquence quand son ACK se perd.
 * Le récepteur doit donc reconnaître les doublons pour ne pas les livrer deux fois à l'application.
 * La détection repose uniquement sur le numéro de séquence, jamais sur le drapeau DUP.
 */
#pragma once

#include <stdint.h>

namespace mps {

/**
 * @brief Fenêtre de réception d'un lien : mémorise les 32 derniers numéros de séquence vus.
 *
 * @details L'état est un couple (dernier numéro vu, masque de bits) : le bit @c k du masque
 * indique que le numéro @c dernier-k a été reçu. Les numéros sont des @c uint16_t qui reviennent à
 * 0 après 65 535 ; la distance entre deux numéros est calculée en arithmétique signée modulo 2^16.
 *
 * Un doublon rejeté doit tout de même être ré-acquitté si ACK_REQ est posé (l'ACK précédent a pu
 * se perdre). Les ACK eux-mêmes ne passent pas par cette fenêtre.
 */
class DuplicateWindow {
 public:
  /// Verdict de la classification d'un numéro de séquence.
  enum class Verdict : uint8_t {
    Accept,     ///< Nouvelle trame, ou arrivée dans le désordre : à livrer.
    Duplicate,  ///< Déjà reçue : ré-acquitter si ACK_REQ, ne pas livrer.
    TooOld,     ///< Plus de 32 trames en arrière : à rejeter.
  };

  /**
   * @brief Oublie tout l'historique du lien.
   * @details À appeler dans exactement trois cas (§7.1) : la passerelle accepte un HELLO de ce pair,
   * un émetteur de HELLO reçoit l'ACK de statut OK, l'hôte reçoit @c sys_gw_boot. Le premier numéro
   * reçu ensuite est toujours accepté, quelle que soit sa valeur.
   */
  void reset() { hasReceivedFrame_ = false; }

  /**
   * @brief Classe un numéro de séquence reçu et met la fenêtre à jour.
   * @param sequenceNumber Numéro de séquence de la trame reçue.
   * @return Accept si la trame doit être traitée, Duplicate ou TooOld sinon.
   */
  Verdict classify(uint16_t sequenceNumber) {
    if (!hasReceivedFrame_) {
      hasReceivedFrame_   = true;
      lastSequenceNumber_ = sequenceNumber;
      receivedBitmap_     = 1;
      return Verdict::Accept;
    }
    // Distance signée au dernier numéro vu : positive si la trame est plus récente.
    const int16_t distance = (int16_t)(uint16_t)(sequenceNumber - lastSequenceNumber_);
    if (distance > 0) {
      // Nouvelle trame la plus récente : la fenêtre glisse de `distance` positions.
      receivedBitmap_ = (distance >= 32) ? 0u : (receivedBitmap_ << distance);
      receivedBitmap_ |= 1u;
      lastSequenceNumber_ = sequenceNumber;
      return Verdict::Accept;
    }
    const int framesBehind = -(int)distance;  // distance == INT16_MIN donne 32768 : rejeté ci-dessous
    if (framesBehind >= 32) return Verdict::TooOld;
    if (receivedBitmap_ & (1u << framesBehind)) return Verdict::Duplicate;
    receivedBitmap_ |= (1u << framesBehind);  // arrivée dans le désordre, jamais vue
    return Verdict::Accept;
  }

 private:
  bool     hasReceivedFrame_   = false;  ///< Faux tant qu'aucune trame n'a été reçue depuis reset().
  uint16_t lastSequenceNumber_ = 0;      ///< Numéro de séquence le plus récent accepté.
  uint32_t receivedBitmap_     = 0;      ///< Bit k = le numéro (dernier - k) a été reçu.
};

}  // namespace mps
