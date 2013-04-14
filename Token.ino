
#include <toneAC2.h>
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
//void enableDisplay(boolean state);
void writeDisplay(void);
void writeDigitNum(uint8_t d, uint8_t num);
void drawColon(boolean state);
void clearDisplay(void);

int pollSocket();

static const int HTTP_SUCCESS =0;
static const int HTTP_ERROR_CONNECTION_FAILED =-1;
static const int HTTP_ERROR_API =-2;
static const int HTTP_ERROR_TIMED_OUT =-3;
static const int HTTP_ERROR_INVALID_RESPONSE =-4;

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

bool flash = true;

void showTime(uint16_t time, boolean colon) {
  clearDisplay();
  uint8_t mins = time / 60;
  uint8_t secs = time % 60;
  writeDigitNum(0,mins / 10);
  writeDigitNum(1,mins % 10);
  drawDots(colon ? 0x02 : 0);
  writeDigitNum(3,secs / 10);
  writeDigitNum(4,secs % 10);
  if (isEndpointConnected() || flash) {
    drawDots(0x04);
  }
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

void addTime(uint16_t secs) {
  Serial.print("Adding ");
  Serial.print(secs);
  Serial.println(" seconds");
  timeRemaining += secs;
  state = 2;
  flash = true;
//  enableDisplay(true);
}

void loop() {    
  // keep network going
  pollSocket();
  
  // check for inserted coins
  if (coins) {
    digitalWrite(OE_LOW, HIGH); // enable
    addTime(MINUTES_PER_COIN * 60);
    coins--;
  }
  
  // state machine
  if (state == 0) {    
    showTime(0,flash);
    if (!timerState) {
      // display off
//      enableDisplay(false);
      state = 1;
    } else {
      if (!flash) {
        timerState--;
      }
    }
  } else if (state == 1) {
    clearDisplay();
    if (isEndpointConnected() || flash) {
      drawDots(0x04);
    }      
    writeDisplay();
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
      timerState = 30;
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
}

