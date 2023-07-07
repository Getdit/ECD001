
#include "general.h"


// Valores que serão enviados ao servidor TESTE QUALQUER VALOR
void send_data()
{
  dht.begin();
  float Temperatura = dht.readTemperature();
  float Umidade = dht.readHumidity();  //Temperatura em Celsius
  dtostrf(Temperatura, 1, 2, tempString);
  dtostrf(Umidade, 1, 2, umiString);
  sprintf(strbuf, "{\"version\" : 1, \"temp\" : %.1f, \"volum\" : %.1f \"umid\" : %.1f}", tempString, Volume, umiString);
  Serial.println(strbuf);
  client.publish(topic_out, strbuf);
}

// Aqui deve ser introduzido o código para captura dos dados
void capture()
{
  if (sensorMag == false)
  {
    Volume += 10;
  }
}

void reset_counters()
{
  Volume = 0;
}