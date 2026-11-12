/**
 * @file MPSGateway.cpp
 * @brief Implémentation de l'adaptateur ESP32 de la passerelle MPS (ESP-NOW et port série).
 *
 * @details Les callbacks ESP-NOW s'exécutent dans la tâche Wi-Fi, de haute priorité : ils se limitent à
 * copier la trame dans une file ou à lever un drapeau (spec §11.3). Tout le traitement a lieu dans
 * MPSGatewayClass::loop().
 */
#include "MPSGateway.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

MPSGatewayClass MPSGateway;

namespace {

// ---- état partagé avec la tâche Wi-Fi ---------------------------------------------------------------

/// Une trame reçue, avec l'adresse de son émetteur, copiée hors du callback.
struct ReceivedFrameSlot {
  uint8_t  sourceMacAddress[6];
  uint16_t length;
  uint8_t  bytes[mps::kMaxFrameSize];
};

ReceivedFrameSlot receiveQueue[MPS_GATEWAY_RECEIVE_QUEUE_DEPTH];  ///< File circulaire tâche Wi-Fi vers loop().
volatile size_t   receiveQueueHead  = 0;
volatile size_t   receiveQueueCount = 0;
volatile uint32_t droppedFrameCounter   = 0;  ///< Trames perdues faute de place dans la file.
volatile uint32_t broadcastFrameCounter = 0;  ///< Trames de diffusion, que MPS n'utilise jamais.
portMUX_TYPE      receiveQueueLock      = portMUX_INITIALIZER_UNLOCKED;

volatile bool     isRadioSending            = false;  ///< Un envoi est en cours, son callback n'est pas revenu.
volatile uint32_t radioSendStartTimeMs      = 0;
volatile uint32_t radioSendFailureCounter   = 0;
volatile uint32_t radioSendSuccessCounter   = 0;

/// Un envoi dont le callback ne revient jamais ne doit pas bloquer la liaison (§4.6).
constexpr uint32_t kRadioSendTimeoutMs = 50;

/// Callback de réception ESP-NOW (signature d'Arduino-ESP32 3.x) : copie la trame dans la file.
void onEspNowReceive(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int dataLength) {
  // MPS n'utilise jamais la diffusion (§8) : on ignore ce qui n'est pas adressé à cette passerelle.
  static const uint8_t kBroadcastMacAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (memcmp(receiveInfo->des_addr, kBroadcastMacAddress, 6) == 0) {
    broadcastFrameCounter = broadcastFrameCounter + 1;
    return;
  }
  if (dataLength < (int)mps::kHeaderSize || dataLength > (int)mps::kMaxFrameSize) return;  // §4.5 étape 1
  portENTER_CRITICAL(&receiveQueueLock);
  if (receiveQueueCount >= MPS_GATEWAY_RECEIVE_QUEUE_DEPTH) {
    droppedFrameCounter = droppedFrameCounter + 1;
  } else {
    ReceivedFrameSlot& slot = receiveQueue[(receiveQueueHead + receiveQueueCount) % MPS_GATEWAY_RECEIVE_QUEUE_DEPTH];
    memcpy(slot.sourceMacAddress, receiveInfo->src_addr, 6);
    slot.length = (uint16_t)dataLength;
    memcpy(slot.bytes, data, (size_t)dataLength);
    receiveQueueCount = receiveQueueCount + 1;
  }
  portEXIT_CRITICAL(&receiveQueueLock);
}

/// Callback de fin d'envoi ESP-NOW : compte le résultat MAC et libère le canal radio.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onEspNowSent(const esp_now_send_info_t*, esp_now_send_status_t status) {
#else
void onEspNowSent(const uint8_t*, esp_now_send_status_t status) {
#endif
  if (status == ESP_NOW_SEND_SUCCESS)
    radioSendSuccessCounter = radioSendSuccessCounter + 1;
  else
    radioSendFailureCounter = radioSendFailureCounter + 1;
  isRadioSending = false;  // l'ACK applicatif et la retransmission décident du reste
}

/**
 * Lit l'adresse MAC Wi-Fi (station) de la carte, celle qu'utilise ESP-NOW.
 * WiFi.macAddress() passe par l'interface réseau, créée de façon asynchrone après WiFi.mode() : appelée trop
 * tôt (cas de l'ESP32-S3), elle échoue sans erreur et laisse 00:00:00:00:00:00. esp_wifi_get_mac() répond dès
 * que le Wi-Fi est initialisé.
 */
void readStationMacAddress(uint8_t macAddress[6]) {
  if (esp_wifi_get_mac(WIFI_IF_STA, macAddress) != ESP_OK) memset(macAddress, 0, 6);
}

/// Affiche des octets en hexadécimal séparés par des espaces.
void printHexadecimal(Print* output, const uint8_t* bytes, size_t length) {
  for (size_t byteIndex = 0; byteIndex < length; ++byteIndex) output->printf("%s%02X", byteIndex ? " " : "", bytes[byteIndex]);
}

}  // namespace

MPSGatewayClass::MPSGatewayClass() {}

void     MPSGatewayClass::readOwnMacAddress(uint8_t macAddress[6]) const { readStationMacAddress(macAddress); }
uint32_t MPSGatewayClass::radioSendFailureCount() const { return radioSendFailureCounter; }
uint32_t MPSGatewayClass::radioSendSuccessCount() const { return radioSendSuccessCounter; }

bool MPSGatewayClass::begin(Stream* hostSerialPort, uint8_t wifiChannel, const mps::GatewayConfiguration& configuration) {
  hostSerialPort_ = hostSerialPort;
  wifiChannel_    = wifiChannel;
  if (gatewayEngine_) return false;  // begin() ne s'appelle qu'une fois
  gatewayEngine_ = new mps::Gateway(*this, configuration);  // une seule fois au démarrage : pas de fragmentation

  // §9.4 : liaison série (déjà ouverte par l'appelant), puis radio (Wi-Fi avant esp_now_init, canal
  // fixé), puis profil d'admission.
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);  // récepteur toujours allumé (voir MPSNode::begin)
  WiFi.disconnect();              // une connexion à un routeur verrouillerait le canal (§11.3)
  if (esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onEspNowReceive);
  esp_now_register_send_cb(onEspNowSent);

  if (logOutput_) {
    uint8_t ownMacAddress[6];
    readStationMacAddress(ownMacAddress);
    logOutput_->printf("[MPSGW] MAC passerelle %02X:%02X:%02X:%02X:%02X:%02X canal %u\n", ownMacAddress[0],
                       ownMacAddress[1], ownMacAddress[2], ownMacAddress[3], ownMacAddress[4], ownMacAddress[5],
                       wifiChannel);
    logOutput_->printf("[MPSGW] profil d'admission : %s\n",
                       configuration.admissionProfile == mps::AdmissionProfile::Open ? "OPEN (tout MAC accepte)"
                                                                                      : "ALLOWLIST");
    if (configuration.admissionProfile == mps::AdmissionProfile::Allowlist)
      for (size_t entryIndex = 0; entryIndex < configuration.allowedMacAddressCount; ++entryIndex) {
        const uint8_t* allowed = configuration.allowedMacAddresses[entryIndex];
        logOutput_->printf("[MPSGW]   autorise : %02X:%02X:%02X:%02X:%02X:%02X\n", allowed[0], allowed[1], allowed[2],
                           allowed[3], allowed[4], allowed[5]);
      }
  }
  uint8_t gatewayMacAddress[6];
  readStationMacAddress(gatewayMacAddress);
  gatewayEngine_->setGatewayIdentity(gatewayMacAddress, wifiChannel);  // transmise à l'hôte, qui l'affiche
  gatewayEngine_->begin(millis());  // octet 0x00 seul puis sys_gw_boot vers l'hôte
  hasBegun_ = true;
  return true;
}

void MPSGatewayClass::loop() {
  if (!hasBegun_) return;
  const uint32_t currentTimeMs = millis();

  releaseTemporaryPeer();

  // Trames radio : copiées hors de la section critique, puis traitées.
  for (;;) {
    ReceivedFrameSlot frame;
    bool              hasFrame = false;
    portENTER_CRITICAL(&receiveQueueLock);
    if (receiveQueueCount > 0) {
      const ReceivedFrameSlot& slot = receiveQueue[receiveQueueHead];
      memcpy(frame.sourceMacAddress, slot.sourceMacAddress, 6);
      frame.length = slot.length;
      memcpy(frame.bytes, slot.bytes, slot.length);
      receiveQueueHead  = (receiveQueueHead + 1) % MPS_GATEWAY_RECEIVE_QUEUE_DEPTH;
      receiveQueueCount = receiveQueueCount - 1;
      hasFrame          = true;
    }
    portEXIT_CRITICAL(&receiveQueueLock);
    if (!hasFrame) break;
    gatewayEngine_->handleRadioFrame(frame.sourceMacAddress, frame.bytes, frame.length, currentTimeMs);
  }
  droppedReceivedFrameCount_ = droppedFrameCounter;
  foreignFrameCount_         = broadcastFrameCounter;

  // Octets série, par blocs bornés pour ne pas affamer la radio.
  if (hostSerialPort_) {
    for (int byteCount = 0; byteCount < 512 && hostSerialPort_->available(); ++byteCount) {
      const int receivedByte = hostSerialPort_->read();
      if (receivedByte < 0) break;
      gatewayEngine_->handleSerialByte((uint8_t)receivedByte, currentTimeMs);
    }
  }
  gatewayEngine_->poll(currentTimeMs);
}

// ---- mps::IGatewayPort ------------------------------------------------------------------------------

bool MPSGatewayClass::sendRadioFrame(const uint8_t macAddress[6], const uint8_t* frame, size_t frameLength) {
  // Un seul envoi radio en cours (§4.6) ; le moteur réessaie quand on répond « occupé ».
  if (isRadioSending && (uint32_t)(millis() - radioSendStartTimeMs) < kRadioSendTimeoutMs) return false;
  if (isTemporaryPeerPending_) return false;  // un pair temporaire attend d'être retiré

  // ESP-NOW refuse d'envoyer vers un pair inconnu : la réponse à une adresse MAC non enregistrée
  // (NOT_REGISTERED, FORBIDDEN, TABLE_FULL...) passe par un pair temporaire.
  if (!esp_now_is_peer_exist(macAddress)) {
    esp_now_peer_info_t temporaryPeer = {};
    memcpy(temporaryPeer.peer_addr, macAddress, 6);
    temporaryPeer.channel = wifiChannel_;
    temporaryPeer.ifidx   = WIFI_IF_STA;
    temporaryPeer.encrypt = false;
    if (esp_now_add_peer(&temporaryPeer) != ESP_OK) return true;  // table pleine : réponse abandonnée
    memcpy(temporaryPeerMacAddress_, macAddress, 6);
    isTemporaryPeerPending_ = true;
  }
  const esp_err_t result = esp_now_send(macAddress, frame, frameLength);
  if (result != ESP_OK) {
    releaseTemporaryPeer();
    return result == ESP_ERR_ESPNOW_NO_MEM ? false : true;  // NO_MEM : court délai avant de réessayer (§4.6)
  }
  radioSendStartTimeMs = millis();
  isRadioSending       = true;
  return true;
}

void MPSGatewayClass::releaseTemporaryPeer() {
  // Retiré dès que le callback d'envoi est revenu, pour ne pas occuper l'un des 20 pairs.
  if (isTemporaryPeerPending_ && (!isRadioSending || (uint32_t)(millis() - radioSendStartTimeMs) >= kRadioSendTimeoutMs)) {
    esp_now_del_peer(temporaryPeerMacAddress_);
    isTemporaryPeerPending_ = false;
  }
}

bool MPSGatewayClass::addRadioPeer(const uint8_t macAddress[6]) {
  if (isTemporaryPeerPending_ && memcmp(temporaryPeerMacAddress_, macAddress, 6) == 0) {
    isTemporaryPeerPending_ = false;  // déjà ajouté temporairement : on le conserve
    return true;
  }
  if (esp_now_is_peer_exist(macAddress)) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, macAddress, 6);
  peer.channel = wifiChannel_;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;  // v1 : rien n'est chiffré (§8)
  return esp_now_add_peer(&peer) == ESP_OK;
}

void MPSGatewayClass::removeRadioPeer(const uint8_t macAddress[6]) { esp_now_del_peer(macAddress); }

bool MPSGatewayClass::writeSerialBytes(const uint8_t* data, size_t length) {
  if (!hostSerialPort_) return true;  // pas d'hôte (console texte) : rien à écrire, rien à compter
  // Tout ou rien, sans jamais bloquer : un hôte absent remplit le tampon USB (§11.5).
  if ((size_t)hostSerialPort_->availableForWrite() < length) return false;
  return hostSerialPort_->write(data, length) == length;
}

void MPSGatewayClass::onNodeOnline(uint8_t nodeId, const uint8_t macAddress[6], const char* deviceName,
                                   size_t deviceNameLength) {
  if (!logOutput_) return;
  logOutput_->printf("[MPSGW] + noeud %u en ligne : %.*s  (%02X:%02X:%02X:%02X:%02X:%02X)\n", nodeId,
                     (int)deviceNameLength, deviceName, macAddress[0], macAddress[1], macAddress[2], macAddress[3],
                     macAddress[4], macAddress[5]);
}

void MPSGatewayClass::onNodeOffline(uint8_t nodeId, mps::OfflineReason reason) {
  if (!logOutput_) return;
  logOutput_->printf("[MPSGW] - noeud %u hors ligne (%s)\n", nodeId,
                     reason == mps::OfflineReason::KeepaliveExpired ? "keepalive expire" : "nouveau HELLO");
}

void MPSGatewayClass::onPublishObserved(uint8_t sourceId, uint32_t subjectId, const uint8_t* payload,
                                        size_t payloadLength) {
  if (!logOutput_ || !isPublicationLoggingEnabled_) return;
  const char* subjectName = findSubjectName(subjectId);
  if (subjectName)
    logOutput_->printf("[MPSGW] PUB de %u : %s (%u o) ", sourceId, subjectName, (unsigned)payloadLength);
  else
    logOutput_->printf("[MPSGW] PUB de %u : 0x%08lX (%u o) ", sourceId, (unsigned long)subjectId,
                       (unsigned)payloadLength);
  printHexadecimal(logOutput_, payload, payloadLength);
  logOutput_->println();
}

void MPSGatewayClass::registerSubjectName(const char* subjectName) {
  if (!mps::isValidSubjectName(subjectName)) return;
  const uint32_t subjectId = mps::fnv1aHash(subjectName);
  for (size_t nameIndex = 0; nameIndex < knownSubjectNameCount_; ++nameIndex)
    if (knownSubjectNames_[nameIndex].subjectId == subjectId) return;  // déjà connu
  if (knownSubjectNameCount_ >= MPS_GATEWAY_KNOWN_SUBJECT_NAME_CAPACITY) return;
  KnownSubjectName& entry = knownSubjectNames_[knownSubjectNameCount_];
  entry.subjectId         = subjectId;
  strncpy(entry.name, subjectName, sizeof entry.name - 1);
  entry.name[sizeof entry.name - 1] = '\0';
  ++knownSubjectNameCount_;
}

const char* MPSGatewayClass::findSubjectName(uint32_t subjectId) const {
  for (size_t nameIndex = 0; nameIndex < knownSubjectNameCount_; ++nameIndex)
    if (knownSubjectNames_[nameIndex].subjectId == subjectId) return knownSubjectNames_[nameIndex].name;
  return nullptr;
}

#endif  // ARDUINO_ARCH_ESP32
