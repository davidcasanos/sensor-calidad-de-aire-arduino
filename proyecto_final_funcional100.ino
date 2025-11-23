#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

// ═══════════════════════════════════════════════════════════
// CONFIGURACIÓN - MODIFICA ESTOS VALORES
// ═══════════════════════════════════════════════════════════

// WiFi
#define WLAN_SSID       "HUAWEI-2.4G-XmP3"
#define WLAN_PASS       "9GaB95Sc"

// Adafruit IO
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "davidcasanos"
#define AIO_KEY         "aio_sYLG57i1pwY8pfNi4J2h5uJ2UOaf"

// Telegram Bot
#define BOT_TOKEN       "8339151841:AAEoaQailCra3d6WoV_1reTN7Oq7hzKYBd4"
#define CHAT_ID         "8210007793"

// WhatsApp (CallMeBot)
#define PHONE_NUMBER    "+51951312014"
#define CALLMEBOT_KEY   "8913601"

// Google Sheets
String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbyOZ5aSzDTN7EvxE1fscKKi8oCtmz-w7ON0j1iqf1p_NP6HHbJRIY2SFSLiQGGS3M4XZw/exec";

// ═══════════════════════════════════════════════════════════
// HABILITAR/DESHABILITAR SERVICIOS (true = activo)
// ═══════════════════════════════════════════════════════════
#define ENABLE_ADAFRUIT   true
#define ENABLE_TELEGRAM   true
#define ENABLE_WHATSAPP   true  // Cambia a true si quieres usar WhatsApp
#define ENABLE_SHEETS     true

// ═══════════════════════════════════════════════════════════
// INTERVALOS DE TIEMPO (en milisegundos)
// ═══════════════════════════════════════════════════════════
const unsigned long PUBLISH_INTERVAL = 10000;   // Envío a Adafruit/Sheets: 10 seg
const unsigned long PING_INTERVAL = 30000;      // Ping MQTT: 30 seg
const unsigned long ALERT_INTERVAL = 300000;    // Alertas repetidas: 5 min

// ═══════════════════════════════════════════════════════════
// VARIABLES GLOBALES
// ═══════════════════════════════════════════════════════════

// PMS5003 conectado en D4
SoftwareSerial pmsSerial(D4, -1);

struct pmsData {
  uint16_t pm1_0;
  uint16_t pm2_5;
  uint16_t pm10;
};

// Cliente MQTT para Adafruit IO
WiFiClient wifiClient;
Adafruit_MQTT_Client mqtt(&wifiClient, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Feeds de Adafruit IO
Adafruit_MQTT_Publish pm1_feed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/pm1");
Adafruit_MQTT_Publish pm25_feed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/pm25");
Adafruit_MQTT_Publish pm10_feed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/pm10");

// Control de tiempos
unsigned long lastPublish = 0;
unsigned long lastPing = 0;
unsigned long lastAlert = 0;

// Datos del sensor
pmsData lastValidData = {0, 0, 0};
bool hasValidData = false;
bool firstPublish = true;

// Control de alertas
int lastAlertLevel = -1;

// Niveles de calidad del aire
enum AirQuality {
  OPTIMA = 0,
  MODERADA = 1,
  MALA = 2,
  MUY_DANINA = 3,
  PELIGROSA = 4
};

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  pmsSerial.begin(9600);
  
  delay(1000);
  Serial.println("\n╔════════════════════════════════════════════╗");
  Serial.println("║   SISTEMA DE MONITOREO DE CALIDAD DEL AIRE ║");
  Serial.println("║   Adafruit IO + Telegram + WhatsApp + Sheets║");
  Serial.println("╚════════════════════════════════════════════╝");
  
  // Mostrar servicios activos
  Serial.println("\nServicios activos:");
  if (ENABLE_ADAFRUIT) Serial.println("  ✓ Adafruit IO");
  if (ENABLE_TELEGRAM) Serial.println("  ✓ Telegram");
  if (ENABLE_WHATSAPP) Serial.println("  ✓ WhatsApp");
  if (ENABLE_SHEETS)   Serial.println("  ✓ Google Sheets");
  
  // Conectar WiFi
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  Serial.print("\nConectando a WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n✓ WiFi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  Serial.println("\nEsperando estabilización del sensor...");
  delay(3000);
  
  // Mensaje de inicio
  if (ENABLE_TELEGRAM) {
    sendTelegram("🚀 *Sistema de monitoreo iniciado*\n\nSensor de calidad del aire activo.");
  }
  if (ENABLE_WHATSAPP) {
    sendWhatsApp("🚀 Sistema de monitoreo iniciado. Sensor activo.");
  }
  
  Serial.println("\n✓ Sistema listo");
  Serial.println("→ Monitoreando calidad del aire...\n");
}

// ═══════════════════════════════════════════════════════════
// LOOP PRINCIPAL
// ═══════════════════════════════════════════════════════════
void loop() {
  // Mantener conexión MQTT
  if (ENABLE_ADAFRUIT) {
    MQTT_connect();
  }
  
  // Leer sensor
  pmsData data;
  if (readPMSdata(&data)) {
    lastValidData = data;
    hasValidData = true;
    
    Serial.println("┌──────────────────────────────────┐");
    Serial.print("│ PM 1.0: "); Serial.print(data.pm1_0); Serial.println(" µg/m³");
    Serial.print("│ PM 2.5: "); Serial.print(data.pm2_5); Serial.println(" µg/m³");
    Serial.print("│ PM 10:  "); Serial.print(data.pm10);  Serial.println(" µg/m³");
    Serial.println("└──────────────────────────────────┘");
    
    // Evaluar calidad y enviar alertas si es necesario
    evaluateAirQuality(data);
  }
  
  // Publicar datos a Adafruit IO y Google Sheets
  if (hasValidData && (firstPublish || (millis() - lastPublish >= PUBLISH_INTERVAL))) {
    
    if (firstPublish) {
      Serial.println("\n🚀 PRIMERA PUBLICACIÓN\n");
    }
    
    // Enviar a Adafruit IO
    if (ENABLE_ADAFRUIT) {
      sendToAdafruit(lastValidData);
    }
    
    // Enviar a Google Sheets
    if (ENABLE_SHEETS) {
      sendToGoogleSheets(lastValidData);
    }
    
    firstPublish = false;
    lastPublish = millis();
  }
  
  // Ping MQTT
  if (ENABLE_ADAFRUIT && (millis() - lastPing >= PING_INTERVAL)) {
    if (!mqtt.ping()) {
      Serial.println("⟳ Reconectando MQTT...");
      mqtt.disconnect();
    }
    lastPing = millis();
  }
  
  if (ENABLE_ADAFRUIT) {
    mqtt.processPackets(100);
  }
  
  delay(500);
}

// ═══════════════════════════════════════════════════════════
// FUNCIONES DE ENVÍO DE DATOS
// ═══════════════════════════════════════════════════════════

// --- ADAFRUIT IO ---
void sendToAdafruit(pmsData data) {
  Serial.println("\n📤 Enviando a Adafruit IO...");
  
  MQTT_connect();
  
  if (!mqtt.connected()) {
    Serial.println("✗ Sin conexión MQTT");
    return;
  }
  
  bool success = true;
  
  Serial.print("   PM1.0... ");
  if (pm1_feed.publish(data.pm1_0)) Serial.println("✓");
  else { Serial.println("✗"); success = false; }
  delay(500);
  
  Serial.print("   PM2.5... ");
  if (pm25_feed.publish(data.pm2_5)) Serial.println("✓");
  else { Serial.println("✗"); success = false; }
  delay(500);
  
  Serial.print("   PM10... ");
  if (pm10_feed.publish(data.pm10)) Serial.println("✓");
  else { Serial.println("✗"); success = false; }
  
  if (success) Serial.println("✓ Adafruit IO: Datos enviados\n");
}

void MQTT_connect() {
  int8_t ret;
  
  if (mqtt.connected()) return;
  
  Serial.print("Conectando a MQTT... ");
  
  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) {
    Serial.println(mqtt.connectErrorString(ret));
    mqtt.disconnect();
    delay(5000);
    retries--;
    if (retries == 0) {
      Serial.println("Reiniciando...");
      ESP.restart();
    }
  }
  
  Serial.println("✓ Conectado!");
}

// --- GOOGLE SHEETS ---
void sendToGoogleSheets(pmsData data) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("✗ Sin conexión WiFi");
    return;
  }
  
  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient https;
  
  Serial.println("📊 Enviando a Google Sheets...");
  
  https.begin(client, GOOGLE_SCRIPT_URL);
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String postData = "pm1=" + String(data.pm1_0);
  postData += "&pm25=" + String(data.pm2_5);
  postData += "&pm10=" + String(data.pm10);
  
  int httpCode = https.POST(postData);
  
  if (httpCode == 200 || httpCode == 302) {
    Serial.println("✓ Google Sheets: Datos enviados\n");
  } else {
    Serial.print("✗ Error: ");
    Serial.println(httpCode);
  }
  
  https.end();
}

// --- TELEGRAM ---
bool sendTelegram(String message) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  
  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  String payload = "{\"chat_id\":\"" + String(CHAT_ID) + "\",";
  payload += "\"text\":\"" + message + "\",";
  payload += "\"parse_mode\":\"Markdown\"}";
  
  int httpCode = http.POST(payload);
  http.end();
  
  if (httpCode == 200) {
    Serial.println("✓ Telegram: Mensaje enviado");
    return true;
  }
  return false;
}

// --- WHATSAPP ---
bool sendWhatsApp(String message) {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  message.replace(" ", "%20");
  message.replace("\n", "%0A");
  message.replace("*", "%2A");
  message.replace(":", "%3A");
  message.replace("(", "%28");
  message.replace(")", "%29");
  message.replace("•", "%E2%80%A2");
  
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + 
               String(PHONE_NUMBER) + 
               "&text=" + message + 
               "&apikey=" + String(CALLMEBOT_KEY);
  
  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  http.begin(client, url);
  
  int httpCode = http.GET();
  http.end();
  
  if (httpCode > 0) {
    Serial.println("✓ WhatsApp: Mensaje enviado");
    return true;
  }
  return false;
}

// ═══════════════════════════════════════════════════════════
// FUNCIONES DE CALIDAD DEL AIRE Y ALERTAS
// ═══════════════════════════════════════════════════════════

void evaluateAirQuality(pmsData data) {
  int currentLevel = getAirQualityLevel(data.pm2_5);
  String levelName = getAirQualityName(currentLevel);
  
  Serial.print("Estado: ");
  Serial.println(levelName);
  
  bool shouldAlert = false;
  
  // Alerta si cambió de nivel o pasaron 5 min en nivel malo
  if (currentLevel != lastAlertLevel) {
    shouldAlert = true;
    Serial.println("→ Cambio de nivel detectado");
  } else if (currentLevel >= MALA && (millis() - lastAlert >= ALERT_INTERVAL)) {
    shouldAlert = true;
    Serial.println("→ Recordatorio de calidad del aire");
  }
  
  if (shouldAlert) {
    String message = buildAlertMessage(currentLevel, data);
    
    Serial.println("\n📱 Enviando alertas...");
    
    if (ENABLE_TELEGRAM) sendTelegram(message);
    if (ENABLE_WHATSAPP) sendWhatsApp(message);
    
    lastAlert = millis();
    lastAlertLevel = currentLevel;
  }
  
  Serial.println();
}

int getAirQualityLevel(int pm25) {
  if (pm25 < 12) return OPTIMA;
  else if (pm25 <= 35) return MODERADA;
  else if (pm25 <= 55) return MALA;
  else if (pm25 <= 150) return MUY_DANINA;
  else return PELIGROSA;
}

String getAirQualityName(int level) {
  switch(level) {
    case OPTIMA: return "🌿 ÓPTIMA";
    case MODERADA: return "🙂 MODERADA";
    case MALA: return "🚨 MALA";
    case MUY_DANINA: return "⚠️ MUY DAÑINA";
    case PELIGROSA: return "☠️ PELIGROSA";
    default: return "DESCONOCIDA";
  }
}

String buildAlertMessage(int level, pmsData data) {
  String msg = "";
  
  switch(level) {
    case OPTIMA:
      msg = "🌿 *Calidad de aire ÓPTIMA*\n\n";
      msg += "PM2.5: " + String(data.pm2_5) + " µg/m³\n";
      msg += "PM10: " + String(data.pm10) + " µg/m³\n\n";
      msg += "✔ El ambiente es seguro y saludable.\n";
      msg += "¡Respira tranquilo! 😌";
      break;
      
    case MODERADA:
      msg = "🙂 *Calidad de aire MODERADA*\n\n";
      msg += "PM2.5: " + String(data.pm2_5) + " µg/m³\n";
      msg += "PM10: " + String(data.pm10) + " µg/m³\n\n";
      msg += "⚠ Personas sensibles podrían sentir leves molestias.\n";
      msg += "Recomendación: ventilar o evitar actividades intensas.";
      break;
      
    case MALA:
      msg = "🚨 *Calidad de aire MALA*\n\n";
      msg += "PM2.5: " + String(data.pm2_5) + " µg/m³\n";
      msg += "PM10: " + String(data.pm10) + " µg/m³\n\n";
      msg += "❗ Podría provocar irritación y problemas respiratorios.\n\n";
      msg += "Recomendaciones:\n";
      msg += "• Reduce actividad física\n";
      msg += "• Usa mascarilla en exteriores\n";
      msg += "• Ventila el ambiente";
      break;
      
    case MUY_DANINA:
      msg = "⚠️ *ALERTA ROJA: Aire MUY DAÑINO*\n\n";
      msg += "PM2.5: " + String(data.pm2_5) + " µg/m³\n";
      msg += "PM10: " + String(data.pm10) + " µg/m³\n\n";
      msg += "❗ Riesgo alto para todas las personas.\n\n";
      msg += "Recomendaciones:\n";
      msg += "• Evita salir\n";
      msg += "• Usa KN95\n";
      msg += "• Mantén ventanas cerradas";
      break;
      
    case PELIGROSA:
      msg = "☠️ *ALERTA EXTREMA: Aire PELIGROSO*\n\n";
      msg += "PM2.5: " + String(data.pm2_5) + " µg/m³\n";
      msg += "PM10: " + String(data.pm10) + " µg/m³\n\n";
      msg += "⚔️ Riesgo severo para la salud.\n\n";
      msg += "Acciones inmediatas:\n";
      msg += "• NO salir al exterior\n";
      msg += "• Puertas y ventanas cerradas\n";
      msg += "• Usa KN95 si es inevitable salir";
      break;
  }
  
  return msg;
}

// ═══════════════════════════════════════════════════════════
// LECTURA DEL SENSOR PMS5003
// ═══════════════════════════════════════════════════════════

bool readPMSdata(pmsData *data) {
  while (pmsSerial.available() > 32) {
    pmsSerial.read();
  }
  
  if (!pmsSerial.available()) return false;
  if (pmsSerial.read() != 0x42) return false;
  
  uint32_t timeout = millis();
  while (!pmsSerial.available()) {
    if (millis() - timeout > 100) return false;
  }
  
  if (pmsSerial.read() != 0x4d) return false;
  
  timeout = millis();
  while (pmsSerial.available() < 2) {
    if (millis() - timeout > 100) return false;
  }
  
  uint16_t frameLen = pmsSerial.read() << 8;
  frameLen |= pmsSerial.read();
  
  if (frameLen != 28) return false;
  
  timeout = millis();
  while (pmsSerial.available() < 28) {
    if (millis() - timeout > 1000) return false;
  }
  
  uint8_t buffer[28];
  uint16_t sum = 0x42 + 0x4d + (frameLen >> 8) + (frameLen & 0xFF);
  
  for (int i = 0; i < 28; i++) {
    buffer[i] = pmsSerial.read();
    if (i < 26) sum += buffer[i];
  }
  
  uint16_t checksum = (buffer[26] << 8) | buffer[27];
  if (sum != checksum) return false;
  
  data->pm1_0 = (buffer[4] << 8) | buffer[5];
  data->pm2_5 = (buffer[6] << 8) | buffer[7];
  data->pm10  = (buffer[8] << 8) | buffer[9];
  
  return true;
}
