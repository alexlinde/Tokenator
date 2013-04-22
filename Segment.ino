#include <Wire.h>

static uint8_t _addr;
static uint16_t _displaybuffer[8];

const uint8_t HT16K33_SETUP_CMD = 0x20;
const uint8_t HT16K33_SETUP_ON = 0x01;

const uint8_t HT16K33_CMD_BRIGHTNESS = 0x0E;

const uint8_t HT16K33_BLINK_CMD = 0x80;
const uint8_t HT16K33_BLINK_DISPLAYON = 0x01;

enum {
    HT16K33_BLINK_OFF = 0,
    HT16K33_BLINK_2HZ = 1,
    HT16K33_BLINK_1HZ = 2,
    HT16K33_BLINK_HALFHZ = 3
};

// 0x0002 => middle colon
// 0x0004 => top-left dot
// 0x0008 => bottom-left dot
// 0x0010 => top-right dot
enum {
  MIDDLE_COLON = 0x02,
  TOPLEFT_DOT = 0x04,
  BOTTOMLEFT_DOT = 0x08,
  TOPRIGHT_DOT = 0x10
};

// PGFEDCBA
//        A
//      -----
//   F |     | B
//      --G--
//   E |     | C
//      -----
//        D
static const uint8_t numbertable[] = {
	0x3F, /* 0 */
	0x06, /* 1 */
	0x5B, /* 2 */
	0x4F, /* 3 */
	0x66, /* 4 */
	0x6D, /* 5 */
	0x7D, /* 6 */
	0x07, /* 7 */
	0x7F, /* 8 */
	0x6F, /* 9 */
	0x77, /* a */
	0x7C, /* b */
	0x39, /* C */
	0x5E, /* d */
	0x79, /* E */
	0x71, /* F */
};

void setBrightness(uint8_t b) {
    if (b > 15) b = 15;
    Wire.beginTransmission(_addr);
    Wire.write(HT16K33_CMD_BRIGHTNESS | b);
    Wire.endTransmission();
}

void enableDisplay(boolean state) {
}

void initDisplay(uint8_t address) {
    _addr = address;
    Wire.begin(_addr);
    Wire.beginTransmission(_addr);
    Wire.write(HT16K33_SETUP_CMD | HT16K33_SETUP_ON);  // turn on oscillator
    Wire.write(HT16K33_BLINK_CMD | HT16K33_BLINK_DISPLAYON);
    Wire.endTransmission();
    setBrightness(15); // max brightness
}

void clearDisplay(void) {
  memset(_displaybuffer,0,16);
}

void writeDisplay(void) {
    Wire.beginTransmission(_addr);
    Wire.write((uint8_t)0x00); // start at address $00
    
    for (uint8_t i=0; i<8; i++) {
        Wire.write(_displaybuffer[i] & 0xFF);
        Wire.write(_displaybuffer[i] >> 8);
    }
    Wire.endTransmission();
}

void writeDigitNum(uint8_t d, uint8_t num) {
    if (d > 4) return;
    _displaybuffer[d] = numbertable[num];
}

void drawDots(uint8_t mask) {

    _displaybuffer[2] |= mask;
}

