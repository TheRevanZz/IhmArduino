/**
 * @file mps_client.h
 * @brief Moteur client MPS : session HELLO/SUB, ACK, retransmission, doublons, keepalive et backoff.
 *
 * @details Spécification §6 et §7. Ce moteur est commun au nœud (lien radio) et à l'hôte (lien série) :
 * « l'hôte se comporte comme un nœud » (§7). Il ne fait aucune entrée/sortie : le transport
 * (ESP-NOW, série) et l'horloge sont injectés, ce qui le rend testable sur PC avec un lien
 * simulé. Aucune allocation dynamique.
 *
 * @par Contrat d'appel (mono-thread)
 * processReceivedFrame() et poll() doivent être appelés depuis le même contexte : la fonction
 * @c loop() d'Arduino, ou le thread de lecture de l'hôte. Jamais depuis la tâche Wi-Fi (§11.3).
 *
 * @par Cycle de vie d'une session
 * Unregistered → (HELLO envoyé) HelloSent → (ACK reçu, SUB envoyés) Subscribing → (dernier ACK de
 * SUB) Registered. Toute perte de session ramène à Unregistered avec un nouveau HELLO après un
 * délai de backoff (1, 2, 4, 8 puis 10 s, plus 0 à 250 ms d'aléa pour les nœuds).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mps_duplicate_window.h"
#include "mps_frame.h"
#include "mps_protocol.h"
#include "mps_subject.h"

#ifndef MPS_MAX_HANDLERS
#define MPS_MAX_HANDLERS 32  ///< Abonnements par lien (§9.1).
#endif
#ifndef MPS_TRANSMIT_QUEUE_DEPTH
#define MPS_TRANSMIT_QUEUE_DEPTH 8  ///< Profondeur de la file d'envoi, recommandée par le §7.6.
#endif
#ifndef MPS_ACKNOWLEDGEMENT_QUEUE_DEPTH
#define MPS_ACKNOWLEDGEMENT_QUEUE_DEPTH 4  ///< ACK en attente de transport (radio occupée).
#endif

namespace mps {

// ------------------------------------------------------------------------------------------------
// Subjects vérifiés à la compilation
// ------------------------------------------------------------------------------------------------

/**
 * @brief Un subject : son nom lisible et son identifiant sur le fil.
 */
struct Subject {
  const char* name;       ///< Nom du subject (chaîne littérale, doit rester valide).
  uint32_t    subjectId;  ///< Hachage FNV-1a du nom (§3.3).
};

/**
 * @brief Fonction volontairement jamais définie, appelée quand un nom de subject est invalide.
 * @details Appelée dans une expression constante, elle provoque une erreur de compilation dont le
 * message nomme cette fonction : c'est ainsi qu'un nom invalide « ne compile pas ».
 */
void invalidSubjectName_see_spec_3_2();

/**
 * @brief Construit un Subject en validant son nom à la compilation.
 *
 * @details Refuse la grammaire du §3.2 (majuscule, espace, @c _ double...) et les identifiants
 * réservés du §3.3 quand elle est évaluée dans un @c constexpr.
 *
 * @code
 * constexpr auto kLedState = mps::makeSubject("set_sensor_a_led_state");
 * @endcode
 *
 * @param name Nom du subject (chaîne littérale).
 * @return Le Subject, dont l'identifiant est calculé à la compilation.
 */
constexpr Subject makeSubject(const char* name) {
  return (isValidSubjectName(name) && !isReservedSubjectId(fnv1aHash(name)))
             ? Subject{name, fnv1aHash(name)}
             : (invalidSubjectName_see_spec_3_2(), Subject{name, 0});
}

// ------------------------------------------------------------------------------------------------
// Types publics
// ------------------------------------------------------------------------------------------------

/// Code de retour des opérations du client.
enum class ErrorCode : uint8_t {
  Ok,                      ///< Succès.
  NotRegistered,           ///< Pas de session ouverte : refuser la publication (§7.3).
  QueueFull,               ///< File d'envoi pleine (§7.2).
  PayloadTooBig,           ///< Payload supérieur à min(238, max_payload) (§8.3).
  InvalidName,             ///< Nom hors de la grammaire du §3.2.
  ReservedSubject,         ///< Identifiant 0x00000000 ou 0xFFFFFFFF.
  SystemSubjectForbidden,  ///< Publication d'un subject @c sys_ interdite (§3.4).
  HashCollision,           ///< Deux noms différents ont le même identifiant (§3.5).
  HandlerTableFull,        ///< Plus de place pour un abonnement supplémentaire.
  NotFound,                ///< Subject non abonné.
};

/// État de la session avec la passerelle.
enum class SessionState : uint8_t {
  Unregistered,  ///< Pas de session : un HELLO sera envoyé quand le délai de backoff sera écoulé.
  HelloSent,     ///< HELLO envoyé, en attente de l'ACK.
  Subscribing,   ///< ACK reçu, envoi des abonnements en cours.
  Registered,    ///< Session ouverte : les publications sont acceptées.
};

/// Cause de la perte de session, transmise à IClientListener::onUnregistered().
enum class SessionLossReason : uint8_t {
  SendFailed,            ///< Un envoi est resté sans ACK après toutes les retransmissions (§7.4).
  GatewayForgotSession,  ///< ACK NOT_REGISTERED : la passerelle a perdu la session (§7.5).
  Rejected,              ///< HELLO ou SUB refusé définitivement.
  GatewayRestarted,      ///< @c sys_gw_boot reçu (hôte, §7.5).
};

/// Cause d'échec d'un envoi, transmise à IClientListener::onSendFailed().
enum class SendFailureReason : uint8_t {
  Timeout,   ///< Aucun ACK après MAX_RETRANSMIT retransmissions.
  Rejected,  ///< ACK d'erreur définitive ; le statut est fourni à part.
};

/**
 * @brief Notifications du moteur vers l'application. Toutes les méthodes sont facultatives.
 * @details Appelées depuis poll() ou processReceivedFrame(), donc depuis le contexte de l'appelant.
 * @warning Ne pas publier depuis onSendFailed() : la session est sur le point d'être perdue.
 */
class IClientListener {
 public:
  virtual ~IClientListener() {}

  /**
   * @brief La session est ouverte et tous les abonnements sont acquittés.
   * @param assignedNodeId            Identifiant attribué par la passerelle (0xFE pour l'hôte).
   * @param maxPayloadSize            Taille maximale de payload annoncée par la passerelle.
   * @param keepaliveIntervalSeconds  Intervalle de keepalive (0 sur le lien série).
   */
  virtual void onRegistered(uint8_t assignedNodeId, uint16_t maxPayloadSize, uint8_t keepaliveIntervalSeconds) {
    (void)assignedNodeId;
    (void)maxPayloadSize;
    (void)keepaliveIntervalSeconds;
  }

  /**
   * @brief La session a été perdue ; un nouveau HELLO suivra.
   * @param reason Cause de la perte.
   */
  virtual void onUnregistered(SessionLossReason reason) { (void)reason; }

  /**
   * @brief Un envoi a échoué : afficher « nœud injoignable » plutôt que réessayer en boucle (§11.6).
   * @param sequenceNumber  Numéro de séquence de la trame perdue.
   * @param subjectId       Subject concerné (0 si la trame n'en porte pas).
   * @param reason          Timeout ou refus définitif.
   * @param rejectionStatus Statut de l'ACK de refus (Ok pour un Timeout).
   */
  virtual void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, SendFailureReason reason,
                            AcknowledgementStatus rejectionStatus) {
    (void)sequenceNumber;
    (void)subjectId;
    (void)reason;
    (void)rejectionStatus;
  }
};

/**
 * @brief Transport : envoie une trame MPS complète vers la passerelle.
 */
class ITransport {
 public:
  virtual ~ITransport() {}

  /**
   * @brief Envoie une trame.
   * @param[in] frame       Trame MPS complète (en-tête + payload).
   * @param[in] frameLength Taille de la trame.
   * @return @c false si l'envoi est impossible maintenant (par exemple un envoi radio est déjà en
   *         cours, §4.6) : le moteur réessaiera au prochain poll() sans compter de tentative.
   */
  virtual bool send(const uint8_t* frame, size_t frameLength) = 0;
};

/**
 * @brief Fonction appelée à chaque message reçu sur un subject abonné.
 *
 * @details @c message.payload n'est valable que pendant l'appel (§11.3). Le handler doit être court
 * et vérifier @c message.payloadLength avant de lire (§6.4) : readPayloadAs() le fait.
 *
 * @param message     Message reçu.
 * @param userContext Pointeur fourni à l'abonnement (peut être nul).
 */
typedef void (*MessageHandler)(const FrameView& message, void* userContext);

/// Rôle du client : le nœud et l'hôte diffèrent par leur identifiant et leur gestion du jitter.
enum class Role : uint8_t {
  Node,  ///< Nœud radio : HELLO avec @c sourceId 0xFF, jitter sur le backoff.
  Host,  ///< Hôte série : HELLO avec @c sourceId 0xFE, réagit à @c sys_gw_boot.
};

/**
 * @brief Configuration d'un Client.
 */
struct ClientConfiguration {
  Role        role                       = Role::Node;
  const char* deviceName                 = "";  ///< Nom envoyé dans HELLO : 0 à 16 caractères ASCII imprimables (§4.4).
  uint32_t    acknowledgementTimeoutMs   = kAcknowledgementTimeoutMs;
  uint8_t     maxRetransmissions         = kMaxRetransmissions;
  /// Source d'aléa pour le jitter de 0 à 250 ms du backoff HELLO (§7.3) ; sans elle, pas de jitter.
  uint32_t (*randomSource)() = nullptr;
};

/**
 * @brief Compteurs de diagnostic du client (noms de la spec §9.5 entre parenthèses).
 */
struct ClientCounters {
  uint32_t invalidLengthCount     = 0;  ///< Trames de taille ou de longueur incohérente (@c rx_len_err).
  uint32_t unsupportedVersionCount = 0; ///< Trames d'une version inconnue (@c rx_ver_err).
  uint32_t unknownTypeCount       = 0;  ///< Trames de type inconnu (@c rx_unknown_type).
  uint32_t duplicateCount         = 0;  ///< Doublons rejetés (@c rx_dup).
  uint32_t retransmissionCount    = 0;  ///< Retransmissions effectuées (@c tx_retry).
  uint32_t sendFailureCount       = 0;  ///< Envois échoués après MAX_RETRANSMIT (@c tx_fail).
  uint32_t droppedFrameCount      = 0;  ///< Trames de la file perdues avec la session (@c tx_drop).
};

// ------------------------------------------------------------------------------------------------
// Moteur
// ------------------------------------------------------------------------------------------------

/**
 * @brief Moteur client MPS d'un nœud ou de l'hôte.
 */
class Client {
 public:
  /**
   * @param transport     Transport vers la passerelle (doit survivre au client).
   * @param configuration Configuration initiale, voir configure().
   */
  Client(ITransport& transport, const ClientConfiguration& configuration) : transport_(transport) {
    configure(configuration);
  }

  Client(const Client&)            = delete;
  Client& operator=(const Client&) = delete;

  /**
   * @brief Change la configuration (rôle, nom d'appareil...).
   * @details À appeler avant begin(). La chaîne @c deviceName doit rester valide aussi longtemps
   * que le client : elle n'est pas copiée.
   * @param configuration Nouvelle configuration.
   */
  void configure(const ClientConfiguration& configuration) {
    configuration_       = configuration;
    helloSourceId_       = configuration.role == Role::Host ? kSourceIdHost : kSourceIdUnassigned;
    ownSourceId_         = helloSourceId_;
    size_t nameLength    = 0;
    if (configuration.deviceName)
      while (configuration.deviceName[nameLength] && nameLength < kMaxDeviceNameLength) ++nameLength;
    deviceNameLength_ = nameLength;
  }

  /// @param listener Destinataire des notifications (peut être nul).
  void setListener(IClientListener* listener) { listener_ = listener; }

  // ---- abonnements --------------------------------------------------------------------------

  /**
   * @brief Abonne un subject et enregistre son handler.
   *
   * @details Si la session est ouverte, le SUB part tout de suite ; sinon il part avec ceux de la
   * session. Enregistrer deux fois le même nom remplace le handler. Valide le nom (§3.2) et détecte
   * les collisions de hachage (§3.5).
   *
   * @param subject     Subject à écouter.
   * @param handler     Fonction appelée à chaque message.
   * @param userContext Pointeur transmis au handler.
   * @return Ok, ou la raison du refus.
   */
  ErrorCode addHandler(const Subject& subject, MessageHandler handler, void* userContext = nullptr) {
    if (!isValidSubjectName(subject.name)) return ErrorCode::InvalidName;
    if (subject.subjectId != fnv1aHash(subject.name)) return ErrorCode::InvalidName;
    return addHandlerInternal(subject.subjectId, subject.name, handler, userContext);
  }

  /**
   * @brief Abonne un subject désigné par son nom (validé à l'exécution).
   * @param name        Nom du subject ; la chaîne doit rester valide aussi longtemps que l'abonnement.
   * @param handler     Fonction appelée à chaque message.
   * @param userContext Pointeur transmis au handler.
   * @return Ok, ou la raison du refus.
   */
  ErrorCode addHandler(const char* name, MessageHandler handler, void* userContext = nullptr) {
    if (!name || !isValidSubjectName(name)) return ErrorCode::InvalidName;
    return addHandlerInternal(fnv1aHash(name), name, handler, userContext);
  }

  /**
   * @brief Abonne un subject désigné par son identifiant seul.
   * @details Utile à l'hôte qui ne connaît que des identifiants ; la détection de collision n'est
   * alors pas possible.
   */
  ErrorCode addHandlerById(uint32_t subjectId, MessageHandler handler, void* userContext = nullptr) {
    return addHandlerInternal(subjectId, nullptr, handler, userContext);
  }

  /**
   * @brief Se désabonne d'un subject.
   * @param subjectId Identifiant du subject.
   * @return Ok, ou NotFound si le subject n'était pas abonné.
   */
  ErrorCode removeHandler(uint32_t subjectId) {
    for (size_t handlerIndex = 0; handlerIndex < handlerCount_; ++handlerIndex) {
      if (handlers_[handlerIndex].subjectId != subjectId) continue;
      handlers_[handlerIndex] = handlers_[handlerCount_ - 1];  // suppression sans trou
      --handlerCount_;
      if (sessionState_ == SessionState::Registered) {
        uint8_t payload[4];
        writeUint32LittleEndian(payload, subjectId);
        // Si la file est pleine, la passerelle garde l'abonnement : sans conséquence grave.
        enqueueFrame(MessageType::Unsubscribe, kFlagAcknowledgementRequested, subjectId, payload, 4);
        transmitPendingFrames();
      }
      return ErrorCode::Ok;
    }
    return ErrorCode::NotFound;
  }

  // ---- publication --------------------------------------------------------------------------

  /**
   * @brief Publie un message sur un subject.
   * @param subject                Subject (un subject @c sys_ est refusé).
   * @param payload                Octets à envoyer.
   * @param payloadLength          Nombre d'octets, au plus min(238, max_payload).
   * @param requestAcknowledgement @c true pour les @c set_ et @c cmd_ (§11.6).
   * @return Ok, ou NotRegistered / PayloadTooBig / QueueFull / SystemSubjectForbidden...
   */
  ErrorCode publish(const Subject& subject, const void* payload, size_t payloadLength,
                    bool requestAcknowledgement = false) {
    if (hasReservedSystemPrefix(subject.name, stringLength(subject.name))) return ErrorCode::SystemSubjectForbidden;
    return publishById(subject.subjectId, payload, payloadLength, requestAcknowledgement);
  }

  /// @copydoc publish
  /// @details Variante par identifiant : le préfixe @c sys_ ne peut pas être vérifié (la passerelle refusera).
  ErrorCode publishById(uint32_t subjectId, const void* payload, size_t payloadLength,
                        bool requestAcknowledgement = false) {
    if (isReservedSubjectId(subjectId)) return ErrorCode::ReservedSubject;
    if (sessionState_ != SessionState::Registered) return ErrorCode::NotRegistered;
    if (payloadLength > maxPayloadSize_) return ErrorCode::PayloadTooBig;
    const uint8_t flags = requestAcknowledgement ? kFlagAcknowledgementRequested : 0;
    if (!enqueueFrame(MessageType::Publish, flags, subjectId, (const uint8_t*)payload, payloadLength))
      return ErrorCode::QueueFull;
    transmitPendingFrames();
    return ErrorCode::Ok;
  }

  // ---- pilotage -----------------------------------------------------------------------------

  /**
   * @brief Démarre la procédure d'enregistrement : le HELLO part au prochain poll().
   * @param currentTimeMs Horloge monotone en millisecondes.
   */
  void begin(uint32_t currentTimeMs) {
    currentTimeMs_ = currentTimeMs;
    resetSession();
    sessionState_      = SessionState::Unregistered;
    helloBackoffStep_  = 0;
    nextHelloTimeMs_   = currentTimeMs;
    hasStarted_        = true;
  }

  /**
   * @brief Fait avancer les temporisations : HELLO, keepalive, retransmissions.
   * @details À appeler au moins toutes les 10 ms (§11.3).
   * @param currentTimeMs Horloge monotone en millisecondes (le débordement est géré).
   */
  void poll(uint32_t currentTimeMs) {
    currentTimeMs_ = currentTimeMs;
    if (!hasStarted_) return;
    if (sessionState_ == SessionState::Unregistered &&
        (int32_t)(currentTimeMs_ - nextHelloTimeMs_) >= 0)
      sendHello();
    // Keepalive : un PING si la passerelle n'a rien acquitté depuis l'intervalle qu'elle impose (§7.4 :
    // « toute trame acquittée vaut keepalive »). Les trames sans ACK_REQ (heartbeats) ne comptent pas :
    // sinon un nœud qui ne publie que des hb_ ne verrait jamais que la passerelle ne répond plus.
    if (sessionState_ == SessionState::Registered && keepaliveIntervalSeconds_ > 0 && !keepalivePingPending_ &&
        (uint32_t)(currentTimeMs_ - lastAcknowledgedTimeMs_) >= (uint32_t)keepaliveIntervalSeconds_ * 1000u) {
      if (enqueueFrame(MessageType::Ping, kFlagAcknowledgementRequested, 0, nullptr, 0))
        keepalivePingPending_ = true;
    }
    transmitPendingFrames();
  }

  /**
   * @brief Traite une trame reçue de la passerelle.
   * @details L'origine doit avoir été filtrée par l'appelant (adresse MAC de la passerelle côté nœud).
   * @param frameData     Octets reçus.
   * @param frameLength   Nombre d'octets.
   * @param currentTimeMs Horloge monotone en millisecondes.
   */
  void processReceivedFrame(const uint8_t* frameData, size_t frameLength, uint32_t currentTimeMs) {
    currentTimeMs_ = currentTimeMs;
    FrameView view;
    const ParseResult parseResult = parseFrame(frameData, frameLength, &view);
    if (parseResult != ParseResult::Ok) {
      ++counters_.invalidLengthCount;
      if (parseResult == ParseResult::LengthMismatch && view.isAcknowledgementRequested() &&
          view.version == kProtocolVersion && isKnownMessageType(view.type) &&
          view.type != (uint8_t)MessageType::Acknowledge)
        queueAcknowledgement(view.sequenceNumber, AcknowledgementStatus::BadFormat);
      transmitPendingFrames();
      return;
    }
    if (view.version != kProtocolVersion) {
      ++counters_.unsupportedVersionCount;
      return;
    }
    if (!isKnownMessageType(view.type)) {
      ++counters_.unknownTypeCount;
      if (view.isAcknowledgementRequested())
        queueAcknowledgement(view.sequenceNumber, AcknowledgementStatus::UnknownType);
      transmitPendingFrames();
      return;
    }
    switch ((MessageType)view.type) {
      case MessageType::Acknowledge: handleAcknowledgement(view); break;
      case MessageType::Publish: handlePublish(view); break;
      default: break;  // SUB, UNSUB, HELLO et PING ne sont pas destinés à un client
    }
    transmitPendingFrames();
  }

  // ---- lecture d'état -----------------------------------------------------------------------

  /// @return L'état courant de la session.
  SessionState sessionState() const { return sessionState_; }
  /// @return @c true si la session est ouverte.
  bool isRegistered() const { return sessionState_ == SessionState::Registered; }
  /// @return L'identifiant attribué par la passerelle (0xFF ou 0xFE tant qu'aucun HELLO n'a abouti).
  uint8_t assignedNodeId() const { return ownSourceId_; }
  /// @return La taille de payload maximale négociée.
  uint16_t maxPayloadSize() const { return maxPayloadSize_; }
  /// @return L'intervalle de keepalive imposé (0 : pas de keepalive).
  uint8_t keepaliveIntervalSeconds() const { return keepaliveIntervalSeconds_; }
  /// @return Les compteurs de diagnostic.
  const ClientCounters& counters() const { return counters_; }
  /// @return @c true si la passerelle a transmis son identité (ACK du HELLO de l'hôte, lien série).
  bool hasGatewayIdentity() const { return hasGatewayIdentity_; }
  /// @return L'adresse MAC de la passerelle (6 octets), valide si hasGatewayIdentity().
  const uint8_t* gatewayMacAddress() const { return gatewayMacAddress_; }
  /// @return Le canal Wi-Fi de la passerelle, valide si hasGatewayIdentity().
  uint8_t gatewayWifiChannel() const { return gatewayWifiChannel_; }
  /// @return Le nombre d'abonnements.
  size_t handlerCount() const { return handlerCount_; }
  /// @return Le nombre de trames en attente d'envoi ou d'acquittement.
  size_t pendingFrameCount() const { return queueCount_ + (inFlightFrame_.isActive ? 1 : 0); }

 private:
  /// Abonnement : un subject et la fonction à appeler.
  struct HandlerEntry {
    uint32_t       subjectId;
    const char*    subjectName;  ///< Peut être nul (abonnement par identifiant).
    MessageHandler handler;
    void*          userContext;
  };

  /// Trame en file d'attente d'émission.
  struct QueuedFrame {
    size_t  length;
    uint8_t bytes[kMaxFrameSize];
    bool    hasSequenceNumber;  ///< Le numéro de séquence est attribué au premier essai d'envoi.
  };

  /// Trame ACK_REQ envoyée, en attente de son ACK (arrêt-et-attente : une seule à la fois).
  struct InFlightFrame {
    bool        isActive              = false;
    size_t      length                = 0;
    uint8_t     bytes[kMaxFrameSize];
    uint16_t    sequenceNumber        = 0;
    uint32_t    subjectId             = 0;  ///< Pour signaler l'échec à l'application.
    MessageType messageType           = MessageType::Publish;
    uint32_t    lastSentAtMs          = 0;
    uint8_t     retransmissionsDone   = 0;
    bool        needsResend           = false;  ///< Transport occupé : renvoyer dès que possible.
  };

  /// ACK à envoyer en réponse à une trame reçue.
  struct PendingAcknowledgement {
    uint8_t bytes[kHeaderSize + 1];
  };

  // ---- abonnements --------------------------------------------------------------------------

  /// Cœur commun de addHandler() : collision, table pleine, SUB immédiat si la session est ouverte.
  ErrorCode addHandlerInternal(uint32_t subjectId, const char* subjectName, MessageHandler handler,
                               void* userContext) {
    if (isReservedSubjectId(subjectId)) return ErrorCode::ReservedSubject;
    for (size_t handlerIndex = 0; handlerIndex < handlerCount_; ++handlerIndex) {
      if (handlers_[handlerIndex].subjectId != subjectId) continue;
      if (subjectName && handlers_[handlerIndex].subjectName &&
          strcmp(subjectName, handlers_[handlerIndex].subjectName) != 0)
        return ErrorCode::HashCollision;
      handlers_[handlerIndex].handler     = handler;
      handlers_[handlerIndex].userContext = userContext;
      if (subjectName) handlers_[handlerIndex].subjectName = subjectName;
      return ErrorCode::Ok;  // déjà abonné : l'opération est idempotente
    }
    if (handlerCount_ >= MPS_MAX_HANDLERS) return ErrorCode::HandlerTableFull;
    // Session ouverte, ou abonnement en cours dont la liste est déjà partie : SUB immédiat.
    const bool mustSubscribeNow =
        (sessionState_ == SessionState::Registered || sessionState_ == SessionState::Subscribing);
    if (mustSubscribeNow) {
      uint8_t payload[4];
      writeUint32LittleEndian(payload, subjectId);
      if (!enqueueFrame(MessageType::Subscribe, kFlagAcknowledgementRequested, subjectId, payload, 4))
        return ErrorCode::QueueFull;
      if (sessionState_ == SessionState::Subscribing) ++pendingSubscribeAcknowledgements_;
    }
    handlers_[handlerCount_++] = HandlerEntry{subjectId, subjectName, handler, userContext};
    if (mustSubscribeNow) transmitPendingFrames();
    return ErrorCode::Ok;
  }

  // ---- file d'envoi -------------------------------------------------------------------------

  /// Construit une trame et la place en fin de file. @return @c false si la file est pleine.
  bool enqueueFrame(MessageType messageType, uint8_t flags, uint32_t subjectId, const uint8_t* payload,
                    size_t payloadLength) {
    if (queueCount_ >= MPS_TRANSMIT_QUEUE_DEPTH) return false;
    QueuedFrame& frame = queue_[(queueHead_ + queueCount_) % MPS_TRANSMIT_QUEUE_DEPTH];
    // Le champ subjectId de la trame vaut 0 pour tout type autre que Publish (§4.1) ; pour SUB et
    // UNSUB, l'identifiant ne sert qu'à signaler un échec à l'application.
    const uint32_t subjectIdOnWire = (messageType == MessageType::Publish) ? subjectId : 0;
    frame.length = buildFrame(frame.bytes, sizeof frame.bytes, messageType, flags, ownSourceId_, subjectIdOnWire,
                              0, payload, payloadLength);
    if (frame.length == 0) return false;
    frame.hasSequenceNumber = false;
    ++queueCount_;
    return true;
  }

  /// Identifiant de subject à signaler en cas d'échec : celui d'un PUB, le premier d'un SUB/UNSUB.
  static uint32_t subjectIdOfQueuedFrame(const uint8_t* frameBytes, size_t frameLength) {
    const MessageType messageType = (MessageType)frameBytes[1];
    if (messageType == MessageType::Publish) return readUint32LittleEndian(frameBytes + 4);
    if ((messageType == MessageType::Subscribe || messageType == MessageType::Unsubscribe) &&
        frameLength >= kHeaderSize + 4)
      return readUint32LittleEndian(frameBytes + kHeaderSize);
    return 0;
  }

  /**
   * Cœur de l'émission, appelé après chaque événement. Dans l'ordre :
   * 1. les ACK en attente de transport ;
   * 2. la trame ACK_REQ en vol (renvoi, expiration) ;
   * 3. la prochaine trame de la file, seulement si plus aucune n'est en vol.
   */
  void transmitPendingFrames() {
    // 1. ACK : prioritaires et courts, ils ne passent pas par l'arrêt-et-attente.
    while (pendingAcknowledgementCount_ > 0) {
      PendingAcknowledgement& acknowledgement = pendingAcknowledgements_[pendingAcknowledgementHead_];
      if (!transport_.send(acknowledgement.bytes, sizeof acknowledgement.bytes)) break;
      pendingAcknowledgementHead_ = (pendingAcknowledgementHead_ + 1) % MPS_ACKNOWLEDGEMENT_QUEUE_DEPTH;
      --pendingAcknowledgementCount_;
    }
    // 2. Trame ACK_REQ en vol : un seul ACK_REQ par lien (§7.2).
    if (inFlightFrame_.isActive) {
      if (inFlightFrame_.needsResend) {
        if (!transport_.send(inFlightFrame_.bytes, inFlightFrame_.length)) return;
        inFlightFrame_.needsResend  = false;
        inFlightFrame_.lastSentAtMs = currentTimeMs_;
        return;
      }
      if ((uint32_t)(currentTimeMs_ - inFlightFrame_.lastSentAtMs) < configuration_.acknowledgementTimeoutMs) return;
      if (inFlightFrame_.retransmissionsDone >= configuration_.maxRetransmissions) {
        failInFlightFrame();
        return;
      }
      ++inFlightFrame_.retransmissionsDone;
      ++counters_.retransmissionCount;
      inFlightFrame_.bytes[2] |= kFlagDuplicate;  // même numéro de séquence, drapeau DUP
      if (transport_.send(inFlightFrame_.bytes, inFlightFrame_.length))
        inFlightFrame_.lastSentAtMs = currentTimeMs_;
      else
        inFlightFrame_.needsResend = true;
      return;
    }
    // 3. Prochaine trame de la file.
    while (!inFlightFrame_.isActive && queueCount_ > 0) {
      QueuedFrame& frame = queue_[queueHead_];
      if (!frame.hasSequenceNumber) {
        writeUint16LittleEndian(frame.bytes + 8, nextSequenceNumber_++);
        frame.hasSequenceNumber = true;
      }
      if (!transport_.send(frame.bytes, frame.length)) return;  // occupé : retenter au prochain poll()
      const bool needsAcknowledgement = (frame.bytes[2] & kFlagAcknowledgementRequested) != 0;
      if (needsAcknowledgement) {
        memcpy(inFlightFrame_.bytes, frame.bytes, frame.length);
        inFlightFrame_.length              = frame.length;
        inFlightFrame_.sequenceNumber      = readUint16LittleEndian(frame.bytes + 8);
        inFlightFrame_.messageType         = (MessageType)frame.bytes[1];
        inFlightFrame_.subjectId           = subjectIdOfQueuedFrame(frame.bytes, frame.length);
        inFlightFrame_.lastSentAtMs        = currentTimeMs_;
        inFlightFrame_.retransmissionsDone = 0;
        inFlightFrame_.needsResend         = false;
        inFlightFrame_.isActive            = true;
      }
      queueHead_ = (queueHead_ + 1) % MPS_TRANSMIT_QUEUE_DEPTH;
      --queueCount_;
    }
  }

  /// Met un ACK en file d'envoi ; le plus ancien est supprimé si la file est pleine.
  void queueAcknowledgement(uint16_t acknowledgedSequenceNumber, AcknowledgementStatus status) {
    if (pendingAcknowledgementCount_ >= MPS_ACKNOWLEDGEMENT_QUEUE_DEPTH) {
      pendingAcknowledgementHead_ = (pendingAcknowledgementHead_ + 1) % MPS_ACKNOWLEDGEMENT_QUEUE_DEPTH;
      --pendingAcknowledgementCount_;
    }
    PendingAcknowledgement& acknowledgement =
        pendingAcknowledgements_[(pendingAcknowledgementHead_ + pendingAcknowledgementCount_) %
                                 MPS_ACKNOWLEDGEMENT_QUEUE_DEPTH];
    const uint8_t statusByte = (uint8_t)status;
    buildFrame(acknowledgement.bytes, sizeof acknowledgement.bytes, MessageType::Acknowledge, 0, ownSourceId_, 0,
               acknowledgedSequenceNumber, &statusByte, 1);
    ++pendingAcknowledgementCount_;
  }

  // ---- session ------------------------------------------------------------------------------

  /// Efface tout l'état de session : files, trame en vol, identifiant, paramètres négociés.
  void resetSession() {
    queueHead_ = queueCount_ = 0;
    pendingAcknowledgementHead_ = pendingAcknowledgementCount_ = 0;
    inFlightFrame_.isActive          = false;
    keepalivePingPending_            = false;
    pendingSubscribeAcknowledgements_ = 0;
    ownSourceId_                     = helloSourceId_;
    keepaliveIntervalSeconds_        = 0;
    maxPayloadSize_                  = kMaxPayloadSize;
    hasGatewayIdentity_              = false;
  }

  /// Ouvre une session : HELLO de numéro de séquence 0 (§7.1), avec le nom d'appareil (§4.4).
  void sendHello() {
    resetSession();
    nextSequenceNumber_ = 0;
    uint8_t payload[2 + kMaxDeviceNameLength] = {0, 0};  // capacités (réservé, 0 en v1) puis nom
    if (deviceNameLength_) memcpy(payload + 2, configuration_.deviceName, deviceNameLength_);
    enqueueFrame(MessageType::Hello, kFlagAcknowledgementRequested, 0, payload, 2 + deviceNameLength_);
    sessionState_ = SessionState::HelloSent;
  }

  /**
   * Jitter de 0 à 250 ms ajouté au délai de HELLO. Il désynchronise les NŒUDS après un
   * redémarrage de la passerelle (§7.3) ; l'hôte, unique, ne l'applique pas : l'attendre ferait
   * seulement rater les @c sys_node_online des nœuds qui le devancent.
   */
  uint32_t backoffJitterMs() const {
    return (configuration_.role == Role::Node && configuration_.randomSource) ? (configuration_.randomSource() % 251u)
                                                                              : 0u;
  }

  /// Perd la session : vide les files et programme le prochain HELLO.
  void leaveSession(SessionLossReason reason) {
    const SessionState previousState = sessionState_;
    counters_.droppedFrameCount += (uint32_t)queueCount_;
    resetSession();
    sessionState_ = SessionState::Unregistered;
    if (reason == SessionLossReason::GatewayForgotSession || reason == SessionLossReason::GatewayRestarted) {
      // La passerelle a perdu la session : HELLO tout de suite, avec le seul jitter (§7.3).
      helloBackoffStep_ = 0;
      nextHelloTimeMs_  = currentTimeMs_ + backoffJitterMs();
    } else {
      // Échec ou refus : backoff croissant 1, 2, 4, 8 puis 10 s.
      static const uint16_t kHelloBackoffMs[] = {1000, 2000, 4000, 8000, 10000};
      const uint8_t stepIndex = helloBackoffStep_ < 4 ? helloBackoffStep_ : 4;
      nextHelloTimeMs_        = currentTimeMs_ + kHelloBackoffMs[stepIndex] + backoffJitterMs();
      if (helloBackoffStep_ < 255) ++helloBackoffStep_;
    }
    if ((previousState == SessionState::Registered || previousState == SessionState::Subscribing) && listener_)
      listener_->onUnregistered(reason);
  }

  /// La trame en vol n'a jamais été acquittée : tout envoi échoué fait perdre la session (§7.4).
  void failInFlightFrame() {
    const uint16_t sequenceNumber = inFlightFrame_.sequenceNumber;
    const uint32_t subjectId      = inFlightFrame_.subjectId;
    inFlightFrame_.isActive       = false;
    ++counters_.sendFailureCount;
    if (listener_)
      listener_->onSendFailed(sequenceNumber, subjectId, SendFailureReason::Timeout, AcknowledgementStatus::Ok);
    leaveSession(SessionLossReason::SendFailed);
  }

  /// Dernier ACK de SUB reçu : la session est complète.
  void completeRegistration() {
    sessionState_     = SessionState::Registered;
    helloBackoffStep_ = 0;
    if (listener_) listener_->onRegistered(ownSourceId_, maxPayloadSize_, keepaliveIntervalSeconds_);
  }

  /// Envoie les SUB de tous les abonnements, par paquets d'au plus 59 identifiants (§7.3).
  void beginSubscribing() {
    sessionState_                     = SessionState::Subscribing;
    pendingSubscribeAcknowledgements_ = 0;
    size_t handlerIndex = 0;
    while (handlerIndex < handlerCount_) {
      uint8_t payload[kMaxSubjectsPerFrame * 4];
      size_t  subjectCount = 0;
      while (handlerIndex < handlerCount_ && subjectCount < kMaxSubjectsPerFrame)
        writeUint32LittleEndian(payload + 4 * subjectCount++, handlers_[handlerIndex++].subjectId);
      if (!enqueueFrame(MessageType::Subscribe, kFlagAcknowledgementRequested, 0, payload, 4 * subjectCount)) {
        // Ne peut pas arriver tant que MPS_MAX_HANDLERS <= 59 * profondeur de la file.
        leaveSession(SessionLossReason::Rejected);
        return;
      }
      ++pendingSubscribeAcknowledgements_;
    }
    if (pendingSubscribeAcknowledgements_ == 0) completeRegistration();  // aucun abonnement à acquitter
  }

  // ---- réception ----------------------------------------------------------------------------

  /// Traite un ACK reçu : ouverture de session, fin d'arrêt-et-attente, erreurs.
  void handleAcknowledgement(const FrameView& view) {
    if (view.payloadLength < 1) return;
    const AcknowledgementStatus status = (AcknowledgementStatus)view.payload[0];

    // La passerelle ne connaît plus ce pair : elle répond NOT_REGISTERED même sans ACK_REQ, donc
    // le numéro de séquence peut ne correspondre à rien (§4.5). Refaire la session.
    if (status == AcknowledgementStatus::NotRegistered &&
        (sessionState_ == SessionState::Registered || sessionState_ == SessionState::Subscribing)) {
      leaveSession(SessionLossReason::GatewayForgotSession);
      return;
    }
    if (!inFlightFrame_.isActive || view.sequenceNumber != inFlightFrame_.sequenceNumber) return;  // ACK orphelin (§7.2)

    const MessageType acknowledgedType = inFlightFrame_.messageType;
    const uint16_t    sequenceNumber   = inFlightFrame_.sequenceNumber;
    const uint32_t    subjectId        = inFlightFrame_.subjectId;

    if (status == AcknowledgementStatus::Busy) {
      // Retransmission après le délai d'ACK, comptée comme une tentative.
      inFlightFrame_.lastSentAtMs = currentTimeMs_;
      return;
    }
    if (status != AcknowledgementStatus::Ok) {  // erreur définitive : pas de retransmission (§7.2)
      inFlightFrame_.isActive = false;
      if (acknowledgedType == MessageType::Ping) keepalivePingPending_ = false;
      if (listener_) listener_->onSendFailed(sequenceNumber, subjectId, SendFailureReason::Rejected, status);
      if (acknowledgedType == MessageType::Hello ||
          (acknowledgedType == MessageType::Subscribe && sessionState_ == SessionState::Subscribing))
        leaveSession(SessionLossReason::Rejected);
      return;
    }

    inFlightFrame_.isActive = false;
    lastAcknowledgedTimeMs_ = currentTimeMs_;
    switch (acknowledgedType) {
      case MessageType::Hello:
        if (view.payloadLength < kHelloAcknowledgementPayloadSize) {  // ACK OK sans informations de session : invalide
          if (listener_)
            listener_->onSendFailed(sequenceNumber, subjectId, SendFailureReason::Rejected,
                                    AcknowledgementStatus::BadFormat);
          leaveSession(SessionLossReason::Rejected);
          return;
        }
        // Payload : statut, node_id, max_payload (u16), keepalive_s (§4.4).
        ownSourceId_    = view.payload[1];
        maxPayloadSize_ = readUint16LittleEndian(view.payload + 2);
        if (maxPayloadSize_ > kMaxPayloadSize) maxPayloadSize_ = kMaxPayloadSize;  // min(238, max_payload) (§8.3)
        keepaliveIntervalSeconds_ = view.payload[4];
        if (view.payloadLength >= kHostHelloAcknowledgementPayloadSize) {  // identité de la passerelle (lien série)
          memcpy(gatewayMacAddress_, view.payload + 5, 6);
          gatewayWifiChannel_ = view.payload[11];
          hasGatewayIdentity_ = true;
        }
        duplicateWindow_.reset();  // l'ACK d'un HELLO réinitialise la fenêtre (§7.1)
        lastAcknowledgedTimeMs_ = currentTimeMs_;
        beginSubscribing();
        break;
      case MessageType::Subscribe:
        if (sessionState_ == SessionState::Subscribing && pendingSubscribeAcknowledgements_ > 0 &&
            --pendingSubscribeAcknowledgements_ == 0)
          completeRegistration();
        break;
      case MessageType::Ping: keepalivePingPending_ = false; break;
      default: break;
    }
  }

  /// Traite un message publié : doublons, ACK, remise au handler. Gère @c sys_gw_boot pour l'hôte.
  void handlePublish(const FrameView& view) {
    if (configuration_.role == Role::Host && view.subjectId == kSubjectIdGatewayBoot) {
      // La passerelle repart de la séquence 0 : le contrôle de doublon est contourné (§7.1), puis
      // l'hôte ouvre une nouvelle session HELLO + SUB (§7.5).
      duplicateWindow_.reset();
      dispatchToHandler(view);
      if (sessionState_ != SessionState::Unregistered && sessionState_ != SessionState::HelloSent)
        leaveSession(SessionLossReason::GatewayRestarted);
      return;
    }
    if (sessionState_ != SessionState::Subscribing && sessionState_ != SessionState::Registered) return;

    switch (duplicateWindow_.classify(view.sequenceNumber)) {
      case DuplicateWindow::Verdict::Duplicate:
        ++counters_.duplicateCount;
        // L'ACK précédent a pu se perdre : on le renvoie, sans livrer le message (§7.1).
        if (view.isAcknowledgementRequested()) queueAcknowledgement(view.sequenceNumber, AcknowledgementStatus::Ok);
        return;
      case DuplicateWindow::Verdict::TooOld: ++counters_.duplicateCount; return;
      case DuplicateWindow::Verdict::Accept: break;
    }
    // L'ACK est mis en file avant d'appeler le handler : un handler lent ne retarde pas l'acquittement.
    if (view.isAcknowledgementRequested()) queueAcknowledgement(view.sequenceNumber, AcknowledgementStatus::Ok);
    dispatchToHandler(view);
  }

  /// Appelle le handler du subject, s'il existe.
  void dispatchToHandler(const FrameView& view) {
    for (size_t handlerIndex = 0; handlerIndex < handlerCount_; ++handlerIndex)
      if (handlers_[handlerIndex].subjectId == view.subjectId) {
        if (handlers_[handlerIndex].handler)
          handlers_[handlerIndex].handler(view, handlers_[handlerIndex].userContext);
        return;
      }
  }

  // ---- données ------------------------------------------------------------------------------

  ITransport&          transport_;
  ClientConfiguration  configuration_;
  IClientListener*     listener_ = nullptr;

  uint8_t      helloSourceId_;                       ///< Identifiant utilisé dans HELLO : 0xFF (nœud) ou 0xFE (hôte).
  uint8_t      ownSourceId_;                         ///< Identifiant courant : celui attribué après HELLO.
  size_t       deviceNameLength_;                    ///< Longueur du nom d'appareil, bornée à 16.
  SessionState sessionState_ = SessionState::Unregistered;
  bool         hasStarted_   = false;                ///< Vrai après begin().
  uint16_t     maxPayloadSize_          = kMaxPayloadSize;
  uint8_t      keepaliveIntervalSeconds_ = 0;
  uint32_t     currentTimeMs_      = 0;              ///< Dernière horloge reçue.
  uint32_t     lastAcknowledgedTimeMs_ = 0;          ///< Dernier ACK OK reçu, pour le keepalive (§7.4).
  uint32_t     nextHelloTimeMs_    = 0;              ///< Instant du prochain HELLO quand non enregistré.
  uint8_t      helloBackoffStep_   = 0;              ///< Rang dans la suite 1, 2, 4, 8, 10 s.
  uint16_t     nextSequenceNumber_ = 0;              ///< Compteur d'émission du lien.
  bool         keepalivePingPending_ = false;        ///< Un PING est en file ou en vol.
  size_t       pendingSubscribeAcknowledgements_ = 0;///< SUB envoyés dont l'ACK est attendu.

  bool         hasGatewayIdentity_ = false;            ///< Identité de la passerelle reçue.
  uint8_t      gatewayMacAddress_[6] = {0};          ///< Adresse MAC de la passerelle (ACK du HELLO de l'hôte).
  uint8_t      gatewayWifiChannel_   = 0;            ///< Canal Wi-Fi de la passerelle (ACK du HELLO de l'hôte).

  DuplicateWindow duplicateWindow_;                  ///< Fenêtre de réception du lien.

  HandlerEntry handlers_[MPS_MAX_HANDLERS];
  size_t       handlerCount_ = 0;

  QueuedFrame   queue_[MPS_TRANSMIT_QUEUE_DEPTH];
  size_t        queueHead_  = 0;
  size_t        queueCount_ = 0;
  InFlightFrame inFlightFrame_;
  PendingAcknowledgement pendingAcknowledgements_[MPS_ACKNOWLEDGEMENT_QUEUE_DEPTH];
  size_t        pendingAcknowledgementHead_  = 0;
  size_t        pendingAcknowledgementCount_ = 0;

  ClientCounters counters_;
};

}  // namespace mps
