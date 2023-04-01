#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "SPIFFS.h"

#define LED             2
#define BROKER_ADDRESS "test.mosquitto.org"
#define MQTT_USER       NULL
#define MQTT_PASSWORD   NULL

// Instanciando um Servidor WEB assíncrono na porta 80
AsyncWebServer server(80);

// Configurações de wifi
String ssid;
String pass;
String ip;
String gateway;
String mac =  "ESP-"+WiFi.macAddress();
const char * ssidAP = mac.c_str();
bool wifi_success = false;

// Caminhos que serão guardados os valores salvos permanentemente
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";

IPAddress localIP;
IPAddress localGateway;
IPAddress subnet(255,255,0,0);

// Variáves de timer
unsigned long previousMillis = 0;
const long interval = 10000;

// Configurações MQTT
EthernetClient ethClient;
PubSubClient client(ethClient);
String mqtt_publish_topic = "sensor/"+WiFi.macAddress()+"/out";
String mqtt_subscribe_topic = "sensor/"+WiFi.macAddress()+"/in";


// Guardará o stado do led
String ledState;

// Inicializar SPIFFS (sistma de arquivos do ESP)
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }
}

// Ler arquivo do SPIFFS
String readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()){
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Gravar arquivo no SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }

  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

// Inicializar Wifi
bool initWiFi() {
  if (ssid==""){
    Serial.println("Undefined SSID or IP address");
    return false;
  }

  WiFi.mode(WIFI_STA);

  if (ip!="" && gateway != ""){
    localIP.fromString(ip.c_str());
    localGateway.fromString(gateway.c_str());

    if (!WiFi.config(localIP, localGateway, subnet)){
      Serial.println("STA Failed to configure");
      return false;
    }
  }
  

  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi");

  unsigned long currentMilis = millis();
  previousMillis = currentMilis;

  while (WiFi.status() != WL_CONNECTED) {
    currentMilis = millis();
    if (currentMilis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  return true;
}

void wifi_config(){
  // Levantando o AP para possibilitar a configuração
  Serial.println("Setting AP (Access Point)");
  WiFi.softAP(ssidAP, NULL);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Servidor Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/wifimanager.html", "text/html");
  });

  server.serveStatic("/", SPIFFS, "/");

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request){
    int params = request->params();
    for (int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if (p->isPost()){
        String name = p->name();
        String value = p->value().c_str();

        if (name == "ssid"){
          ssid = value;
          writeFile(SPIFFS, ssidPath, value.c_str());
          Serial.print("SSID set to: ");
          Serial.println(ssid);

        } else if (name == "pass") {
          pass = value;
          writeFile(SPIFFS, passPath, value.c_str());
          Serial.print("Password set to: ");
          Serial.println(pass);

        } else if (name == "ip") {
          ip = value;
          writeFile(SPIFFS, ipPath, value.c_str());
          Serial.print("IP set to: ");
          Serial.println(ip);
          
        } else if (name == "gateway") {
          gateway = value;
          writeFile(SPIFFS, gatewayPath, value.c_str());
          Serial.print("Gateway set to: ");
          Serial.println(gateway);            
        }
      }
    }

    request->send(200, "text/plain", "Done. ESP will restart and then connect to your router at the ip: " + ip);
    delay(3000);
    ESP.restart();
  });

  server.begin();
}

void server_config(){
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(SPIFFS, "/index.html", "text/html", false);
    });
  server.begin();
}

void on_message(char* topic, byte* payload, unsigned int length){
  Serial.println("MQTT RECEIVE MESSAGE");
}

void setup() {
  Serial.begin(115200);

  initSPIFFS();

  // Configurar GPIO 2 como saída
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  // Carregar valores salvos no SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  gateway = readFile(SPIFFS, gatewayPath);

  wifi_success = initWiFi();

  if (wifi_success){
    server_config();

    client.setServer(BROKER_ADDRESS, 1883);
    client.setCallback(on_message);
  } else {
    wifi_config();
  }

}

void loop() {
  
}