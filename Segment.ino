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
    Wire.beginTransmission(_addr);
    Wire.write(HT16K33_SETUP_CMD | (state ? HT16K33_SETUP_ON : 0));  // turn on oscillator
    Wire.endTransmission();
    Wire.beginTransmission(_addr);
    Wire.write(HT16K33_BLINK_CMD | (state ? HT16K33_BLINK_DISPLAYON : 0));
    Wire.endTransmission();
}

void initDisplay(uint8_t address) {
    _addr = address;
    Wire.begin(_addr);
    enableDisplay(true);
    setBrightness(15); // max brightness
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

void drawColon(boolean state) {
    _displaybuffer[2] = (state ? 0x2 : 0);
}

