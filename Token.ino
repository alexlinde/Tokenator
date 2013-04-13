
#include <toneAC2.h>
#include <EEPROM.h>
#include <TrueRandom.h>
#include <SPI.h>
#include <Dhcp.h>
#include <Dns.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>
#include <NBEthernet.h>
#include <util.h>

#define DEBUG true // flag to turn on/off debugging
#define Serial if(DEBUG)Serial

static const int MINUTES_PER_COIN = 10;

void initDisplay(uint8_t address);
void enableDisplay(boolean state);
void writeDisplay(void);
void writeDigitNum(uint8_t d, uint8_t num);
void drawColon(boolean state);

void initHttp();
int startRequest(const char* aServerName, const char* aURLPath);
int poll();

typedef enum {
    eIdle,
    eRequestStarted,
    eRequestSent,
    eReadingStatusCode,
    eStatusCodeRead,
    eReadingHeaders,
    eReadingContentLength,
    eSkipToEndOfHeader,
    eLineStartingCRFound,
    eReadingBody,
    eComplete
} tHttpState;

EthernetClient iClient;
tHttpState iState;
int iStatusCode;
int iContentLength;
int iBodyLengthConsumed;
const char* iContentLengthPtr;
const char* statusPtr;
const char* iServerName;
const char* iURLPath;

static const int HTTP_SUCCESS =0;
static const int HTTP_ERROR_CONNECTION_FAILED =-1;
static const int HTTP_ERROR_API =-2;
static const int HTTP_ERROR_TIMED_OUT =-3;
static const int HTTP_ERROR_INVALID_RESPONSE =-4;

static const int READLIMIT = 2048;

// Ethernet shield uses 10,11,12,13 = SPI
// HDMI Switch OE digital pin -> 5
// .. actual pin is active low, but using a transistor so inverted
// Coin acceptor strobe digital pin <- 2
// Display uses I2C, 4=SDA, 5=SCL
// Speaker uses pins 9/10

#define OE_LOW 5
#define COIN_IN 2
#define SPINA 6
#define SPINB 7

byte mac[6] = { 0x90, 0xA2, 0xDA, 0x00, 0x00, 0x00 };
void setupMac() {
 if (EEPROM.read(1) == '#') {
    for (int i = 3; i < 6; i++) {
      mac[i] = EEPROM.read(i);
    }
  } else {
    for (int i = 3; i < 6; i++) {
      mac[i] = TrueRandom.randomByte();
      EEPROM.write(i, mac[i]);
    }
    EEPROM.write(1, '#');
  }
}

// the setup routine runs once when you press reset:
void setup() {     

  Serial.begin(9600);    

  pinMode(OE_LOW, OUTPUT);     
  digitalWrite(OE_LOW, LOW); // disable

  pinMode(COIN_IN, INPUT);
  digitalWrite(COIN_IN, HIGH); // pull up
  
  // look for coin pulse
  attachInterrupt(0, pulse, CHANGE);

  // attach to i2c as master  
  initDisplay(0x70);

  // beep!
  toneAC2(SPINA,SPINB,5000,1000,true);

  // make sure we have a MAC address
  // - it will be generated first time then stored in EEPROM
  setupMac();
//  char macstr[18];
//  snprintf(macstr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);  
}

volatile uint16_t coins = 0;
volatile unsigned long start = 0;
void pulse() {
  if (!start) {
    start = millis();
  } else {
    uint8_t length = millis() - start;
    if ((length > 20) && (length < 60)) {
      coins++;
    }
    start = 0;
  }
}

void showTime(uint16_t time, boolean colon) {
    uint8_t mins = time / 60;
    uint8_t secs = time % 60;
    writeDigitNum(0,mins / 10);
    writeDigitNum(1,mins % 10);
    drawColon(colon);
    writeDigitNum(3,secs / 10);
    writeDigitNum(4,secs % 10);
    writeDisplay();
}

// the loop routine runs over and over again forever:
uint16_t timeRemaining = 0;
#ifdef DEBUG
uint16_t timerState = 10;
#else
uint16_t timerState = 30;
#endif
uint8_t state = 0; // startup
bool flash = true;

static uint32_t next_connect;
static byte ethernetState = 99;
static uint16_t bytes_read;

void loop() {    

  // check if we have an IP  
  int result;
  switch(ethernetState){
    case 0:
      // waiting to initialize
      result = Ethernet.initialized();
      // 0 = still going, 1 = done, 2 = error
      if (1 == result) {      
        // done
        ethernetState = 1;
        Serial.print("obtained address: ");
        Serial.println(Ethernet.localIP());
      } else if (2 == result) {
        Serial.println("DHCP failed");
        ethernetState = 99;
        next_connect = 30;
      }
      break;
    case 1:
      // start connection or renew dhcp if needed
      if (Ethernet.maintainNeeded() != DHCP_CHECK_NONE) {
        Serial.println("Renewing DHCP lease");
        ethernetState = 10;
      } else {
        Serial.println("Starting a new connection");
        initHttp();
        bytes_read = 0;
        if (startRequest("www.google.com", "/arduino") != 0) {
          Serial.println("initializing connection failed");
          ethernetState = 99;
          next_connect = 30;
        } else {
          ethernetState = 2;
        }
      }
      break;
    case 2:
      // waiting for connection
      if (poll() != HTTP_SUCCESS) {
        // error
        iClient.stop();
        ethernetState = 99;
        next_connect = 30;
      }
      if (eReadingBody == iState) {
        Serial.println("now reading body");
        ethernetState = 3;
      }
      break;
    case 3:
      // waiting for response
      while (available()) {
        if (bytes_read < READLIMIT) {
          char c = read();
          Serial.print(c);
          bytes_read++;
        } else {
          iClient.flush();
        }
      }
      
      if (!iClient.connected()) {
        Serial.println();
        Serial.println("disconnecting.");
        iClient.stop();
        ethernetState = 4;
        next_connect = 30;
        break;
      }

      break;
    case 4:
      // timer until we want to poll again
//      Serial.println("waiting to poll again");
      if (!next_connect) {
        ethernetState = 1;
      }
      break;
    case 10:
      // renewing lease
      result = Ethernet.maintainFinished();
      if (1 == result) {
        // done
        Serial.println("DHCP lease renewed");
        ethernetState = 1;
        Serial.println(Ethernet.localIP());
      } else if (2 == result) {
        Serial.println("DHCP failed");
        ethernetState = 99;
        next_connect = 30;        
      }
      break;
    case 99:
      // something failed, try again in 30 seconds
      if (!next_connect) {
        // start looking for a DHCP address
        Serial.println("Starting Ethernet / DHCP");
        Ethernet.begin(mac);
        ethernetState = 0;
      }
      break;
  }
  
  // check for inserted coins
  if (coins) {
    digitalWrite(OE_LOW, HIGH); // enable
#ifdef DEBUG
    timeRemaining += 10;
#else
    timeRemaining += MINUTES_PER_COIN * 60;
#endif

    coins--;
    state = 2;
    flash = true;
    enableDisplay(true);
  }
  
  // state machine
  if (state == 0) {    
    showTime(0,flash);
    if (!timerState) {
      // display off
      enableDisplay(false);
      state = 1;
    } else {
      if (!flash) {
        timerState--;
      }
    }
  } else if (state == 1) {
    // paused
  } else if (state == 2) {
    // show timer
    showTime(timeRemaining,flash);
    if (!timeRemaining) {
      // time up
      digitalWrite(OE_LOW, LOW); // disable
      toneAC2(SPINA,SPINB,5000,1000,true);
        
      // flash for 30 secs then power down
      state = 0;
#ifdef DEBUG
      timerState = 10;
#else
      timerState = 30;
#endif
    } else {
      if (!flash) {
        timeRemaining--;
      } else if (timeRemaining < 30) {
        toneAC2(SPINA,SPINB,5000,200,true);      // runnin
      }
    }
  }
  delay(500);  
  flash = !flash;
  if (!flash && next_connect) {
    next_connect--;
  }
}

