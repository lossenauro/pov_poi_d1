// Use only esp8266 wemos arduino ver 2.5.0
//This sketch is taking Adafruit's Supernova Poi code and is modified to use FastLed with ESP8266. 
//Using #include graphicsNoprogmem.h and using imageinit() in void setup, the poi work but store patterns in RAM, limiting number of patterns. 
//Using #include graphicswithprogmem.h and imageinitwithprogmem in void setup, the poi contiually restart. 
//Seeking help to store patterns in flash memory and increase number of patterns stored in flash.

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
const uint16_t kRecvPin = D3;
#include <PGMSPACE.h>
#include <Arduino.h>
#include "FastLED.h"
#define NUM_LEDS 36

typedef uint16_t line_t;

boolean autoCycle = true; // Set to true to cycle images by default
uint8_t CYCLE_TIME = 10; // Time, in seconds, between auto-cycle images
#define kRecvPin D3
IRrecv irrecv(kRecvPin);
decode_results results;  // Somewhere to store the results

//   Button       Code         Button  Code
//   -----------  ------       ------  -----
//   *            FF6897       0:      FF9867
//   #            FFB04F       1:      FFA25D
//                             2:      FF629D 
//                             3:      FFE21D
//   OK:          FF38C7       4:      FF22DD
//   UP:          FF18E7       5:      FF02FD
//   DOWN:        FF4AB5       6:      FFC23D
//   LEFT:        FF10EF       7:      FFE01F
//   RIGHT:       FF5AA5       8:      FFA857
//                             9:      FF906F  
#define BTN_BRIGHT_UP    0xFFB04F //   #
#define BTN_BRIGHT_DOWN  0xFF6897 //   *
#define BTN_RESTART      0xFF629D //   2
#define BTN_BATTERY      0xFF9867 //   0
#define BTN_FASTER       0xFF18E7 //   UP
#define BTN_SLOWER       0xFF4AB5 //   DOWN
#define BTN_OFF          0xFFA857 //   8
#define BTN_PATTERN_PREV 0xFF10EF //   LEFT
#define BTN_PATTERN_NEXT 0xFF5AA5 //   RIGHT
#define BTN_AUTOPLAY     0xFF38C7 //   ok
#define BTN_CYCLE_DOWN   0xFF22DD //   4
#define BTN_CYCLE_UP     0xFFC23D //   6
#define BTN_NONE         -1

CRGB leds[NUM_LEDS];
// #include <SPI.h> // Enable this line on Pro Trinket

//These are attempts to change how progmem stores in memory. 
//#define PROGMEM   ICACHE_RODATA_ATTR
//#define ICACHE_RODATA_ATTR  __attribute__((section(".irom.text")))
//#define PGM_P       const char * 
//#define PGM_VOID_P  const void * 
//#define PSTR(s) (__extension__({static const char __c[] PROGMEM = (s); &__c[0];}))
//#undef pgm_read_byte(addr)

#define pgm_read_byte(addr)                                                \
(__extension__({                                                               \
    PGM_P __local = (PGM_P)(addr);  /* isolate varible for macro expansion */         \
    ptrdiff_t __offset = ((uint32_t)__local & 0x00000003); /* byte aligned mask */            \
    const uint32_t* __addr32 = (const uint32_t*)((const uint8_t*)(__local)-__offset); \
    volatile uint8_t __result = ((*__addr32) >> (__offset * 8));                        \
    __result;                                                                  \
}))

//Include one of these, but make sure to include the matching part in void setup.
#include "graphicswithProgmem.h"; //stores patterns using progmem but causes board to constantly restart <<<<< no, it is not in WEMOS d1 mini
//#include "graphicsNoprogmem.h";   //works but does not store in flash, see void setup and ccomment out imageinit or imageinitwithprogmem

#define DATA_PIN  D2
#define CLOCK_PIN D1

void     imageInit(void),
         IRinterrupt(void); // TRY IR
void     imageInitwithProgmem(void);

void setup() {
  
  //FastLED.setBrightness(BRIGHTNESS);
  FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds, NUM_LEDS);
  FastLED.show(); // before measuring battery
  
   // modify this part along with the #include graphics file part above
   // imageInitwithProgmem();    //use with graphicwithProgmem.h  <<<<<<<<<<<<<<<<<<<<<<<<<include this line and comment out imaaginit or imagewithprogmem.    // NO-NO! Use as is
      imageInit();               //use with graphicsNoprogmem.h  <<<<<<<<<<<  NO-NO! Use as is

  // attachInterrupt(1, IRinterrupt, CHANGE); // IR remote interrupt
  irrecv.enableIRIn(); // Start the receiver

}
// GLOBAL STATE STUFF ------------------------------------------------------

uint32_t lastImageTime = 0L, // Time of last image change
         lastLineTime  = 0L;
uint8_t  imageNumber   = 0,  // Current image being displayed
         imageType,          // Image type: PALETTE[1,4,8] or TRUECOLOR
        *imagePalette,       // -> palette data in PROGMEM
        *imagePixels,        // -> pixel data in PROGMEM
         palette[16][3];     // RAM-based color table for 1- or 4-bit images
line_t   imageLines,         // Number of lines in active image
         imageLine;          // Current line number in image

volatile uint16_t irCode = BTN_NONE; // Last valid IR code received
const uint8_t PROGMEM brightness[] = {15, 31, 63, 127, 254};
uint8_t bLevel = sizeof(brightness) - 1;

// Microseconds per line for various speed settings
const uint16_t PROGMEM lineTable[] = {
  1000000L /  375,
  1000000L /  472,
  1000000L /  595,
  1000000L /  750,
  1000000L /  945,
  1000000L / 1191,
  1000000L / 1500
};
uint8_t  lineIntervalIndex = 3;
uint16_t lineInterval      = 1000000L / 750;

void imageInit() { // Works with graphics.h but does not read from PROGMEM
  imageType    = images[imageNumber].type;
  imageLines   = images[imageNumber].lines;
  imageLine    = 0;
  imagePalette = (uint8_t *)images[imageNumber].palette;
  imagePixels  = (uint8_t *)images[imageNumber].pixels;
  // 1- and 4-bit images have their color palette loaded into RAM both for
  // faster access and to allow dynamic color changing.  Not done w/8-bit
  // because that would require inordinate RAM (328P could handle it, but
  // I'd rather keep the RAM free for other features in the future).
  if(imageType == PALETTE1)      memcpy_P(palette, imagePalette,  2 * 3);
  else if(imageType == PALETTE4) memcpy_P(palette, imagePalette, 16 * 3);
  lastImageTime = millis(); // Save time of image init for next auto-cycle
}

void imageInitwithProgmem() { // this version is not working on ESP8266 but is used to read from PROGMEM 
  imageType    = pgm_read_byte(&images[imageNumber].type);
  imageLines   = pgm_read_word(&images[imageNumber].lines);
  imageLine    = 0;
  imagePalette = (uint8_t *)pgm_read_word(&images[imageNumber].palette); 
  imagePixels  = (uint8_t *)pgm_read_word(&images[imageNumber].pixels);
  // 1- and 4-bit images have their color palette loaded into RAM both for
  // faster access and to allow dynamic color changing.  Not done w/8-bit
  // because that would require inordinate RAM (328P could handle it, but
  // I'd rather keep the RAM free for other features in the future).
  if(imageType == PALETTE1)      memcpy_P(palette, imagePalette,  2 * 3);
  else if(imageType == PALETTE4) memcpy_P(palette, imagePalette, 16 * 3);
  lastImageTime = millis(); // Save time of image init for next auto-cycle
}

void nextImage(void) {
  if(++imageNumber >= NUM_IMAGES) imageNumber = 0;
  imageInit();
}

void prevImage(void) {
  imageNumber = imageNumber ? imageNumber - 1 : NUM_IMAGES - 1;
  imageInit();
}

// MAIN LOOP ---------------------------------------------------------------

void loop() {
  uint32_t t = millis();               // Current time, milliseconds
  if(autoCycle) {
    if((t - lastImageTime) >= (CYCLE_TIME * 1000L)) nextImage();
  }


  switch(imageType) {

    case PALETTE1: { // 1-bit (2 color) palette-based image
      uint8_t  pixelNum = 0, byteNum, bitNum, pixels, idx,
              *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS / 8];
      for(byteNum = NUM_LEDS/8; byteNum--; ) { // Always padded to next byte
        pixels = pgm_read_byte(ptr++);  // 8 pixels of data (pixel 0 = LSB)
        for(bitNum = 8; bitNum--; pixels >>= 1) {
          idx = pixels & 1; // Color table index for pixel (0 or 1)
          leds[pixelNum++] = CRGB (palette[idx][0], palette[idx][1], palette[idx][2]);
        }
      }
      break;
    }

    case PALETTE4: { // 4-bit (16 color) palette-based image
      uint8_t  pixelNum, p1, p2,
              *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS / 2];
      for(pixelNum = 0; pixelNum < NUM_LEDS; ) {
        p2  = pgm_read_byte(ptr++); // Data for two pixels...
        p1  = p2 >> 4;              // Shift down 4 bits for first pixel
        p2 &= 0x0F;                 // Mask out low 4 bits for second pixel
       leds[pixelNum++] = CRGB (palette[p1][0], palette[p1][1], palette[p1][2]);
       leds[pixelNum++] = CRGB (palette[p2][0], palette[p2][1], palette[p2][2]);
      }
      break;
    }

    case PALETTE8: { // 8-bit (256 color) PROGMEM-palette-based image
      uint16_t  o;
      uint8_t   pixelNum,
               *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS];
      for(pixelNum = 0; pixelNum < NUM_LEDS; pixelNum++) {
        o = pgm_read_byte(ptr++) * 3; // Offset into imagePalette
      leds[pixelNum++] = CRGB(
          pgm_read_byte(&imagePalette[o]),
          pgm_read_byte(&imagePalette[o + 1]),
          pgm_read_byte(&imagePalette[o + 2]));
      }
      break;
    }

    case TRUECOLOR: { // 24-bit ('truecolor') image (no palette)
      uint8_t  pixelNum, r, g, b,
              *ptr = (uint8_t *)&imagePixels[imageLine * NUM_LEDS * 3];
      for(pixelNum = 0; pixelNum < NUM_LEDS; pixelNum++) {
        r = pgm_read_byte(ptr++);
        g = pgm_read_byte(ptr++);
        b = pgm_read_byte(ptr++);
       leds[pixelNum] = (pixelNum, r, g, b);
      }
      break;
    }
  }
  
 if(++imageLine >= imageLines) imageLine = 0; // Next scanline, wrap around
 
  IRinterrupt();
  while(((t = micros()) - lastLineTime) < lineInterval) {
    if(results.value != BTN_NONE) {
      //if(!FastLED.getBrightness()) { // If strip is off...
      // Set brightness to last level
      //  FastLED.setBrightness(brightness[bLevel]);
      // and ignore button press (don't fall through)
      // effectively, first press is 'wake'
      //} else 
      {
        switch(results.value) {
         case BTN_BRIGHT_UP:
          if(bLevel < (sizeof(brightness) - 1))
            FastLED.setBrightness(brightness[++bLevel]);
          break;
         case BTN_BRIGHT_DOWN:
          if(bLevel)
            FastLED.setBrightness(brightness[--bLevel]);
          break;
         case BTN_FASTER:
          if(lineIntervalIndex < (sizeof(lineTable) / sizeof(lineTable[0]) - 1))
           lineInterval = lineTable[++lineIntervalIndex];
          break;
         case BTN_SLOWER:
          if(lineIntervalIndex)
           lineInterval = lineTable[--lineIntervalIndex];
          break;
         case BTN_CYCLE_UP:
          if(CYCLE_TIME < 20) CYCLE_TIME++; // Soft-limit for me
          break;
         case BTN_CYCLE_DOWN:
          if(CYCLE_TIME > 1) CYCLE_TIME--;
          break;
         case BTN_RESTART:
          imageNumber = 0;
          imageInit();
          break;
         case BTN_OFF:
          FastLED.setBrightness(0);
          break;
         case BTN_PATTERN_PREV:
          prevImage();
          break;
         case BTN_PATTERN_NEXT:
          nextImage();
          break;
         case BTN_AUTOPLAY:
          autoCycle = !autoCycle;
          break;
        }
      }
      results.value = BTN_NONE;
    }
  }


    FastLED.show(); // Refresh LEDs
    lastLineTime = t;
    
//#if !defined(LED_DATA_PIN) && !defined(LED_CLOCK_PIN)
//  delayMicroseconds(900);  // Because hardware SPI is ludicrously fast
//#endif
//if(++imageLine >= imageLines) imageLine = 0; // Next scanline, wrap around
}





void IRinterrupt() {
 
  if (irrecv.decode(&results)) {
//  Serial.println(results.value, HEX); // not in esp8266
    serialPrintUint64(results.value, HEX);
    irrecv.resume(); // Receive the next value
  }
}
