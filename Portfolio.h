/* ------------------------------------------------- */
/*
  Functions to access the Atari Portfolio's Parallel Port
  Lennart Hennigs, 2022

  taken & converted from:
  - https://github.com/skudi/transfolio/blob/master/transfolio.c
  - https://github.com/petrkr/PortfolioESPlink/blob/master/PortfolioESPlink.ino  

  details on the protocol (in German):
  - http://www.pofowiki.de/doku.php?id=software:vorstellung:exchanges:transfolio
*/
/* ------------------------------------------------- */

unsigned char transmitInit[90] = { 
  /* Offset 0: Funktion */
  0x03, 
  /* Offset 7: Dateilaenge */
  0x00, 0x70, 0x0C, 0x7A, 0x21, 0x32,
  /* Offset 11: Pfad */
  0, 0, 0, 0
};
unsigned char receiveInit[82] = { 
  /* Offset 0: Funktion */
  0x06, 
  /* Offset 2: Puffergroesse = 28672 Byte */
  0x00, 0x70    
  /* Offset 3: Pfad */
};

/* ------------------------------------------------- */

const unsigned char transmitOverwrite[3] = { 0x05, 0x00, 0x70 };
const unsigned char transmitCancel[3] = { 0x00, 0x00, 0x00 };
const unsigned char receiveFinish[3] = { 0x20, 0x00, 0x03 };

/* ------------------------------------------------- */

unsigned char *payload;
unsigned char *controlData;
unsigned char *list;

#define PAYLOAD_BUFSIZE     28672
#define CONTROL_BUFSIZE     100
#define LIST_BUFSIZE       2000
#define MAX_FILENAME_LEN     79

/* ------------------------------------------------- */

static inline void writePort(const unsigned char data);
static inline void waitClockHigh();
static inline void waitClockLow();
static inline unsigned char getBit();
void syncTick();
unsigned char receiveByte();
void sendByte(unsigned char data);
bool sendBlock(const unsigned char *pData, const unsigned int len);
int receiveBlock(unsigned char *pData, const int maxLen);
bool detectPortfolio();
bool allocateMemory();
char sendTransmitInit(int len, const char* filename);
bool sendTransmitDone();

/* ------------------------------------------------- */

char sendTransmitInit(int len, const char* filename) {
  transmitInit[7] = len & 255;
  transmitInit[8] = (len >> 8) & 255;
  transmitInit[9] = (len >> 16) & 255;
  strncpy((char*)transmitInit + 11, filename, MAX_FILENAME_LEN);    
  sendBlock(transmitInit, sizeof(transmitInit));
  // check answer
  receiveBlock(controlData, CONTROL_BUFSIZE);
  return controlData[0];
}

/* ------------------------------------------------- */

bool sendTransmitOverwrite() {
  return sendBlock(transmitOverwrite, sizeof(transmitOverwrite));
}

/* ------------------------------------------------- */

bool sendTransmitCancel() {
  return sendBlock(transmitCancel, sizeof(transmitCancel));
}  

/* ------------------------------------------------- */

bool sendTransmitDone() {
  receiveBlock(controlData, CONTROL_BUFSIZE);
//  Serial.println(controlData[0], HEX);
  return (controlData[0] == 0x20);
}

/* ------------------------------------------------- */

bool allocateMemory() {
  payload = (unsigned char*) malloc(PAYLOAD_BUFSIZE);
  controlData = (unsigned char*) malloc(CONTROL_BUFSIZE);
  list = (unsigned char*) malloc(LIST_BUFSIZE);
  return !(payload == NULL || controlData == NULL || list == NULL);
}

/* ------------------------------------------------- */

static inline void writePort(const unsigned char data) {
  digitalWrite(DATA_OUT, data & 1);
  digitalWrite(CLK_OUT, data & 2);
}

/* ------------------------------------------------- */

static inline void waitClockHigh() {
  while (!digitalRead(CLK_IN)) {
    yield();
  }
}

/* ------------------------------------------------- */

static inline void waitClockLow() {
  while (digitalRead(CLK_IN)) {
    yield();
  }
}

/* ------------------------------------------------- */

static inline unsigned char getBit() {
  return digitalRead(DATA_IN);
}

/* ------------------------------------------------- */

void syncTick() {
  waitClockLow();
  writePort(0);
  waitClockHigh();
  writePort(2);
}

/* ------------------------------------------------- */

unsigned char receiveByte() {
  unsigned char recv;

  for (int i = 0; i < 4; i++) {
    waitClockLow();
    recv = (recv << 1) | getBit();
    writePort(0);
    waitClockHigh();
    recv = (recv << 1) | getBit();
    writePort(2);
  }
  return recv;
}

/* ------------------------------------------------- */

void sendByte(unsigned char data) {
  unsigned char b;

  delayMicroseconds(250);
  for (int i = 0; i < 4; i++) {
    b = ((data & 0x80) >> 7) | 2;     /* Output data bit */
    writePort(b);
    b = (data & 0x80) >> 7;           /* Set clock low   */
    writePort(b);

    data = data << 1;
    waitClockLow();

    b = (data & 0x80) >> 7;           /* Output data bit */
    writePort(b);
    b = ((data & 0x80) >> 7) | 2;     /* Set clock high  */
    writePort(b);

    data = data << 1;    
    waitClockHigh();
  }
}

/* ------------------------------------------------- */

bool detectPortfolio() {
  byte recv;
  syncTick();
  recv = receiveByte();
  return recv == 90;
}

/* ------------------------------------------------- */

bool sendBlock(const unsigned char *pData, const unsigned int len) {
  unsigned char recv;
  unsigned int  i;
  unsigned char lenH, lenL;
  unsigned char checksum = 0;

  if (!len) return false;
  recv = receiveByte();
  if (recv != 'Z') {
    Serial.println( "Portfolio not ready!\n");
    return false;        
  }
  delayMicroseconds(50000);
  sendByte(0x0a5);
  
  lenH = len >> 8;
  lenL = len & 255;
  sendByte(lenL); checksum -= lenL;
  sendByte(lenH); checksum -= lenH;
  
  for (i = 0; i < len; i++) {
    recv = pData[i];
    sendByte(recv); checksum -= recv;
  }
  sendByte(checksum);
  recv = receiveByte();
  
  if (recv != checksum) {
      Serial.printf( "checksum ERR: %d\n", recv);
      return false;
  }
  return true;
}

/* ------------------------------------------------- */

int receiveBlock(unsigned char *pData, const int maxLen) {
  unsigned int len, i;
  unsigned char lenH, lenL;
  unsigned char checksum = 0;
  unsigned char recv;

  sendByte('Z');
  recv = receiveByte();
  if (recv != 0x0a5) {
    Serial.printf( "Acknowledge ERROR (received %2X instead of A5)\n", recv);
    return 0;
  }
  lenL = receiveByte();  checksum += lenL;
  lenH = receiveByte();  checksum += lenH;
  len = (lenH << 8) | lenL;

  if (len > maxLen) {
      Serial.printf( "Receive buffer too small (%d instead of %d bytes).\n", maxLen, len);
    return 0;
  }

  for (i = 0; i < len; i++) {
    unsigned char recv = receiveByte();
    checksum += recv;
    pData[i] = recv;
  }
  recv = receiveByte();
  if ((unsigned char)(256 - recv) != checksum) {
      Serial.printf( "checksum ERR %d %d\n", (unsigned char)(256 - recv), checksum);
      return 0;
  }
  delayMicroseconds(100);
  sendByte((unsigned char)(256 - checksum));
  return len;
}
