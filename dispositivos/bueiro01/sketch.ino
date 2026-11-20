/*
 * ============================================================
 * Projeto: Bueiro Inteligente
 * Universidade Presbiteriana Mackenzie
 * Disciplina: Objetos Inteligentes Conectados
 * ------------------------------------------------------------
 * Dispositivo: bueiro01 (ESP32 + HC-SR04 + servo da grade autolimpante)
 * Local: Rua da Consolação, 930 - Consolação, São Paulo/SP
 *
 * Função:
 *   1) Medir, a cada 5 segundos, a distância entre o topo da caixa
 *      da boca de lobo e a superfície dos resíduos e calcular o
 *      VOLUME de resíduos acumulado (litros), publicado via MQTT.
 *   2) Receber o comando "limpar" e movimentar a grade autolimpante
 *      com o servo motor (duas passadas de 0° a 180°), informando
 *      o início e o fim da limpeza.
 *
 * Dimensões internas da caixa (SINAPI, caixa simples, composição 97956):
 *   1,00 m (comprimento) x 0,60 m (largura) x 1,20 m (profundidade)
 *   Área da base = 0,60 m²  |  Capacidade = 720 litros
 *   Volume (L) = área da base (m²) x altura dos resíduos (cm) x 10
 *
 * Pinagem (diagram.json):
 *   HC-SR04 VCC  -> VIN (5 V)    |  Servo V+  -> VIN (5 V)
 *   HC-SR04 GND  -> GND          |  Servo GND -> GND
 *   HC-SR04 TRIG -> GPIO 5       |  Servo PWM -> GPIO 13
 *   HC-SR04 ECHO -> GPIO 18
 *
 * Tópicos (prefixo mackenzie/bueiro-inteligente/bueiro01):
 *   Publica: /sensor/volume       volume de resíduos em litros (ex.: 540.0)
 *   Assina:  /grade/comando       "limpar"
 *   Publica: /grade/confirmacao   "limpando" / "limpeza_concluida"
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ---------- Wi-Fi (rede simulada do Wokwi) ----------
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// ---------- Broker MQTT ----------
const char* MQTT_BROKER    = "broker.hivemq.com";
const int   MQTT_PORT      = 1883;
const char* MQTT_CLIENT_ID = "mackenzie-bueiro01";

// ---------- Identificação e tópicos ----------
const char* ID_BUEIRO = "bueiro01";
const char* PREFIXO   = "mackenzie/bueiro-inteligente";

char topicoVolume[80];
char topicoComando[80];
char topicoConfirmacao[80];

// ---------- Dimensões da caixa da boca de lobo ----------
const float AREA_BASE_M2     = 0.60;  // 1,00 m x 0,60 m
const float PROFUNDIDADE_CM  = 120.0;       // sensor instalado no topo da caixa
const float CAPACIDADE_L     = AREA_BASE_M2 * PROFUNDIDADE_CM * 10.0; // 720 L

// ---------- Pinos ----------
const int PIN_TRIG  = 5;
const int PIN_ECHO  = 18;
const int PIN_SERVO = 13;

// ---------- Grade autolimpante ----------
const int ANGULO_REPOUSO  = 0;
const int ANGULO_FINAL    = 180;
const int PASSADAS        = 2;   // quantas vezes o raspador varre a grade
const int ATRASO_PASSO_MS = 10;  // velocidade do movimento (ms por grau)

// ---------- Temporização ----------
const unsigned long INTERVALO_ENVIO_MS = 5000;
unsigned long ultimoEnvio = 0;

Servo servoGrade;
bool limpando = false;

WiFiClient espClient;
PubSubClient mqtt(espClient);

// Mede a distância (cm) entre o sensor e a superfície dos resíduos
float medirDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duracao = pulseIn(PIN_ECHO, HIGH, 30000);
  if (duracao == 0) return PROFUNDIDADE_CM; // sem eco: considera caixa vazia
  return duracao * 0.0343 / 2.0;
}

// Converte a distância em volume de resíduos (litros)
float calcularVolumeLitros(float distanciaCm) {
  float alturaResiduosCm = constrain(PROFUNDIDADE_CM - distanciaCm, 0.0, PROFUNDIDADE_CM);
  // m² x cm x 10 = litros (1 m² x 1 cm = 10 L)
  return AREA_BASE_M2 * alturaResiduosCm * 10.0;
}

// Move o servo grau a grau, mantendo a conexão MQTT ativa durante o movimento
void moverServo(int de, int ate) {
  int passo = (ate > de) ? 1 : -1;
  for (int angulo = de; angulo != ate + passo; angulo += passo) {
    servoGrade.write(angulo);
    delay(ATRASO_PASSO_MS);
    mqtt.loop();
  }
}

void executarLimpeza() {
  limpando = true;
  mqtt.publish(topicoConfirmacao, "limpando");
  Serial.println("Iniciando limpeza da grade...");

  for (int i = 1; i <= PASSADAS; i++) {
    moverServo(ANGULO_REPOUSO, ANGULO_FINAL);
    moverServo(ANGULO_FINAL, ANGULO_REPOUSO);
  }

  mqtt.publish(topicoConfirmacao, "limpeza_concluida");
  Serial.println("Limpeza concluída.");
  limpando = false;
}

// Chamada sempre que chega uma mensagem no tópico de comando
void callback(char* topic, byte* payload, unsigned int length) {
  String comando;
  for (unsigned int i = 0; i < length; i++) comando += (char)payload[i];
  comando.trim();
  comando.toLowerCase();

  Serial.print("Comando recebido: ");
  Serial.println(comando);

  if (limpando) {
    Serial.println("Limpeza em andamento, comando ignorado.");
    return;
  }
  if (comando == "limpar") {
    executarLimpeza();
  } else {
    Serial.println("Comando desconhecido, ignorado.");
  }
}

void conectarWiFi() {
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" conectado!");
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Conectando ao broker MQTT...");
    if (mqtt.connect(MQTT_CLIENT_ID)) {
      Serial.println(" conectado!");
      mqtt.subscribe(topicoComando, 1);
    } else {
      Serial.print(" falhou, rc=");
      Serial.print(mqtt.state());
      Serial.println(". Nova tentativa em 2s");
      delay(2000);
    }
  }
}

void montarTopicos() {
  snprintf(topicoVolume,      sizeof(topicoVolume),      "%s/%s/sensor/volume",     PREFIXO, ID_BUEIRO);
  snprintf(topicoComando,     sizeof(topicoComando),     "%s/%s/grade/comando",     PREFIXO, ID_BUEIRO);
  snprintf(topicoConfirmacao, sizeof(topicoConfirmacao), "%s/%s/grade/confirmacao", PREFIXO, ID_BUEIRO);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  servoGrade.setPeriodHertz(50);           // servo padrão: 50 Hz
  servoGrade.attach(PIN_SERVO, 500, 2400); // pulso mínimo e máximo (µs)
  servoGrade.write(ANGULO_REPOUSO);

  montarTopicos();
  conectarWiFi();

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(callback);
  mqtt.setKeepAlive(60); // tolera melhor a lentidão da simulação
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  unsigned long agora = millis();
  if (agora - ultimoEnvio < INTERVALO_ENVIO_MS) return;
  ultimoEnvio = agora;

  float distanciaCm = medirDistanciaCm();
  float volumeL     = calcularVolumeLitros(distanciaCm);

  char bufVolume[16];
  dtostrf(volumeL, 0, 1, bufVolume);
  mqtt.publish(topicoVolume, bufVolume);

  Serial.printf("Volume de residuos: %s L de %.0f L (%.0f%%)\n",
                bufVolume, CAPACIDADE_L, volumeL / CAPACIDADE_L * 100.0);
}
