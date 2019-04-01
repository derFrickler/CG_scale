/*
  ------------------------------------------------------------------
                            CG scale
                      (c) 2019 by M. Lehmann
  ------------------------------------------------------------------
*/
#define CGSCALE_VERSION "1.2.1"
/*

  ******************************************************************
  history:
  V1.2.1  31.03.19      small bug fixed 
                        values in model database are rounded
                        mDNS and OTA did not work in AP mode
  V1.2    23.02.19      Add OTA (over the air update)
                        mDNS default enabled
                        add percentlists for many battery types
                        memory optimization
  V1.1    02.02.19      Supports ESP8266, webpage integrated, STA and AP mode
  V1.0    12.01.19      first release


  ******************************************************************

  Software License Agreement (BSD License)

  Copyright (c) 2019, Michael Lehmann
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
  3. Neither the name of the copyright holders nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

// Required libraries, can be installed from the library manager
#include <HX711_ADC.h>      // library for the HX711 24-bit ADC for weight scales (https://github.com/olkal/HX711_ADC)
#include <U8g2lib.h>        // Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)

// built-in libraries
#include <EEPROM.h>
#include <Wire.h>

// libraries for ESP8266
#if defined(ESP8266)
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#endif

// load settings
#if defined(__AVR__)
#include "settings_AVR.h"
#elif defined(ESP8266)
#include "settings_ESP8266.h"
#endif

// HX711 constructor array (dout pin, sck pint):
HX711_ADC LoadCell[]{HX711_ADC(PIN_LOADCELL1_DOUT, PIN_LOADCELL1_PD_SCK),HX711_ADC(PIN_LOADCELL2_DOUT, PIN_LOADCELL2_PD_SCK),HX711_ADC(PIN_LOADCELL3_DOUT, PIN_LOADCELL3_PD_SCK)};

// webserver constructor
#if defined(ESP8266)
ESP8266WebServer server(80);
IPAddress apIP(ip[0], ip[1], ip[2], ip[3]);
File fsUploadFile;              // a File object to temporarily store the received file
#endif

#include "defaults.h"

// load default values
uint8_t nLoadcells = NUMBER_LOADCELLS;
float distance[] = {DISTANCE_X1, DISTANCE_X2, DISTANCE_X3};
float calFactorLoadcell[] = {LOADCELL1_CALIBRATION_FACTOR, LOADCELL2_CALIBRATION_FACTOR, LOADCELL3_CALIBRATION_FACTOR};
float resistor[] = {RESISTOR_R1, RESISTOR_R2};
uint8_t batType = BAT_TYPE;
uint8_t batCells = BAT_CELLS;
float refWeight = REF_WEIGHT;
float refCG = REF_CG;
#if defined(ESP8266)
char ssid_STA[MAX_SSID_PW_LENGHT + 1] = SSID_STA;
char password_STA[MAX_SSID_PW_LENGHT + 1] = PASSWORD_STA;
char ssid_AP[MAX_SSID_PW_LENGHT + 1] = SSID_AP;
char password_AP[MAX_SSID_PW_LENGHT + 1] = PASSWORD_AP;
#endif

// declare variables
float weightLoadCell[] = {0, 0, 0};
float lastWeightLoadCell[] = {0, 0, 0};
float weightTotal = 0;
float CG_length = 0;
float CG_trans = 0;
float batVolt = 0;
unsigned long lastTimeMenu = 0;
unsigned long lastTimeLoadcell = 0;
bool updateMenu = true;
int menuPage = 0;
String errMsg[5] = "";
int errMsgCnt = 0;
#if defined(ESP8266)
String wifiMsg = "";
String updateMsg = "";
bool wifiSTAmode = true;
char curModelName[MAX_MODELNAME_LENGHT + 1] = "";
#endif


// Restart CPU
#if defined(__AVR__)
void(* resetCPU) (void) = 0;
#elif defined(ESP8266)
void resetCPU() {}
#endif


// save calibration factor
void saveCalFactor(int nLC) {
  LoadCell[nLC].setCalFactor(calFactorLoadcell[nLC]);
  EEPROM.put(P_LOADCELL1_CALIBRATION_FACTOR + (nLC * sizeof(float)), calFactorLoadcell[nLC]);
#if defined(ESP8266)
  EEPROM.commit();
#endif
}


void updateLoadcells(){
  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      LoadCell[i].update();
    }
  }
}


void tareLoadcells(){
  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      LoadCell[i].tare();
    }
  }
}


void printNewValueText(){
  Serial.print(F("Set new value:"));
}


// run auto calibration
bool runAutoCalibrate() {
  Serial.print(F("\nAutocalibration is running"));
  for (int i = 0; i <= 20; i++) {
    Serial.print(F("."));
    delay(100);
  }
  // calculate weight
  float toWeightLoadCell[] = {0, 0, 0};
  toWeightLoadCell[LC2] = ((refCG - distance[X1]) * refWeight) / distance[X2];
  toWeightLoadCell[LC1] = refWeight - toWeightLoadCell[LC2];
  if (nLoadcells == 3) {
    toWeightLoadCell[LC1] = toWeightLoadCell[LC1] / 2;
    toWeightLoadCell[LC3] = toWeightLoadCell[LC1];
  }
  // calculate calibration factors
  for (int i = LC1; i <= LC3; i++) {
    calFactorLoadcell[i] = calFactorLoadcell[i] / (toWeightLoadCell[i] / weightLoadCell[i]);
    saveCalFactor(i);
  }
  
  // finish
  Serial.println(F("done"));
}


// check if a loadcell has error
bool getLoadcellError() {
  bool err = false;

  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      if (LoadCell[i].getTareTimeoutFlag()) {
        errMsg[++errMsgCnt] = "ERROR: Timeout TARE Lc";
        errMsg[errMsgCnt] += (i + 1) ;
        errMsg[errMsgCnt] += "\n";
        err = true;
      }
    }
  }

  return err;
}


// Count percentage from cell voltage
int percentBat(float cellVoltage) {

  int result = 0;
  int elementCount = DATAPOINTS_PERCENTLIST;
  byte batTypeArray = batType - 2;

  for (int i = 0; i < elementCount; i++) {
    if (pgm_read_float( &percentList[batTypeArray][i][1]) == 100 ) {
      elementCount = i;
      break;
    }
  }
  
  float cellempty = pgm_read_float( &percentList[batTypeArray][0][0]);
  float cellfull = pgm_read_float( &percentList[batTypeArray][elementCount][0]);

  if (cellVoltage >= cellfull) {
    result = 100;
  } else if (cellVoltage <= cellempty) {
    result = 0;
  } else {
    for (int i = 0; i <= elementCount; i++) {
      float curVolt = pgm_read_float(&percentList[batTypeArray][i][0]);
      if (curVolt >= cellVoltage && i > 0) {
        float lastVolt = pgm_read_float(&percentList[batTypeArray][i-1][0]);
        float curPercent = pgm_read_float(&percentList[batTypeArray][i][1]);
        float lastPercent = pgm_read_float(&percentList[batTypeArray][i-1][1]);
        result = float((cellVoltage - lastVolt) / (curVolt - lastVolt)) * (curPercent - lastPercent) + lastPercent;
        break;
      }
    }
  }

  return result;
}



void setup() {

  // init serial
  Serial.begin(9600);

#if defined(ESP8266)
  // init filesystem
  SPIFFS.begin();
  EEPROM.begin(EEPROM_SIZE);
#endif

  // read settings from eeprom
  if (EEPROM.read(P_NUMBER_LOADCELLS) != 0xFF) {
    nLoadcells = EEPROM.read(P_NUMBER_LOADCELLS);
  }

  for (int i = LC1; i <= LC3; i++) {
    if (EEPROM.read(P_DISTANCE_X1 + (i * sizeof(float))) != 0xFF) {
      EEPROM.get(P_DISTANCE_X1 + (i * sizeof(float)), distance[i]);
    }

    if (EEPROM.read(P_LOADCELL1_CALIBRATION_FACTOR + (i * sizeof(float))) != 0xFF) {
      EEPROM.get(P_LOADCELL1_CALIBRATION_FACTOR + (i * sizeof(float)), calFactorLoadcell[i]);
    }
  }

  if (EEPROM.read(P_BAT_TYPE) != 0xFF) {
    batType = EEPROM.read(P_BAT_TYPE);
  }

  if (EEPROM.read(P_BATT_CELLS) != 0xFF) {
    batCells = EEPROM.read(P_BATT_CELLS);
  }

  if (EEPROM.read(P_REF_WEIGHT) != 0xFF) {
    EEPROM.get(P_REF_WEIGHT, refWeight);
  }

  if (EEPROM.read(P_REF_CG) != 0xFF) {
    EEPROM.get(P_REF_CG, refCG);
  }

  for (int i = R1; i <= R2; i++) {
    if (EEPROM.read(P_RESISTOR_R1 + (i * sizeof(float))) != 0xFF) {
      EEPROM.get(P_RESISTOR_R1 + (i * sizeof(float)), resistor[i]);
    }
  }

#if defined(ESP8266)
  if (EEPROM.read(P_SSID_STA) != 0xFF) {
    EEPROM.get(P_SSID_STA, ssid_STA);
  }

  if (EEPROM.read(P_PASSWORD_STA) != 0xFF) {
    EEPROM.get(P_PASSWORD_STA, password_STA);
  }

  if (EEPROM.read(P_SSID_AP) != 0xFF) {
    EEPROM.get(P_SSID_AP, ssid_AP);
  }

  if (EEPROM.read(P_PASSWORD_AP) != 0xFF) {
    EEPROM.get(P_PASSWORD_AP, password_AP);
  }

  if (EEPROM.read(P_MODELNAME) != 0xFF) {
    EEPROM.get(P_MODELNAME, curModelName);
  }

  // load current model
  if (!openModelJson(curModelName)) {
    curModelName[0] = '\0';
  }

#endif

  // init OLED display
  oledDisplay.begin();
  oledDisplay.firstPage();
  do {
    oledDisplay.drawXBMP(20, 12, 18, 18, CGImage);
    oledDisplay.setFont(u8g2_font_helvR12_tr);
    oledDisplay.setCursor(45, 28);
    oledDisplay.print(F("CG scale"));

    oledDisplay.setFont(u8g2_font_5x7_tr);
    oledDisplay.setCursor(35, 55);
    oledDisplay.print(F("Version: "));
    oledDisplay.print(CGSCALE_VERSION);
    oledDisplay.setCursor(20, 64);
    oledDisplay.print(F("(c) 2019 M. Lehmann"));
  } while ( oledDisplay.nextPage() );

  // init & tare Loadcells
  for (int i = LC1; i <= LC3; i++) {
    if (i < nLoadcells) {
      LoadCell[i].begin();
      LoadCell[i].setCalFactor(calFactorLoadcell[i]);
    }
  }

  // stabilize scale values
  while (millis() < STABILISINGTIME) {
    updateLoadcells();
  }

  tareLoadcells();

  getLoadcellError();

#if defined(ESP8266)

  // Set Hostname
  String hostname = "disabled";
#if ENABLE_MDNS
  hostname = ssid_AP;
  hostname.replace(" ", "");
  hostname.toLowerCase();
  if (!MDNS.begin(hostname, WiFi.localIP())) {
    hostname = "mDNS failed";
  }else{
    hostname += ".local";
  }
#endif
  wifiMsg += TimeToString(millis());
  wifiMsg += " Hostname: ";
  wifiMsg += hostname;
  wifiMsg += "\n";
  

  // Start by connecting to a WiFi network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_STA, password_STA);

  wifiMsg += TimeToString(millis());
  wifiMsg += " STA mode - connect with wifi: ";
  wifiMsg += ssid_STA;
  wifiMsg += "\n";

  long timeoutWiFi = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (WiFi.status() == WL_NO_SSID_AVAIL) {
      wifiMsg += TimeToString(millis());
      wifiMsg += " No SSID available\n";
      break;
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
      wifiMsg += TimeToString(millis());
      wifiMsg += " Connection failed\n";
      break;
    } else if ((millis() - timeoutWiFi) > TIMEOUT_CONNECT) {
      wifiMsg += TimeToString(millis());
      wifiMsg += " Timeout\n";
      break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    // if WiFi not connected, switch to access point mode
    wifiSTAmode = false;
    wifiMsg += TimeToString(millis());
    wifiMsg += " AP mode - create access point: ";
    wifiMsg += ssid_AP;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid_AP, password_AP);
    wifiMsg += "\n";
    wifiMsg += TimeToString(millis());
    wifiMsg += " IP: ";
    wifiMsg += WiFi.softAPIP().toString();
  } else {
    wifiMsg += TimeToString(millis());
    wifiMsg += " Connected, IP: ";
    wifiMsg += WiFi.localIP().toString();
  }

  // print wifi status
  oledDisplay.firstPage();
  do {
    oledDisplay.setFont(u8g2_font_5x7_tr);
    oledDisplay.setCursor(0, 14);
    oledDisplay.print(F("WiFi:"));
    oledDisplay.setCursor(0, 39);
    oledDisplay.print(F("Host:"));
    oledDisplay.setCursor(0, 64);
    oledDisplay.print(F("IP:"));

    oledDisplay.setFont(u8g2_font_helvR10_tr);
    oledDisplay.setCursor(28, 14);
    if (wifiSTAmode) {
      oledDisplay.print(ssid_STA);
    } else {
      oledDisplay.print(ssid_AP);
    }
    oledDisplay.setCursor(28, 39);
    oledDisplay.print(hostname);
    oledDisplay.setCursor(28, 64);
    if (wifiSTAmode) {
      oledDisplay.print(WiFi.localIP());
    } else {
      oledDisplay.print(WiFi.softAPIP());
    }
  } while ( oledDisplay.nextPage() );
  delay(3000);

  // When the client requests data
  server.on("/getHead", getHead);
  server.on("/getValue", getValue);
  server.on("/getRawValue", getRawValue);
  server.on("/getParameter", getParameter);
  server.on("/getWiFiNetworks", getWiFiNetworks);
  server.on("/saveParameter", saveParameter);
  server.on("/autoCalibrate", autoCalibrate);
  server.on("/tare", runTare);
  server.on("/saveModel", saveModel);
  server.on("/openModel", openModel);
  server.on("/deleteModel", deleteModel);

  // When the client upload file
  server.on("/models.html", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  // If the client requests any URI
  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "404: Not Found");
  });

  // init webserver
  server.begin();

  // init OTA (over the air update)
  ArduinoOTA.setHostname(ssid_AP);
  ArduinoOTA.setPassword(password_AP);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "firmware";
    } else { // U_SPIFFS
      //SPIFFS.end();
      type = "SPIFFS";
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    updateMsg = "Updating " + type;
  });

  ArduinoOTA.onEnd([]() {
    updateMsg = "successful..";
    printUpdateProgress(100, 100);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    printUpdateProgress(progress, total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) {
      updateMsg = "Auth Failed";
    } else if (error == OTA_BEGIN_ERROR) {
      updateMsg = "Begin Failed";
    } else if (error == OTA_CONNECT_ERROR) {
      updateMsg = "Connect Failed";
    } else if (error == OTA_RECEIVE_ERROR) {
      updateMsg = "Receive Failed";
    } else if (error == OTA_END_ERROR) {
      updateMsg = "End Failed";
    }
    printUpdateProgress(0, 100);
  });

  ArduinoOTA.begin();

#endif

}


void loop() {

#if defined(ESP8266)

#if ENABLE_MDNS
  MDNS.update();
#endif
  
  ArduinoOTA.handle();
  server.handleClient();
#endif

  updateLoadcells();

  // update loadcell values
  if ((millis() - lastTimeLoadcell) > UPDATE_INTERVAL_LOADCELL) {
    lastTimeLoadcell = millis();

    // get Loadcell weights
    for (int i = LC1; i <= LC3; i++) {
      if (i < nLoadcells) {
        weightLoadCell[i] = LoadCell[i].getData();
        // IIR filter
        weightLoadCell[i] = weightLoadCell[i] + SMOOTHING_LOADCELL * (lastWeightLoadCell[i] - weightLoadCell[i]);
        lastWeightLoadCell[i] = weightLoadCell[i];
      }
    }
  }

  // update display and serial menu
  if ((millis() - lastTimeMenu) > UPDATE_INTERVAL_OLED_MENU) {

    lastTimeMenu = millis();

    // total model weight
    weightTotal = weightLoadCell[LC1] + weightLoadCell[LC2] + weightLoadCell[LC3];
    if (weightTotal < MINIMAL_TOTAL_WEIGHT && weightTotal > MINIMAL_TOTAL_WEIGHT * -1) {
      weightTotal = 0;
    }

    if (weightTotal > MINIMAL_CG_WEIGHT) {
      // CG longitudinal axis
      CG_length = ((weightLoadCell[LC2] * distance[X2]) / weightTotal) + distance[X1];

      // CG transverse axis
      if (nLoadcells == 3) {
        CG_trans = (distance[X3] / 2) - (((weightLoadCell[LC1] + weightLoadCell[LC2] / 2) * distance[X3]) / weightTotal);
      }
    } else {
      CG_length = 0;
      CG_trans = 0;
    }

    // read battery voltage
    if (batType > B_OFF) {
      batVolt = (analogRead(VOLTAGE_PIN) / 1024.0) * V_REF * ((resistor[R1] + resistor[R2]) / resistor[R2]) / 1000.0;
#if ENABLE_PERCENTLIST
      if (batType > B_VOLT) {
        batVolt = percentBat(batVolt / batCells);
      }
#endif
    }

    // print to display
    char buff[8];
    int pos_weightTotal = 7;
    int pos_CG_length = 28;
    if (nLoadcells == 2) {
      pos_weightTotal = 17;
      pos_CG_length = 45;
      if (batType == 0) {
        pos_weightTotal = 12;
        pos_CG_length = 40;
      }
    }

    oledDisplay.firstPage();
    do {
      if (errMsgCnt == 0) {
        // print battery
        if (batType > B_OFF) {
          oledDisplay.drawXBMP(88, 1, 12, 6, batteryImage);
          if (batType == B_VOLT) {
            dtostrf(batVolt, 2, 2, buff);
          } else {
            dtostrf(batVolt, 3, 0, buff);
            oledDisplay.drawBox(89, 2, (batVolt / (100 / 8)), 4);
          }
          oledDisplay.setFont(u8g2_font_5x7_tr);
          oledDisplay.setCursor(123 - oledDisplay.getStrWidth(buff), 7);
          oledDisplay.print(buff);
          if (batType == B_VOLT) {
            oledDisplay.print(F("V"));
          } else {
            oledDisplay.print(F("%"));
          }
        }

        // print total weight
        oledDisplay.drawXBMP(2, pos_weightTotal, 18, 18, weightImage);
        dtostrf(weightTotal, 5, 1, buff);
        oledDisplay.setFont(u8g2_font_helvR12_tr);
        oledDisplay.setCursor(93 - oledDisplay.getStrWidth(buff), pos_weightTotal + 17);
        oledDisplay.print(buff);
        oledDisplay.print(F(" g"));

        // print CG longitudinal axis
        oledDisplay.drawXBMP(2, pos_CG_length, 18, 18, CGImage);
        dtostrf(CG_length, 5, 1, buff);
        oledDisplay.setCursor(93 - oledDisplay.getStrWidth(buff), pos_CG_length + 16);
        oledDisplay.print(buff);
        oledDisplay.print(F(" mm"));

        // print CG transverse axis
        if (nLoadcells == 3) {
          oledDisplay.drawXBMP(2, 47, 18, 18, CGtransImage);
          dtostrf(CG_trans, 5, 1, buff);
          oledDisplay.setCursor(93 - oledDisplay.getStrWidth(buff), 64);
          oledDisplay.print(buff);
          oledDisplay.print(F(" mm"));
        }
      } else {
        oledDisplay.setFont(u8g2_font_5x7_tr);
        for (int i = 1; i <= errMsgCnt; i++) {
          oledDisplay.setCursor(0, 7 * i);
          oledDisplay.print(errMsg[i]);
        }
      }

    } while ( oledDisplay.nextPage() );

    // serial connection
    if (Serial) {
      if (Serial.available() > 0) {

        switch (menuPage)
        {
          case MENU_HOME:
            menuPage = Serial.parseInt();
            updateMenu = true;
            break;
          case MENU_LOADCELLS:
            nLoadcells = Serial.parseInt();
            EEPROM.put(P_NUMBER_LOADCELLS, nLoadcells);
#if defined(ESP8266)
            EEPROM.commit();
#endif
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_DISTANCE_X1 ... MENU_DISTANCE_X3:
            distance[menuPage - MENU_DISTANCE_X1] = Serial.parseFloat();
            EEPROM.put(P_DISTANCE_X1 + ((menuPage - MENU_DISTANCE_X1) * sizeof(float)), distance[menuPage - MENU_DISTANCE_X1]);
#if defined(ESP8266)
            EEPROM.commit();
#endif
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_REF_WEIGHT:
            refWeight = Serial.parseFloat();
            EEPROM.put(P_REF_WEIGHT, refWeight);
#if defined(ESP8266)
            EEPROM.commit();
#endif
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_REF_CG:
            refCG = Serial.parseFloat();
            EEPROM.put(P_REF_CG, refCG);
#if defined(ESP8266)
            EEPROM.commit();
#endif
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_AUTO_CALIBRATE:
            if (Serial.read() == 'J') {
              runAutoCalibrate();
            }
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_LOADCELL1_CALIBRATION_FACTOR ... MENU_LOADCELL3_CALIBRATION_FACTOR:
            calFactorLoadcell[menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR] = Serial.parseFloat();
            saveCalFactor(menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR);
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_RESISTOR_R1 ... MENU_RESISTOR_R2:
            resistor[menuPage - MENU_RESISTOR_R1] = Serial.parseFloat();
            EEPROM.put(P_RESISTOR_R1 + ((menuPage - MENU_RESISTOR_R1) * sizeof(float)), resistor[menuPage - MENU_RESISTOR_R1]);
#if defined(ESP8266)
            EEPROM.commit();
#endif
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_BATTERY_MEASUREMENT:
            batType = Serial.parseInt();
            EEPROM.put(P_BAT_TYPE, batType);
#if defined(ESP8266)
            EEPROM.commit();
#endif
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_BATTERY_CELLS:
            batCells = Serial.parseInt();
            EEPROM.put(P_BATT_CELLS, batCells);
#if defined(ESP8266)
            EEPROM.commit();
#endif
            menuPage = 0;
            updateMenu = true;
            break;
          case MENU_RESET_DEFAULT:
            if (Serial.read() == 'J') {
              // reset eeprom
              for (int i = 0; i < EEPROM_SIZE; i++) {
                EEPROM.write(i, 0xFF);
              }
              Serial.end();
#if defined(ESP8266)
              EEPROM.commit();
              // delete json model file
              if (SPIFFS.exists(MODEL_FILE)) {
                SPIFFS.remove(MODEL_FILE);
              }
#endif
              resetCPU();
            }
            menuPage = 0;
            updateMenu = true;
            break;
          default:
            Serial.readString();
            menuPage = 0;
            updateMenu = true;
            break;
        }
        Serial.readString();

      }

      if (!updateMenu)
        return;

      switch (menuPage)
      {
        case MENU_HOME: {
            Serial.print(F("\n\n********************************************\nCG scale by M.Lehmann - V"));
            Serial.print(CGSCALE_VERSION);
            Serial.print(F("\n\n"));

            Serial.print(MENU_LOADCELLS);
            Serial.print(F("  - Set number of load cells ("));
            Serial.print(nLoadcells);
            Serial.print(F(")\n"));

            for (int i = X1; i <= X3; i++) {
              Serial.print(MENU_DISTANCE_X1 + i);
              Serial.print(F("  - Set distance X"));
              Serial.print(i+1);
              Serial.print(F(" ("));
              Serial.print(distance[i]);
              Serial.print(F("mm)\n"));
            }

            Serial.print(MENU_REF_WEIGHT);
            Serial.print(F("  - Set reference weight ("));
            Serial.print(refWeight);
            Serial.print(F("g)\n"));

            Serial.print(MENU_REF_CG);
            Serial.print(F("  - Set reference CG ("));
            Serial.print(refCG);
            Serial.print(F("mm)\n"));

            Serial.print(MENU_AUTO_CALIBRATE);
            Serial.print(F("  - Start autocalibration\n"));

            for (int i = LC1; i <= LC3; i++) {
              Serial.print(MENU_LOADCELL1_CALIBRATION_FACTOR + i);
              if((MENU_LOADCELL1_CALIBRATION_FACTOR + i) < 10) Serial.print(F(" "));
              Serial.print(F(" - Set calibration factor of load cell "));
              Serial.print(i+1);
              Serial.print(F(" ("));
              Serial.print(calFactorLoadcell[i]);
              Serial.print(F(")\n"));
            }

            for (int i = R1; i <= R2; i++) {
              Serial.print(MENU_RESISTOR_R1 + i);
              Serial.print(F(" - Set value of resistor R"));
              Serial.print(i+1);
              Serial.print(F(" ("));
              Serial.print(resistor[i]);
              Serial.print(F("ohm)\n"));
            }

            Serial.print(MENU_BATTERY_MEASUREMENT);
            Serial.print(F(" - Set battery type ("));
            Serial.print(battTypName[batType]);
            Serial.print(F(")\n"));

            Serial.print(MENU_BATTERY_CELLS);
            Serial.print(F(" - Set number of battery cells ("));
            Serial.print(batCells);
            Serial.print(F(")\n"));

            Serial.print(MENU_SHOW_ACTUAL);
            Serial.print(F(" - Show actual values\n"));

#if defined(ESP8266)
            Serial.print(MENU_WIFI_INFO);
            Serial.print(F(" - Show WiFi network info\n"));
#endif

            Serial.print(MENU_RESET_DEFAULT);
            Serial.print(F(" - Reset to factory defaults\n"));

            Serial.print(F("\n"));
            for (int i = 1; i <= errMsgCnt; i++) {
              Serial.print(errMsg[i]);
            }

            Serial.print(F("\nPlease choose the menu number:"));

            updateMenu = false;
            break;
          }
        case MENU_LOADCELLS:
          Serial.print(F("\n\nNumber of load cells: "));
          Serial.println(nLoadcells);
          printNewValueText();
          updateMenu = false;
          break;
        case MENU_DISTANCE_X1 ... MENU_DISTANCE_X3:
          Serial.print("\n\nDistance X");
          Serial.print(menuPage - MENU_DISTANCE_X1 + 1);
          Serial.print(F(": "));
          Serial.print(distance[menuPage - MENU_DISTANCE_X1]);
          Serial.print(F("mm\n"));
          printNewValueText();
          updateMenu = false;
          break;
        case MENU_REF_WEIGHT:
          Serial.print(F("\n\nReference weight: "));
          Serial.print(refWeight);
          Serial.print(F("g\n"));
          printNewValueText();
          updateMenu = false;
          break;
        case MENU_REF_CG:
          Serial.print(F("\n\nReference CG: "));
          Serial.print(refCG);
          Serial.print(F("mm\n"));
          printNewValueText();
          updateMenu = false;
          break;
        case MENU_AUTO_CALIBRATE:
          Serial.print(F("\n\nPlease put the reference weight on the scale.\nStart auto calibration (J/N)?\n"));
          updateMenu = false;
          break;
        case MENU_LOADCELL1_CALIBRATION_FACTOR ... MENU_LOADCELL3_CALIBRATION_FACTOR:
          Serial.print("\n\nCalibration factor of load cell ");
          Serial.print(menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR + 1);
          Serial.print(F(": "));
          Serial.println(calFactorLoadcell[menuPage - MENU_LOADCELL1_CALIBRATION_FACTOR]);
          printNewValueText();
          updateMenu = false;
          break;
        case MENU_RESISTOR_R1 ... MENU_RESISTOR_R2:
          Serial.print(F("\n\nValue of resistor R"));
          Serial.print(menuPage - MENU_RESISTOR_R1 + 1);
          Serial.print(F(": "));
          Serial.println(resistor[menuPage - MENU_RESISTOR_R1]);
          printNewValueText();
          updateMenu = false;
          break;
        case MENU_BATTERY_MEASUREMENT: {
            Serial.print(F("\n\nBattery type: "));
            Serial.println(battTypName[batType]);
            for (int i = 0; i < NUMBER_BAT_TYPES; i++) {
              Serial.print(i);
              Serial.print(" = ");
              Serial.println(battTypName[i]);
            }
            printNewValueText();
            updateMenu = false;
            break;
          }
        case MENU_BATTERY_CELLS:
          Serial.print(F("\n\nBattery cells: "));
          Serial.println(batCells);
          printNewValueText();
          updateMenu = false;
          break;
        case MENU_SHOW_ACTUAL:
          for (int i = LC1; i <= LC3; i++) {
            if (i < nLoadcells) {
              Serial.print(F("Lc"));
              Serial.print(i + 1);
              Serial.print(F(": "));
              Serial.print(weightLoadCell[i]);
              Serial.print(F("g  "));
            }
          }
          Serial.print(F("Total weight: "));
          Serial.print(weightTotal);
          Serial.print(F("g  CG length: "));
          Serial.print(CG_length);
          if (nLoadcells == 3) {
            Serial.print(F("mm  CG trans: "));
            Serial.print(CG_trans);
            Serial.print(F("mm"));
          }
          if (batType > B_OFF) {
            Serial.print(F("  Battery:"));
            Serial.print(batVolt);
            if (batType == B_VOLT) {
              Serial.print(F("V"));
            } else {
              Serial.print(F("%"));
            }
          }
          Serial.println();
          break;
#if defined(ESP8266)
        case MENU_WIFI_INFO:
          {
            Serial.println("\n\n********************************************\nWiFi network information\n");
            Serial.println("# Startup log:");
            Serial.println(wifiMsg);
            Serial.println("# end of log\n");

            Serial.println("# Current WiFi status:");
            WiFi.printDiag(Serial);
            if (wifiSTAmode == false) {
              Serial.print("Connected clients: ");
              Serial.println(WiFi.softAPgetStationNum());
            }

            Serial.println("\n# Available WiFi networks:");
            int wifiCnt = WiFi.scanNetworks();
            if (wifiCnt == 0) {
              Serial.println("no networks found");
            } else {
              for (int i = 0; i < wifiCnt; ++i) {
                // Print SSID and RSSI for each network found
                Serial.print(i + 1);
                Serial.print(": ");
                Serial.print(WiFi.SSID(i));
                Serial.print(" (");
                Serial.print(WiFi.RSSI(i));
                Serial.print("dBm) ");
                switch (WiFi.encryptionType(i)) {
                  case ENC_TYPE_WEP:
                    Serial.print("WEP");
                    break;
                  case ENC_TYPE_TKIP:
                    Serial.print("WPA");
                    break;
                  case ENC_TYPE_CCMP:
                    Serial.print("WPA2");
                    break;
                  case ENC_TYPE_AUTO:
                    Serial.print("Auto");
                    break;
                }
                Serial.println("");
              }
            }
          }
          updateMenu = false;
          break;
#endif
        case MENU_RESET_DEFAULT:
          Serial.print(F("\n\nReset to factory defaults (J/N)?\n"));
          updateMenu = false;
          break;
      }

    } else {
      updateMenu = true;
    }
  }
}



#if defined(ESP8266)

// send headvalues to client
void getHead() {
  String response = ssid_AP;
  response += "&";
  for (int i = 1; i <= errMsgCnt; i++) {
    response += errMsg[i];
  }
  response += "&";
  response += CGSCALE_VERSION;
  server.send(200, "text/html", response);
}


// send values to client
void getValue() {
  char buff[8];
  String response = "";
  dtostrf(weightTotal, 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(CG_length, 5, 1, buff);
  response += buff;
  response += "mm&";
  dtostrf(CG_trans, 5, 1, buff);
  response += buff;
  response += "mm&";
  if (batType == B_VOLT) {
    dtostrf(batVolt, 5, 2, buff);
    response += buff;
    response += "V";
  } else {
    dtostrf(batVolt, 5, 0, buff);
    response += buff;
    response += "%";
  }
  server.send(200, "text/html", response);
}


// send raw values to client
void getRawValue() {
  char buff[8];
  String response = "";
  dtostrf(weightLoadCell[LC1], 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(weightLoadCell[LC2], 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(weightLoadCell[LC3], 5, 1, buff);
  response += buff;
  response += "g";
  server.send(200, "text/html", response);
}


// send parameters to client
void getParameter() {
  char buff[8];
  String response = "";
  float weightTotal_saved = 0;
  float CG_length_saved = 0;
  float CG_trans_saved = 0;

  StaticJsonBuffer<JSONBUFFER_SIZE> jsonBuffer;
  //DynamicJsonBuffer  jsonBuffer(JSONBUFFER_SIZE);

  if (SPIFFS.exists(MODEL_FILE)) {
    // read json file
    File f = SPIFFS.open(MODEL_FILE, "r");
    JsonObject& root = jsonBuffer.parseObject(f);
    f.close();
    // check if model exists
    if (root.success() && root.containsKey(curModelName)) {
      JsonObject& object = root[curModelName];
      weightTotal_saved = object["wt"];
      CG_length_saved = object["cg"];
      CG_trans_saved = object["cglr"];
    }
  }

  // parameter list
  response += nLoadcells;
  response += "&";
  for (int i = X1; i <= X3; i++) {
    response += distance[i];
    response += "&";
  }
  response += refWeight;
  response += "&";
  response += refCG;
  response += "&";
  for (int i = LC1; i <= LC3; i++) {
    response += calFactorLoadcell[i];
    response += "&";
  }
  for (int i = R1; i <= R2; i++) {
    response += resistor[i];
    response += "&";
  }
  response += batType;
  response += "&";
  response += batCells;
  response += "&";
  response += ssid_STA;
  response += "&";
  response += password_STA;
  response += "&";
  response += ssid_AP;
  response += "&";
  response += password_AP;
  response += "&";
  response += curModelName;
  response += "&";
  dtostrf(weightTotal_saved, 5, 1, buff);
  response += buff;
  response += "g&";
  dtostrf(CG_length_saved, 5, 1, buff);
  response += buff;
  response += "mm&";
  dtostrf(CG_trans_saved, 5, 1, buff);
  response += buff;
  response += "mm";
  server.send(200, "text/html", response);
}


// send available WiFi networks to client
void getWiFiNetworks() {
  bool ssidSTAavailable = false;
  String response = "";
  int n = WiFi.scanNetworks();
  
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      response += WiFi.SSID(i);
      if(WiFi.SSID(i) == ssid_STA) ssidSTAavailable = true;
      if (i < n - 1) response += "&";
    }
    if(!ssidSTAavailable){
      response += "&";
      response += ssid_STA;
    }
  }
  server.send(200, "text/html", response);
}


// save parameters
void saveParameter() {
  if (server.hasArg("nLoadcells")) nLoadcells = server.arg("nLoadcells").toInt();
  if (server.hasArg("distanceX1")) distance[X1] = server.arg("distanceX1").toFloat();
  if (server.hasArg("distanceX2")) distance[X2] = server.arg("distanceX2").toFloat();
  if (server.hasArg("distanceX3")) distance[X3] = server.arg("distanceX3").toFloat();
  if (server.hasArg("refWeight")) refWeight = server.arg("refWeight").toFloat();
  if (server.hasArg("refCG")) refCG = server.arg("refCG").toFloat();
  if (server.hasArg("calFactorLoadcell1")) calFactorLoadcell[LC1] = server.arg("calFactorLoadcell1").toFloat();
  if (server.hasArg("calFactorLoadcell2")) calFactorLoadcell[LC2] = server.arg("calFactorLoadcell2").toFloat();
  if (server.hasArg("calFactorLoadcell3")) calFactorLoadcell[LC3] = server.arg("calFactorLoadcell3").toFloat();
  if (server.hasArg("resistorR1")) resistor[R1] = server.arg("resistorR1").toFloat();
  if (server.hasArg("resistorR2")) resistor[R2] = server.arg("resistorR2").toFloat();
  if (server.hasArg("batType")) batType = server.arg("batType").toInt();
  if (server.hasArg("batCells")) batCells = server.arg("batCells").toInt();
  if (server.hasArg("ssid_STA")) server.arg("ssid_STA").toCharArray(ssid_STA, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("password_STA")) server.arg("password_STA").toCharArray(password_STA, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("ssid_AP")) server.arg("ssid_AP").toCharArray(ssid_AP, MAX_SSID_PW_LENGHT + 1);
  if (server.hasArg("password_AP")) server.arg("password_AP").toCharArray(password_AP, MAX_SSID_PW_LENGHT + 1);

  EEPROM.put(P_NUMBER_LOADCELLS, nLoadcells);
  for (int i = LC1; i <= LC3; i++) {
    EEPROM.put(P_DISTANCE_X1 + (i * sizeof(float)), distance[i]);
    saveCalFactor(i);
  }
  EEPROM.put(P_REF_WEIGHT, refWeight);
  EEPROM.put(P_REF_CG, refCG);
  for (int i = R1; i <= R2; i++) {
    EEPROM.put(P_RESISTOR_R1 + (i * sizeof(float)), resistor[i]);
  }
  EEPROM.put(P_BAT_TYPE, batType);
  EEPROM.put(P_BATT_CELLS, batCells);
  EEPROM.put(P_SSID_STA, ssid_STA);
  EEPROM.put(P_PASSWORD_STA, password_STA);
  EEPROM.put(P_SSID_AP, ssid_AP);
  EEPROM.put(P_PASSWORD_AP, password_AP);
  EEPROM.commit();

  server.send(200, "text/plain", "saved");
}


// calibrate cg scale
void autoCalibrate() {
  while (!runAutoCalibrate());
  server.send(200, "text/plain", "parameters saved");
}


// tare cg scale
void runTare() {
  tareLoadcells();
  if (!getLoadcellError()) {
    server.send(200, "text/plain", "tare completed");
    return;
  }
  server.send(404, "text/plain", "404: tare failed !");
}


// save model
void saveModel() {
  if (server.hasArg("modelname")) {
    if (saveModelJson(server.arg("modelname"))) {
      server.send(200, "text/plain", "saved");
      return;
    }
  }
  server.send(404, "text/plain", "404: Save model failed !");
}


// open model
void openModel() {
  if (server.hasArg("modelname")) {
    if (openModelJson(server.arg("modelname"))) {
      server.send(200, "text/plain", "opened");
      return;
    }
  }
  server.send(404, "text/plain", "404: Open model failed !");
}


// delete model
void deleteModel() {
  if (server.hasArg("modelname")) {
    if (deleteModelJson(server.arg("modelname"))) {
      server.send(200, "text/plain", "deleted");
      return;
    }
  }
  server.send(404, "text/plain", "404: Delete model failed !");
}


// convert the file extension to the MIME type
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".png")) return "text/css";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".map")) return "application/json";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}


// send file to the client (if it exists)
bool handleFileRead(String path) {

  // If a folder is requested, send the index file
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";

  // If the file exists, either as a compressed archive, or normal
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }

  return false;
}


// upload a new file to the SPIFFS
void handleFileUpload() {

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (filename != MODEL_FILE ) server.send(500, "text/plain", "wrong file !");
    // Open the file for writing in SPIFFS (create if it doesn't exist)
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write the received bytes to the file
    fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    // If the file was successfully created
    if (fsUploadFile) {
      fsUploadFile.close();
      // Redirect the client to the success page
      server.sendHeader("Location", "/models.html");
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }

}


// save model to json file
bool saveModelJson(String modelName) {

  if (modelName.length() > MAX_MODELNAME_LENGHT) {
    return false;
  }

  StaticJsonBuffer<JSONBUFFER_SIZE> jsonBuffer;
  //DynamicJsonBuffer  jsonBuffer(JSONBUFFER_SIZE);

  if (SPIFFS.exists(MODEL_FILE)) {
    // read json file
    File f = SPIFFS.open(MODEL_FILE, "r");
    JsonObject& root = jsonBuffer.parseObject(f);
    f.close();
    if (!root.success()) {
      return false;
    }
    // check if model exists
    if (root.containsKey(modelName)) {
      writeModelData(root[modelName]);
    } else {
      // otherwise create new
      writeModelData(root.createNestedObject(modelName));
    }
    // write to file
    if (root.success()) {
      f = SPIFFS.open(MODEL_FILE, "w");
      root.printTo(f);
      f.close();
    } else {
      return false;
    }
  } else {
    // creat new json
    JsonObject& root = jsonBuffer.createObject();
    writeModelData(root.createNestedObject(modelName));
    // write to file
    if (root.success()) {
      File f = SPIFFS.open(MODEL_FILE, "w");
      root.printTo(f);
      f.close();
    } else {
      return false;
    }
  }

  return true;
}


// read model data from json file
bool openModelJson(String modelName) {

  StaticJsonBuffer<JSONBUFFER_SIZE> jsonBuffer;
  //DynamicJsonBuffer  jsonBuffer(JSONBUFFER_SIZE);

  if (SPIFFS.exists(MODEL_FILE)) {
    // read json file
    File f = SPIFFS.open(MODEL_FILE, "r");
    JsonObject& root = jsonBuffer.parseObject(f);
    f.close();
    if (!root.success()) {
      return false;
    }
    // check if model exists
    if (root.containsKey(modelName)) {
      JsonObject& object = root[modelName];
      // load parameters from model
      distance[X1] = object["x1"];
      distance[X2] = object["x2"];
      distance[X3] = object["x3"];
    } else {
      return false;
    }

    // save current model name to eeprom
    modelName.toCharArray(curModelName, MAX_MODELNAME_LENGHT + 1);
    EEPROM.put(P_MODELNAME, curModelName);
    EEPROM.commit();

    return true;
  }

  return false;
}


// delete model from json file
bool deleteModelJson(String modelName) {

  StaticJsonBuffer<JSONBUFFER_SIZE> jsonBuffer;
  //DynamicJsonBuffer  jsonBuffer(JSONBUFFER_SIZE);

  if (SPIFFS.exists(MODEL_FILE)) {
    // read json file
    File f = SPIFFS.open(MODEL_FILE, "r");
    JsonObject& root = jsonBuffer.parseObject(f);
    f.close();
    if (!root.success()) {
      return false;
    }
    // check if model exists
    if (root.containsKey(modelName)) {
      root.remove(modelName);
    } else {
      return false;
    }
    // if no models in json, kill it
    if (root.size() == 0) {
      SPIFFS.remove(MODEL_FILE);
    } else {
      // write to file
      if (root.success()) {
        File f = SPIFFS.open(MODEL_FILE, "w");
        root.printTo(f);
        f.close();
      } else {
        return false;
      }
    }
    return true;
  }

  return false;

}

void writeModelData(JsonObject& object) {
  char buff[8];
  String stringBuff;
  
  dtostrf(weightTotal, 5, 1, buff);
  stringBuff = buff;
  stringBuff.trim();
  object["wt"] = stringBuff;
  dtostrf(CG_length, 5, 1, buff);
  stringBuff = buff;
  stringBuff.trim();
  object["cg"] = stringBuff;
  dtostrf(CG_trans, 5, 1, buff);
  stringBuff = buff;
  stringBuff.trim();
  object["cglr"] = stringBuff;
  object["x1"] = distance[X1];
  object["x2"] = distance[X2];
  object["x3"] = distance[X3];
}


// print update progress screen
void printUpdateProgress(unsigned int progress, unsigned int total) {
  oledDisplay.firstPage();
  do {
    oledDisplay.setFont(u8g2_font_helvR08_tr);
    oledDisplay.setCursor(0, 12);
    oledDisplay.print(updateMsg);

    oledDisplay.setFont(u8g2_font_5x7_tr);
    oledDisplay.setCursor(107, 35);
    oledDisplay.printf("%u%%\r", (progress / (total / 100)));

    oledDisplay.drawFrame(0, 40, 128, 10);
    oledDisplay.drawBox(0, 40, (progress / (total / 128)), 10);

  } while ( oledDisplay.nextPage() );
}


// convert time to string
char * TimeToString(unsigned long t)
{
  static char str[13];
  int h = t / 3600000;
  t = t % 3600000;
  int m = t / 60000;
  t = t % 60000;
  int s = t / 1000;
  int ms = t - (s * 1000);
  sprintf(str, "%02ld:%02d:%02d.%03d", h, m, s, ms);
  return str;
}

#endif
