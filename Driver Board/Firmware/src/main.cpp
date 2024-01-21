#include <Arduino.h>
#include <SPI.h>

#define MODULE_WIDTH 5
#define MODULE_HEIGHT 7

#define RCLK 0
#define SRCLR 6

#define ADDR_0 1
#define ADDR_1 2
#define ADDR_2 3

#ifdef MILLIS_USE_TIMERA0 // Uses timer B0
  #error "This sketch takes over TCA0 - please use a different timer for millis"
#endif

uint8_t address = 0;

uint32_t registerFrames[35] = {0};
uint32_t registerBuffer = 0;  

uint8_t stateBuffer[5] = {0b01111111};
uint8_t frameBuffer[5] = {0};

const int rowHigh[7] = {1, 2, 3, 20, 19, 18, 17};
const int rowLow[7] = {11, 10, 9, 28, 27, 25, 26};

const int colHigh[5] = {7, 6, 5, 22, 23};
const int colLow[5] = {15, 14, 13, 30, 31};

enum flipTimeVolts {
  nineV = 1000,
  twelveV = 500,
  fifteenV = 250,
  twentyV = 200
};

int flipTime = 5100;
int saturationTime = 5000;

bool counterRunning = false;
int index = 0;

int incomingByte = 0; // for incoming serial data
uint8_t selectedRegister = 0;
bool moduleActive = false;
bool frameBufferWrite = true;
bool fullRedraw = true;



void shift32(uint32_t registerFrame) {  // Shift 32 bits to registers in 8 bit chunks
  uint8_t shift = 32U; 
  do {
      shift -= 8U;
      SPI.transfer((uint8_t)(registerFrame >> shift));
  } while ( shift ) ;
}

void clockRegisters() {  // Cycle RCLK pin
  digitalWrite(RCLK, HIGH);
  digitalWrite(RCLK, LOW);
}

void registerSet(int segmentX, int segmentY, bool segmentValue) { // Modify register buffer to flip single segment
  if (segmentValue) {
    registerBuffer = registerBuffer | ((uint32_t)1 << colLow[segmentX]);
    registerBuffer = registerBuffer & ~((uint32_t)1 << colHigh[segmentX]);

    registerBuffer = registerBuffer | ((uint32_t)1 << rowHigh[segmentY]);
    registerBuffer = registerBuffer & ~((uint32_t)1 << rowLow[segmentY]);
  } 
  else {
    registerBuffer = registerBuffer | ((uint32_t)1 << colHigh[segmentX]);
    registerBuffer = registerBuffer & ~((uint32_t)1 << colLow[segmentX]);

    registerBuffer = registerBuffer | ((uint32_t)1 << rowLow[segmentY]);
    registerBuffer = registerBuffer & ~((uint32_t)1 << rowHigh[segmentY]);
  }
}

bool genStates() {  // Generate register states to update module
  bool updateRequired = false;
  for (int sweep = 0; sweep < 5; sweep++) {
    for (int step = 0; step < 7; step++) {
      bool currentValue = bitRead(stateBuffer[sweep], step);
      bool segmentValue = bitRead(frameBuffer[sweep], step);
      if (currentValue != segmentValue || fullRedraw) {
        updateRequired = true;
        registerSet (sweep, step, segmentValue);
        registerFrames[sweep*7 + step] = registerBuffer;
        bitWrite(stateBuffer[sweep], step, segmentValue);
      }
      else {
        registerFrames[sweep*7 + step] = 0;
      }
      registerBuffer = 0;
    }
  } 
  return updateRequired;
}

void setup() {
  _PROTECTED_WRITE(CLKCTRL_MCLKCTRLB, CLKCTRL_PEN_bm);  // Set 10 MHz clock

  pinMode(RCLK, OUTPUT);
  pinMode(SRCLR, OUTPUT);

  pinMode(ADDR_0, INPUT_PULLUP);
  pinMode(ADDR_1, INPUT_PULLUP);
  pinMode(ADDR_2, INPUT_PULLUP);

  address = digitalRead(ADDR_0) | digitalRead(ADDR_1) << 1 | digitalRead(ADDR_2) << 2;

  digitalWrite(SRCLR, HIGH);
  SPI.begin();
  Serial.begin(115200);

  takeOverTCA0();
  TCA0.SINGLE.CTRLB = (TCA_SINGLE_WGMODE_NORMAL_gc); //Normal mode counter
  TCA0.SINGLE.CTRLESET = TCA_SINGLE_DIR_DOWN_gc;
  TCA0.SINGLE.INTCTRL = (TCA_SINGLE_CMP0_bm | TCA_SINGLE_OVF_bm); // enable compare channel 0 and overflow interrupts

  TCA0.SINGLE.PER = flipTime; //count from top
  TCA0.SINGLE.CMP0 = saturationTime; //compare at midpoint

  //TCA0.SINGLE.CTRLA = TCA_SINGLE_ENABLE_bm; // enable the timer 100ns increments per step at 10MHz clock
}



void loop() {

  // SERIAL PROTOCOL:
  // Module address and register selection:
  // 0b1AAARRRR
  // AAA - ADDRESS, RRRR = register
  // Register write:
  // 0b0VVVVVVV
  // VVVVVVV - Value

  // Registers 0 - 4: Framebuffer
  // Register 5: Framebuffer Write
  // Register 6: Framebuffer write with full redraw

  while (Serial.available()) {
    incomingByte = Serial.read();

    if (bitRead(incomingByte, 7)) {
      if ((incomingByte & 0b01110000) >> 4 == address) {
        moduleActive = true;
        selectedRegister = incomingByte & 0b00001111;

        if (selectedRegister == 5) {
          frameBufferWrite = true;
          moduleActive = false;
        }

        if (selectedRegister == 6) {
          frameBufferWrite = true;
          fullRedraw = true;
          moduleActive = false;
        }
      }
      else {
        moduleActive = false;
      }
    }
    else if (moduleActive) {
      if (selectedRegister >= 0 && selectedRegister <= 4) {
        for (int y = 0; y < 7; y ++) {
          frameBuffer[selectedRegister] = incomingByte;
        }
        selectedRegister ++;
        if (selectedRegister > 4) {
          selectedRegister = 0;
        }
      }
    }
  }

  if (!counterRunning && frameBufferWrite){
    frameBufferWrite = false;
    genStates();
    fullRedraw = false;
    TCA0.SINGLE.CNT = flipTime;
    counterRunning = true;
    index = 0;
    TCA0.SINGLE.CTRLA = TCA_SINGLE_ENABLE_bm;
  }
}


ISR(TCA0_OVF_vect) {    // on overflow, shift out 0 and enter recovery time
  if (index >= 35) {
    TCA0.SINGLE.CTRLA = 0;
    counterRunning = false;
  }
  shift32(0x00); 
  clockRegisters();
  TCA0.SINGLE.INTFLAGS  = TCA_SINGLE_OVF_bm; // Always remember to clear the interrupt flags, otherwise the interrupt will fire continually!
}


ISR(TCA0_CMP0_vect) {    // on compare, get next pixel and set state
  shift32(registerFrames[index]);
  clockRegisters();
  index ++;
  TCA0.SINGLE.INTFLAGS  = TCA_SINGLE_CMP0_bm; // Always remember to clear the interrupt flags, otherwise the interrupt will fire continually!
}