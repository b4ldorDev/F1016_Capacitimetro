#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>  // Para log()
#include <ArduinoJson.h>

// Configuración OLED
#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C  // Dirección típica para pantallas SSD1306

// Configuración de pines para ESP32 estándar
#define OUT_PIN 25
#define IN_PIN 26
// En ESP32 estándar usamos pines I2C por defecto, no necesitamos definirlos
// SDA = GPIO 21, SCL = GPIO 22 por defecto

// Constantes para medición de capacitancia
const float IN_STRAY_CAP_TO_GND = 24.48;
const float IN_CAP_TO_GND = IN_STRAY_CAP_TO_GND;
const float R_PULLUP = 34.8;  
const int MAX_ADC_VALUE = 4095; // ESP32 tiene ADC de 12 bits (0-4095)

// Datos de WiFi (reemplaza con tus datos)
const char* ssid = "TU_SSID";
const char* password = "TU_PASSWORD";

// Variables globales
float capacitance = 0.0;
String capacitanceUnit = "pF";
String scaleMode = "";
bool isLowScale = true;

// Objeto para la pantalla OLED
Adafruit_SSD1306 display(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, OLED_RESET);

// Crear objeto de servidor AsyncWebServer en el puerto 80
AsyncWebServer server(80);
AsyncEventSource events("/events");

// Array histórico para almacenar datos para la gráfica
#define MAX_DATA_POINTS 100
float dataHistory[MAX_DATA_POINTS];
int dataIndex = 0;

void setup() {
  Serial.begin(115200);
  
  // Inicializar SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("Error al montar SPIFFS");
    return;
  }
  
  // Inicializar pines
  pinMode(OUT_PIN, OUTPUT);
  pinMode(IN_PIN, OUTPUT);
  
  // Inicializar la pantalla OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Error al inicializar la pantalla SSD1306");
    while (true); // Si no se encuentra la pantalla, detener
  }
  
  // Mostrar mensaje de bienvenida
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Capacimetro ESP32"));
  display.println(F("----------------"));
  
  // Verificar voltaje de alimentación
  float voltage = analogRead(34) * (3.3 / 4095.0) * 2; // Suponiendo un divisor de voltaje
  display.setCursor(0, 16);
  display.print(F("Voltaje: "));
  display.print(voltage, 1);
  display.println(F("V"));
  
  display.setCursor(0, 24);
  display.println(F("Iniciando WiFi..."));
  display.display();
  
  // Conectarse a WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  
  // Mostrar dirección IP
  Serial.println(WiFi.localIP());
  display.setCursor(0, 32);
  display.println("Conectado a WiFi!");
  display.setCursor(0, 40);
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
  
  // Inicializar array de datos
  for(int i = 0; i < MAX_DATA_POINTS; i++) {
    dataHistory[i] = 0;
  }
  
  // Configurar CORS para permitir acceso desde Netlify
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  
  // Rutas del servidor web
  
  // Ruta para la página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  
  // Ruta para obtener datos actuales en formato JSON
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = getJSONData();
    request->send(200, "application/json", json);
  });
  
  // Ruta para archivos estáticos (CSS, JS)
  server.serveStatic("/", SPIFFS, "/");
  
  // Configurar Server-Sent Events
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Cliente reconectado! Último mensaje recibido: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);
  
  // Iniciar servidor
  server.begin();
  
  // Mostrar mensaje de inicio completo
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Listo para medir!"));
  display.setCursor(0, 16);
  display.println(F("Servidor web activo"));
  display.setCursor(0, 32);
  display.print(F("IP: "));
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
}

void loop() {
  // Medir capacitancia
  measureCapacitance();
  
  // Actualizar la pantalla OLED
  updateOLEDDisplay();
  
  // Guardar datos en el historial
  dataHistory[dataIndex] = capacitance;
  dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;
  
  // Enviar datos por Server-Sent Events
  events.send(getJSONData().c_str(), "capacitance_data", millis());
  
  // Pequeña pausa
  delay(500);
}

void measureCapacitance() {
  // Detectar automáticamente escala baja o alta
  pinMode(IN_PIN, INPUT);
  digitalWrite(OUT_PIN, HIGH);
  int val = analogRead(IN_PIN);
  digitalWrite(OUT_PIN, LOW);
  
  // Escala baja (1pF - 1nF)
  if (val < 4000) {
    isLowScale = true;
    scaleMode = "Escala: 1pF - 1nF";
    
    pinMode(IN_PIN, OUTPUT);
    
    // Fórmula para calcular capacitancia en escala baja
    capacitance = (float)val * IN_CAP_TO_GND / (float)(MAX_ADC_VALUE - val);
    capacitanceUnit = "pF";
  } 
  // Escala alta (1nF - 100uF)
  else {
    isLowScale = false;
    scaleMode = "Escala: 1nF - 100uF";
    
    pinMode(IN_PIN, OUTPUT);
    delay(1);
    pinMode(OUT_PIN, INPUT_PULLUP);
    unsigned long u1 = micros();
    unsigned long t;
    int digVal;

    do {
      digVal = digitalRead(OUT_PIN);
      unsigned long u2 = micros();
      t = u2 > u1 ? u2 - u1 : u1 - u2;
    } while ((digVal < 1) && (t < 400000L));

    pinMode(OUT_PIN, INPUT);  
    val = analogRead(OUT_PIN);
    digitalWrite(IN_PIN, HIGH);
    int dischargeTime = (int)(t / 1000L) * 5;
    delay(dischargeTime);   
    pinMode(OUT_PIN, OUTPUT);  
    digitalWrite(OUT_PIN, LOW);
    digitalWrite(IN_PIN, LOW);

    // Fórmula para calcular capacitancia en escala alta
    capacitance = -(float)t / R_PULLUP / log(1.0 - (float)val / (float)MAX_ADC_VALUE);
    
    if (capacitance > 1000.0) {
      capacitance = capacitance / 1000.0;
      capacitanceUnit = "uF";
    } else {
      capacitanceUnit = "nF";
    }
  }
}

void updateOLEDDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(scaleMode);
  
  display.setCursor(0, 16);
  display.setTextSize(2);
  
  // Mostrar valor con 2 decimales
  if (capacitance < 10) {
    display.print(capacitance, 2);
  } else if (capacitance < 100) {
    display.print(capacitance, 1);
  } else {
    display.print((int)capacitance);
  }
  
  display.print(" ");
  display.println(capacitanceUnit);
  
  // Dibujar una barra que represente el valor
  int barHeight = min(20, (int)(capacitance / 10));
  if (capacitanceUnit == "nF") barHeight = min(20, (int)(capacitance / 3));
  if (capacitanceUnit == "uF") barHeight = min(20, (int)(capacitance * 3));
  
  display.drawRect(0, 44, ANCHO_PANTALLA, 20, SSD1306_WHITE);
  display.fillRect(2, 46, barHeight * 6, 16, SSD1306_WHITE);
  
  display.display();
}

String getJSONData() {
  StaticJsonDocument<1024> doc;
  
  // Datos actuales
  doc["capacitance"] = capacitance;
  doc["unit"] = capacitanceUnit;
  doc["scale"] = scaleMode;
  
  // Historial para gráfico
  JsonArray history = doc.createNestedArray("history");
  for (int i = 0; i < MAX_DATA_POINTS; i++) {
    int idx = (dataIndex + i) % MAX_DATA_POINTS;
    history.add(dataHistory[idx]);
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}
