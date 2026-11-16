/**
 * @file mps_protocol.h
 * @brief Constantes, types de messages et en-tête de trame du protocole MPS v1 (Mini Pub/Sub).
 *
 * @details Ce fichier est la transcription de la spécification MPS v1 : les numéros de paragraphe
 * cités dans les commentaires (§x.y) renvoient à ce document. Il ne dépend d'aucune plateforme :
 * C++14, aucun en-tête Arduino, Godot ou système.
 *
 * @par Vocabulaire
 * - **Nœud** : ESP32 applicatif (capteur, actionneur), relié à la passerelle par radio ESP-NOW.
 * - **Passerelle** : ESP32 central qui joue le rôle de broker (routage des messages).
 * - **Hôte** : programme sur le SBC Linux (GDExtension Godot), relié à la passerelle par liaison série.
 * - **Subject** : nom d'un canal de messages ; seul son identifiant (@c subjectId) circule sur le fil.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @namespace mps
 * @brief Tout le code du protocole MPS.
 */
namespace mps {

/// @name Dimensions des trames (§4, §5, §12.1)
/// @{
constexpr uint8_t kProtocolVersion = 1;                                   ///< Valeur du champ @c version (§4.1).
constexpr size_t  kHeaderSize      = 12;                                  ///< Taille de l'en-tête, sans bourrage (§4.1).
constexpr size_t  kMaxPayloadSize  = 238;                                 ///< @c MPS_MAX_PAYLOAD : tient dans ESP-NOW v1 comme v2 (§4).
constexpr size_t  kMaxFrameSize    = kHeaderSize + kMaxPayloadSize;       ///< Trame radio maximale : 250 octets.
constexpr size_t  kCrc16Size       = 2;                                   ///< Taille du CRC-16 ajouté aux trames série (§5.2).
constexpr size_t  kMaxSerialUnencodedSize = kMaxFrameSize + kCrc16Size;   ///< Trame série avant COBS : 252 octets.
constexpr size_t  kMaxSerialEncodedSize   = kMaxSerialUnencodedSize + 2;  ///< Après COBS et délimiteur : 254 octets (§5.1).
constexpr size_t  kSerialReceiveBufferSize = 260;                         ///< Tampon de réception série recommandé (§5.1).
constexpr size_t  kMaxSubjectNameLength  = 64;                            ///< Longueur maximale d'un nom de subject (§3.2).
constexpr size_t  kMaxDeviceNameLength   = 16;                            ///< Longueur maximale du nom d'appareil dans HELLO (§4.4).
constexpr size_t  kMaxSubjectsPerFrame   = 59;                            ///< Entrées maximales d'un SUB ou UNSUB (§4.4).
/// @}

/// @name Identifiants de subject réservés (§3.3)
/// @{
constexpr uint32_t kReservedSubjectIdZero = 0x00000000u;  ///< Valeur interdite : tous les bits à 0.
constexpr uint32_t kReservedSubjectIdMax  = 0xFFFFFFFFu;  ///< Valeur interdite : tous les bits à 1.
/// @}

/// @name Identifiants d'émetteur @c sourceId (§2.3)
/// @{
constexpr uint8_t kSourceIdGateway    = 0x00;  ///< La passerelle.
constexpr uint8_t kSourceIdFirstNode  = 0x01;  ///< Premier identifiant attribuable à un nœud.
constexpr uint8_t kSourceIdLastNode   = 0xEF;  ///< Dernier identifiant attribuable à un nœud.
constexpr uint8_t kSourceIdHost       = 0xFE;  ///< L'hôte SBC, sur le lien série.
constexpr uint8_t kSourceIdUnassigned = 0xFF;  ///< Nœud sans identifiant (HELLO uniquement).
/// @}

/**
 * @brief Types de messages (champ @c type de l'en-tête, §4.2).
 * @note Les types 0x07 à 0x7F sont réservés au protocole ; 0x80 à 0xFF sont expérimentaux et
 *       ignorés par une implémentation qui ne les connaît pas.
 */
enum class MessageType : uint8_t {
  Publish     = 0x01,  ///< PUB : publication sur un subject.
  Subscribe   = 0x02,  ///< SUB : abonnement à une liste de subjects.
  Unsubscribe = 0x03,  ///< UNSUB : désabonnement d'une liste de subjects.
  Acknowledge = 0x04,  ///< ACK : acquittement applicatif d'une trame.
  Hello       = 0x05,  ///< HELLO : ouverture de session d'un nœud ou de l'hôte.
  Ping        = 0x06,  ///< PING : keepalive.
};

/**
 * @brief Indique si une valeur du champ @c type correspond à un type de message défini par la v1.
 * @param rawType Valeur brute lue dans l'en-tête.
 * @return @c true pour les types 0x01 à 0x06.
 */
constexpr bool isKnownMessageType(uint8_t rawType) { return rawType >= 0x01 && rawType <= 0x06; }

/// @name Drapeaux (champ @c flags, §4.3)
/// @{
constexpr uint8_t kFlagAcknowledgementRequested = 0x01;  ///< ACK_REQ : le récepteur DOIT répondre par un ACK.
constexpr uint8_t kFlagDuplicate                = 0x02;  ///< DUP : posé sur chaque retransmission (informatif).
/// @}

/**
 * @brief Statut transporté par un ACK (§4.4).
 */
enum class AcknowledgementStatus : uint8_t {
  Ok            = 0x00,  ///< Trame acceptée.
  BadFormat     = 0x01,  ///< Longueur incohérente ou payload invalide pour le type.
  BadVersion    = 0x02,  ///< Version non supportée (réponse à un HELLO uniquement).
  UnknownType   = 0x03,  ///< Type de message inconnu.
  NotRegistered = 0x04,  ///< L'émetteur doit d'abord envoyer HELLO.
  TableFull     = 0x05,  ///< Plus de place : pairs radio, abonnements du nœud ou file de la passerelle.
  Busy          = 0x06,  ///< Passerelle momentanément saturée : réessayer plus tard.
  Forbidden     = 0x07,  ///< Action interdite (par exemple PUB d'un subject @c sys_ ou nœud hors liste).
};

/// @name Payload de l'ACK d'un HELLO accepté (§4.4)
/// @{
constexpr size_t kHelloAcknowledgementPayloadSize = 5;  ///< statut, node_id, max_payload u16, keepalive_s.
/// Sur le lien série, l'ACK du HELLO de l'hôte ajoute l'adresse MAC de la passerelle (6 octets) et son canal
/// Wi-Fi (1 octet), que l'IHM affiche pour configurer les nœuds. Ajout en fin de payload : compatible (§8.3).
constexpr size_t kHostHelloAcknowledgementPayloadSize = kHelloAcknowledgementPayloadSize + 7;
/// @}

/// @name Valeurs par défaut de fiabilité (§7.6)
/// @{
constexpr uint32_t kAcknowledgementTimeoutMs   = 100;  ///< Attente d'un ACK avant retransmission (ne pas descendre sous 30 ms).
constexpr uint8_t  kMaxRetransmissions         = 3;    ///< Retransmissions après la première émission (4 émissions au plus).
constexpr uint8_t  kKeepaliveIntervalSeconds   = 5;    ///< Intervalle de keepalive imposé par la passerelle.
constexpr uint8_t  kDuplicateWindowSize        = 32;   ///< Taille de la fenêtre de détection des doublons.
/// @}

#pragma pack(push, 1)

/**
 * @brief En-tête commun à toutes les trames, radio et série (12 octets, little-endian, §4.1).
 *
 * @details Ordre des octets sur le fil. La structure est @c packed et son nombre d'octets est
 * vérifié à la compilation ; elle sert à documenter le format, l'encodage et le décodage passent
 * par readUint16LittleEndian() et consorts pour rester indépendants du boutisme de la cible.
 */
struct FrameHeader {
  uint8_t  version;         ///< Version majeure du protocole (@c ver dans la spec) : 1.
  uint8_t  type;            ///< Type de message, voir MessageType.
  uint8_t  flags;           ///< Drapeaux, voir @c kFlagAcknowledgementRequested et @c kFlagDuplicate.
  uint8_t  sourceId;        ///< Émetteur d'origine (@c src dans la spec), voir @c kSourceId*.
  uint32_t subjectId;       ///< Identifiant de subject (0 pour tout type autre que Publish).
  uint16_t sequenceNumber;  ///< Numéro de séquence du lien (@c seq) ; dans un ACK, celui de la trame acquittée.
  uint16_t payloadLength;   ///< Longueur du payload en octets (@c len) : taille reçue moins 12.
};

/**
 * @brief Payload d'un heartbeat métier (§6.2), 7 octets.
 * @note Le heartbeat prouve que le code métier tourne ; il est publié par ce code, jamais
 *       automatiquement par la bibliothèque.
 */
struct HeartbeatPayload {
  uint8_t  healthState;        ///< 0 = OK, 1 = dégradé, 2 = défaut.
  uint16_t heartbeatCounter;   ///< Incrémenté à chaque heartbeat, repasse à 0 après 65 535.
  uint32_t uptimeMilliseconds; ///< Durée depuis le démarrage du nœud (déborde après environ 49,7 jours).
};

/**
 * @brief Payload partagé par @c set_*_led_state et @c status_*_led_state (§6.3), 5 octets.
 * @details Une seule structure pour l'état désiré et l'état observé : le handler compare
 * directement l'un à l'autre.
 */
struct LedPayload {
  uint8_t  ledState;  ///< 0 = éteinte, 1 = allumée.
  uint32_t rgbColor;  ///< Couleur 0x00RRGGBB ; l'octet de poids fort vaut 0.
};

#pragma pack(pop)

static_assert(sizeof(FrameHeader) == kHeaderSize, "L'en-tete MPS doit faire exactement 12 octets");
static_assert(sizeof(HeartbeatPayload) == 7, "HeartbeatPayload doit faire 7 octets");
static_assert(sizeof(LedPayload) == 5, "LedPayload doit faire 5 octets");

}  // namespace mps
