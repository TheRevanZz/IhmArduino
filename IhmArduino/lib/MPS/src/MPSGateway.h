/**
 * @file MPSGateway.h
 * @brief Passerelle MPS v1 pour ESP32 : ESP-NOW côté nœuds, port série côté hôte. Cible Arduino-ESP32 3.x.
 *
 * @warning Le port série de l'hôte transporte le flux binaire MPS. N'y écrivez RIEN d'autre (pas de
 *          @c Serial.print) : chaque octet parasite coûte une trame (spec §11.5). Pour les journaux,
 *          utilisez MPSGatewayClass::setLogOutput() avec un AUTRE port, ou démarrez sans hôte
 *          (@c begin(nullptr, ...)) avec la console texte du croquis @c gateway.
 *
 * @par Exemple
 * @code
 * void setup() {
 *   Serial.begin(115200);
 *   MPSGateway.begin(&Serial, 1);   // flux binaire MPS sur l'USB, canal Wi-Fi 1
 * }
 * void loop() { MPSGateway.loop(); }
 * @endcode
 */
#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "La bibliotheque MPS cible l'ESP32 (Arduino-ESP32 3.x)."
#endif

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <stddef.h>
#include <stdint.h>

#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "MPS exige Arduino-ESP32 3.x (ESP-IDF 5.x)."
#endif

#include "mps_core/mps.h"
#include "mps_core/mps_gateway.h"

/// Profondeur de la file de réception entre la tâche Wi-Fi et loop() (trames de 250 octets chacune).
#ifndef MPS_GATEWAY_RECEIVE_QUEUE_DEPTH
#define MPS_GATEWAY_RECEIVE_QUEUE_DEPTH 16
#endif
/// Nombre de noms de subjects mémorisables pour des journaux lisibles.
#ifndef MPS_GATEWAY_KNOWN_SUBJECT_NAME_CAPACITY
#define MPS_GATEWAY_KNOWN_SUBJECT_NAME_CAPACITY 32
#endif

/**
 * @brief Passerelle MPS : adaptateur ESP32 autour du moteur mps::Gateway.
 *
 * @details Fournit au moteur la radio ESP-NOW (un seul envoi à la fois, pairs temporaires pour répondre
 * aux adresses MAC inconnues) et le port série (écriture non bloquante, tout ou rien).
 */
class MPSGatewayClass : public mps::IGatewayPort {
 public:
  MPSGatewayClass();

  /**
   * @brief Démarre la radio, le profil d'admission puis annonce @c sys_gw_boot à l'hôte (§9.4).
   *
   * @param hostSerialPort Port de l'hôte (@c Serial pour l'USB natif, @c Serial1 pour une UART externe
   *                       à 921 600 bauds 8N1). @c nullptr : pas d'hôte (console texte).
   * @param wifiChannel    Canal Wi-Fi commun avec les nœuds (1 par défaut).
   * @param configuration  Profil d'admission (OPEN ou ALLOWLIST) et délais. Le profil actif est annoncé
   *                       sur le journal (spec §8.1) si setLogOutput() a été appelé avant.
   * @return @c false si la radio ne démarre pas, ou si begin() a déjà été appelé.
   */
  bool begin(Stream* hostSerialPort, uint8_t wifiChannel = 1,
             const mps::GatewayConfiguration& configuration = mps::GatewayConfiguration());

  /// @brief À appeler à chaque tour de @c loop() (au moins toutes les 10 ms) : radio, série, temporisations.
  void loop();

  /// @brief Journal texte (nœuds en ligne ou hors ligne, publications). Jamais le port MPS de l'hôte.
  void setLogOutput(Print* output) { logOutput_ = output; }
  /// @brief Active ou coupe le journal de chaque publication observée.
  void setPublicationLoggingEnabled(bool isEnabled) { isPublicationLoggingEnabled_ = isEnabled; }

  /**
   * @brief Associe un nom à son identifiant pour des journaux lisibles ; ne change rien au protocole.
   * @param subjectName Nom du subject (copié).
   */
  void registerSubjectName(const char* subjectName);
  /// @return Le nom mémorisé pour cet identifiant, ou @c nullptr s'il est inconnu.
  const char* findSubjectName(uint32_t subjectId) const;

  /// @return Le moteur de passerelle (diagnostic, console de debug). Valable après begin().
  mps::Gateway& gatewayEngine() { return *gatewayEngine_; }
  /// @return Les trames reçues perdues car la file Wi-Fi vers loop() était pleine.
  uint32_t droppedReceivedFrameCount() const { return droppedReceivedFrameCount_; }
  /// @return Les trames reçues en diffusion (broadcast), jamais utilisée par MPS, ignorées.
  uint32_t foreignFrameCount() const { return foreignFrameCount_; }
  /// @return Les envois radio acquittés au niveau MAC par le pair.
  uint32_t radioSendSuccessCount() const;
  /// @return Les envois radio SANS ACK MAC : le pair n'a pas entendu (canal, distance, récepteur endormi).
  uint32_t radioSendFailureCount() const;
  /// @param[out] macAddress Reçoit l'adresse MAC de la passerelle, à saisir côté nœuds.
  void readOwnMacAddress(uint8_t macAddress[6]) const;

  // ---- mps::IGatewayPort (usage interne) -----------------------------------------------------
  bool sendRadioFrame(const uint8_t macAddress[6], const uint8_t* frame, size_t frameLength) override;
  bool addRadioPeer(const uint8_t macAddress[6]) override;
  void removeRadioPeer(const uint8_t macAddress[6]) override;
  bool writeSerialBytes(const uint8_t* data, size_t length) override;
  void onNodeOnline(uint8_t nodeId, const uint8_t macAddress[6], const char* deviceName,
                    size_t deviceNameLength) override;
  void onNodeOffline(uint8_t nodeId, mps::OfflineReason reason) override;
  void onPublishObserved(uint8_t sourceId, uint32_t subjectId, const uint8_t* payload,
                         size_t payloadLength) override;

 private:
  /// Retire le pair ESP-NOW ajouté temporairement une fois son envoi terminé.
  void releaseTemporaryPeer();

  /// Un nom de subject mémorisé pour les journaux.
  struct KnownSubjectName {
    uint32_t subjectId;
    char     name[mps::kMaxSubjectNameLength + 1];
  };

  mps::Gateway* gatewayEngine_  = nullptr;
  Stream*       hostSerialPort_ = nullptr;
  Print*        logOutput_      = nullptr;
  bool          isPublicationLoggingEnabled_ = true;
  uint8_t       wifiChannel_    = 1;
  bool          hasBegun_       = false;
  uint8_t       temporaryPeerMacAddress_[6] = {0};
  bool          isTemporaryPeerPending_     = false;
  uint32_t      droppedReceivedFrameCount_  = 0;
  uint32_t      foreignFrameCount_          = 0;
  KnownSubjectName knownSubjectNames_[MPS_GATEWAY_KNOWN_SUBJECT_NAME_CAPACITY];
  size_t        knownSubjectNameCount_ = 0;
};

/// Instance globale de la passerelle MPS.
extern MPSGatewayClass MPSGateway;
