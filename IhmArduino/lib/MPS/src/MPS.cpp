/**
 * @file MPS.cpp
 * @brief Implémentation de MPSNode : ESP-NOW côté nœud (Arduino-ESP32 3.x).
 *
 * @details Les callbacks ESP-NOW s'exécutent dans la tâche Wi-Fi, de haute priorité : ils se limitent à
 * copier la trame dans une file ou à lever un drapeau (spec §11.3). Tout le traitement a lieu dans
 * MPSNode::loop().
 */
#include "MPS.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

MPSNode MPS;

namespace {

// ---- état partagé avec la tâche Wi-Fi ---------------------------------------------------------------

/// Une trame reçue, copiée hors du callback (ses pointeurs ne survivent pas à l'appel).
struct ReceivedFrameSlot {
  uint16_t length;
  uint8_t  bytes[mps::kMaxFrameSize];
};

ReceivedFrameSlot receiveQueue[MPS_RECEIVE_QUEUE_DEPTH];  ///< File circulaire tâche Wi-Fi vers loop().
volatile size_t   receiveQueueHead  = 0;
volatile size_t   receiveQueueCount = 0;
volatile uint32_t droppedFrameCounter = 0;  ///< Trames perdues faute de place dans la file.
volatile uint32_t foreignFrameCounter = 0;  ///< Trames d'une autre adresse MAC que la passerelle.
portMUX_TYPE      receiveQueueLock    = portMUX_INITIALIZER_UNLOCKED;

uint8_t           gatewayPeerMacAddress[6] = {0};
volatile bool     isRadioSending        = false;  ///< Un envoi est en cours, son callback n'est pas revenu.
volatile uint32_t radioSendStartTimeMs  = 0;
volatile uint32_t radioSendFailureCounter = 0;

/// Un envoi dont le callback ne revient jamais ne doit pas bloquer la liaison (§4.6).
constexpr uint32_t kRadioSendTimeoutMs = 50;

/// Copie une trame reçue dans la file, après avoir vérifié l'origine (§2.2) et la taille (§4.5 étape 1).
void copyReceivedFrameToQueue(const uint8_t* sourceMacAddress, const uint8_t* data, int dataLength) {
  if (memcmp(sourceMacAddress, gatewayPeerMacAddress, 6) != 0) {  // un nœud ignore toute trame d'une autre MAC
    foreignFrameCounter = foreignFrameCounter + 1;
    return;
  }
  if (dataLength < (int)mps::kHeaderSize || dataLength > (int)mps::kMaxFrameSize) return;
  portENTER_CRITICAL(&receiveQueueLock);
  if (receiveQueueCount >= MPS_RECEIVE_QUEUE_DEPTH) {
    droppedFrameCounter = droppedFrameCounter + 1;
  } else {
    ReceivedFrameSlot& slot = receiveQueue[(receiveQueueHead + receiveQueueCount) % MPS_RECEIVE_QUEUE_DEPTH];
    slot.length             = (uint16_t)dataLength;
    memcpy(slot.bytes, data, (size_t)dataLength);
    receiveQueueCount = receiveQueueCount + 1;
  }
  portEXIT_CRITICAL(&receiveQueueLock);
}

/// Callback de réception ESP-NOW (signature d'Arduino-ESP32 3.x).
void onEspNowReceive(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int dataLength) {
  copyReceivedFrameToQueue(receiveInfo->src_addr, data, dataLength);
}

/// Callback de fin d'envoi ESP-NOW : libère le canal radio. La signature dépend de la version d'ESP-IDF.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onEspNowSent(const esp_now_send_info_t*, esp_now_send_status_t status) {
#else
void onEspNowSent(const uint8_t*, esp_now_send_status_t status) {
#endif
  // Un échec MAC n'est que compté : l'ACK applicatif et la retransmission décident de la suite.
  if (status != ESP_NOW_SEND_SUCCESS) radioSendFailureCounter = radioSendFailureCounter + 1;
  isRadioSending = false;
}

/// Source d'aléa matérielle pour le jitter du backoff (§7.3).
uint32_t readHardwareRandomValue() { return esp_random(); }

}  // namespace

uint32_t MPSNode::radioSendFailureCount() const { return radioSendFailureCounter; }

MPSNode::MPSNode() : client_(*this, mps::ClientConfiguration()) {}

bool MPSNode::begin(const uint8_t gatewayMacAddress[6], const char* deviceName, uint8_t wifiChannel) {
  memcpy(gatewayPeerMacAddress, gatewayMacAddress, 6);

  mps::ClientConfiguration configuration;
  configuration.role         = mps::Role::Node;
  configuration.deviceName   = deviceName;
  configuration.randomSource = readHardwareRandomValue;
  client_.configure(configuration);
  client_.setListener(this);

  // Station sans connexion : une connexion à un routeur verrouillerait le canal (§11.3).
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // Le récepteur doit rester allumé : avec l'économie d'énergie par défaut (modem sleep), un ESP32-C3
  // manque les trames reçues entre ses propres émissions. MPS v1 ne spécifie pas le sommeil.
  esp_wifi_set_ps(WIFI_PS_NONE);
  if (esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    if (logOutput_) logOutput_->println("[MPS] canal Wi-Fi refuse");
    return false;
  }
  if (esp_now_init() != ESP_OK) {
    if (logOutput_) logOutput_->println("[MPS] esp_now_init a echoue");
    return false;
  }
  esp_now_register_recv_cb(onEspNowReceive);
  esp_now_register_send_cb(onEspNowSent);

  esp_now_peer_info_t gatewayPeer = {};
  memcpy(gatewayPeer.peer_addr, gatewayPeerMacAddress, 6);
  gatewayPeer.channel = wifiChannel;  // doit correspondre au canal de l'interface locale
  gatewayPeer.ifidx   = WIFI_IF_STA;
  gatewayPeer.encrypt = false;  // v1 : rien n'est chiffré (§8)
  if (!esp_now_is_peer_exist(gatewayPeerMacAddress) && esp_now_add_peer(&gatewayPeer) != ESP_OK) {
    if (logOutput_) logOutput_->println("[MPS] ajout du pair passerelle impossible");
    return false;
  }

  client_.begin(millis());
  hasBegun_ = true;
  if (logOutput_)
    logOutput_->printf("[MPS] demarre : nom=%s canal=%u passerelle=%02X:%02X:%02X:%02X:%02X:%02X\n", deviceName,
                       wifiChannel, gatewayPeerMacAddress[0], gatewayPeerMacAddress[1], gatewayPeerMacAddress[2],
                       gatewayPeerMacAddress[3], gatewayPeerMacAddress[4], gatewayPeerMacAddress[5]);
  return true;
}

void MPSNode::loop() {
  if (!hasBegun_) return;
  const uint32_t currentTimeMs = millis();
  // Vide la file de réception : chaque trame est copiée hors de la section critique, puis traitée.
  for (;;) {
    ReceivedFrameSlot frame;
    bool              hasFrame = false;
    portENTER_CRITICAL(&receiveQueueLock);
    if (receiveQueueCount > 0) {
      frame.length = receiveQueue[receiveQueueHead].length;
      memcpy(frame.bytes, receiveQueue[receiveQueueHead].bytes, frame.length);
      receiveQueueHead  = (receiveQueueHead + 1) % MPS_RECEIVE_QUEUE_DEPTH;
      receiveQueueCount = receiveQueueCount - 1;
      hasFrame          = true;
    }
    portEXIT_CRITICAL(&receiveQueueLock);
    if (!hasFrame) break;
    ++receivedFrameCount_;
    client_.processReceivedFrame(frame.bytes, frame.length, currentTimeMs);
  }
  droppedReceivedFrameCount_ = droppedFrameCounter;
  foreignFrameCount_         = foreignFrameCounter;
  client_.poll(currentTimeMs);
}

bool MPSNode::send(const uint8_t* frame, size_t frameLength) {
  // Un seul envoi radio en cours : deux envois rapprochés peuvent faire arriver les callbacks dans le
  // désordre (§4.6). Retourner false signifie « occupé » : le moteur réessaie au prochain loop().
  if (isRadioSending && (uint32_t)(millis() - radioSendStartTimeMs) < kRadioSendTimeoutMs) return false;
  const esp_err_t result = esp_now_send(gatewayPeerMacAddress, frame, frameLength);
  if (result != ESP_OK) return false;  // par exemple ESP_ERR_ESPNOW_NO_MEM : court délai avant de réessayer
  radioSendStartTimeMs = millis();
  isRadioSending       = true;
  return true;
}

mps::ErrorCode MPSNode::on(const mps::Subject& subject, mps::MessageHandler handler, void* userContext) {
  const mps::ErrorCode result = client_.addHandler(subject, handler, userContext);
  if (!logOutput_) return result;
  switch (result) {
    case mps::ErrorCode::Ok:
      if (!mps::hasKnownMessageVerb(subject.name, mps::stringLength(subject.name)))
        logOutput_->printf("[MPS] avertissement : '%s' ne commence pas par get/set/cmd/status/evt/hb\n", subject.name);
      break;
    case mps::ErrorCode::HashCollision:
      logOutput_->printf("[MPS] ERREUR : '%s' a le meme subject_id qu'un autre nom, changez-le\n", subject.name);
      break;
    case mps::ErrorCode::HandlerTableFull:
      logOutput_->printf("[MPS] ERREUR : plus de place pour '%s' (%d max)\n", subject.name, MPS_MAX_HANDLERS);
      break;
    default: logOutput_->printf("[MPS] ERREUR : '%s' refuse (code %u)\n", subject.name, (unsigned)result);
  }
  return result;
}

mps::ErrorCode MPSNode::off(const mps::Subject& subject) { return client_.removeHandler(subject.subjectId); }

mps::ErrorCode MPSNode::publish(const mps::Subject& subject, const void* data, size_t dataLength,
                                bool requestAcknowledgement) {
  return client_.publish(subject, data, dataLength, requestAcknowledgement);
}

mps::ErrorCode MPSNode::publishHeartbeat(const mps::Subject& subject, uint8_t healthState) {
  mps::HeartbeatPayload heartbeat;
  heartbeat.healthState        = healthState;
  heartbeat.heartbeatCounter   = nextHeartbeatCounter_++;  // repasse à 0 après 65 535
  heartbeat.uptimeMilliseconds = millis();
  return client_.publish(subject, &heartbeat, sizeof heartbeat, false);  // un heartbeat NE DOIT PAS demander d'ACK (§6.1)
}

void MPSNode::onRegistered(uint8_t assignedNodeId, uint16_t maxPayloadSize, uint8_t keepaliveIntervalSeconds) {
  if (logOutput_)
    logOutput_->printf("[MPS] enregistre : node_id=%u max_payload=%u keepalive=%us\n", assignedNodeId, maxPayloadSize,
                       keepaliveIntervalSeconds);
  if (registeredCallback_) registeredCallback_();
}

void MPSNode::onUnregistered(mps::SessionLossReason reason) {
  // Indexé par SessionLossReason : SendFailed, GatewayForgotSession, Rejected, GatewayRestarted.
  static const char* const kReasonDescriptions[] = {"envoi echoue", "passerelle sans session", "refuse",
                                                    "redemarrage passerelle"};
  if (logOutput_)
    logOutput_->printf("[MPS] session perdue (%s), nouveau HELLO\n", kReasonDescriptions[(unsigned)reason & 3]);
  if (unregisteredCallback_) unregisteredCallback_();
}

void MPSNode::onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason reason,
                           mps::AcknowledgementStatus rejectionStatus) {
  if (logOutput_)
    logOutput_->printf("[MPS] envoi echoue : seq=%u subject=0x%08lX %s (statut %u)\n", sequenceNumber,
                       (unsigned long)subjectId, reason == mps::SendFailureReason::Timeout ? "sans ACK" : "refuse",
                       (unsigned)rejectionStatus);
  if (sendFailedCallback_) sendFailedCallback_(sequenceNumber, subjectId, reason, rejectionStatus);
}

#endif  // ARDUINO_ARCH_ESP32
