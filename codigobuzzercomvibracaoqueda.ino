#include <Wire.h>
#include <PubSubClient.h>
#include "MAX30100_PulseOximeter.h"
#include <WiFi.h>

// ─── Pinos ───────────────────────────────────────────────────
#define SDA_PIN            32
#define SCL_PIN            27
#define BUZZER_PIN         25   // buzzer — alerta de BPM alto
#define VIBRA_PIN          21   // sensor de vibração/queda (INPUT, só leitura)

// ─── Temporização ────────────────────────────────────────────
#define REPORTING_PERIOD_MS   1000   // intervalo de publicação MQTT dos sinais vitais
#define MQTT_RETRY_MS         5000   // intervalo entre tentativas de reconnect MQTT
#define JANELA_VIBRACAO_MS     500   // janela de contagem do sensor de vibração

// ─── Limites de alerta ───────────────────────────────────────
#define LIMITE_BPM            100    // BPM acima disto → buzzer liga
#define LIMITE_QUEDA_FORTE     60    // contagem >= 60 numa janela de 500ms → queda
#define LIMITE_QUEDA_MEDIA     30    // contagem >= 30 → impacto forte

// ─── Wi-Fi e MQTT ────────────────────────────────────────────
const char* ssid             = "Michelleb";
const char* password         = "280989mi";
const char* mqtt_server      = "test.mosquitto.org";
//const char* mqtt_server      = "10.15.102.147";
const int   mqtt_port        = 1883;
const char* mqtt_topic       = "healthsensor";        // sinais vitais
const char* mqtt_beat_topic  = "healthsensor/beat";   // batimento detectado
const char* mqtt_queda_topic = "healthsensor/queda";  // alerta de queda

WiFiClient   espClient;
PubSubClient client(espClient);

PulseOximeter pox;
uint32_t tsLastReport      = 0;
uint32_t tsLastMqttAttempt = 0;

// ─── Variáveis do sensor de vibração/queda ───────────────────
unsigned long inicioJanela  = 0;
int contagem                = 0;
unsigned long tsUltimaQueda = 0;      // timestamp da última publicação de queda
#define INTERVALO_QUEDA_MS  2000      // mínimo 2s entre publicações de queda

// ─── Mutex I2C ────────────────────────────────────────────────
SemaphoreHandle_t i2cMutex;

// ─── Wi-Fi ────────────────────────────────────────────────────
void connectToWiFi() {
  Serial.print("Conectando WiFi...");
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " Falhou");
  WiFi.setSleep(false);
}

// ─── Reconnect MQTT ──────────────────────────────────────────
void tryMqttReconnect() {
  if (client.connected()) return;
  if (millis() - tsLastMqttAttempt < MQTT_RETRY_MS) return;
  tsLastMqttAttempt = millis();
  Serial.print("MQTT... ");
  String clientId = "esp32-" + String(random(0xffff), HEX); // ID único evita conflito
  if (client.connect(clientId.c_str())) {
    Serial.println("OK");
  } else {
    Serial.print("falhou rc=");
    Serial.println(client.state());
  }
}

// ─── Callback de batimento ───────────────────────────────────
void onBeatDetected() {
  Serial.println("Batimento detectado!");
  if (client.connected()) {
    client.publish(mqtt_beat_topic, "1");
  }
}

// ─── Publica queda no MQTT ───────────────────────────────────
void publicarQueda(const char* tipo, const char* intensidade, int cnt) {
  // Respeita intervalo mínimo entre publicações
  if (millis() - tsUltimaQueda < INTERVALO_QUEDA_MS) return;

  String payload = "{\"tipo\":\"";
  payload += tipo;
  payload += "\",\"intensidade\":\"";
  payload += intensidade;
  payload += "\",\"contagem\":";
  payload += cnt;
  payload += "}";

  if (client.connected()) {
    client.publish(mqtt_queda_topic, payload.c_str());
    Serial.println("MQTT queda: " + payload);
    tsUltimaQueda = millis();
  } else {
    Serial.println("MQTT desligado — queda não publicada");
  }
}

// ─── Detecção de queda ───────────────────────────────────────
void verificarQueda() {
  if (digitalRead(VIBRA_PIN) == LOW) {
    contagem++;
  }

  if (millis() - inicioJanela >= JANELA_VIBRACAO_MS) {

    // Barra visual no Serial Monitor
    Serial.print("[");
    int barras = min(contagem, 20);
    for (int i = 0; i < barras; i++)  Serial.print("#");
    for (int i = barras; i < 20; i++) Serial.print(".");
    Serial.print("] ");

    if (contagem == 0) {
      Serial.println("Sem vibração");

    } else if (contagem < 10) {
      Serial.println("FRACA");

    } else if (contagem < LIMITE_QUEDA_MEDIA) {
      Serial.println("MÉDIA");

    } else if (contagem < LIMITE_QUEDA_FORTE) {
      Serial.println("FORTE — impacto detectado");
      publicarQueda("impacto", "FORTE", contagem);

    } else {
      Serial.println("MUITO FORTE! — QUEDA DETECTADA");
      publicarQueda("queda", "MUITO_FORTE", contagem);
    }

    Serial.print("Contagem: ");
    Serial.println(contagem);
    Serial.println("─────────────────────────────");

    contagem     = 0;
    inicioJanela = millis();
  }
}

// ─── Tarefa dedicada ao sensor MAX30100 (Núcleo 1) ───────────
void sensorTask(void* pvParameters) {
  for (;;) {
    if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
      pox.update();
      xSemaphoreGive(i2cMutex);
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// ─── SETUP ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(VIBRA_PIN, INPUT);

  connectToWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(512);
  client.setCallback([](char* topic, byte* payload, unsigned int length) {});

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.print("Oximetro... ");
  if (!pox.begin()) {
    Serial.println("FALHA");
    while (1);
  }
  Serial.println("OK");

  pox.setIRLedCurrent(MAX30100_LED_CURR_24MA);
  pox.setOnBeatDetectedCallback(onBeatDetected);

  i2cMutex     = xSemaphoreCreateMutex();
  inicioJanela = millis();

  xTaskCreatePinnedToCore(
    sensorTask,
    "sensorTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );

  Serial.println("Sistema pronto.");
}

// ─── LOOP ────────────────────────────────────────────────────
void loop() {
  tryMqttReconnect();
  client.loop();

  verificarQueda();

  if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
    float heartRate, spO2;

    if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
      heartRate = pox.getHeartRate();
      spO2      = pox.getSpO2();
      xSemaphoreGive(i2cMutex);
    }

    Serial.print("HR: ");
    Serial.print(heartRate);
    Serial.print(" bpm | SpO2: ");
    Serial.print(spO2);
    Serial.println(" %");

    if (heartRate > 0 && spO2 > 0) {
      String payload = "{\"heartRate\":";
      payload += heartRate;
      payload += ", \"spO2\":";
      payload += spO2;
      payload += "}";

      if (client.connected()) {
        client.publish(mqtt_topic, payload.c_str());
        Serial.println("MQTT: " + payload);
      }

      if (heartRate > LIMITE_BPM) {
        digitalWrite(BUZZER_PIN, HIGH);
        Serial.println("ALERTA! BPM alto — buzzer ligado.");
      } else {
        digitalWrite(BUZZER_PIN, LOW);
      }
    }

    tsLastReport = millis();
  }

  delay(10);
}
