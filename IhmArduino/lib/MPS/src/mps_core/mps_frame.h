/**
 * @file mps_frame.h
 * @brief Construction et analyse des trames MPS (§4) et cadrage série CRC-16 + COBS + 0x00 (§5).
 *
 * @details Aucune allocation dynamique : l'appelant fournit tous les tampons.
 *
 * @par Chaîne d'émission série
 * Trame MPS (en-tête + payload) → CRC-16 little-endian ajouté → encodage COBS → octet 0x00
 * délimiteur. La réception suit le chemin inverse, voir SerialReceiver.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mps_cobs.h"
#include "mps_crc16.h"
#include "mps_endian.h"
#include "mps_protocol.h"

namespace mps {

/**
 * @brief Écrit une trame MPS complète : en-tête de 12 octets puis payload.
 *
 * @param[out] outputBuffer   Tampon de destination.
 * @param[in]  outputCapacity Capacité de @p outputBuffer.
 * @param[in]  messageType    Type de message.
 * @param[in]  flags          Drapeaux (@c kFlagAcknowledgementRequested, @c kFlagDuplicate).
 * @param[in]  sourceId       Identifiant de l'émetteur d'origine.
 * @param[in]  subjectId      Identifiant de subject (0 pour tout type autre que Publish).
 * @param[in]  sequenceNumber Numéro de séquence du lien.
 * @param[in]  payload        Octets du payload (peut être nul si @p payloadLength vaut 0).
 * @param[in]  payloadLength  Longueur du payload.
 * @return La taille de la trame écrite, ou 0 si le payload dépasse @c kMaxPayloadSize ou si le
 *         tampon est trop petit.
 */
inline size_t buildFrame(uint8_t* outputBuffer, size_t outputCapacity, MessageType messageType, uint8_t flags,
                         uint8_t sourceId, uint32_t subjectId, uint16_t sequenceNumber,
                         const uint8_t* payload, size_t payloadLength) {
  if (payloadLength > kMaxPayloadSize || outputCapacity < kHeaderSize + payloadLength) return 0;
  outputBuffer[0] = kProtocolVersion;
  outputBuffer[1] = (uint8_t)messageType;
  outputBuffer[2] = flags;
  outputBuffer[3] = sourceId;
  writeUint32LittleEndian(outputBuffer + 4, subjectId);
  writeUint16LittleEndian(outputBuffer + 8, sequenceNumber);
  writeUint16LittleEndian(outputBuffer + 10, (uint16_t)payloadLength);
  if (payloadLength) memcpy(outputBuffer + kHeaderSize, payload, payloadLength);
  return kHeaderSize + payloadLength;
}

/**
 * @brief Vue décodée d'une trame reçue, sans copie des octets.
 * @warning @c payload pointe dans le tampon d'origine : il n'est valable que tant que ce tampon
 *          l'est (pendant l'appel d'un handler, §11.3).
 */
struct FrameView {
  uint8_t        version;         ///< Version du protocole annoncée par l'émetteur.
  uint8_t        type;            ///< Type de message brut (voir MessageType).
  uint8_t        flags;           ///< Drapeaux bruts.
  uint8_t        sourceId;        ///< Émetteur d'origine.
  uint32_t       subjectId;       ///< Identifiant de subject.
  uint16_t       sequenceNumber;  ///< Numéro de séquence du lien.
  uint16_t       payloadLength;   ///< Longueur du payload, déjà vérifiée par parseFrame().
  const uint8_t* payload;         ///< Premier octet du payload.

  /// @return @c true si l'émetteur demande un ACK (ACK_REQ).
  bool isAcknowledgementRequested() const { return (flags & kFlagAcknowledgementRequested) != 0; }
  /// @return @c true si la trame est une retransmission (DUP).
  bool isDuplicate() const { return (flags & kFlagDuplicate) != 0; }
};

/**
 * @brief Lit un payload métier de taille fixe (§6.4).
 *
 * @details À utiliser dans tous les handlers : la longueur est vérifiée avant toute lecture, et la
 * copie passe par @c memcpy, donc sans accès non aligné (§11.2).
 *
 * @tparam PayloadType Structure triviale (@c packed) décrivant le payload.
 * @param[in]  message Trame reçue.
 * @param[out] output  Reçoit la structure décodée, seulement si la taille correspond.
 * @return @c true si @c message.payloadLength vaut exactement @c sizeof(PayloadType).
 */
template <typename PayloadType>
inline bool readPayloadAs(const FrameView& message, PayloadType* output) {
  static_assert(__is_trivially_copyable(PayloadType), "le payload doit etre un type trivial");
  if (message.payloadLength != sizeof(PayloadType)) return false;
  memcpy(output, message.payload, sizeof(PayloadType));
  return true;
}

/// Résultat de parseFrame().
enum class ParseResult : uint8_t {
  Ok,              ///< Trame cohérente.
  InvalidSize,     ///< Taille hors de 12 à 250 octets (§4.5, étape 1).
  LengthMismatch,  ///< @c payloadLength différent de (taille reçue − 12) (§4.5, étape 2).
};

/**
 * @brief Analyse l'en-tête d'une trame reçue : étapes 1 et 2 de la validation (§4.5).
 *
 * @details Ne contrôle ni la version, ni le type, ni l'origine (étapes 3 à 5) : le récepteur a
 * besoin de la vue pour répondre (ACK BAD_FORMAT, BAD_VERSION...). La vue est renseignée même en
 * cas de LengthMismatch, pas en cas d'InvalidSize.
 *
 * @param[in]  frameData   Octets reçus.
 * @param[in]  frameLength Nombre d'octets reçus.
 * @param[out] view        Reçoit la vue décodée.
 * @return Ok, ou la première incohérence détectée.
 */
inline ParseResult parseFrame(const uint8_t* frameData, size_t frameLength, FrameView* view) {
  if (frameLength < kHeaderSize || frameLength > kMaxFrameSize) return ParseResult::InvalidSize;
  view->version        = frameData[0];
  view->type           = frameData[1];
  view->flags          = frameData[2];
  view->sourceId       = frameData[3];
  view->subjectId      = readUint32LittleEndian(frameData + 4);
  view->sequenceNumber = readUint16LittleEndian(frameData + 8);
  view->payloadLength  = readUint16LittleEndian(frameData + 10);
  view->payload        = frameData + kHeaderSize;
  if (view->payloadLength != frameLength - kHeaderSize) return ParseResult::LengthMismatch;
  return ParseResult::Ok;
}

/**
 * @brief Encode une trame MPS pour la liaison série : CRC-16, COBS, puis délimiteur 0x00 (§5).
 *
 * @param[in]  frame          Trame MPS (en-tête + payload).
 * @param[in]  frameLength    Taille de la trame.
 * @param[out] outputBuffer   Reçoit les octets à écrire sur le port série, délimiteur compris.
 * @param[in]  outputCapacity Capacité de @p outputBuffer (@c kMaxSerialEncodedSize suffit).
 * @return Le nombre d'octets écrits, ou 0 en cas d'erreur (trame trop grande ou tampon insuffisant).
 */
inline size_t encodeSerialFrame(const uint8_t* frame, size_t frameLength, uint8_t* outputBuffer,
                                size_t outputCapacity) {
  if (frameLength < kHeaderSize || frameLength > kMaxFrameSize) return 0;
  uint8_t frameWithCrc[kMaxSerialUnencodedSize];
  memcpy(frameWithCrc, frame, frameLength);
  writeUint16LittleEndian(frameWithCrc + frameLength, computeCrc16(frame, frameLength));  // CRC en fin de trame
  const size_t withCrcLength = frameLength + kCrc16Size;
  if (outputCapacity < cobsMaxEncodedSize(withCrcLength) + 1) return 0;
  const size_t encodedLength = cobsEncode(frameWithCrc, withCrcLength, outputBuffer, outputCapacity);
  if (encodedLength == 0) return 0;
  outputBuffer[encodedLength] = 0x00;  // délimiteur de trame
  return encodedLength + 1;
}

/// Résultat de decodeSerialBlock().
enum class SerialDecodeResult : uint8_t {
  Ok,          ///< Trame valide décodée.
  EmptyBlock,  ///< Bloc vide (deux 0x00 consécutifs) : ignoré sans erreur.
  CobsError,   ///< COBS invalide (compteur @c rx_cobs_err).
  TooShort,    ///< Moins de 14 octets décodés (en-tête + CRC).
  CrcError,    ///< CRC incorrect (compteur @c rx_crc_err).
};

/**
 * @brief Décode un bloc reçu entre deux délimiteurs 0x00 (§5.4), en place.
 *
 * @param[in,out] block        Bloc reçu sans le 0x00 final ; son contenu est modifié par le décodage.
 * @param[in]     blockLength  Nombre d'octets du bloc.
 * @param[out]    frame        En cas de succès, pointe vers la trame MPS (CRC retiré) dans @p block.
 * @param[out]    frameLength  En cas de succès, taille de la trame MPS.
 * @return Ok, ou la cause du rejet.
 */
inline SerialDecodeResult decodeSerialBlock(uint8_t* block, size_t blockLength, const uint8_t** frame,
                                            size_t* frameLength) {
  if (blockLength == 0) return SerialDecodeResult::EmptyBlock;
  const int decodedLength = cobsDecode(block, blockLength, block, blockLength);
  if (decodedLength < 0) return SerialDecodeResult::CobsError;
  if ((size_t)decodedLength < kHeaderSize + kCrc16Size) return SerialDecodeResult::TooShort;
  const size_t frameLengthWithoutCrc = (size_t)decodedLength - kCrc16Size;
  const uint16_t expectedCrc = readUint16LittleEndian(block + frameLengthWithoutCrc);
  if (computeCrc16(block, frameLengthWithoutCrc) != expectedCrc) return SerialDecodeResult::CrcError;
  *frame       = block;
  *frameLength = frameLengthWithoutCrc;
  return SerialDecodeResult::Ok;
}

/**
 * @brief Compteurs de diagnostic du récepteur série (§9.5).
 */
struct SerialReceiveCounters {
  uint32_t cobsErrorCount      = 0;  ///< Blocs rejetés pour COBS invalide (@c rx_cobs_err).
  uint32_t crcErrorCount       = 0;  ///< Blocs rejetés pour CRC incorrect (@c rx_crc_err).
  uint32_t tooShortBlockCount  = 0;  ///< Blocs trop courts pour contenir un en-tête et un CRC.
  uint32_t bufferOverflowCount = 0;  ///< Tampon plein avant d'avoir vu un 0x00.
};

/**
 * @brief Récepteur de flux série : accumule les octets jusqu'à 0x00 puis décode (§5.4).
 *
 * @details Le flux n'a pas d'autre frontière que le délimiteur 0x00, que COBS garantit absent des
 * données. Un récepteur qui démarre au milieu d'une trame obtient donc un bloc partiel qui échoue
 * au CRC ; la trame suivante est lue correctement (resynchronisation garantie). Si le tampon
 * déborde avant d'avoir vu un 0x00, il est vidé et les octets suivants sont ignorés jusqu'au
 * prochain 0x00.
 */
class SerialReceiver {
 public:
  /**
   * @brief Traite un octet reçu.
   * @param receivedByte Octet lu sur le port série.
   * @return @c true quand une trame valide est complète : completedFrame() et
   *         completedFrameLength() restent valables jusqu'au prochain appel.
   */
  bool pushByte(uint8_t receivedByte) {
    if (receivedByte != 0x00) {
      if (isDiscarding_) return false;
      if (blockLength_ >= sizeof(blockBuffer_)) {  // débordement : on jette jusqu'au prochain 0x00
        blockLength_ = 0;
        isDiscarding_ = true;
        ++counters_.bufferOverflowCount;
        return false;
      }
      blockBuffer_[blockLength_++] = receivedByte;
      return false;
    }
    // Octet 0x00 : fin du bloc accumulé.
    const bool wasDiscarding = isDiscarding_;
    const size_t completedBlockLength = blockLength_;
    isDiscarding_ = false;
    blockLength_  = 0;
    if (wasDiscarding) return false;
    switch (decodeSerialBlock(blockBuffer_, completedBlockLength, &completedFrame_, &completedFrameLength_)) {
      case SerialDecodeResult::Ok:         return true;
      case SerialDecodeResult::EmptyBlock: return false;
      case SerialDecodeResult::CobsError:  ++counters_.cobsErrorCount; return false;
      case SerialDecodeResult::TooShort:   ++counters_.tooShortBlockCount; return false;
      case SerialDecodeResult::CrcError:   ++counters_.crcErrorCount; return false;
    }
    return false;
  }

  /// @return La dernière trame valide (CRC retiré), valable jusqu'au prochain pushByte().
  const uint8_t* completedFrame() const { return completedFrame_; }
  /// @return La taille de la dernière trame valide.
  size_t completedFrameLength() const { return completedFrameLength_; }
  /// @return Les compteurs de diagnostic.
  const SerialReceiveCounters& counters() const { return counters_; }

 private:
  uint8_t        blockBuffer_[kSerialReceiveBufferSize];  ///< Octets du bloc en cours d'accumulation.
  size_t         blockLength_          = 0;               ///< Nombre d'octets accumulés.
  bool           isDiscarding_         = false;           ///< Vrai après un débordement, jusqu'au prochain 0x00.
  const uint8_t* completedFrame_       = nullptr;         ///< Dernière trame valide (dans blockBuffer_).
  size_t         completedFrameLength_ = 0;               ///< Taille de la dernière trame valide.
  SerialReceiveCounters counters_;
};

}  // namespace mps
