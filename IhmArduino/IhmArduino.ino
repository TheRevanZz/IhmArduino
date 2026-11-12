#include <MPS.h>
#include <Arduino.h>
#include "config.h"
#include "CounterController.h"
#include "HeartbeatController.h"
#include "LedController.h"
#include "ControllerManager.h"

// ---- Structures de payload ----
// placer ici vos structures de payloads, par exemple :
// struct __attribute__((packed)) SomePayload { // __attribute__((packed)) permet de contrôler la disposition des octets de celle-ci
//   uint8_t someVariableA;
//   int16_t someVariableB;
// };

ControllerManager controllerManager{ new CounterController(), new HeartbeatController(), new LedController() };
// ---- Subjects ----
// ... ajoutez vos sujets ici sur le même schéma que le heartbeat, mais en précisant la donnée envoyée,
// par exemple : `constexpr mps::Subject kSubStatusSomeData = mps::makeSubject("status_" DEVICE_NAME "_some_data");`
// ou : `constexpr mps::Subject kSubCmdSomeOrder = mps::makeSubject("cmd_" DEVICE_NAME "_some_order");`


// ---- Callbacks ----
// Appelé lors de l'ouverture de la session (callback)
static void onSessionOpened() {
  Serial.println("Session opened!");
  controllerManager.onSessionOpened();
  // republiez ici l’état initial, la passerelle ne garde rien
}

// Appelé en cas d'échec d'envoi (callback)
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) {
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

// ---- Fonctions Arduino ----
// Exécutée une fois au démarrage
void setup() {
  Serial.begin(115200);       // Démarrage de la communication série à 115200 bauds (sortie des Serial.print.. par exemple)
  MPS.setLogOutput(&Serial);  // Définition de la sortie des messages de log de la lib MPS

  Serial.println("MPS Node starting...");

  // enregistrement des callbacks
  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);
  // MPS.on(kSubStatusCounter, )
  // ... ajoutez vos callbacks à l'aide de MPS.on(const mps::Subject& subject, mps::MessageHandler handler, void* userContext = nullptr)

  controllerManager.setup();

  // constante d'adresse vide pour vérifier que l'adresse a été définie dans config.h
  static const uint8_t kUnsetMacAddress[6] = { 0, 0, 0, 0, 0, 0 };
  if (memcmp(kGatewayMacAddress, kUnsetMacAddress, 6) == 0) {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  pinMode(G3_BUTTON, INPUT_PULLUP);
  // démarrage du client MPS
  if (!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel)) Serial.println("MPS: demarrage impossible");
}

void loop() {
  // appel de la boucle MPS (/!\ du code bloquant dans votre boucle loop() pourrait occasionner des problèmes de connexion)
  MPS.loop();

  controllerManager.loop();
}
