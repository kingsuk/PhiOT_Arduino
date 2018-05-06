
#include "Arduino.h"
#include "PhiOT.h"
#include <ESP8266WiFi.h>

#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#include <thirdparty/ArduinoJson/ArduinoJson.h>


int indicatingLed=LED_BUILTIN;
bool NetworkConnected = false;

//Wifi Clients Configs
IPAddress local_IP(192, 168, 4, 22);
IPAddress gateway(192, 168, 4, 9);
IPAddress subnet(255, 255, 255, 0);

//No of seconds system will be busy trying to figure out if it is connected to network or not.
int maxTimeOut = 20;
int maxNoOfWifiToScan = 4;

//Name of the access point to be created.
const char *AccessPointName = "PhiOTConnect";

ESP8266WebServer server(80);

//Mqtt configs
const char* mqtt_server = "phiot.phibasis.com";//Your mqtt server goes here

String token = "";
String publishTopic = "";
String subscribeTopic = "";

WiFiClient espClient;

PhiOT::PhiOT(String token)
{
  _token = token;
  publishTopic = "outTopic/"+token;
  subscribeTopic = "inTopic/"+token;

  this->_state = MQTT_DISCONNECTED;
    setClient(espClient);
    this->stream = NULL;
  
  pinMode(indicatingLed, OUTPUT);
}

void PhiOT::Initialize() 
{
    

  Serial.println("Checking if connection already availabe.");
  PhiOT::CheckingConnectionStatusWithDelay();
  
}

void PhiOT::CheckingConnectionStatusWithDelay()
{
  Serial.println("Checking Connection Status With Delay.");
  int count = 0;
  while (WiFi.status() != WL_CONNECTED) 
  {
    count++;
    delay(1000);
    Serial.print(".");

    if (count == maxTimeOut) 
    {
      Serial.println("");
      Serial.println("Could not connect to wifi,Ending connection loop.");
      Serial.println("Starting up access point.");
      NetworkConnected = false;
      PhiOT::setAccessPoint();
      return;
    }
  }

  NetworkConnected = true;
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Connection Successful");

  PhiOT::SwitchOffAccessPoint();

  //initializing mqtt
  PhiOT::mqttInit();
}

void PhiOT::SwitchOffAccessPoint()
{
  Serial.println("Switching off Access Point.");
  WiFi.softAPdisconnect(false);
  WiFi.enableAP(false);
}

void PhiOT::setAccessPoint()
{
  Serial.print("Configuring access point...");
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AccessPointName);
  
  Serial.println("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  PhiOT::ServerRouters();

  server.begin();
  Serial.println("HTTP server started");

  PhiOT::indicatorForAccessPoint();
}

void PhiOT::ServerRouters()
{
  server.on("/wificonnect", [this]() {

    server.send(200, "text/plain", "Network credentials received. Check MCU led for status.");
    
    PhiOT::WifiConnectionSetup(server.arg("ssid"), server.arg("password"));

  });
  //server.on("/checkstatus", checkConnectionStatus);
  server.on("/wifiscan", [this](){

    Serial.println("scan start");

    int n = WiFi.scanNetworks();
    
    if (n == 0)
       server.send(200, "text/plain", "No networks found!");
    else
    {
        Serial.print(n);
        Serial.println(" networks found");

        StaticJsonBuffer<400> jsonBuffer;
        JsonArray& array = jsonBuffer.createArray();
        
        for (int i = 0; i < n; ++i)
        {
            JsonObject& root = jsonBuffer.createObject();

            root["ssid"] = WiFi.SSID(i);
            root["rssi"] = WiFi.RSSI(i);
            root["encryptionType"] = WiFi.encryptionType(i);
            
            array.add(root);
            if(i==maxNoOfWifiToScan)
                break;
        }

        char wifiInfo[200];
        array.printTo(wifiInfo, sizeof(wifiInfo));
        server.send(200, "text/plain", wifiInfo);
    }
    
  });
}

void PhiOT::WifiConnectionSetup(String ssid, String password) 
{
  Serial.println("Connecting to ");
  Serial.println(ssid);
  Serial.println(password);

  WiFi.begin(ssid.c_str(), password.c_str());

  PhiOT::CheckingConnectionStatusWithDelay();
}

void PhiOT::mqttInit()
{
  PhiOT::setServer(mqtt_server, 1883);
  //the callback function
  PhiOT::setCallback([this](char* topic, byte* payload, unsigned int length) {
  	  	PhiOT::Phicallback(topic, payload, length);
        PhiOT::lightIndicatorConfirmation();
	});
}

void PhiOT::Phicallback(char* topic, byte* payload, unsigned int length) 
{
  char json[length];
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    json[i] = (char)payload[i];
  }
  Serial.println();

  StaticJsonBuffer<2000> jsonBuffer;
 
  JsonArray& totalArray = jsonBuffer.parseArray(json);
  if (!totalArray.success()) {
    Serial.println("parseObject() failed");
    return;
  }
  
  JsonObject& totalObj = totalArray[0];
  String header = totalObj["header"];

  if(header == "data")
  {
    JsonArray& dataArr = totalObj["data"];
    for(int i=0;i<dataArr.size(); i++)
    {
        JsonObject& data = dataArr[i];
        pinMode(data["pin"], OUTPUT);
        digitalWrite(data["pin"], data["value"]);
    }
  }
  else if(header == "station")
  {
      PhiOT::setAccessPoint();
      NetworkConnected = false;
  }

}

void PhiOT::reconnect() 
{
  int count = 0;
  while (!PhiOT::connected()) 
  {
    Serial.println("Attempting MQTT connection... With Delay");
    String clientId = _token;
    
    //checking if connection successful
    if (PhiOT::connect(clientId.c_str())) 
    {

      Serial.println("Connected and publishing status to server");
      PhiOT::publish(publishTopic.c_str(), "conn");
      PhiOT::subscribe(subscribeTopic.c_str());
      PhiOT::lightIndicatorConfirmation();
    }
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(PhiOT::state());
      Serial.println(" try again in 5 seconds");
      count++;
      // Wait 5 seconds before retrying
      delay(5000);
      if (count == maxTimeOut/5) 
      {
        Serial.println("");
        Serial.println("Could not connect to Mqtt server,Ending Mqtt loop.");
        Serial.println("Starting up access point.");
        PhiOT::setAccessPoint();
        return;
      }
    }
  }

  

}

void PhiOT::phiLoop()
{
  //delay(1000);
  server.handleClient();

  if(NetworkConnected)
  {
    if (!PhiOT::connected()) 
    {
      Serial.println("Mqtt connection initializing..");
      PhiOT::reconnect();
    }
    PhiOT::loop();
  }

	
  
}

void PhiOT::lightIndicatorConfirmation()
{
   for(int i=0;i<=5;i++)
    {
      if(i%2==0)
      {
        digitalWrite(indicatingLed,0);
      }
      else
      {
        digitalWrite(indicatingLed,1);
      }
      delay(200);
    } 
      
}

void PhiOT::indicatorForAccessPoint()
{
  digitalWrite(indicatingLed,0);
}























//////////////////////////////      MQTT Codes      ////////////////////////////////////////////////////


boolean PhiOT::connect(const char *id) {
    return connect(id,NULL,NULL,0,0,0,0);
}

boolean PhiOT::connect(const char *id, const char *user, const char *pass, const char* willTopic, uint8_t willQos, boolean willRetain, const char* willMessage) {
    if (!connected()) {
        int result = 0;

        if (domain != NULL) {
            result = _client->connect(this->domain, this->port);
        } else {
            result = _client->connect(this->ip, this->port);
        }
        if (result == 1) {
            nextMsgId = 1;
            // Leave room in the buffer for header and variable length field
            uint16_t length = 5;
            unsigned int j;

#if MQTT_VERSION == MQTT_VERSION_3_1
            uint8_t d[9] = {0x00,0x06,'M','Q','I','s','d','p', MQTT_VERSION};
#define MQTT_HEADER_VERSION_LENGTH 9
#elif MQTT_VERSION == MQTT_VERSION_3_1_1
            uint8_t d[7] = {0x00,0x04,'M','Q','T','T',MQTT_VERSION};
#define MQTT_HEADER_VERSION_LENGTH 7
#endif
            for (j = 0;j<MQTT_HEADER_VERSION_LENGTH;j++) {
                buffer[length++] = d[j];
            }

            uint8_t v;
            if (willTopic) {
                v = 0x06|(willQos<<3)|(willRetain<<5);
            } else {
                v = 0x02;
            }

            if(user != NULL) {
                v = v|0x80;

                if(pass != NULL) {
                    v = v|(0x80>>1);
                }
            }

            buffer[length++] = v;

            buffer[length++] = ((MQTT_KEEPALIVE) >> 8);
            buffer[length++] = ((MQTT_KEEPALIVE) & 0xFF);
            length = writeString(id,buffer,length);
            if (willTopic) {
                length = writeString(willTopic,buffer,length);
                length = writeString(willMessage,buffer,length);
            }

            if(user != NULL) {
                length = writeString(user,buffer,length);
                if(pass != NULL) {
                    length = writeString(pass,buffer,length);
                }
            }

            write(MQTTCONNECT,buffer,length-5);

            lastInActivity = lastOutActivity = millis();

            while (!_client->available()) {
                unsigned long t = millis();
                if (t-lastInActivity >= ((int32_t) MQTT_SOCKET_TIMEOUT*1000UL)) {
                    _state = MQTT_CONNECTION_TIMEOUT;
                    _client->stop();
                    return false;
                }
            }
            uint8_t llen;
            uint16_t len = readPacket(&llen);

            if (len == 4) {
                if (buffer[3] == 0) {
                    lastInActivity = millis();
                    pingOutstanding = false;
                    _state = MQTT_CONNECTED;
                    return true;
                } else {
                    _state = buffer[3];
                }
            }
            _client->stop();
        } else {
            _state = MQTT_CONNECT_FAILED;
        }
        return false;
    }
    return true;
}

// reads a byte into result
boolean PhiOT::readByte(uint8_t * result) {
   uint32_t previousMillis = millis();
   while(!_client->available()) {
     uint32_t currentMillis = millis();
     if(currentMillis - previousMillis >= ((int32_t) MQTT_SOCKET_TIMEOUT * 1000)){
       return false;
     }
   }
   *result = _client->read();
   return true;
}

// reads a byte into result[*index] and increments index
boolean PhiOT::readByte(uint8_t * result, uint16_t * index){
  uint16_t current_index = *index;
  uint8_t * write_address = &(result[current_index]);
  if(readByte(write_address)){
    *index = current_index + 1;
    return true;
  }
  return false;
}

uint16_t PhiOT::readPacket(uint8_t* lengthLength) {
    uint16_t len = 0;
    if(!readByte(buffer, &len)) return 0;
    bool isPublish = (buffer[0]&0xF0) == MQTTPUBLISH;
    uint32_t multiplier = 1;
    uint16_t length = 0;
    uint8_t digit = 0;
    uint16_t skip = 0;
    uint8_t start = 0;

    do {
        if(!readByte(&digit)) return 0;
        buffer[len++] = digit;
        length += (digit & 127) * multiplier;
        multiplier *= 128;
    } while ((digit & 128) != 0);
    *lengthLength = len-1;

    if (isPublish) {
        // Read in topic length to calculate bytes to skip over for Stream writing
        if(!readByte(buffer, &len)) return 0;
        if(!readByte(buffer, &len)) return 0;
        skip = (buffer[*lengthLength+1]<<8)+buffer[*lengthLength+2];
        start = 2;
        if (buffer[0]&MQTTQOS1) {
            // skip message id
            skip += 2;
        }
    }

    for (uint16_t i = start;i<length;i++) {
        if(!readByte(&digit)) return 0;
        if (this->stream) {
            if (isPublish && len-*lengthLength-2>skip) {
                this->stream->write(digit);
            }
        }
        if (len < MQTT_MAX_PACKET_SIZE) {
            buffer[len] = digit;
        }
        len++;
    }

    if (!this->stream && len > MQTT_MAX_PACKET_SIZE) {
        len = 0; // This will cause the packet to be ignored.
    }

    return len;
}

boolean PhiOT::loop() {
    if (connected()) {
        unsigned long t = millis();
        if ((t - lastInActivity > MQTT_KEEPALIVE*1000UL) || (t - lastOutActivity > MQTT_KEEPALIVE*1000UL)) {
            if (pingOutstanding) {
                this->_state = MQTT_CONNECTION_TIMEOUT;
                _client->stop();
                return false;
            } else {
                buffer[0] = MQTTPINGREQ;
                buffer[1] = 0;
                _client->write(buffer,2);
                lastOutActivity = t;
                lastInActivity = t;
                pingOutstanding = true;
            }
        }
        if (_client->available()) {
            uint8_t llen;
            uint16_t len = readPacket(&llen);
            uint16_t msgId = 0;
            uint8_t *payload;
            if (len > 0) {
                lastInActivity = t;
                uint8_t type = buffer[0]&0xF0;
                if (type == MQTTPUBLISH) {
                    if (callback) {
                        uint16_t tl = (buffer[llen+1]<<8)+buffer[llen+2];
                        char topic[tl+1];
                        for (uint16_t i=0;i<tl;i++) {
                            topic[i] = buffer[llen+3+i];
                        }
                        topic[tl] = 0;
                        // msgId only present for QOS>0
                        if ((buffer[0]&0x06) == MQTTQOS1) {
                            msgId = (buffer[llen+3+tl]<<8)+buffer[llen+3+tl+1];
                            payload = buffer+llen+3+tl+2;
                            callback(topic,payload,len-llen-3-tl-2);

                            buffer[0] = MQTTPUBACK;
                            buffer[1] = 2;
                            buffer[2] = (msgId >> 8);
                            buffer[3] = (msgId & 0xFF);
                            _client->write(buffer,4);
                            lastOutActivity = t;

                        } else {
                            payload = buffer+llen+3+tl;
                            callback(topic,payload,len-llen-3-tl);
                        }
                    }
                } else if (type == MQTTPINGREQ) {
                    buffer[0] = MQTTPINGRESP;
                    buffer[1] = 0;
                    _client->write(buffer,2);
                } else if (type == MQTTPINGRESP) {
                    pingOutstanding = false;
                }
            }
        }
        return true;
    }
    return false;
}

boolean PhiOT::publish(const char* topic, const char* payload) {
    return publish(topic,(const uint8_t*)payload,strlen(payload),false);
}

boolean PhiOT::publish(const char* topic, const uint8_t* payload, unsigned int plength, boolean retained) {
    if (connected()) {
        if (MQTT_MAX_PACKET_SIZE < 5 + 2+strlen(topic) + plength) {
            // Too long
            return false;
        }
        // Leave room in the buffer for header and variable length field
        uint16_t length = 5;
        length = writeString(topic,buffer,length);
        uint16_t i;
        for (i=0;i<plength;i++) {
            buffer[length++] = payload[i];
        }
        uint8_t header = MQTTPUBLISH;
        if (retained) {
            header |= 1;
        }
        return write(header,buffer,length-5);
    }
    return false;
}


boolean PhiOT::write(uint8_t header, uint8_t* buf, uint16_t length) {
    uint8_t lenBuf[4];
    uint8_t llen = 0;
    uint8_t digit;
    uint8_t pos = 0;
    uint16_t rc;
    uint16_t len = length;
    do {
        digit = len % 128;
        len = len / 128;
        if (len > 0) {
            digit |= 0x80;
        }
        lenBuf[pos++] = digit;
        llen++;
    } while(len>0);

    buf[4-llen] = header;
    for (int i=0;i<llen;i++) {
        buf[5-llen+i] = lenBuf[i];
    }

#ifdef MQTT_MAX_TRANSFER_SIZE
    uint8_t* writeBuf = buf+(4-llen);
    uint16_t bytesRemaining = length+1+llen;  //Match the length type
    uint8_t bytesToWrite;
    boolean result = true;
    while((bytesRemaining > 0) && result) {
        bytesToWrite = (bytesRemaining > MQTT_MAX_TRANSFER_SIZE)?MQTT_MAX_TRANSFER_SIZE:bytesRemaining;
        rc = _client->write(writeBuf,bytesToWrite);
        result = (rc == bytesToWrite);
        bytesRemaining -= rc;
        writeBuf += rc;
    }
    return result;
#else
    rc = _client->write(buf+(4-llen),length+1+llen);
    lastOutActivity = millis();
    return (rc == 1+llen+length);
#endif
}

boolean PhiOT::subscribe(const char* topic) {
    return subscribe(topic, 0);
}

boolean PhiOT::subscribe(const char* topic, uint8_t qos) {
    if (qos < 0 || qos > 1) {
        return false;
    }
    if (MQTT_MAX_PACKET_SIZE < 9 + strlen(topic)) {
        // Too long
        return false;
    }
    if (connected()) {
        // Leave room in the buffer for header and variable length field
        uint16_t length = 5;
        nextMsgId++;
        if (nextMsgId == 0) {
            nextMsgId = 1;
        }
        buffer[length++] = (nextMsgId >> 8);
        buffer[length++] = (nextMsgId & 0xFF);
        length = writeString((char*)topic, buffer,length);
        buffer[length++] = qos;
        return write(MQTTSUBSCRIBE|MQTTQOS1,buffer,length-5);
    }
    return false;
}

boolean PhiOT::unsubscribe(const char* topic) {
    if (MQTT_MAX_PACKET_SIZE < 9 + strlen(topic)) {
        // Too long
        return false;
    }
    if (connected()) {
        uint16_t length = 5;
        nextMsgId++;
        if (nextMsgId == 0) {
            nextMsgId = 1;
        }
        buffer[length++] = (nextMsgId >> 8);
        buffer[length++] = (nextMsgId & 0xFF);
        length = writeString(topic, buffer,length);
        return write(MQTTUNSUBSCRIBE|MQTTQOS1,buffer,length-5);
    }
    return false;
}

void PhiOT::disconnect() {
    buffer[0] = MQTTDISCONNECT;
    buffer[1] = 0;
    _client->write(buffer,2);
    _state = MQTT_DISCONNECTED;
    _client->stop();
    lastInActivity = lastOutActivity = millis();
}

uint16_t PhiOT::writeString(const char* string, uint8_t* buf, uint16_t pos) {
    const char* idp = string;
    uint16_t i = 0;
    pos += 2;
    while (*idp) {
        buf[pos++] = *idp++;
        i++;
    }
    buf[pos-i-2] = (i >> 8);
    buf[pos-i-1] = (i & 0xFF);
    return pos;
}


boolean PhiOT::connected() {
    boolean rc;
    if (_client == NULL ) {
        rc = false;
    } else {
        rc = (int)_client->connected();
        if (!rc) {
            if (this->_state == MQTT_CONNECTED) {
                this->_state = MQTT_CONNECTION_LOST;
                _client->flush();
                _client->stop();
            }
        }
    }
    return rc;
}

PhiOT& PhiOT::setServer(const char * domain, uint16_t port) {
    this->domain = domain;
    this->port = port;
    return *this;
}

PhiOT& PhiOT::setCallback(MQTT_CALLBACK_SIGNATURE) {
    this->callback = callback;
    return *this;
}

PhiOT& PhiOT::setClient(Client& client){
    this->_client = &client;
    return *this;
}

int PhiOT::state() {
    return this->_state;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
