#include <DHT.h>
#include <DHT_U.h>

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

struct __attribute__((packed)) LightPowerPayload{
  bool value;
};
static_assert(sizeof(LightPowerPayload) == 1, "LightPowerPayload = 1 octet");

struct __attribute__((packed)) LightColorPayload{
  uint8_t color[3];
};
static_assert(sizeof(LightColorPayload) == 3, "LightColorPayload = 3 octets");

struct __attribute__((packed)) TemperatureHumidityPayload {
  float temperature;
  float humidity;
};
static_assert(sizeof(TemperatureHumidityPayload) == 8, "TemperatureHumidityPayload = 8 octets");


DHT dht(DHTPIN, DHTTYPE);

static CounterPayload counter = {0};

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdResetCounter = mps::makeSubject("cmd_" DEVICE_NAME "_reset_counter");
constexpr mps::Subject kSubLightPowerSwitch = mps::makeSubject("set_" DEVICE_NAME "_light_power");
constexpr mps::Subject kSubLightColorSwitch = mps::makeSubject("set_" DEVICE_NAME "_light_color");
constexpr mps::Subject kSubStatusTemperatureHumidity = mps::makeSubject("status_" DEVICE_NAME "_temperature_humidity");
// ... ajoutez vos sujets ici sur le même schéma que le heartbeat, mais en précisant la donnée envoyée,
// par exemple : `constexpr mps::Subject kSubStatusSomeData = mps::makeSubject("status_" DEVICE_NAME "_some_data");`
// ou : `constexpr mps::Subject kSubCmdSomeOrder = mps::makeSubject("cmd_" DEVICE_NAME "_some_order");`

uint8_t color_cursor = 0;
bool holding_button = false;
uint8_t click_threshold = 90;
uint16_t switch_light_threshold = 5000;

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

static void onSetLightPower(const mps::FrameView& message, void*)
{
    
    if(
      LightPowerPayload p; 
      mps::readPayloadAs(message, &p)
    )
    {
      ChangeLedColor(Colors[p.value ? 1 : 0]);
    }

}

static void onSetLightColor(const mps::FrameView& message, void*)
{
  if (
    LightColorPayload p;
    mps::readPayloadAs(message, &p)
  )
  {

    ChangeLedColor(p.color);
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
  MPS.on(kSubLightPowerSwitch, onSetLightPower);
  MPS.on(kSubLightColorSwitch, onSetLightColor);

  // constante d'adresse vide pour vérifier que l'adresse a été définie dans config.h
  static const uint8_t kUnsetMacAddress[6] = {0, 0, 0, 0, 0, 0};
  if(memcmp(kGatewayMacAddress, kUnsetMacAddress, 6) == 0) {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  pinMode(G3_BUTTON, INPUT_PULLUP);
  // démarrage du client MPS
  if(!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel)) Serial.println("MPS: demarrage impossible");

  Serial.println(F("DHT11 test!"));
  dht.begin();
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

void publishTemperatureHumidity()
{
  static uint32_t lastCounterTimeMs = 0;
  int temp = dht.readTemperature();
  int hum = dht.readHumidity();
  if (std::isnan(temp) || std::isnan(hum))
  {
    Serial.print(F("Impossible de lire la température ou l'humidité"));
    return;
  }
  if (millis() - lastCounterTimeMs >= 500)
  {
    lastCounterTimeMs = millis();
    MPS.publishValue(kSubStatusTemperatureHumidity, TemperatureHumidityPayload{temp * 10, hum * 10});
  }
}

bool action_done = false;

void LightControl()
{
  static uint32_t buttonTimer = 0;

  if(!digitalRead(G3_BUTTON))
  {

        
    const auto interval = millis() - buttonTimer;

    if(interval >= click_threshold && interval <= click_threshold * 1.2f && !action_done && color_cursor != 0)
    {
      action_done = true;
      color_cursor++;
      color_cursor %= Colors.size();
      if(color_cursor == 0)
        color_cursor = 1;
      ChangeLedColor(Colors[color_cursor]);
    }

    else if(interval >= switch_light_threshold && interval <= switch_light_threshold * 1.1)
    {
      action_done = true;
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
    action_done = false;
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
  publishTemperatureHumidity();
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


