#include <MPS.h>
#include "config.h"




// ---- Structures de payload ----
// placer ici vos structures de payloads, par exemple :
// struct __attribute__((packed)) SomePayload { // __attribute__((packed)) permet de contrôler la disposition des octets de celle-ci
//   uint8_t someVariableA;
//   int16_t someVariableB;
// };
// static_assert(sizeof(SomePayload) == 3, "SomePayload = 3 octets"); // Filet de sécurité à la compilation

// ---- Variables globales ----
// placez ici vos variables à partager entre plusieurs contextes (ex : setup, loop, callbacks)

struct __attribute__((packed)) CounterPayload{
  uint32_t value;
};
static_assert(sizeof(CounterPayload) == 4, "CounterPayload = 4 octets");

static CounterPayload counter = {0};

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdResetCounter = mps::makeSubject("cmd_" DEVICE_NAME "_reset_counter");
// ... ajoutez vos sujets ici sur le même schéma que le heartbeat, mais en précisant la donnée envoyée,
// par exemple : `constexpr mps::Subject kSubStatusSomeData = mps::makeSubject("status_" DEVICE_NAME "_some_data");`
// ou : `constexpr mps::Subject kSubCmdSomeOrder = mps::makeSubject("cmd_" DEVICE_NAME "_some_order");`

uint8_t color_cursor = 10;
bool holding_button = false;
uint8_t click_threshold = 90;

// ---- Callbacks ----
// Appelé lors de l'ouverture de la session (callback)
static void onSessionOpened() {
  Serial.println("Session opened!");
  MPS.publishValue(kSubStatusCounter, counter);
  ChangeLedColor(Colors[color_cursor]);
  // republiez ici l’état initial, la passerelle ne garde rien
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

// ... ajoutez vos callbacks ici, ex : 
// static void onCmdSomeOrder(const mps::FrameView& message, void*) {
//   SomePayload payload;
//   if(!mps::readPayloadAs(message, &payload)) return;
//   Serial.printf("Donnée : %02X\n", payload.someVariableA);
// }

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
  // MPS.on(kSubStatusCounter, )
  // ... ajoutez vos callbacks à l'aide de MPS.on(const mps::Subject& subject, mps::MessageHandler handler, void* userContext = nullptr)
  // par exemple :
  // MPS.on(kSubCmdSomeOrder, onCmdSomeOrder);
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

  // Appel non bloquant de l'envoi d'HB (polling)
  publishHeartbeat();
  publishCounter();
  // ... ajoutez vos envois de données et autres comportements **non bloquants**
  // par exemple :
  // static uint32_t lastDataStatusTimeMs = 0;
  // if(millis() - lastDataStatusTimeMs >= 2000){
  //   lastDataStatusTimeMs = millis();
  //   SomePayload payload = {0, 0};
  //   payload.someVariableA = digitalRead(3);
  //   payload.someVariableB = analogRead(5);
  //   MPS.publishValue(kSubStatusSomeData, payload);
  // }

  LightControl();
}


