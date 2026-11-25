#include <MPS.h>
#include <Arduino.h>
#include "config.h"
#include "CounterController.h"
#include "HeartbeatController.h"
#include "LedController.h"
#include "ControllerManager.h"
#include "TemperatureHumidityController.h"

ControllerManager controllerManager{
    new CounterController(),
    new HeartbeatController(),
    new LedController(),
    new TemperatureHumidityController()
  };

static void onSessionOpened()
{
  Serial.println("Session opened!");
  controllerManager.onSessionOpened();
  // republiez ici l’état initial, la passerelle ne garde rien
}

// Appelé en cas d'échec d'envoi (callback)
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus)
{
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

// ---- Fonctions Arduino ----
// Exécutée une fois au démarrage
void setup()
{
  Serial.begin(115200);      // Démarrage de la communication série à 115200 bauds (sortie des Serial.print.. par exemple)
  MPS.setLogOutput(&Serial); // Définition de la sortie des messages de log de la lib MPS

  Serial.println("MPS Node starting...");

  // enregistrement des callbacks
  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);

  controllerManager.setup();

  // constante d'adresse vide pour vérifier que l'adresse a été définie dans config.h
  static const uint8_t kUnsetMacAddress[6] = {0, 0, 0, 0, 0, 0};
  if (memcmp(kGatewayMacAddress, kUnsetMacAddress, 6) == 0)
  {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  pinMode(G3_BUTTON, INPUT_PULLUP);
  // démarrage du client MPS
  if (!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel))
    Serial.println("MPS: demarrage impossible");
}

void loop()
{
  // appel de la boucle MPS (/!\ du code bloquant dans votre boucle loop() pourrait occasionner des problèmes de connexion)
  MPS.loop();

  controllerManager.loop();
}