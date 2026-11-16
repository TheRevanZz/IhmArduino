/**
 * @file mps_gateway.h
 * @brief Moteur de passerelle MPS : pairs, abonnements, routage, ACK, retransmission, keepalive, admission.
 *
 * @details Spécification §7, §8 et §9. La passerelle est le broker du système : elle tient la table
 * des abonnements et route chaque publication vers les abonnés, sans jamais lire les payloads
 * métier. Le moteur ne fait aucune entrée/sortie : la radio et le port série sont injectés par
 * IGatewayPort, l'horloge par les arguments @c currentTimeMs. Il est donc testable sur PC.
 *
 * @par Liens
 * Une passerelle gère au plus 20 nœuds radio (indices 0 à 19) et un hôte série (indice 20). Chaque
 * lien a sa file d'envoi de 8 trames, son arrêt-et-attente et sa fenêtre de réception : un nœud
 * lent ou absent ne bloque pas les autres.
 *
 * @par Contrat d'appel (mono-thread)
 * handleRadioFrame(), handleSerialByte() et poll() sont appelés depuis le même contexte
 * (@c loop()), jamais depuis la tâche Wi-Fi (§11.3). Aucune allocation dynamique.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mps_duplicate_window.h"
#include "mps_frame.h"
#include "mps_protocol.h"
#include "mps_subject.h"

#ifndef MPS_GATEWAY_QUEUE_DEPTH
#define MPS_GATEWAY_QUEUE_DEPTH 8  ///< Trames en file par lien (§9.1).
#endif
#ifndef MPS_GATEWAY_MAX_SUBSCRIPTIONS
#define MPS_GATEWAY_MAX_SUBSCRIPTIONS 32  ///< Abonnements par lien (§9.1).
#endif
#ifndef MPS_GATEWAY_ACKNOWLEDGEMENT_RING_SIZE
#define MPS_GATEWAY_ACKNOWLEDGEMENT_RING_SIZE 16  ///< ACK en attente de transport, tous liens confondus.
#endif

namespace mps {

constexpr size_t kMaxNodeCount     = 20;                     ///< ESP_NOW_MAX_TOTAL_PEER_NUM (§2.4).
constexpr size_t kHostLinkIndex    = kMaxNodeCount;          ///< Indice du lien série de l'hôte.
constexpr size_t kGatewayLinkCount = kMaxNodeCount + 1;      ///< Nombre total de liens.
constexpr size_t kNoOriginLink     = 0xFF;                   ///< « Aucune origine » : message émis par la passerelle.

/// Profil d'admission des nœuds (§8.1). Aucun des deux ne chiffre ni n'authentifie.
enum class AdmissionProfile : uint8_t {
  Open,       ///< Tout MAC est accepté (TP et développement sur réseau isolé).
  Allowlist,  ///< Seuls les MAC d'une liste sont acceptés ; les autres reçoivent ACK FORBIDDEN.
};

/// Raison transportée par @c sys_node_offline (§3.4).
enum class OfflineReason : uint8_t {
  KeepaliveExpired = 0,  ///< Le nœud n'a plus rien envoyé depuis 3 × keepalive.
  NodeRestarted    = 1,  ///< Nouveau HELLO du même nœud.
};

/**
 * @brief Interface entre le moteur de passerelle et le matériel (radio, port série).
 * @details Les notifications (onNodeOnline, onNodeOffline, onPublishObserved) ont une implémentation
 * vide par défaut : elles ne servent qu'au diagnostic.
 */
class IGatewayPort {
 public:
  virtual ~IGatewayPort() {}

  /**
   * @brief Envoie une trame MPS en unicast ESP-NOW.
   * @param[in] macAddress  Adresse MAC du destinataire (6 octets).
   * @param[in] frame       Trame MPS complète.
   * @param[in] frameLength Taille de la trame.
   * @return @c false si la radio est occupée (callback d'envoi pas encore revenu, §4.6) : le moteur
   *         réessaiera au prochain poll() sans compter de tentative.
   */
  virtual bool sendRadioFrame(const uint8_t macAddress[6], const uint8_t* frame, size_t frameLength) = 0;

  /// @brief Ajoute un pair ESP-NOW. @return @c false s'il n'y a plus de place (20 pairs au plus).
  virtual bool addRadioPeer(const uint8_t macAddress[6]) = 0;

  /// @brief Retire un pair ESP-NOW.
  virtual void removeRadioPeer(const uint8_t macAddress[6]) = 0;

  /**
   * @brief Écrit des octets sur le port série de l'hôte, TOUT OU RIEN et sans jamais bloquer (§11.5).
   * @param[in] data   Octets à écrire.
   * @param[in] length Nombre d'octets.
   * @return @c false si le tampon d'émission ne peut pas tout contenir (hôte absent) : le moteur
   *         supprime alors la trame et incrémente @c droppedFrameCount.
   */
  virtual bool writeSerialBytes(const uint8_t* data, size_t length) = 0;

  /// @brief Un nœud vient de s'enregistrer (HELLO accepté).
  virtual void onNodeOnline(uint8_t nodeId, const uint8_t macAddress[6], const char* deviceName,
                            size_t deviceNameLength) {
    (void)nodeId;
    (void)macAddress;
    (void)deviceName;
    (void)deviceNameLength;
  }

  /// @brief Un nœud n'est plus enregistré.
  virtual void onNodeOffline(uint8_t nodeId, OfflineReason reason) {
    (void)nodeId;
    (void)reason;
  }

  /// @brief Une PUB vient d'être acceptée, avant routage (diagnostic uniquement).
  virtual void onPublishObserved(uint8_t sourceId, uint32_t subjectId, const uint8_t* payload,
                                 size_t payloadLength) {
    (void)sourceId;
    (void)subjectId;
    (void)payload;
    (void)payloadLength;
  }
};

/// Configuration de la passerelle.
struct GatewayConfiguration {
  AdmissionProfile admissionProfile = AdmissionProfile::Open;
  /// Pour AdmissionProfile::Allowlist : tableau d'adresses MAC de 6 octets (doit rester valide).
  const uint8_t (*allowedMacAddresses)[6] = nullptr;
  size_t   allowedMacAddressCount         = 0;
  uint32_t acknowledgementTimeoutMs       = kAcknowledgementTimeoutMs;
  uint8_t  maxRetransmissions             = kMaxRetransmissions;
  uint8_t  keepaliveIntervalSeconds       = kKeepaliveIntervalSeconds;  ///< Imposé aux nœuds dans l'ACK du HELLO (§7.6).
};

/// Compteurs de diagnostic de la passerelle (noms de la spec §9.5 entre parenthèses).
struct GatewayCounters {
  uint32_t invalidLengthCount        = 0;  ///< Trames de taille ou de longueur incohérente (@c rx_len_err).
  uint32_t unsupportedVersionCount   = 0;  ///< Version inconnue (@c rx_ver_err).
  uint32_t unknownTypeCount          = 0;  ///< Type inconnu (@c rx_unknown_type).
  uint32_t notRegisteredCount        = 0;  ///< Trames d'un lien sans session (@c rx_not_registered).
  uint32_t duplicateCount            = 0;  ///< Doublons rejetés (@c rx_dup).
  uint32_t cobsErrorCount            = 0;  ///< Trames série au COBS invalide (@c rx_cobs_err).
  uint32_t crcErrorCount             = 0;  ///< Trames série au CRC incorrect (@c rx_crc_err).
  uint32_t originMismatchCount       = 0;  ///< @c sourceId incohérent avec le pair.
  uint32_t forbiddenCount            = 0;  ///< Actions refusées (PUB @c sys_, nœud hors liste).
  uint32_t retransmissionCount       = 0;  ///< Retransmissions (@c tx_retry).
  uint32_t sendFailureCount          = 0;  ///< Envois échoués après MAX_RETRANSMIT (@c tx_fail).
  uint32_t droppedFrameCount         = 0;  ///< Trames supprimées par débordement de file (@c tx_drop).
};

/// Description d'un lien, pour l'affichage (console de debug).
struct NodeInformation {
  bool     isActive;                              ///< Session ouverte.
  uint8_t  nodeId;
  uint8_t  macAddress[6];
  char     deviceName[kMaxDeviceNameLength + 1];  ///< Terminé par 0.
  uint8_t  subscriptionCount;
  uint32_t lastReceiveTimeMs;                     ///< Horloge de la dernière trame valide reçue.
};

/**
 * @brief Moteur de passerelle MPS.
 */
class Gateway {
 public:
  /**
   * @param port          Radio et port série (doit survivre à la passerelle).
   * @param configuration Admission, délais.
   */
  Gateway(IGatewayPort& port, const GatewayConfiguration& configuration)
      : port_(port), configuration_(configuration) {}
  Gateway(const Gateway&)            = delete;
  Gateway& operator=(const Gateway&) = delete;

  /**
   * @brief Démarre (ou redémarre) la passerelle : étapes 3 et 4 du §9.4.
   *
   * @details Envoie un octet 0x00 seul (la liaison se resynchronise), puis @c sys_gw_boot vers l'hôte
   * sans ACK_REQ et sans abonnement préalable. Tout l'état précédent (pairs, abonnements) est
   * perdu, comme après un redémarrage réel. Les étapes 1 et 2 (liaison série, radio, profil
   * d'admission) sont faites par l'appelant avant.
   *
   * @param currentTimeMs Horloge monotone en millisecondes.
   */
  void begin(uint32_t currentTimeMs) {
    currentTimeMs_ = currentTimeMs;
    for (size_t linkIndex = 0; linkIndex < kGatewayLinkCount; ++linkIndex) links_[linkIndex] = Link();
    pendingAcknowledgementCount_ = pendingAcknowledgementHead_ = 0;
    isRadioBusy_      = false;
    serialReceiver_   = SerialReceiver();
    const uint8_t resynchronizationByte = 0x00;
    port_.writeSerialBytes(&resynchronizationByte, 1);
    uint8_t bootPayload[3];  // proto_version u8, max_payload u16 (§3.4)
    bootPayload[0] = kProtocolVersion;
    writeUint16LittleEndian(bootPayload + 1, (uint16_t)kMaxPayloadSize);
    uint8_t frame[kMaxFrameSize];
    const size_t frameLength = buildFrame(frame, sizeof frame, MessageType::Publish, 0, kSourceIdGateway,
                                          kSubjectIdGatewayBoot, 0, bootPayload, 3);
    writeSerialFrame(frame, frameLength);
  }

  // ---- entrées ------------------------------------------------------------------------------

  /**
   * @brief Traite une trame ESP-NOW reçue.
   * @param macAddress    Adresse MAC de l'émetteur (6 octets).
   * @param frameData     Octets reçus.
   * @param frameLength   Nombre d'octets.
   * @param currentTimeMs Horloge monotone en millisecondes.
   */
  void handleRadioFrame(const uint8_t macAddress[6], const uint8_t* frameData, size_t frameLength,
                        uint32_t currentTimeMs) {
    currentTimeMs_ = currentTimeMs;
    handleFrame(false, macAddress, frameData, frameLength);
    transmitPendingFrames();
  }

  /**
   * @brief Traite un octet reçu sur le port série. La trame est traitée quand le 0x00 final arrive.
   * @param receivedByte  Octet lu sur le port série de l'hôte.
   * @param currentTimeMs Horloge monotone en millisecondes.
   */
  void handleSerialByte(uint8_t receivedByte, uint32_t currentTimeMs) {
    currentTimeMs_ = currentTimeMs;
    if (serialReceiver_.pushByte(receivedByte)) {
      handleFrame(true, nullptr, serialReceiver_.completedFrame(), serialReceiver_.completedFrameLength());
      transmitPendingFrames();
    }
    counters_.cobsErrorCount = serialReceiver_.counters().cobsErrorCount;
    counters_.crcErrorCount  = serialReceiver_.counters().crcErrorCount;
  }

  /**
   * @brief Fait avancer les temporisations : expiration des nœuds muets, retransmissions.
   * @details À appeler au moins toutes les 10 ms.
   * @param currentTimeMs Horloge monotone en millisecondes.
   */
  void poll(uint32_t currentTimeMs) {
    currentTimeMs_ = currentTimeMs;
    // Un nœud est absent après 3 × keepalive_s sans aucune trame valide (§7.4).
    const uint32_t silenceLimitMs = (uint32_t)configuration_.keepaliveIntervalSeconds * 3000u;
    for (size_t linkIndex = 0; linkIndex < kMaxNodeCount; ++linkIndex) {
      Link& link = links_[linkIndex];
      if (link.isActive && (uint32_t)(currentTimeMs_ - link.lastReceiveTimeMs) >= silenceLimitMs)
        expireNode(linkIndex);
    }
    transmitPendingFrames();
  }

  /**
   * @brief Publie un message comme s'il venait de l'hôte (console de debug) : il est routé aux abonnés.
   * @param subjectId              Identifiant du subject.
   * @param payload                Octets du payload.
   * @param payloadLength          Longueur du payload (au plus 238).
   * @param requestAcknowledgement ACK_REQ vers les destinataires.
   */
  void publishAsHost(uint32_t subjectId, const uint8_t* payload, size_t payloadLength, bool requestAcknowledgement) {
    if (payloadLength > kMaxPayloadSize || isReservedSubjectId(subjectId)) return;
    routeToSubscribers(kHostLinkIndex, kSourceIdHost, subjectId, payload, payloadLength,
                       requestAcknowledgement ? kFlagAcknowledgementRequested : 0);
    transmitPendingFrames();
  }

  /**
   * @brief Renseigne l'identité radio de la passerelle, transmise à l'hôte dans l'ACK de son HELLO.
   * @details L'hôte l'affiche : c'est l'adresse et le canal que les nœuds doivent configurer.
   * @param macAddress  Adresse MAC Wi-Fi (station) de la passerelle (6 octets).
   * @param wifiChannel Canal Wi-Fi utilisé par ESP-NOW.
   */
  void setGatewayIdentity(const uint8_t macAddress[6], uint8_t wifiChannel) {
    memcpy(gatewayMacAddress_, macAddress, 6);
    wifiChannel_ = wifiChannel;
  }

  /**
   * @brief Publie @c sys_gw_log (§3.4) aux abonnés.
   * @param text Texte ASCII de 238 octets au plus, sans terminateur sur le fil.
   */
  void publishGatewayLog(const char* text) {
    size_t textLength = 0;
    while (text[textLength] && textLength < kMaxPayloadSize) ++textLength;
    routeToSubscribers(kNoOriginLink, kSourceIdGateway, kSubjectIdGatewayLog, (const uint8_t*)text, textLength, 0);
    transmitPendingFrames();
  }

  // ---- lecture d'état -----------------------------------------------------------------------

  /// @return Les compteurs de diagnostic.
  const GatewayCounters& counters() const { return counters_; }

  /// @return Le nombre de nœuds radio dont la session est ouverte.
  size_t activeNodeCount() const {
    size_t count = 0;
    for (size_t linkIndex = 0; linkIndex < kMaxNodeCount; ++linkIndex) count += links_[linkIndex].isActive ? 1 : 0;
    return count;
  }

  /// @return @c true si l'hôte a ouvert sa session série.
  bool isHostRegistered() const { return links_[kHostLinkIndex].isActive; }

  /**
   * @brief Décrit un lien.
   * @param[in]  linkIndex Indice du lien (0 à 19 : nœuds, 20 : hôte).
   * @param[out] information Reçoit la description.
   * @return @c false si le lien n'a jamais été attribué.
   */
  bool getNodeInformation(size_t linkIndex, NodeInformation* information) const {
    if (linkIndex >= kGatewayLinkCount || !links_[linkIndex].isAllocated) return false;
    const Link& link = links_[linkIndex];
    information->isActive = link.isActive;
    information->nodeId   = link.nodeId;
    memcpy(information->macAddress, link.macAddress, 6);
    memcpy(information->deviceName, link.deviceName, link.deviceNameLength);
    information->deviceName[link.deviceNameLength] = '\0';
    information->subscriptionCount                 = link.subscriptionCount;
    information->lastReceiveTimeMs                 = link.lastReceiveTimeMs;
    return true;
  }

 private:
  /// Trame en file d'émission d'un lien.
  struct QueuedFrame {
    uint16_t length;
    uint8_t  bytes[kMaxFrameSize];
  };

  /// Trame ACK_REQ envoyée à un lien, en attente de son ACK (arrêt-et-attente).
  struct InFlightFrame {
    bool     isActive            = false;
    bool     needsResend         = false;  ///< Radio occupée : renvoyer dès que possible.
    uint8_t  retransmissionsDone = 0;
    uint16_t sequenceNumber      = 0;
    uint16_t length              = 0;
    uint32_t lastSentAtMs        = 0;
    uint8_t  bytes[kMaxFrameSize];
  };

  /// Tout ce que la passerelle sait d'un pair (nœud radio ou hôte).
  struct Link {
    bool        isAllocated = false;  ///< Emplacement attribué : MAC et identifiant mémorisés même hors session.
    bool        isActive    = false;  ///< Session ouverte.
    uint8_t     macAddress[6] = {0};
    uint8_t     nodeId        = 0;
    uint8_t     deviceNameLength = 0;
    char        deviceName[kMaxDeviceNameLength] = {0};
    DuplicateWindow duplicateWindow;
    uint16_t    nextSequenceNumber  = 0;  ///< Compteur d'émission vers ce lien (renuméroté par lien, §9.2).
    uint32_t    lastReceiveTimeMs   = 0;
    uint32_t    helloAcceptedAtMs   = 0;
    uint32_t    subscribedSubjectIds[MPS_GATEWAY_MAX_SUBSCRIPTIONS] = {0};
    uint8_t     subscriptionCount   = 0;
    QueuedFrame queue[MPS_GATEWAY_QUEUE_DEPTH];
    uint8_t     queueHead  = 0;
    uint8_t     queueCount = 0;
    InFlightFrame inFlightFrame;
  };

  /// ACK en attente de transport (radio occupée ou série).
  struct PendingAcknowledgement {
    bool    isForSerialLink;
    uint8_t macAddress[6];  ///< Destinataire radio (inutilisé pour le lien série).
    uint8_t length;
    uint8_t bytes[kHeaderSize + kHostHelloAcknowledgementPayloadSize];  ///< Le plus long : ACK du HELLO de l'hôte.
  };

  /// Dernier ACK NOT_REGISTERED envoyé à un MAC inconnu, pour la limite d'un par seconde.
  struct NotRegisteredRateLimit {
    bool     isValid = false;
    uint8_t  macAddress[6] = {0};
    uint32_t lastSentAtMs  = 0;
  };

  // ---- aides sur les pairs ------------------------------------------------------------------

  static bool areMacAddressesEqual(const uint8_t* first, const uint8_t* second) {
    return memcmp(first, second, 6) == 0;
  }

  /// @return L'indice du nœud actif ayant cette adresse MAC, ou -1.
  int findActiveLinkByMacAddress(const uint8_t* macAddress) const {
    for (size_t linkIndex = 0; linkIndex < kMaxNodeCount; ++linkIndex)
      if (links_[linkIndex].isActive && areMacAddressesEqual(links_[linkIndex].macAddress, macAddress))
        return (int)linkIndex;
    return -1;
  }

  /// @return L'indice du lien attribué à cette adresse MAC (actif ou non), ou -1.
  int findAllocatedLinkByMacAddress(const uint8_t* macAddress) const {
    for (size_t linkIndex = 0; linkIndex < kMaxNodeCount; ++linkIndex)
      if (links_[linkIndex].isAllocated && areMacAddressesEqual(links_[linkIndex].macAddress, macAddress))
        return (int)linkIndex;
    return -1;
  }

  /// Applique le profil d'admission (§8.1).
  bool isMacAddressAllowed(const uint8_t* macAddress) const {
    if (configuration_.admissionProfile == AdmissionProfile::Open) return true;
    for (size_t entryIndex = 0; entryIndex < configuration_.allowedMacAddressCount; ++entryIndex)
      if (areMacAddressesEqual(configuration_.allowedMacAddresses[entryIndex], macAddress)) return true;
    return false;
  }

  /**
   * La passerelle ne connaît que des hachages, jamais les noms : elle ne peut refuser que les
   * subjects @c sys_ de la spec (§3.4). Tout nouveau subject @c sys_ doit être ajouté ici.
   */
  static bool isSystemSubject(uint32_t subjectId) {
    return subjectId == kSubjectIdGatewayBoot || subjectId == kSubjectIdNodeOnline ||
           subjectId == kSubjectIdNodeOffline || subjectId == kSubjectIdGatewayLog;
  }

  /// Plus petit identifiant de nœud (1 à 0xEF) qu'aucun lien attribué n'utilise (§7.3 étape 2).
  uint8_t smallestFreeNodeId() const {
    for (unsigned candidateId = kSourceIdFirstNode; candidateId <= kSourceIdLastNode; ++candidateId) {
      bool isTaken = false;
      for (size_t linkIndex = 0; linkIndex < kMaxNodeCount; ++linkIndex)
        if (links_[linkIndex].isAllocated && links_[linkIndex].nodeId == candidateId) isTaken = true;
      if (!isTaken) return (uint8_t)candidateId;
    }
    return 0;
  }

  /**
   * Cherche un emplacement pour une adresse MAC : celui déjà attribué (même identifiant qu'avant),
   * un emplacement libre, ou le pair hors session le plus ancien.
   * @return L'indice du lien, ou -1 si les 20 emplacements ont une session ouverte.
   */
  int allocateLink(const uint8_t* macAddress) {
    int linkIndex = findAllocatedLinkByMacAddress(macAddress);
    if (linkIndex >= 0) return linkIndex;
    for (size_t candidateIndex = 0; candidateIndex < kMaxNodeCount; ++candidateIndex)
      if (!links_[candidateIndex].isAllocated) {
        linkIndex = (int)candidateIndex;
        break;
      }
    if (linkIndex < 0) {
      uint32_t oldestSilenceMs = 0;
      for (size_t candidateIndex = 0; candidateIndex < kMaxNodeCount; ++candidateIndex) {
        if (links_[candidateIndex].isActive) continue;
        const uint32_t silenceMs = (uint32_t)(currentTimeMs_ - links_[candidateIndex].lastReceiveTimeMs);
        if (linkIndex < 0 || silenceMs > oldestSilenceMs) {
          linkIndex       = (int)candidateIndex;
          oldestSilenceMs = silenceMs;
        }
      }
    }
    if (linkIndex < 0) return -1;
    Link& link = links_[linkIndex];
    link              = Link();
    link.isAllocated  = true;
    memcpy(link.macAddress, macAddress, 6);
    link.nodeId = smallestFreeNodeId();
    return linkIndex;
  }

  /// Efface abonnements, fenêtre de réception, files et compteur d'émission (§7.3 étape 4).
  static void resetLinkState(Link& link) {
    link.subscribedSubjectIds[0] = 0;
    link.subscriptionCount       = 0;
    link.queueHead = link.queueCount = 0;
    link.inFlightFrame.isActive = false;
    link.duplicateWindow.reset();
    link.nextSequenceNumber = 0;
  }

  // ---- ACK ----------------------------------------------------------------------------------

  /// Met un ACK en file d'envoi ; le plus ancien est supprimé si la file est pleine.
  void pushAcknowledgement(bool isForSerialLink, const uint8_t* macAddress, uint16_t acknowledgedSequenceNumber,
                           AcknowledgementStatus status, const uint8_t* extraPayload = nullptr,
                           size_t extraPayloadLength = 0) {
    if (pendingAcknowledgementCount_ >= MPS_GATEWAY_ACKNOWLEDGEMENT_RING_SIZE) {
      pendingAcknowledgementHead_ = (pendingAcknowledgementHead_ + 1) % MPS_GATEWAY_ACKNOWLEDGEMENT_RING_SIZE;
      --pendingAcknowledgementCount_;
      ++counters_.droppedFrameCount;
    }
    PendingAcknowledgement& acknowledgement =
        pendingAcknowledgements_[(pendingAcknowledgementHead_ + pendingAcknowledgementCount_) %
                                 MPS_GATEWAY_ACKNOWLEDGEMENT_RING_SIZE];
    acknowledgement.isForSerialLink = isForSerialLink;
    if (macAddress) memcpy(acknowledgement.macAddress, macAddress, 6);
    uint8_t payload[kHostHelloAcknowledgementPayloadSize];
    payload[0] = (uint8_t)status;
    if (extraPayloadLength) memcpy(payload + 1, extraPayload, extraPayloadLength);
    acknowledgement.length = (uint8_t)buildFrame(acknowledgement.bytes, sizeof acknowledgement.bytes,
                                                 MessageType::Acknowledge, 0, kSourceIdGateway, 0,
                                                 acknowledgedSequenceNumber, payload, 1 + extraPayloadLength);
    ++pendingAcknowledgementCount_;
  }

  /// ACK vers un lien connu (radio ou hôte).
  void acknowledgeOnLink(size_t linkIndex, uint16_t acknowledgedSequenceNumber, AcknowledgementStatus status) {
    pushAcknowledgement(linkIndex == kHostLinkIndex, linkIndex == kHostLinkIndex ? nullptr : links_[linkIndex].macAddress,
                        acknowledgedSequenceNumber, status);
  }

  /// ACK NOT_REGISTERED vers un MAC sans session : au plus un par seconde et par MAC (§4.5).
  void replyNotRegistered(const uint8_t* macAddress, uint16_t sequenceNumber) {
    ++counters_.notRegisteredCount;
    NotRegisteredRateLimit* entry = nullptr;
    for (NotRegisteredRateLimit& candidate : notRegisteredRateLimits_) {
      if (candidate.isValid && areMacAddressesEqual(candidate.macAddress, macAddress)) {
        if ((uint32_t)(currentTimeMs_ - candidate.lastSentAtMs) < 1000u) return;  // déjà répondu il y a moins d'une seconde
        entry = &candidate;
        break;
      }
    }
    if (!entry) {
      for (NotRegisteredRateLimit& candidate : notRegisteredRateLimits_)
        if (!candidate.isValid) {
          entry = &candidate;
          break;
        }
      if (!entry) {  // table pleine : remplacer l'entrée la plus ancienne
        entry = &notRegisteredRateLimits_[0];
        for (NotRegisteredRateLimit& candidate : notRegisteredRateLimits_)
          if ((uint32_t)(currentTimeMs_ - candidate.lastSentAtMs) > (uint32_t)(currentTimeMs_ - entry->lastSentAtMs))
            entry = &candidate;
      }
    }
    entry->isValid = true;
    memcpy(entry->macAddress, macAddress, 6);
    entry->lastSentAtMs = currentTimeMs_;
    pushAcknowledgement(false, macAddress, sequenceNumber, AcknowledgementStatus::NotRegistered);
  }

  // ---- réception ----------------------------------------------------------------------------

  /**
   * Validation d'une trame reçue (§4.5), dans l'ordre : taille, longueur, version, origine, type,
   * doublon. Abandonne à la première incohérence en incrémentant un compteur.
   * @param isSerialLink @c true pour une trame de l'hôte (pas d'adresse MAC), @c false pour la radio.
   * @param macAddress   Adresse MAC de l'émetteur (nulle pour le lien série).
   */
  void handleFrame(bool isSerialLink, const uint8_t* macAddress, const uint8_t* frameData, size_t frameLength) {
    FrameView view;
    const ParseResult parseResult = parseFrame(frameData, frameLength, &view);
    int linkIndex = isSerialLink ? (links_[kHostLinkIndex].isActive ? (int)kHostLinkIndex : -1)
                                 : findActiveLinkByMacAddress(macAddress);

    if (parseResult != ParseResult::Ok) {
      ++counters_.invalidLengthCount;
      // ACK BAD_FORMAT seulement vers un pair connu : un MAC inconnu n'obtient rien d'une trame cassée.
      if (parseResult == ParseResult::LengthMismatch && linkIndex >= 0 && view.isAcknowledgementRequested() &&
          view.version == kProtocolVersion && view.type != (uint8_t)MessageType::Acknowledge)
        acknowledgeOnLink((size_t)linkIndex, view.sequenceNumber, AcknowledgementStatus::BadFormat);
      return;
    }
    if (view.version != kProtocolVersion) {  // §4.5 étape 3 : seul un HELLO reçoit une réponse
      ++counters_.unsupportedVersionCount;
      if (view.type == (uint8_t)MessageType::Hello)
        pushAcknowledgement(isSerialLink, macAddress, view.sequenceNumber, AcknowledgementStatus::BadVersion);
      return;
    }
    if (view.type == (uint8_t)MessageType::Hello) {  // §4.5 étape 4 : seule trame admise sans session
      const uint8_t expectedSourceId = isSerialLink ? kSourceIdHost : kSourceIdUnassigned;
      if (view.sourceId != expectedSourceId) {
        ++counters_.originMismatchCount;
        return;
      }
      handleHello(isSerialLink, macAddress, view);
      return;
    }
    if (linkIndex < 0) {  // pas de session
      // §4.5 étape 4 : seuls PUB, SUB, UNSUB et PING d'un pair inconnu reçoivent NOT_REGISTERED ;
      // répondre à un ACK ou à un type inconnu ferait dialoguer deux équipements à l'infini.
      const bool isAnswerable = view.type == (uint8_t)MessageType::Publish || view.type == (uint8_t)MessageType::Subscribe ||
                                view.type == (uint8_t)MessageType::Unsubscribe || view.type == (uint8_t)MessageType::Ping;
      if (!isAnswerable) return;
      if (isSerialLink) {
        ++counters_.notRegisteredCount;
        pushAcknowledgement(true, nullptr, view.sequenceNumber, AcknowledgementStatus::NotRegistered);
      } else {
        replyNotRegistered(macAddress, view.sequenceNumber);
      }
      return;
    }
    Link& link = links_[linkIndex];
    if (view.sourceId != link.nodeId) {  // MAC enregistré mais identifiant d'émetteur incohérent
      ++counters_.originMismatchCount;
      return;
    }
    link.lastReceiveTimeMs = currentTimeMs_;  // toute trame valide vaut preuve de vie (§7.4)
    if (!isKnownMessageType(view.type)) {  // §4.2 : type inconnu ignoré, ACK UNKNOWN_TYPE si ACK_REQ
      ++counters_.unknownTypeCount;
      if (view.isAcknowledgementRequested())
        acknowledgeOnLink((size_t)linkIndex, view.sequenceNumber, AcknowledgementStatus::UnknownType);
      return;
    }
    switch ((MessageType)view.type) {
      case MessageType::Acknowledge: handleAcknowledgement(link, view); break;
      case MessageType::Ping:
        if (!isDuplicateAndHandled(link, (size_t)linkIndex, view)) {
          recordSequenceNumber(link, view);
          acknowledgeOnLink((size_t)linkIndex, view.sequenceNumber, AcknowledgementStatus::Ok);
        }
        break;
      case MessageType::Subscribe:
      case MessageType::Unsubscribe: handleSubscriptionChange((size_t)linkIndex, view); break;
      case MessageType::Publish: handlePublish((size_t)linkIndex, view); break;
      default: break;
    }
  }

  /**
   * Étape 6 du §4.5 : détection des doublons, SANS enregistrer le numéro de séquence.
   * @details L'enregistrement (recordSequenceNumber) n'a lieu qu'une fois la trame acceptée : une trame
   * refusée (BUSY, FORBIDDEN, TABLE_FULL...) n'est donc pas prise pour un doublon à sa retransmission,
   * et reçoit de nouveau le même refus plutôt qu'un OK trompeur.
   * @return @c true pour un doublon (ré-acquitté OK si ACK_REQ) ou une trame trop ancienne : la trame
   *         est alors traitée et l'appelant s'arrête.
   */
  bool isDuplicateAndHandled(const Link& link, size_t linkIndex, const FrameView& view) {
    DuplicateWindow windowCopy = link.duplicateWindow;  // classify() modifie la fenêtre : on la teste sur une copie
    switch (windowCopy.classify(view.sequenceNumber)) {
      case DuplicateWindow::Verdict::Duplicate:
        ++counters_.duplicateCount;
        // L'ACK précédent a pu se perdre : on le renvoie, sans traiter la trame.
        if (view.isAcknowledgementRequested()) acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::Ok);
        return true;
      case DuplicateWindow::Verdict::TooOld: ++counters_.duplicateCount; return true;
      case DuplicateWindow::Verdict::Accept: return false;
    }
    return true;
  }

  /// Enregistre le numéro de séquence d'une trame acceptée dans la fenêtre du lien.
  static void recordSequenceNumber(Link& link, const FrameView& view) { link.duplicateWindow.classify(view.sequenceNumber); }

  /// ACK reçu d'un nœud pour la trame en vol vers lui.
  void handleAcknowledgement(Link& link, const FrameView& view) {
    if (view.payloadLength < 1 || !link.inFlightFrame.isActive || view.sequenceNumber != link.inFlightFrame.sequenceNumber)
      return;  // ACK orphelin (§7.2)
    const AcknowledgementStatus status = (AcknowledgementStatus)view.payload[0];
    if (status == AcknowledgementStatus::Ok) {
      link.inFlightFrame.isActive = false;
    } else if (status == AcknowledgementStatus::Busy) {
      link.inFlightFrame.lastSentAtMs = currentTimeMs_;  // retransmission après le délai d'ACK
    } else {
      link.inFlightFrame.isActive = false;  // erreur définitive : pas de retransmission
      ++counters_.sendFailureCount;
    }
  }

  /**
   * SUB et UNSUB (§9.3). Une liste est appliquée en entier ou pas du tout : la table ne change
   * pas si un identifiant est refusé ou si elle ne peut pas tout contenir.
   */
  void handleSubscriptionChange(size_t linkIndex, const FrameView& view) {
    Link& link = links_[linkIndex];
    if (view.payloadLength < 4 || view.payloadLength > kMaxSubjectsPerFrame * 4 || (view.payloadLength & 3) != 0) {
      acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::BadFormat);
      return;
    }
    if (isDuplicateAndHandled(link, linkIndex, view)) return;
    const size_t listedSubjectCount = view.payloadLength / 4;
    if (view.type == (uint8_t)MessageType::Subscribe) {
      // Travail sur une copie : la table n'est modifiée qu'en cas de succès complet.
      uint32_t workingSubjectIds[MPS_GATEWAY_MAX_SUBSCRIPTIONS];
      size_t   workingCount = link.subscriptionCount;
      memcpy(workingSubjectIds, link.subscribedSubjectIds, workingCount * sizeof(uint32_t));
      for (size_t listIndex = 0; listIndex < listedSubjectCount; ++listIndex) {
        const uint32_t subjectId = readUint32LittleEndian(view.payload + 4 * listIndex);
        if (isReservedSubjectId(subjectId)) {
          acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::BadFormat);
          return;
        }
        bool isAlreadySubscribed = false;
        for (size_t tableIndex = 0; tableIndex < workingCount; ++tableIndex)
          if (workingSubjectIds[tableIndex] == subjectId) {
            isAlreadySubscribed = true;
            break;
          }
        if (isAlreadySubscribed) continue;  // s'abonner deux fois est accepté (idempotent)
        if (workingCount >= MPS_GATEWAY_MAX_SUBSCRIPTIONS) {
          acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::TableFull);
          return;
        }
        workingSubjectIds[workingCount++] = subjectId;
      }
      recordSequenceNumber(link, view);  // seulement si la liste est acceptée : un refus se rejoue à l'identique
      memcpy(link.subscribedSubjectIds, workingSubjectIds, workingCount * sizeof(uint32_t));
      link.subscriptionCount = (uint8_t)workingCount;
    } else {
      recordSequenceNumber(link, view);
      for (size_t listIndex = 0; listIndex < listedSubjectCount; ++listIndex) {
        const uint32_t subjectId = readUint32LittleEndian(view.payload + 4 * listIndex);
        for (size_t tableIndex = 0; tableIndex < link.subscriptionCount; ++tableIndex)
          if (link.subscribedSubjectIds[tableIndex] == subjectId) {
            link.subscribedSubjectIds[tableIndex] = link.subscribedSubjectIds[--link.subscriptionCount];
            break;
          }
        // se désabonner d'un subject inconnu est accepté (idempotent)
      }
    }
    acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::Ok);  // après la mise à jour de la table
  }

  /// Une file qui ne peut plus rien perdre : pleine, et toutes ses trames portent ACK_REQ.
  static bool isQueueBlocked(const Link& link) {
    if (link.queueCount < MPS_GATEWAY_QUEUE_DEPTH) return false;
    for (uint8_t queueOffset = 0; queueOffset < link.queueCount; ++queueOffset)
      if (!(link.queue[(link.queueHead + queueOffset) % MPS_GATEWAY_QUEUE_DEPTH].bytes[2] & kFlagAcknowledgementRequested))
        return false;
    return true;
  }

  static bool isSubscribed(const Link& link, uint32_t subjectId) {
    for (size_t tableIndex = 0; tableIndex < link.subscriptionCount; ++tableIndex)
      if (link.subscribedSubjectIds[tableIndex] == subjectId) return true;
    return false;
  }

  /// PUB reçue : file de routage, doublon, subjects réservés, ACK, puis routage vers les abonnés (§9.2).
  void handlePublish(size_t linkIndex, const FrameView& view) {
    Link& link = links_[linkIndex];
    // Le doublon d'abord : une PUB déjà acceptée est ré-acquittée OK même si la file du destinataire
    // s'est saturée depuis. Les refus (FORBIDDEN, BUSY) ne s'enregistrent pas dans la fenêtre : leur
    // retransmission reçoit le même refus, jamais un OK qui ferait croire à un succès.
    if (isDuplicateAndHandled(link, linkIndex, view)) return;
    if (isSystemSubject(view.subjectId)) {  // seule la passerelle publie des subjects sys_ (§3.4)
      ++counters_.forbiddenCount;
      acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::Forbidden);
      return;
    }
    if (view.isAcknowledgementRequested()) {  // file de destination saturée : BUSY (§9.2)
      for (size_t destinationIndex = 0; destinationIndex < kGatewayLinkCount; ++destinationIndex)
        if (destinationIndex != linkIndex && links_[destinationIndex].isActive &&
            isSubscribed(links_[destinationIndex], view.subjectId) && isQueueBlocked(links_[destinationIndex])) {
          acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::Busy);
          return;
        }
    }
    recordSequenceNumber(link, view);
    // L'ACK signifie « la passerelle a accepté le message », rien de plus sur la livraison.
    if (view.isAcknowledgementRequested()) acknowledgeOnLink(linkIndex, view.sequenceNumber, AcknowledgementStatus::Ok);
    port_.onPublishObserved(view.sourceId, view.subjectId, view.payload, view.payloadLength);
    routeToSubscribers(linkIndex, view.sourceId, view.subjectId, view.payload, view.payloadLength,
                       view.flags & kFlagAcknowledgementRequested);
  }

  // ---- routage ------------------------------------------------------------------------------

  /**
   * Copie un message vers chaque abonné autre que l'origine (§9.2) : @c sourceId, subject et payload
   * conservés, numéro de séquence renuméroté pour le lien de destination, DUP à 0, ACK_REQ conservé.
   */
  void routeToSubscribers(size_t originLinkIndex, uint8_t sourceId, uint32_t subjectId, const uint8_t* payload,
                          size_t payloadLength, uint8_t flags) {
    for (size_t destinationIndex = 0; destinationIndex < kGatewayLinkCount; ++destinationIndex) {
      if (destinationIndex == originLinkIndex) continue;  // jamais livré à l'émetteur d'origine
      Link& destination = links_[destinationIndex];
      if (!destination.isActive || !isSubscribed(destination, subjectId)) continue;
      enqueueForLink(destination, sourceId, subjectId, payload, payloadLength, flags);
    }
  }

  /**
   * Met un message en file d'émission d'un lien. Si la file est pleine, la plus ancienne trame sans
   * ACK_REQ est supprimée ; si toutes en portent un, c'est la nouvelle qui est supprimée (§9.2).
   */
  void enqueueForLink(Link& destination, uint8_t sourceId, uint32_t subjectId, const uint8_t* payload,
                      size_t payloadLength, uint8_t flags) {
    if (destination.queueCount >= MPS_GATEWAY_QUEUE_DEPTH) {
      int victimOffset = -1;
      for (uint8_t queueOffset = 0; queueOffset < destination.queueCount; ++queueOffset)
        if (!(destination.queue[(destination.queueHead + queueOffset) % MPS_GATEWAY_QUEUE_DEPTH].bytes[2] &
              kFlagAcknowledgementRequested)) {
          victimOffset = queueOffset;
          break;
        }
      ++counters_.droppedFrameCount;
      if (victimOffset < 0) return;  // toutes portent ACK_REQ : la nouvelle trame est abandonnée
      for (uint8_t queueOffset = (uint8_t)victimOffset; queueOffset + 1 < destination.queueCount; ++queueOffset)
        destination.queue[(destination.queueHead + queueOffset) % MPS_GATEWAY_QUEUE_DEPTH] =
            destination.queue[(destination.queueHead + queueOffset + 1) % MPS_GATEWAY_QUEUE_DEPTH];
      --destination.queueCount;
    }
    QueuedFrame& frame = destination.queue[(destination.queueHead + destination.queueCount) % MPS_GATEWAY_QUEUE_DEPTH];
    frame.length = (uint16_t)buildFrame(frame.bytes, sizeof frame.bytes, MessageType::Publish,
                                        flags & kFlagAcknowledgementRequested, sourceId, subjectId,
                                        destination.nextSequenceNumber++, payload, payloadLength);
    ++destination.queueCount;
  }

  /// Publie un événement système (@c sys_*) aux abonnés, sans ACK_REQ.
  void publishSystemMessage(uint32_t subjectId, const uint8_t* payload, size_t payloadLength,
                            size_t originLinkIndex) {
    routeToSubscribers(originLinkIndex, kSourceIdGateway, subjectId, payload, payloadLength, 0);
  }

  /// @c sys_node_online : identifiant, adresse MAC, nom d'appareil ASCII (§3.4).
  void publishNodeOnline(size_t linkIndex) {
    const Link& link = links_[linkIndex];
    uint8_t payload[1 + 6 + kMaxDeviceNameLength];
    payload[0] = link.nodeId;
    memcpy(payload + 1, link.macAddress, 6);
    memcpy(payload + 7, link.deviceName, link.deviceNameLength);
    publishSystemMessage(kSubjectIdNodeOnline, payload, 7u + link.deviceNameLength, linkIndex);
    port_.onNodeOnline(link.nodeId, link.macAddress, link.deviceName, link.deviceNameLength);
  }

  /// @c sys_node_offline : identifiant et raison (§3.4).
  void publishNodeOffline(size_t linkIndex, OfflineReason reason) {
    const uint8_t payload[2] = {links_[linkIndex].nodeId, (uint8_t)reason};
    publishSystemMessage(kSubjectIdNodeOffline, payload, 2, linkIndex);
    port_.onNodeOffline(links_[linkIndex].nodeId, reason);
  }

  // ---- session HELLO ------------------------------------------------------------------------

  /**
   * Traite un HELLO (§7.3), seule trame admise sans session : validation du nom, admission,
   * attribution de l'identifiant, ajout du pair radio, remise à zéro de l'état, ACK, événements.
   */
  void handleHello(bool isSerialLink, const uint8_t* macAddress, const FrameView& view) {
    // Payload : capacités u16 puis nom ASCII imprimable de 0 à 16 octets (§4.4).
    if (view.payloadLength < 2 || view.payloadLength > 2u + kMaxDeviceNameLength) {
      pushAcknowledgement(isSerialLink, macAddress, view.sequenceNumber, AcknowledgementStatus::BadFormat);
      return;
    }
    const size_t deviceNameLength = view.payloadLength - 2u;
    for (size_t characterIndex = 0; characterIndex < deviceNameLength; ++characterIndex)
      if (view.payload[2 + characterIndex] < 0x20 || view.payload[2 + characterIndex] > 0x7E) {
        pushAcknowledgement(isSerialLink, macAddress, view.sequenceNumber, AcknowledgementStatus::BadFormat);
        return;
      }

    size_t linkIndex;
    if (isSerialLink) {
      linkIndex = kHostLinkIndex;
    } else {
      if (!isMacAddressAllowed(macAddress)) {
        ++counters_.forbiddenCount;
        pushAcknowledgement(false, macAddress, view.sequenceNumber, AcknowledgementStatus::Forbidden);
        return;
      }
      const int existingLinkIndex = findAllocatedLinkByMacAddress(macAddress);
      // Un HELLO retransmis (DUP) reçu juste après l'acceptation du premier : simple ré-ACK (§11.3).
      if (existingLinkIndex >= 0 && links_[existingLinkIndex].isActive && (view.flags & kFlagDuplicate) &&
          (uint32_t)(currentTimeMs_ - links_[existingLinkIndex].helloAcceptedAtMs) < 1000u) {
        sendHelloAcknowledgement(false, macAddress, view.sequenceNumber, links_[existingLinkIndex].nodeId,
                                 configuration_.keepaliveIntervalSeconds);
        links_[existingLinkIndex].lastReceiveTimeMs = currentTimeMs_;
        return;
      }
      const int allocatedLinkIndex = allocateLink(macAddress);
      if (allocatedLinkIndex < 0 || !port_.addRadioPeer(macAddress)) {
        if (allocatedLinkIndex >= 0 && !links_[allocatedLinkIndex].isActive)
          links_[allocatedLinkIndex].isAllocated = false;  // rend l'emplacement neuf
        pushAcknowledgement(false, macAddress, view.sequenceNumber, AcknowledgementStatus::TableFull);
        return;
      }
      linkIndex = (size_t)allocatedLinkIndex;
    }

    Link& link = links_[linkIndex];
    if (isSerialLink) {
      if (link.isActive && (view.flags & kFlagDuplicate) && (uint32_t)(currentTimeMs_ - link.helloAcceptedAtMs) < 1000u) {
        sendHelloAcknowledgement(true, nullptr, view.sequenceNumber, kSourceIdHost, 0);
        return;
      }
      link.isAllocated = true;
      link.nodeId      = kSourceIdHost;
    }
    const bool wasActive = link.isActive;
    // Le nœud redémarré s'annonce hors ligne (raison 1) aux AUTRES abonnés avant d'être remis à zéro.
    if (wasActive && !isSerialLink) publishNodeOffline(linkIndex, OfflineReason::NodeRestarted);

    resetLinkState(link);
    link.isActive         = true;
    link.deviceNameLength = (uint8_t)deviceNameLength;
    memcpy(link.deviceName, view.payload + 2, deviceNameLength);
    link.lastReceiveTimeMs = currentTimeMs_;
    link.helloAcceptedAtMs = currentTimeMs_;

    sendHelloAcknowledgement(isSerialLink, isSerialLink ? nullptr : macAddress, view.sequenceNumber, link.nodeId,
                             isSerialLink ? 0 : configuration_.keepaliveIntervalSeconds);
    if (!isSerialLink) publishNodeOnline(linkIndex);
  }

  /**
   * ACK de HELLO accepté : statut OK, identifiant attribué, taille de payload maximale, keepalive (§4.4).
   * Vers l'hôte, il ajoute l'adresse MAC et le canal Wi-Fi de la passerelle (voir setGatewayIdentity()).
   */
  void sendHelloAcknowledgement(bool isForSerialLink, const uint8_t* macAddress, uint16_t sequenceNumber,
                                uint8_t nodeId, uint8_t keepaliveIntervalSeconds) {
    uint8_t sessionInformation[kHostHelloAcknowledgementPayloadSize - 1];
    sessionInformation[0] = nodeId;
    writeUint16LittleEndian(sessionInformation + 1, (uint16_t)kMaxPayloadSize);
    sessionInformation[3] = keepaliveIntervalSeconds;
    size_t informationLength = kHelloAcknowledgementPayloadSize - 1;
    if (isForSerialLink) {
      memcpy(sessionInformation + 4, gatewayMacAddress_, 6);
      sessionInformation[10] = wifiChannel_;
      informationLength      = kHostHelloAcknowledgementPayloadSize - 1;
    }
    pushAcknowledgement(isForSerialLink, macAddress, sequenceNumber, AcknowledgementStatus::Ok, sessionInformation,
                        informationLength);
  }

  /// Un nœud muet depuis 3 × keepalive : abonnements et pair radio supprimés, événement offline (§7.4).
  void expireNode(size_t linkIndex) {
    Link& link = links_[linkIndex];
    publishNodeOffline(linkIndex, OfflineReason::KeepaliveExpired);
    port_.removeRadioPeer(link.macAddress);
    resetLinkState(link);
    link.isActive = false;
  }

  // ---- émission -----------------------------------------------------------------------------

  /// Encode une trame pour la liaison série et l'écrit sans jamais bloquer (§11.5).
  bool writeSerialFrame(const uint8_t* frame, size_t frameLength) {
    uint8_t encodedFrame[kMaxSerialEncodedSize + 4];
    const size_t encodedLength = encodeSerialFrame(frame, frameLength, encodedFrame, sizeof encodedFrame);
    if (encodedLength == 0) return false;
    if (!port_.writeSerialBytes(encodedFrame, encodedLength)) {  // hôte absent : la trame est supprimée
      ++counters_.droppedFrameCount;
      return false;
    }
    return true;
  }

  /**
   * Envoie une trame sur un lien.
   * @return @c false uniquement si la radio est occupée (rien n'est consommé). Une écriture série
   *         refusée est une suppression comptée dans @c droppedFrameCount : la trame est consommée.
   */
  bool transmitOnLink(size_t linkIndex, const uint8_t* frame, size_t frameLength) {
    if (linkIndex == kHostLinkIndex) {
      writeSerialFrame(frame, frameLength);
      return true;
    }
    if (!port_.sendRadioFrame(links_[linkIndex].macAddress, frame, frameLength)) {
      isRadioBusy_ = true;
      return false;
    }
    return true;
  }

  /**
   * Cœur de l'émission, appelé après chaque événement : d'abord les ACK en attente, puis les files
   * des liens en tournant pour l'équité entre nœuds. Une radio occupée suspend les liens radio
   * jusqu'au prochain appel.
   */
  void transmitPendingFrames() {
    // 1. ACK : prioritaires, courts, sans arrêt-et-attente.
    while (pendingAcknowledgementCount_ > 0) {
      PendingAcknowledgement& acknowledgement = pendingAcknowledgements_[pendingAcknowledgementHead_];
      if (acknowledgement.isForSerialLink) {
        writeSerialFrame(acknowledgement.bytes, acknowledgement.length);
      } else if (!port_.sendRadioFrame(acknowledgement.macAddress, acknowledgement.bytes, acknowledgement.length)) {
        isRadioBusy_ = true;
        break;
      }
      pendingAcknowledgementHead_ = (pendingAcknowledgementHead_ + 1) % MPS_GATEWAY_ACKNOWLEDGEMENT_RING_SIZE;
      --pendingAcknowledgementCount_;
    }
    // 2. Files des liens, en commençant par un lien différent à chaque appel.
    for (size_t offset = 0; offset < kGatewayLinkCount; ++offset)
      transmitLinkQueue((nextLinkToServe_ + offset) % kGatewayLinkCount);
    nextLinkToServe_ = (nextLinkToServe_ + 1) % kGatewayLinkCount;
    isRadioBusy_     = false;  // la radio est réessayée à chaque appel
  }

  /// Arrêt-et-attente d'un lien : renvoi ou abandon de la trame en vol, puis trames suivantes de la file.
  void transmitLinkQueue(size_t linkIndex) {
    Link& link = links_[linkIndex];
    if (!link.isActive) return;
    if (linkIndex != kHostLinkIndex && isRadioBusy_) return;
    InFlightFrame& inFlight = link.inFlightFrame;
    if (inFlight.isActive) {
      if (inFlight.needsResend) {
        if (!transmitOnLink(linkIndex, inFlight.bytes, inFlight.length)) return;
        inFlight.needsResend  = false;
        inFlight.lastSentAtMs = currentTimeMs_;
        return;
      }
      if ((uint32_t)(currentTimeMs_ - inFlight.lastSentAtMs) < configuration_.acknowledgementTimeoutMs) return;
      if (inFlight.retransmissionsDone >= configuration_.maxRetransmissions) {
        inFlight.isActive = false;  // échec ; si le nœud a disparu, le keepalive le retirera
        ++counters_.sendFailureCount;
      } else {
        ++inFlight.retransmissionsDone;
        ++counters_.retransmissionCount;
        inFlight.bytes[2] |= kFlagDuplicate;  // même numéro de séquence, drapeau DUP
        if (transmitOnLink(linkIndex, inFlight.bytes, inFlight.length))
          inFlight.lastSentAtMs = currentTimeMs_;
        else
          inFlight.needsResend = true;
        return;
      }
    }
    while (!inFlight.isActive && link.queueCount > 0) {
      QueuedFrame& frame = link.queue[link.queueHead];
      if (!transmitOnLink(linkIndex, frame.bytes, frame.length)) return;
      if (frame.bytes[2] & kFlagAcknowledgementRequested) {  // en attente d'ACK : devient la trame en vol
        memcpy(inFlight.bytes, frame.bytes, frame.length);
        inFlight.length              = frame.length;
        inFlight.sequenceNumber      = readUint16LittleEndian(frame.bytes + 8);
        inFlight.lastSentAtMs        = currentTimeMs_;
        inFlight.retransmissionsDone = 0;
        inFlight.needsResend         = false;
        inFlight.isActive            = true;
      }
      link.queueHead = (uint8_t)((link.queueHead + 1) % MPS_GATEWAY_QUEUE_DEPTH);
      --link.queueCount;
    }
  }

  // ---- données ------------------------------------------------------------------------------

  IGatewayPort&        port_;
  GatewayConfiguration configuration_;
  uint32_t             currentTimeMs_ = 0;
  Link                 links_[kGatewayLinkCount];
  PendingAcknowledgement pendingAcknowledgements_[MPS_GATEWAY_ACKNOWLEDGEMENT_RING_SIZE];
  size_t               pendingAcknowledgementHead_  = 0;
  size_t               pendingAcknowledgementCount_ = 0;
  NotRegisteredRateLimit notRegisteredRateLimits_[8];
  SerialReceiver       serialReceiver_;
  bool                 isRadioBusy_     = false;
  size_t               nextLinkToServe_ = 0;
  GatewayCounters      counters_;
  uint8_t              gatewayMacAddress_[6] = {0};  ///< Envoyée à l'hôte (ACK de son HELLO).
  uint8_t              wifiChannel_          = 0;    ///< Envoyé à l'hôte (ACK de son HELLO).
};

}  // namespace mps
