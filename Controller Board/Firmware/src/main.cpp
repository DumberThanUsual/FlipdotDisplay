#include <Arduino.h>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Adafruit_GFX.h>

#include "Font4x5Fixed.h"
#include "Font4x5FixedWide1.h"

#define INPUT_UP 32
#define INPUT_DOWN 27
#define INPUT_LEFT 25
#define INPUT_RIGHT 33
#define INPUT_CENTER 26

#define MODULES 8
#define MODULE_WIDTH 5
#define MODULE_HEIGHT 7

#define DISPLAY_WIDTH MODULES*MODULE_WIDTH 
#define DISPLAY_HEIGHT MODULE_HEIGHT

bool fullRedraw = true; //TODO FIXXXX

enum InputEventType {
    UP_SINGLE
  , DOWN_SINGLE
  , LEFT_SINGLE
  , RIGHT_SINGLE
  , CENTER_SINGLE
};

class BufferProducer {

  bool bufferValid = false;

  public:

  bool visibility = false;
  bool focus = false;

    BufferProducer* getProducer() {
      return this;
    }

    void invalidateBuffer() {
      bufferValid = false;
    }

    bool getBufferValidity() {
      return bufferValid;
    }

    virtual bool getPixel(int, int) = 0;

    virtual bool ensureBufferValidity(bool) = 0;

    virtual bool handleInput(InputEventType) = 0;

    void visible () {
      if (!visibility) {
        enterVisibility();
      }
      visibility = true;
    }

    virtual void invisible() {
      if (visibility) {
        exitVisibility();
      }
      visibility = false;
    }

    void focussed() {
      if (!focus) {
        enterFocus();
      }
      focus = true;
      visibility = true;
    }

    void unfocussed() {
      if (focus) {
        exitFocus();
      }
      focus = false;
      visibility = false;
    }

    virtual void onCreate() {}; 

    virtual void enterVisibility() {};

    virtual void enterFocus() {};

    virtual void exitVisibility() {};

    virtual void exitFocus() {};

    virtual void onDestroy() {};
};

class BufferConsumer {
  BufferProducer* bufferProducer;

  public:
    BufferConsumer() {

    }

    bool getPixel(int x, int y) {
      if (bufferProducer) {
        return bufferProducer->getPixel(x, y);
      }
      else {
        return false;
      }
    }

    void bindToProducer(BufferProducer* buffer) {
      bufferProducer = buffer;
    }

    void releaseProducer() {
      // TODO:tell producer
      bufferProducer = nullptr;
    }

    bool ensureBufferValidity(bool includeInactive = false) {
      if(bufferProducer) {
        return bufferProducer->ensureBufferValidity(includeInactive);
      }
      return false;
    }

    bool handleInput(InputEventType inputEventType) {
      if (bufferProducer) {
        return bufferProducer->handleInput(inputEventType);
      }
      else {
        return false;
      }
    }

    void visible() {
      if (bufferProducer) {
        bufferProducer->visible();
      }
    }

    void focussed() {
      if (bufferProducer) {
        bufferProducer->focussed();
      }
    }

    void invisible() {
      if (bufferProducer) {
        bufferProducer->invisible();
      }
    }

    void unfocussed() {
      if (bufferProducer) {
        bufferProducer->unfocussed();
      }
    }
};

int surfaceNumber = 0;

class StaticBuffer: public BufferProducer {
  GFXcanvas1 buffer;

  public:
    StaticBuffer(String surfaceText = "")
    : buffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      if (surfaceText == "") {
      buffer.print("Surface");
      }
      else {
        buffer.print(surfaceText);
      }
      surfaceNumber++;
    }

    void setText(String surfaceText) {
      buffer.setFont(&Font4x5Fixed);
      buffer.fillScreen(false);
      buffer.setCursor(1, 5);
      buffer.print(surfaceText);
    }

    bool getPixel(int x, int y) {
      return buffer.getPixel(x, y);
    }

    bool ensureBufferValidity(bool includeInactive = false) {
      return true;
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }
};

class ClockFace: public BufferProducer {
  GFXcanvas1 buffer;
  public:
    ClockFace()
    : buffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {

    }

    bool getPixel(int x, int y) {
      return buffer.getPixel(x, y);
    }

    bool ensureBufferValidity(bool includeInactive = false) {
      return true;
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }
};

class FlipDisplay {

  GFXcanvas1 stateBuffer;

  TaskHandle_t renderTask;

  public:

   BufferConsumer frameBuffer;

    FlipDisplay()
    : frameBuffer()
    , stateBuffer(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    {
      
    }

    void begin() {
      Serial2.begin(115200);
      while(!Serial2);
      xTaskCreatePinnedToCore (
        renderer,
        "Flip Display Renderer",
        10000,
        this,
        1,
        &renderTask,
        1
      );
    }

    /*
    On display update call:
    - Check validity of required producers
    - Redraw branches with invalid producers, ending in framebuffer consumer
    - Draw framebuffer to display
    */ 

    void updateDisplay(bool fullRedraw = false) {  //TODO: replace enture display update functionality
      frameBuffer.ensureBufferValidity();

      for (int module = 0; module < MODULES; module ++) {
        Serial2.write(0b10000000 | (module << 4));
        for (int x = 0; x < MODULE_WIDTH; x ++) {
          uint8_t colBuf = 0;
          for (int y = 0; y < 7; y ++) {
            colBuf |= frameBuffer.getPixel(x + module * 5, y) << y;
          }
          Serial2.write(colBuf);
        }
        if (fullRedraw) {
          Serial2.write(0b10000110 | (module << 4));
        }
        else {
          Serial2.write(0b10000101 | (module << 4));
        }
      }
    }

    static void renderer(void* pvParameters) {
      FlipDisplay* flipDisplay = (FlipDisplay*)pvParameters;
      for (;;){
        flipDisplay->updateDisplay(fullRedraw);
        fullRedraw = false;

        #ifdef OLED_DISPLAY
          for (int x = 0; x < 40; x ++) {
            for (int y = 0; y < 7; y ++) {
              oled.drawPixel(x*3, y*3+1, flipDisplay->frameBuffer.getPixel(x, y));
              oled.drawPixel(x*3+1, y*3, flipDisplay->frameBuffer.getPixel(x, y));
              oled.drawPixel(x*3+1, y*3+1, flipDisplay->frameBuffer.getPixel(x, y));
              oled.drawPixel(x*3+1, y*3+2, flipDisplay->frameBuffer.getPixel(x, y));
              oled.drawPixel(x*3+2, y*3+1, flipDisplay->frameBuffer.getPixel(x, y));
            }
          }
        #endif


        vTaskDelay(16/portTICK_PERIOD_MS ); // 60 FPS: 16ms/frame
      }
    }

    void handleInput(InputEventType InputEventType) {
      frameBuffer.handleInput(InputEventType);
    }
};

FlipDisplay display;
StaticBuffer updateScreen("Updating");
ClockFace clockFace;

void setup() {
  display.begin();

  
  #ifdef OLED_DISPLAY
    oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    oled.clearDisplay();
    oled.display();
  #endif

  Serial.begin(115200);


  WiFi.setHostname("Flipdot Display");
  ArduinoOTA.setHostname("Flipdot Display");
  WiFi.mode(WIFI_STA);  
  WiFi.begin("TALKTALK21516E", "YJ7P49A4");

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA
    .onStart([]() {
      display.frameBuffer.bindToProducer(updateScreen.getProducer());
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      updateScreen.setText("OTA: " + (String)(progress / (total / 100)) + "%");
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  fullRedraw = true;

  display.frameBuffer.bindToProducer(&clockFace);

  pinMode(INPUT_CENTER, INPUT_PULLUP);
  pinMode(INPUT_LEFT, INPUT_PULLUP);
  pinMode(INPUT_RIGHT, INPUT_PULLUP);
  pinMode(INPUT_UP, INPUT_PULLUP);
  pinMode(INPUT_DOWN, INPUT_PULLUP);

  #ifdef OLED_DISPLAY
    oled.display();
  #endif

  Serial.println("Running");
}

void loop() {
  if (!digitalRead(INPUT_UP)) {
    display.handleInput(UP_SINGLE);
  }
  if (!digitalRead(INPUT_DOWN)) {
    display.handleInput(DOWN_SINGLE);
  }
  if (!digitalRead(INPUT_LEFT)) {
    display.handleInput(LEFT_SINGLE);
  }
  if (!digitalRead(INPUT_RIGHT)) {
    display.handleInput(RIGHT_SINGLE);
  }
  if (!digitalRead(INPUT_CENTER)) {
    display.handleInput(CENTER_SINGLE);
  }

  for (int i =0; i < 10; i ++) {
    #ifdef OLED_DISPLAY
      oled.display();
    #endif

    ArduinoOTA.handle();
    delay(12);
  }
}