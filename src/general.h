#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "SPIFFS.h"
#include <Ethernet.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

#define LED               2
#define DHTPIN            2                   //Definir pinos que será conectado o dht -- ??
#define DHTTYPE           DHT11

// Instancia DHT
DHT dht(DHTPIN, DHTTYPE);

// Simula sensor magnético
bool sensorMag = false;

// Instanciando um Servidor WEB assíncrono na porta 80
AsyncWebServer server(80);

// Configurações de wifi
String ssid = "";
String pass = "";
String ip;
String gateway;
String mac = "ESP-"+WiFi.macAddress();
const char * ssidAP = mac.c_str();
bool wifi_success = false;

#if BROKER_USE_SECURE
  WiFiClientSecure wifi_client;
  wifi_client.setInsecure();
#endif

#if !BROKER_USE_SECURE
  WiFiClient wifi_client;
#endif


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
PubSubClient client(wifi_client);
String str_topic_in = "sensor/"+WiFi.macAddress()+"/in";
String str_topic_out = "sensor/"+WiFi.macAddress()+"/out";

const char* topic_in = str_topic_in.c_str();
const char* topic_out = str_topic_out.c_str();

// Configuração do OTA
bool HTTP_OTA = false;

// Guardará o estado do led
String ledState;

char strbuf[50];
char tempString[8];
char umiString[8];
float Volume = 0; //Variavel volume se refere a quantidade de chuva acomulada em mm
unsigned int delayPluvi = 0; //Delay utilizado para fazer as leituras do reed switch
unsigned int intervalPluvi = 1000; //Delay utilizado para fazer as leituras do reed switch

// Variável para controle de tempo
long lastMsg = 0;
char msg[50];

// // Valores que serão enviados ao servidor
// void send_data()
// {
// }

// // Aqui deve ser introduzido o código para captura dos dados
// void capture()
// {

// }

// void reset_counters()
// {
// }

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
  // Carregar valores salvos no SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = ""; //readFile(SPIFFS, ipPath);
  gateway = ""; //readFile(SPIFFS, gatewayPath);

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

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("esp32/output");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
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
  Serial.println(topic);
  String message = (char*)payload;
  Serial.println(message);
}

void on_stream(char* topic, byte* payload, unsigned int length){
  Serial.println("MQTT STREAM");
}

void updateota(String url){
  //Update OTA
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  t_httpUpdate_return ret = httpUpdate.update(wifi_client, url, FW_VERSION);

  switch(ret) {
    case HTTP_UPDATE_FAILED:
      Serial.println("Falha ao realizar atualização OTA.");
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("Não há novas atualizações de firmware.");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("Update OTA realizado.");
      break;
  }
}
