#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "SPIFFS.h"
#include <Ethernet.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

#define LED             2
#define BROKER_ADDRESS "test.mosquitto.org"
#define MQTT_USER       NULL
#define MQTT_PASSWORD   NULL
#define mqtt_TopicName  ""
#define FW_VERSION      1
#define DHTPIN 2    //Definir pinos que será conectado o dht -- ??
#define DHTTYPE DHT11

// Instancia DHT
DHT dht(DHTPIN, DHTTYPE);

// Simula sensor magnético
bool sensorMag = false;

//Variável que armazena o volume de água da chuva
float Volume = 0;

// Instanciando um Servidor WEB assíncrono na porta 80
AsyncWebServer server(80);

// Configurações de wifi
String ssid = "";
String pass = "";
String ip;
String gateway;
String mac =  "ESP-"+WiFi.macAddress();
const char * ssidAP = mac.c_str();
bool wifi_success = false;

WiFiClient wifi_client;

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
String mqtt_publish_topic = "sensor/"+WiFi.macAddress()+"/out";
String mqtt_subscribe_topic = "sensor/"+WiFi.macAddress()+"/in";

// Configuração do OTA
bool HTTP_OTA = false;

// Guardará o estado do led
String ledState;

char strbuf[50];
char tempString[8];
char umiString[8];

// Valores que serão enviados ao servidor TESTE QUALQUER VALOR
void ValorTeste()
{
  float tempe = 22.5;
  float umida = 15.9;
  sprintf(strbuf, "{\"Temperatura\" : %.1f, \"Umidade\" : %.1f, \"Volume\" : %.1f}", tempe, umida, Volume);
  Serial.println(strbuf);
  client.publish("", strbuf);
}

// Valores que serão enviados ao servidor COM SENSOR
void Valores()
{
  dht.begin();
  float Temperatura = dht.readTemperature();
  float Umidade = dht.readHumidity();  //Temperatura em Celsius
  dtostrf(Temperatura, 1, 2, tempString);
  dtostrf(Umidade, 1, 2, umiString);
  sprintf(strbuf, "{\"Temperatura\" : %s, \"Umidade\" : %s, \"Volume\" : %1f}", tempString, umiString, Volume);
  Serial.println(strbuf);
  client.publish("Leitura", strbuf);
}


// Variável para controle de tempo
long lastMsg = 0;
char msg[50];

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

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("Esp32Client", MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
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
  Serial.println("MQTT RECEIVE MESSAGE");
}

void updateota(String url){
  //Update OTA
  t_httpUpdate_return ret = httpUpdate.update(wifi_client, url);
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

  if (initWiFi()){
    //updateota("https://drive.google.com/u/0/uc?id=1QdvG8ABmEXMaXKmxqU1C2_CuKqECsJ5I&export=download");
    server_config();
    client.setServer(BROKER_ADDRESS, 1883);
    client.setCallback(on_message);
  } 
  else {
    wifi_config();
  }

}

void loop() {
  long now = millis();
/*  while(sensorMag == false)
  {
    if(now - lastMsg > 90000)
    {
      lastMsg = now;
      Valores(); // publica pro mqtt temperatura, umidade e volume
      Volume = 0;
    }
  }
  Volume += 10;
  */ // Código acima para teste com sensor

// Teste com variaveis aleatorias
  wifi_client.connected();
  if(client.connected() && now - lastMsg > 10000) // Verifica conexão com broker e calcula tempo para envio da publicação
  {
    lastMsg = now;
    ValorTeste(); //Valores aleatórios sendo publicados
  }
  else
  {
    reconnect();
  }
}
