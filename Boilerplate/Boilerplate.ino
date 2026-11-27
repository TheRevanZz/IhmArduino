#include <MPS.h>
#include "config.h"

struct __attribute__((packed)) CounterPayload{
  uint32_t value;
};
static_assert(sizeof(CounterPayload) == 4, "CounterPayload = 4 octets");

static CounterPayload counter = {0};

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdResetCounter = mps::makeSubject("cmd_" DEVICE_NAME "_reset_counter");

uint8_t color_cursor = 10;
bool holding_button = false;
uint8_t click_threshold = 90;

// ---- Callbacks ----
// Appelé lors de l'ouverture de la session (callback)
static void onSessionOpened() {
  Serial.println("Session opened!");
  MPS.publishValue(kSubStatusCounter, counter);
  ChangeLedColor(Colors[color_cursor]);
}

// Appelé en cas d'échec d'envoi (callback)
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) {
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}


static void onCmdReset(const mps::FrameView& message, void*) {
  if (message.payloadLength != 0) return; // ce format n'a pas de payload
    counter.value = 5;
  MPS.publishValue(kSubStatusCounter, counter); // l'IHM voit le résultat réel
}

// ---- Fonctions appelées par setup/loop ----
// envoi de heartbeats réguliers sans bloquer (comparaison au temps écoulé sans delay())
void publishHeartbeat(){
  static uint32_t lastHeartbeatTimeMs = 0;
  static const uint16_t kHbPeriod = 1000;
  if(millis() - lastHeartbeatTimeMs >= kHbPeriod) {
    lastHeartbeatTimeMs = millis();
    
    // L'envoi de heartbeats depuis loop() assure au récepteur (IHM) que le code "métier" n'est pas planté
    MPS.publishHeartbeat(kSubHb);
  }
}



// ---- Fonctions Arduino ----
// Exécutée une fois au démarrage
void setup() {
  Serial.begin(115200); // Démarrage de la communication série à 115200 bauds (sortie des Serial.print.. par exemple)
  MPS.setLogOutput(&Serial); // Définition de la sortie des messages de log de la lib MPS

  // enregistrement des callbacks
  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);
  MPS.on(kSubCmdResetCounter, onCmdReset);

  // constante d'adresse vide pour vérifier que l'adresse a été définie dans config.h
  static const uint8_t kUnsetMacAddress[6] = {0, 0, 0, 0, 0, 0};
  if(memcmp(kGatewayMacAddress, kUnsetMacAddress, 6) == 0) {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  pinMode(G3_BUTTON, INPUT_PULLUP);
  // démarrage du client MPS
  if(!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel)) Serial.println("MPS: demarrage impossible");

}

//envoie les données du compteur toutes les 2 secondes
void publishCounter()
{
  static uint32_t lastCounterTimeMs = 0;
  if (millis() - lastCounterTimeMs >= 2000) {
    lastCounterTimeMs = millis();
    counter.value++;
    MPS.publishValue(kSubStatusCounter, counter);
  }
}

bool b = false;
//Permer de changer la lumière sur le capteur
void LightControl()
{
  static uint32_t buttonTimer = 0;

  if(!digitalRead(G3_BUTTON))
  {

        
    if(millis() - buttonTimer >= click_threshold && !b && color_cursor != 0)
    {
      b = true;
      color_cursor++;
      color_cursor %= Colors.size();
      if(color_cursor == 0)
        color_cursor = 1;
      ChangeLedColor(Colors[color_cursor]);
    }

    else if(millis() - buttonTimer >= 5000)
    {
      buttonTimer = millis();
      if(color_cursor == 0)
      {
        color_cursor = 6;
      }
      else
      {
        color_cursor = 0;
      }
      ChangeLedColor(Colors[color_cursor]);
    }

    holding_button = true;

  }
  else
  {
    b = false;
    buttonTimer = millis();
    holding_button = false;
  }
}

// Exécutée en boucle (= while(true))
void loop() {
  // appel de la boucle MPS (/!\ du code bloquant dans votre boucle loop() pourrait occasionner des problèmes de connexion)
  MPS.loop();

  publishHeartbeat();
  publishCounter();

  LightControl();
}


