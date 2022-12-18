//////////////////////////////////////////////////////////////////////////////////
/*     
    Atari Portfolio WiFi Bridge
    Lennart Hennigs 02/22 
      
- 2048 is the Upload buffer
- 28672 is the portfolio buffer = 14 * 2048 (HTTP_UPLOAD_BUFLEN)

*/
//////////////////////////////////////////////////////////////////////////////////
// Using a D1 Mini:
#if defined (ARDUINO_ESP8266_WEMOS_D1MINI)
//
//                  D1 for I2C Display
//                  D2 for I2C Display
#define BTN_PIN     D3
//                  D4 for LED_BUILTIN
#define CLK_OUT     D5  // --> D'SUB25 pin  3
#define DATA_OUT    D6  // --> D'SUB25 pin  2
#define DATA_IN     D7  // --> D'SUB25 pin 13
#define CLK_IN      D8  // --> D'SUB25 pin 12
//                  GND // --> D'SUB25 pin 18

//////////////////////////////////////////////////////////////////////////////////

#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <FS.h>

//////////////////////////////////////////////////////////////////////////////////

#include "Portfolio.h"
#include "Display.h"

//////////////////////////////////////////////////////////////////////////////////

#define AP_NAME         "Portfolio"

#define SERIAL_SPEED    9600
#define WEBSERVER_PORT  80

#define VERSION         "1.2.0"

//////////////////////////////////////////////////////////////////////////////////

ESP8266WebServer server(WEBSERVER_PORT);
WiFiManager wifiManager;

bool uploadOK = true;
int payloadSize = 0;

//////////////////////////////////////////////////////////////////////////////////

// TODO: needed?
int sourcecount = 0; 

//////////////////////////////////////////////////////////////////////////////////

void handleFileList() {
//  server.send(200, "text/json", "[{\"name\": \"file A\"},{\"name\": \"file C\"},{\"name\": \"file B\"}]");
//  return;
   
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "Parameter 'dir' is missing");
    return;
  }
  char *name;
  String path = server.arg("dir");

  printToRow(4, F("LIST"));
  printToRow(5, path);
  
  strncpy((char*)receiveInit + 3, path.c_str(), MAX_FILENAME_LEN);
  sendBlock(receiveInit, sizeof(receiveInit));
  receiveBlock(payload, PAYLOAD_BUFSIZE);
  int num = payload[0] + (payload[1] << 8);

  String output = "[";
  name = (char*)payload + 2;
  for (int i = 0; i < num; i++) {
    Serial.printf(" - %s\n", name);
    if (output != "[") {
      output += ',';
    }
    output += "{\"name\":\"";
    output += name;
    output += "\"}";
    name += strlen(name) + 1;
  }
  output += "]";
  server.send(200, "text/json", output);
  
  printToRow(4, "LIST: DONE");
  clearRow(5);
}

//////////////////////////////////////////////////////////////////////////////////

void handleRoot() {
  Serial.println("/index.html requested");
  File index = SPIFFS.open(F("/index.html"), "r");
  if (!index) {
    server.send(404, F("text/plain"), F("Error opening index.html"));
    Serial.println("ERROR index");
    Serial.println();
    return;
  }

  int size = index.size();
  server.sendHeader("Content-Length", String(size));
  server.sendHeader("Connection", "Keep-Alive");
  
// !!!   server.streamFile(index, F("text/html"));
  size_t sent = server.streamFile(index, F("text/html"));
  server.send(200);

  Serial.printf(" %d Bytes sent!", sent);
  Serial.printf(" %d index size", size);
  Serial.println("DONE");
  Serial.println();
}

//////////////////////////////////////////////////////////////////////////////////

void handleWiFiReset() {
  resetWifiManager();
  server.send(200, F("text/plain"), F("WiFi credentials deleted!"));
}

//////////////////////////////////////////////////////////////////////////////////

void handleFormatSpiffs() {
  SPIFFS.format();
  server.send(200, F("text/plain"), F("SPIFFS formated!"));
}

//////////////////////////////////////////////////////////////////////////////////

// TODO: FIX
void handleDownload() {
  File download = SPIFFS.open("/TEST3.TXT", "r");
  server.sendHeader(F("Content-Type"), F("text/plain"));
  server.sendHeader(F("Content-Disposition"), F("attachment; filename=") + String("TEST3.TXT"));
  server.sendHeader(F("Connection"), F("close"));
  server.streamFile(download, F("application/octet-stream"));
  download.close();
}

//////////////////////////////////////////////////////////////////////////////////

String formatBytes(size_t bytes) {
  if (bytes < 1024) return String(bytes) + "B";
  if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + "KB";
  if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + "MB";
  else return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
}

//////////////////////////////////////////////////////////////////////////////////

bool initateUpload(const char* filename, int len, bool overwrite = true) {
  char res = sendTransmitInit(len, filename);
  if (res == 0x10) {
    Serial.println("Invalid destination file!\n");
    return false;
  } else if (res == 0x20) {
    Serial.println("File exists on Portfolio");
    if (overwrite) {
      Serial.println("Overwriting file");
      sendTransmitOverwrite();
    } else {
      Serial.println("Cancelling");
      sendTransmitCancel();
      return false;
    }
  } 
  Serial.println("--- initFT");
  // internal check - TODO: could be deleted
  int blocksize = controlData[1] + (controlData[2] << 8);
  if (blocksize > PAYLOAD_BUFSIZE) {
    Serial.print("Payload buffer too small!\n");
    return false;
  }
  Serial.print("filename: "); Serial.println(filename);
  Serial.print("blocksize: "); Serial.println(blocksize);
  Serial.print("len: "); Serial.println(len);
  Serial.println("---");
  return true;
}

//////////////////////////////////////////////////////////////////////////////////

void resetPayloadBuffer() {
  payloadSize = 0;
  payload[0] = 0x00;
}

//////////////////////////////////////////////////////////////////////////////////

bool initRecieve(HTTPUpload& upload) {
  resetPayloadBuffer();
  int size = server.arg("fileSize").toInt();
  Serial.printf("%s - %s", upload.filename, size);
  if (size == 0) return false; 
  return initateUpload(String("C:\\" + upload.filename).c_str(), size);
}

//////////////////////////////////////////////////////////////////////////////////

void receiveBlocks(HTTPUpload& upload) {
  Serial.print(".");
  // is the payload buffer full?
  if (payloadSize + upload.currentSize >= PAYLOAD_BUFSIZE) {
    Serial.print(":");
    uploadOK = sendBlock(payload, payloadSize);
    resetPayloadBuffer();
  }
  // add content block to payload
  // !!! ERROR !!
  memcpy((char*)payload + payloadSize, (char*)upload.buf, upload.currentSize);
  payloadSize += upload.currentSize;
}      


//////////////////////////////////////////////////////////////////////////////////

void finishReceive(HTTPUpload& upload) {
  Serial.print("+");
  Serial.println();
  // do a final upload, if needed
  if (payloadSize > 0) {
    uploadOK = sendBlock(payload, payloadSize);
    Serial.print("Final transmit: ");
    Serial.println(uploadOK ? "OK" : "failed");
  }

  // is the Portfolio happy?
  if (!uploadOK || !sendTransmitDone()) {
    Serial.println("Error finishing up");
    server.send(500, "text/plain", "500: couldn't create file");
    return;
    
  Serial.println("Upload: OK");
  Serial.println(payloadSize);

  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "");        
  // TO DO: redirect to root and add a "success" parameter for a message...
  }
}

//////////////////////////////////////////////////////////////////////////////////

void handleReceiveErrors(HTTPUpload& upload) {
  if (upload.status == UPLOAD_FILE_END) {
    Serial.println("Upload Error");
    server.send(500, "text/plain", "500: upload error");
    sendBlock(transmitCancel, sizeof(transmitCancel));
  } else if (server.arg("fileSize").toInt() == 0){
    Serial.println("File does not exist or is empty!");
    server.sendHeader("Location", String("/"), true);
    server.send(302, "text/plain", "");
  } else {
  // needed?      
    Serial.println("some other error?");
    server.send(400, "text/plain", "");
  }
}

//////////////////////////////////////////////////////////////////////////////////
// this will be called multiple times

void handleUpload() {
  HTTPUpload& upload = server.upload();
  // TO DO: check upload parameter

  if (!uploadOK) {
    handleReceiveErrors(upload);
    return;
  }
  if (upload.status == UPLOAD_FILE_START) {
      uploadOK = initRecieve(upload);
  // receive (n) blocks
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    receiveBlocks(upload);
  } else if (upload.status == UPLOAD_FILE_END) {
    finishReceive(upload);
  }
}

/* ------------------------------------------------- */

void setupWebserver() {
  server.on("/", handleRoot);
  server.on("/list", HTTP_GET, handleFileList);
  server.on("/resetWifi", HTTP_GET, handleWiFiReset);
  server.on("/format", HTTP_GET, handleFormatSpiffs);
  server.on("/upload", HTTP_POST, [](){ server.send(200); }, handleUpload);
//  server.on("/download", HTTP_GET, handleDownload);
//  server.on("/show", HTTP_GET, handleDump);
    server.onNotFound([](){
      server.send(404, F("text/plain"), "404: Not page found!");
    });
  server.begin();
}

/* ------------------------------------------------- */

bool setupSpiffs() {  
  bool res = !SPIFFS.begin();
  if(!res) {
    Serial.println(F("SPIFFS mount error!"));
  } else {
    Serial.println(F("SPIFFS ok!"));
  }
  return res;
}

/* ------------------------------------------------- */

void setupSerial() {
  Serial.begin(SERIAL_SPEED);  
  delay(200);
  Serial.println(F("\n\n"));
}

/* ------------------------------------------------- */

void setupWiFi() {
  wifiManager.setAPCallback([](WiFiManager *myWiFiManager){
    printToRow(2, F("Opened AP"));
    printToRow(3, myWiFiManager->getConfigPortalSSID());
    printToRow(4, WiFi.softAPIP().toString());
  });
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setDebugOutput(false);
  wifiManager.autoConnect(AP_NAME);
  
  if (MDNS.begin(AP_NAME)) {
    Serial.println(F("mDSN started"));
  }
}

/* ------------------------------------------------- */

void setupPins() {
  // LPT pins
  pinMode(DATA_OUT, OUTPUT);
  pinMode(CLK_OUT, OUTPUT);
  pinMode(DATA_IN, INPUT);
  pinMode(CLK_IN, INPUT);
  // reset button
  pinMode(BTN_PIN, INPUT);
  // internal LED (and turn it off)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  
}

/* ------------------------------------------------- */

void resetWifiManager() {
  wifiManager.resetSettings();
  Serial.println("RESET!");
}

/* ------------------------------------------------- */
/* ------------------------------------------------- */

void setup() {
  setupPins();
  initDisplay();

  setupSerial();

  Serial.println();
  Serial.println("PortfolioWiFi " + String(VERSION));
  Serial.println(String("Compiled: ") + __DATE__ + String(" - ") + __TIME__);
  Serial.println();


  printToRow(0, F("APB"));
  // reset button pressed?
  if (digitalRead(BTN_PIN) == LOW) {
    resetWifiManager();
  }
  setupSpiffs();
  setupWiFi();
  
  printToRow(3, "http://" + WiFi.localIP().toString(), false);
  setupWebserver();

  if (!allocateMemory()) {
    printToRow(2, F("Out of memory!"));
    for(;;);
  }
  Serial.println(formatBytes(ESP.getFreeHeap()) + " free heap");
  Serial.println();
}

/* ------------------------------------------------- */

void loop() {
  server.handleClient();
}

/* ------------------------------------------------- */
#else
  #error "Use this code on a D1 Mini"
#endif
