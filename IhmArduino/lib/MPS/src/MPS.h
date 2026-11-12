/**
 * @file MPS.h
 * @brief Bibliothèque MPS v1 pour nœuds ESP32 : client ESP-NOW vers une passerelle MPS.
 *
 * @details Cible Arduino-ESP32 3.x (ESP-IDF 5.x). La signature des callbacks ESP-NOW a changé depuis
 * la version 2.x : la bibliothèque refuse de compiler avec l'ancienne (spec §11.3).
 *
 * @par Exemple minimal
 * @code
 * #include <MPS.h>
 *
 * constexpr auto kLedSubject = mps::makeSubject("set_sensor_a_led_state");  // nom vérifié à la compilation
 *
 * void onLedCommand(const mps::FrameView& message, void*) { ... }
 *
 * void setup() {
 *   MPS.on(kLedSubject, onLedCommand);
 *   MPS.begin(gatewayMacAddress, "sensor_a");
 * }
 *
 * void loop() {
 *   MPS.loop();  // à chaque tour, sans delay() long dans le reste du code
 * }
 * @endcode
 *
 * @par Règles à respecter
 * - Appeler MPS.loop() au moins toutes les 10 ms : les retransmissions et le keepalive en dépendent.
 * - Les handlers sont courts, vérifient la taille du payload (mps::readPayloadAs) et ne bloquent pas.
 * - Le heartbeat est publié par le code métier (MPSNode::publishHeartbeat), jamais automatiquement.
 * - Rien n'est chiffré (spec §8) : aucun secret dans un payload.
 */
#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "La bibliotheque MPS cible l'ESP32 (Arduino-ESP32 3.x)."
#endif

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "MPS exige Arduino-ESP32 3.x (ESP-IDF 5.x) : signature des callbacks ESP-NOW differente en 2.x."
#endif

#include "mps_core/mps.h"
#include "mps_core/mps_client.h"

/// Profondeur de la file de réception entre la tâche Wi-Fi et loop() (trames de 250 octets chacune).
#ifndef MPS_RECEIVE_QUEUE_DEPTH
#define MPS_RECEIVE_QUEUE_DEPTH 8
#endif

/**
 * @brief Nœud MPS : se connecte à une passerelle, s'abonne à des subjects et publie des messages.
 *
 * @details Une instance globale, @c MPS, est fournie (comme @c Serial). La classe est le transport du
 * moteur mps::Client : elle envoie les trames par ESP-NOW et lui remet celles reçues, depuis le
 * contexte de loop() et jamais depuis la tâche Wi-Fi.
 */
class MPSNode : public mps::ITransport, private mps::IClientListener {
 public:
  /// Fonction appelée à l'ouverture ou à la perte de la session.
  typedef void (*EventCallback)();

  /// Fonction appelée quand un envoi échoue (sans ACK, ou refusé définitivement).
  typedef void (*SendFailedCallback)(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason reason,
                                     mps::AcknowledgementStatus rejectionStatus);

  MPSNode();

  /**
   * @brief Démarre le Wi-Fi en station (sans connexion), ESP-NOW et la procédure d'enregistrement.
   *
   * @details Les handlers (on()) peuvent être ajoutés avant ou après begin(). Le canal radio ne peut
   * être choisi librement que parce que le nœud n'est pas connecté à un point d'accès (§11.3).
   *
   * @param gatewayMacAddress Adresse MAC de la passerelle de votre groupe (6 octets).
   * @param deviceName        Nom d'appareil de 0 à 16 caractères, celui des subjects (« sensor_a »).
   *                          La chaîne doit rester valide (un littéral convient).
   * @param wifiChannel       Canal Wi-Fi, identique sur la passerelle (1 par défaut).
   * @return @c false si ESP-NOW ne démarre pas.
   */
  bool begin(const uint8_t gatewayMacAddress[6], const char* deviceName, uint8_t wifiChannel = 1);

  /**
   * @brief Fait tourner le protocole : remet les trames reçues au moteur et avance les temporisations.
   * @details À appeler à chaque tour de @c loop(), au moins toutes les 10 ms. Les handlers sont
   * invoqués d'ici.
   */
  void loop();

  // ---- réception ----------------------------------------------------------------------------

  /**
   * @brief Abonne un subject et appelle @p handler à chaque message reçu.
   * @param subject     Subject créé par mps::makeSubject().
   * @param handler     Fonction courte ; @c message.payload n'est valable que pendant l'appel.
   * @param userContext Pointeur transmis au handler (peut être nul).
   * @return Ok, ou la raison du refus (collision de hachage, table pleine...), aussi journalisée.
   */
  mps::ErrorCode on(const mps::Subject& subject, mps::MessageHandler handler, void* userContext = nullptr);

  /// @brief Se désabonne d'un subject. @return Ok, ou NotFound s'il n'était pas abonné.
  mps::ErrorCode off(const mps::Subject& subject);

  // ---- émission -----------------------------------------------------------------------------

  /**
   * @brief Publie un message.
   * @param subject                Subject (un subject @c sys_ est refusé).
   * @param data                   Octets du payload.
   * @param dataLength             Nombre d'octets, au plus 238.
   * @param requestAcknowledgement @c true pour les @c set_ et @c cmd_ (spec §11.6).
   * @return NotRegistered tant que la session n'est pas ouverte : afficher « nœud injoignable »
   *         plutôt que réessayer en boucle.
   */
  mps::ErrorCode publish(const mps::Subject& subject, const void* data, size_t dataLength,
                         bool requestAcknowledgement = false);

  /**
   * @brief Publie une structure triviale (sa représentation mémoire) comme payload.
   * @tparam PayloadType Type trivial d'au plus 238 octets, de préférence @c packed.
   */
  template <typename PayloadType>
  mps::ErrorCode publishValue(const mps::Subject& subject, const PayloadType& value,
                              bool requestAcknowledgement = false) {
    static_assert(std::is_trivially_copyable<PayloadType>::value, "payload: type trivial uniquement");
    static_assert(sizeof(PayloadType) <= mps::kMaxPayloadSize, "payload: 238 octets au plus");
    return publish(subject, &value, sizeof(PayloadType), requestAcknowledgement);
  }

  /**
   * @brief Publie un heartbeat métier (spec §6.2) : état, compteur et uptime sont renseignés ici.
   * @details À appeler par VOTRE code métier, par exemple après chaque rafraîchissement de la
   * chaîne de LED : si ce code se bloque, le heartbeat s'arrête alors que le keepalive continue.
   * @param subject     Subject @c hb_<appareil>.
   * @param healthState 0 = OK, 1 = dégradé, 2 = défaut.
   */
  mps::ErrorCode publishHeartbeat(const mps::Subject& subject, uint8_t healthState = 0);

  // ---- état et événements -------------------------------------------------------------------

  /// @return @c true si la session avec la passerelle est ouverte.
  bool isRegistered() const { return client_.isRegistered(); }
  /// @return L'identifiant attribué par la passerelle.
  uint8_t assignedNodeId() const { return client_.assignedNodeId(); }
  /// @return Les compteurs de diagnostic du protocole.
  const mps::ClientCounters& counters() const { return client_.counters(); }
  /// @return Les trames reçues perdues car la file Wi-Fi vers loop() était pleine.
  uint32_t droppedReceivedFrameCount() const { return droppedReceivedFrameCount_; }
  /// @return Les trames reçues d'une adresse MAC autre que la passerelle (ignorées).
  uint32_t foreignFrameCount() const { return foreignFrameCount_; }
  /// @return Les trames de la passerelle traitées par le moteur (diagnostic).
  uint32_t receivedFrameCount() const { return receivedFrameCount_; }
  /// @return Les envois radio restés sans ACK au niveau MAC (le pair n'a pas entendu).
  uint32_t radioSendFailureCount() const;

  /// @brief Appelée quand la session s'ouvre : publier ici l'état initial (la passerelle ne garde rien).
  void setRegisteredCallback(EventCallback callback) { registeredCallback_ = callback; }
  /// @brief Appelée quand la session est perdue.
  void setUnregisteredCallback(EventCallback callback) { unregisteredCallback_ = callback; }
  /// @brief Appelée quand un envoi échoue. Ne pas publier depuis cette fonction.
  void setSendFailedCallback(SendFailedCallback callback) { sendFailedCallback_ = callback; }

  /// @brief Journal texte sur un @c Print (typiquement @c Serial). @c nullptr pour le couper.
  void setLogOutput(Print* output) { logOutput_ = output; }

  /// @brief Transport du moteur (usage interne) : envoie une trame par ESP-NOW, un seul envoi à la fois.
  bool send(const uint8_t* frame, size_t frameLength) override;

 private:
  void onRegistered(uint8_t assignedNodeId, uint16_t maxPayloadSize, uint8_t keepaliveIntervalSeconds) override;
  void onUnregistered(mps::SessionLossReason reason) override;
  void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason reason,
                    mps::AcknowledgementStatus rejectionStatus) override;

  mps::Client client_;
  Print*      logOutput_                 = nullptr;
  bool        hasBegun_                  = false;
  uint16_t    nextHeartbeatCounter_      = 0;
  uint32_t    droppedReceivedFrameCount_ = 0;
  uint32_t    foreignFrameCount_         = 0;
  uint32_t    receivedFrameCount_        = 0;
  EventCallback      registeredCallback_   = nullptr;
  EventCallback      unregisteredCallback_ = nullptr;
  SendFailedCallback sendFailedCallback_   = nullptr;
};

/// Instance globale du nœud MPS, à utiliser comme @c Serial.
extern MPSNode MPS;
