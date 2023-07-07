#define BROKER_ADDRESS    "4.tcp.ngrok.io" // "3.133.207.110"
#define BROKER_PORT       15967
#define BROKER_USE_SECURE false
#define FW_VERSION        1

//#include "general.h"
#include "aquisition.h"

void setup() {
  Serial.begin(115200);

  initSPIFFS();

  // Configurar GPIO 2 como saída
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  if (initWiFi()){
    server_config();

    client.setServer(BROKER_ADDRESS, BROKER_PORT);
    client.setCallback(on_message);
    wifi_success = true;
  } 
  else {
    wifi_config();
  }
  
  // Configurar GPIO 5 como ENTRADA e PULL-UP
  pinMode(sensorMag, INPUT_PULLUP);

}

void loop() {
  long now = millis();
  capture();

  if(client.connected() && now - lastMsg > 10000) // Verifica conexão com broker e calcula tempo para envio da publicação
  {
    lastMsg = now;
    send_data();
    reset_counters();
  }
  else
  {
    reconnect();
  }
  client.loop();
}
