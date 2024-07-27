#pragma once

#include "wled.h"
#include "html_simplemanager.h"

static const char s_content_enc[] PROGMEM = "Content-Encoding";

//TO BE DOCUMENTED
class UsermodSimpleManager : public Usermod {
  protected:
    bool simpleManagerON = true;
    AsyncWebServer *_serverSM;

  // the XML array size needs to be bigger that your maximum expected size. 2048 is way too big for this example
  char XML[2048];

  // just some buffer holder for char operations
  char buf[32];

  public:
    UsermodSimpleManager() {
      _serverSM = NULL;      
    }
    
    void setup() {
      //Setup server on port 80
      DEBUG_PRINTLN(F("SM: setting up server"));
      _serverSM = new AsyncWebServer(8080);

      //Define settings.xml handler (GET)
      _serverSM->on("/settings.xml", HTTP_GET, [this](AsyncWebServerRequest *request){
        DEBUG_PRINTLN("SM: /settings.xml GET request");
        this->serveFetchSettings(request);
      });
      
      //Define / handler (GET)
      _serverSM->on("/reboot", HTTP_GET, [this](AsyncWebServerRequest *request){
        DEBUG_PRINTLN("SM: /reboot GET request");
        request->send(200);
        doReboot = true;
      });

      //Define / handler (GET)
      _serverSM->on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        DEBUG_PRINTLN("SM: / GET request");
        this->serveSimpleManager(request,false);
      });

      //Define / handler (POST)
      _serverSM->on("/", HTTP_POST, [this](AsyncWebServerRequest *request){
        DEBUG_PRINTLN("SM: / POST request");
        this->serveSimpleManager(request, true);
      });
    
      //Define onNotFound handler
      _serverSM->onNotFound([](AsyncWebServerRequest *request) {
          DEBUG_PRINT("Not found: ");
          DEBUG_PRINTLN(request->url().c_str());
          request->send(404);
      });
    }
    void connected() {
      _serverSM->begin();
    }

    void loop() {

    }

    void serveSimpleManager(AsyncWebServerRequest* request, bool post)
    {

      if (post) { //Save POST request, saving settings
        //sACN/Artnet
        int t = request->arg(F("EP")).toInt();
        if (t > 0) e131Port = t;
        t = request->arg(F("EU")).toInt();
        if (t >= 0  && t <= 63999) e131Universe = t;
        t = request->arg(F("DA")).toInt();
        if (t >= 0  && t <= 510) DMXAddress = t;
        t = request->arg(F("XX")).toInt();
        if (t >= 0  && t <= 150) DMXSegmentSpacing = t;
        t = request->arg(F("DM")).toInt();
        if (t >= DMX_MODE_DISABLED && t <= DMX_MODE_PRESET) DMXMode = t;
        
        lastEditTime = millis();
        doSerializeConfig = true; //see set.cpp, need to check in case of editing led settings later

        //Network settings
        char k[3]; k[2] = 0;
        for (int i = 0; i<4; i++)
        {
          k[1] = i+48;//ascii 0,1,2,3

          k[0] = 'I'; //static IP
          staticIP[i] = request->arg(k).toInt();

          k[0] = 'G'; //gateway
          staticGateway[i] = request->arg(k).toInt();

          k[0] = 'S'; //subnet
          staticSubnet[i] = request->arg(k).toInt();
        }

        //Bus settings
        for (int i=0; i<busses.getNumBusses(); i++) {
          busConfigs[i] = new BusConfig(type, busses.getBus(i)->getPins(), start, length, colorOrder | (channelSwap<<4), request->hasArg(cv), skip, awmode, freqHz, useGlobalLedBuffer);
        }
        doInitBusses = true;
      }

      AsyncWebServerResponse *response;
      response = request->beginResponse_P(200, "text/html", PAGE_simplemanager, PAGE_simplemanager_L);
      response->addHeader(FPSTR(s_content_enc),"gzip");
      char tmp[12];
      // https://medium.com/@codebyamir/a-web-developers-guide-to-browser-caching-cc41f3b73e7c
      #ifndef WLED_DEBUG
      //this header name is misleading, "no-cache" will not disable cache,
      //it just revalidates on every load using the "If-None-Match" header with the last ETag value
      response->addHeader(F("Cache-Control"),"no-cache");
      #else
      response->addHeader(F("Cache-Control"),"no-store,max-age=0"); // prevent caching if debug build
      #endif
      sprintf_P(tmp, PSTR("%8d-%02x"), VERSION, cacheInvalidate);
      response->addHeader(F("ETag"), tmp);
      request->send(response);
    }

    void serveFetchSettings(AsyncWebServerRequest* request)
    {
      strcpy(XML, "<?xml version = '1.0'?>\n<Data>\n");
      
      //sACN/Artnet
      appendXML("EP",e131Port);
      appendXML("EU",e131Universe); 
      appendXML("DA",DMXAddress); 
      appendXML("EP",e131Port); 
      appendXML("XX",DMXSegmentSpacing);
      appendXML("DM",DMXMode);

      //Network Settings
      char k[3]; k[2] = 0; //IP addresses
      for (int i = 0; i<4; i++)
      {
        k[1] = 48+i; //ascii 0,1,2,3
        k[0] = 'I'; appendXML(k,staticIP[i]);
        k[0] = 'G'; appendXML(k,staticGateway[i]);
        k[0] = 'S'; appendXML(k,staticSubnet[i]);
      }

      //Busses
      strcat(XML, "<Busses>\n");
      uint16_t cUni = e131Universe;
      uint16_t cDMX = DMXAddress;
      for (int i=0; i<busses.getNumBusses(); i++) {
        Bus *bus = busses.getBus(i);
        if (bus == nullptr) continue;
        strcat(XML, "<Bus>\n");
        appendXML("BN",i); //Bus number
        appendXML("TY",bus->getType()); //Type
        appendXML("LE",bus->getLength()); //Length
        appendXML("BU",cUni); //Universe
        appendXML("BA",cDMX); //Start address
        switch (DMXMode) {
          case DMX_MODE_SINGLE_RGB: cDMX += 3; break;
          case DMX_MODE_SINGLE_DRGB: cDMX += 4; break;
          case DMX_MODE_MULTIPLE_RGB: cDMX += 3*bus->getLength(); break;
          case DMX_MODE_MULTIPLE_DRGB:
          case DMX_MODE_MULTIPLE_RGBW: cDMX += 4*bus->getLength(); break;
        }
        while(cDMX > 512) {
            cUni += 1;
            cDMX -= 512;
        }
        strcat(XML, "</Bus>\n");
      }
      strcat(XML, "</Busses>\n");

      strcat(XML, "</Data>\n");

      AsyncWebServerResponse *response;
      response = request->beginResponse_P(200, "text/xml", XML);
      request->send(response);
    }

    void appendXML(const char* key, char* val) {
      sprintf(buf, "<%s>%s</%s>\n", key, val, key);
      strcat(XML, buf);
    }

    void appendXML(const char* key, int val) {
      sprintf(buf, "<%s>%d</%s>\n", key, val, key);
      strcat(XML, buf);
    }
};

