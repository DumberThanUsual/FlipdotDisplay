#include <Arduino.h>

#include <vector>
#include <functional>
#include <map>
#include <stack>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <Adafruit_GFX.h>

#include "Font5x7Fixed.h"
#include "Font4x5Fixed.h"
#include "Font4x5FixedWide1.h"
#include "Font4x7Fixed.h"
#include "Font3x5FixedNum.h"

#include <Wire.h>
#include <SparkFun_STUSB4500.h>

//#define OLED_DISPLAY

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

#ifdef OLED_DISPLAY
  #include <Adafruit_SSD1306.h>
  Adafruit_SSD1306 oled(128, 32, &Wire, -1);
#endif

STUSB4500 usb;

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
      buffer.print("Surface ");
      buffer.print(surfaceNumber);
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

class TextSurface: public BufferProducer {
  GFXcanvas1 textBuffer;
  const GFXfont* font;
  
  public:
    TextSurface(String surfaceText = "", const GFXfont* font = &Font4x5Fixed, int width = DISPLAY_WIDTH, int height = DISPLAY_HEIGHT)
    : textBuffer(width, height)
    , font(font)
    {
      textBuffer.setFont(font);
      textBuffer.fillScreen(false);
      textBuffer.setCursor(1, 5);
      if (surfaceText == "") {
      textBuffer.print("Surface ");
      textBuffer.print(surfaceNumber);
      }
      else {
        textBuffer.print(surfaceText);
      }
      surfaceNumber++;
    }

    void setText(String surfaceText) {
      textBuffer.setFont(font);
      textBuffer.fillScreen(false);
      textBuffer.setCursor(1, 5);
      textBuffer.print(surfaceText);
    }

    bool getPixel(int x, int y) {
      return textBuffer.getPixel(x, y);
    }

    bool ensureBufferValidity(bool includeInactive = false) {
      return true;
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }
};


class SurfaceScrollerImproved: public BufferProducer {
  private:
    
    TaskHandle_t animatorTask;

    StaticBuffer emptyBuffer;

    BufferProducer* activeBuffer;
    BufferProducer* inactiveBuffer;

    int scrollDistance = 0;

  public:

    struct ScrollInstruction {
      BufferProducer* buffer;
      int distance;
      bool direction;
    };

    std::vector<ScrollInstruction> instructionBuffer;

    void setFrame(BufferProducer* buffer) {
      if (inactiveBuffer) {
        inactiveBuffer->exitFocus();
        inactiveBuffer->exitVisibility();
      }

      if (activeBuffer) {
        activeBuffer->exitFocus();
        activeBuffer->exitVisibility();
      }
      activeBuffer = buffer;

      activeBuffer->enterVisibility();
      activeBuffer->enterFocus();

      offset = 0;
      instructionBuffer.clear();
    }

  private:

    int getRemainingDistance() {
      int remainingDistance = 0;
      bool scrollDirection = instructionBuffer[0].direction;

      int searchIndex = 0;

      while(searchIndex < instructionBuffer.size()) {
        remainingDistance += instructionBuffer[searchIndex].distance;
        searchIndex ++;
        if (instructionBuffer[searchIndex].direction != scrollDirection) {
          break;
        }
      }
      
      return remainingDistance;
    }

    int offset = 0;
    bool vertical;


    static void scrollerAnimator (void* pvParameters) {
      SurfaceScrollerImproved* scroller = (SurfaceScrollerImproved*)pvParameters;
      for(;;) {   
        bool animating = false;

        if (scroller->instructionBuffer.size() > 0 && scroller->instructionBuffer[0].buffer) {
          scroller->instructionBegin();
          animating = true;
          bool instructionComplete = false;
          scroller->offset = 0;
          int remainingDistance = scroller->getRemainingDistance();

          ScrollInstruction workingScrollInstruction = scroller->instructionBuffer[0];
          scroller->inactiveBuffer = workingScrollInstruction.buffer;
          scroller->inactiveBuffer->enterVisibility();
          if(!animating){
            scroller->activeBuffer->exitFocus();
          }
          while(!instructionComplete) {
            if(workingScrollInstruction.direction) {
              scroller->offset ++;
            }
            else {
              scroller->offset --;
            }
            remainingDistance = scroller->getRemainingDistance() - abs(scroller->offset);

            vTaskDelay(constrain(100.0/pow(remainingDistance, 1), 10, 200)/portTICK_PERIOD_MS);

            if(abs(scroller->offset) == workingScrollInstruction.distance) {

              scroller->activeBuffer->exitVisibility();
              scroller->activeBuffer = workingScrollInstruction.buffer;
              if(remainingDistance == 0) {
                scroller->activeBuffer->enterFocus();
              }

              scroller->inactiveBuffer = &(scroller->emptyBuffer);

              instructionComplete = true;

            }
          }
          scroller->instructionComplete();
          scroller->instructionBuffer.erase(scroller->instructionBuffer.begin());
          animating = false;
        }
        else {
          vTaskDelay(10/portTICK_PERIOD_MS );
        }
      }
    }

  public:

    SurfaceScrollerImproved(bool isVertical = true) 
    : vertical(isVertical)
    , emptyBuffer("Inactive")
    {
      activeBuffer = &emptyBuffer;
      inactiveBuffer = &emptyBuffer;
      setFrame(&emptyBuffer);
      xTaskCreatePinnedToCore (
        scrollerAnimator,
        "scrollerAnimatedTask",
        10000,
        this,
        1,
        &animatorTask,
        1
      );
    }

    SurfaceScrollerImproved(BufferProducer* startingBuffer) 
    : vertical(true)
    , emptyBuffer("Inactive")
    {
      activeBuffer = startingBuffer;
      inactiveBuffer = &emptyBuffer;
      setFrame(startingBuffer);
      xTaskCreatePinnedToCore (
        scrollerAnimator,
        "scrollerAnimatedTask",
        10000,
        this,
        1,
        &animatorTask,
        1
      );
    }

    virtual void instructionComplete() {

    }

    virtual void instructionBegin() {

    }

    bool getPixel(int x, int y) {
      if(instructionBuffer.size() == 0) {
        return activeBuffer->getPixel(x, y);
      }

      int* positionOffset;
      if (vertical) {
        positionOffset = &y;
      }
      else {
        positionOffset = &x;
      }

      if ((offset + *positionOffset) >= 0 && (offset + *positionOffset) < instructionBuffer[0].distance) {
        if (vertical) {
          return activeBuffer->getPixel(x, (offset + y));
        }
        else {
          return activeBuffer->getPixel((offset + x), y);
        }
      }
      else {
        if (vertical) {
          if (offset >= 0) {
            return inactiveBuffer->getPixel(x, (offset + y)-instructionBuffer[0].distance);
          }
          else {
            return inactiveBuffer->getPixel(x, (offset + y)+instructionBuffer[0].distance);
          }
        }
        else {
          if (offset >= 0) {
            return inactiveBuffer->getPixel((offset + x)-instructionBuffer[0].distance, y);
          }
          else {
            return inactiveBuffer->getPixel((offset + x)+instructionBuffer[0].distance, y);
          }
        }
      }
    }

    bool ensureBufferValidity(bool includeInactive = false) {
      bool activeBufferResult = true;
      bool inactiveBufferResult = true;
      if (activeBuffer) {
        activeBufferResult = activeBuffer->ensureBufferValidity(includeInactive);
      }
      if (inactiveBuffer) {
        inactiveBufferResult = inactiveBuffer->ensureBufferValidity(includeInactive);
      }
      return activeBufferResult && inactiveBufferResult;  
    }

    void enterVisibility () {
      if(instructionBuffer.size() > 0) {
        inactiveBuffer->enterVisibility();
      }
      activeBuffer->enterVisibility();
    }

    void enterFocus () {
      if(instructionBuffer.size() == 0) {
        activeBuffer->enterVisibility();
      }
    }

    void exitFocus() {
      if(instructionBuffer.size() == 0) {
        activeBuffer->exitFocus();
      }
    }

    void exitVisibility() {
      if(instructionBuffer.size() > 0) {
        inactiveBuffer->exitVisibility();
      }
      activeBuffer->exitVisibility();
    }

    void addScrollInstrction(ScrollInstruction nextScrollInstruction) {
      instructionBuffer.push_back(nextScrollInstruction);
    }

    bool handleInput(InputEventType inputEventType) {
      return activeBuffer->handleInput(inputEventType);
    }
};

class NumberInput: public SurfaceScrollerImproved {
  TextSurface evenNumber;
  TextSurface oddNumber;

  int scrollerValue = 0;
  int max;

public:
  int value = 0;

  NumberInput(int max = 9)
  : SurfaceScrollerImproved()
  , evenNumber("0", &Font4x5FixedWide1, 4, 7)
  , oddNumber("1", &Font4x5FixedWide1, 4, 7)
  , max(max)
  {
    setFrame(&evenNumber);
  }

  void instructionBegin() {
    ScrollInstruction instruction = instructionBuffer[0];

    TextSurface* nextSurface;
    if (scrollerValue % 2) {
      nextSurface = &evenNumber;
    } else {
      nextSurface = &oddNumber;
    }

    if (instruction.direction) {

      if (scrollerValue == 0) {
        nextSurface->setText((String)max);
      }
      else {
        nextSurface->setText((String)(scrollerValue - 1));
      }

      scrollerValue --;
      if (scrollerValue < 0) {
        scrollerValue = max;
      }
    }else {
      nextSurface->setText((String)((scrollerValue + 1) % (max + 1)));
      scrollerValue ++;
      if (scrollerValue > max) {
        scrollerValue = 0;
      }
    }
  }

  bool handleInput(InputEventType inputEventType) {
    ScrollInstruction instruction;
    switch (inputEventType) {
      case UP_SINGLE:
        value ++;
        if (value > max) {
          value = 0;
        }
        instruction.direction = false;
        instruction.distance = 8;
        if (value % 2) {
          instruction.buffer = &oddNumber;
        }
        else {
          instruction.buffer = &evenNumber;
        }
        addScrollInstrction(instruction);
        return true;
        break;
      case DOWN_SINGLE:
        value --;
        if (value < 0) {
          value = max;
        }
        instruction.direction = true;
        instruction.distance = 8;
        if (value % 2) {
          instruction.buffer = &oddNumber;
        }
        else {
          instruction.buffer = &evenNumber;
        }
        addScrollInstrction(instruction);
        return true;
        break;
    }
  return false;
  }
};

class Menu: public SurfaceScrollerImproved {


public:
  int menuPosition = 0;

  std::vector<BufferProducer*> menuItems;

  Menu()
  {

  }

  bool scroll(bool direction = true /*Down = true*/) {
    ScrollInstruction instruction;
    
    if (direction) {
      menuPosition ++;
    }
    else {
      menuPosition --;
    }

    if (menuPosition >= (int)menuItems.size()) {
      menuPosition = menuItems.size()-1;
      return false;
    }
    
    if (menuPosition < 0) {
      menuPosition = 0;
      return false;
    }

    instruction.direction = direction;
    instruction.distance = 8;
    instruction.buffer = menuItems[menuPosition];
    addScrollInstrction(instruction);
    return true;
  }
  
  bool handleInput(InputEventType inputEventType) {
    switch (inputEventType) {
      case UP_SINGLE:
        Serial.println("scrolling up");
        return scroll(false);
        break;
      case DOWN_SINGLE:
        Serial.println("scrolling down");
        return scroll(true);
        break;
      default:
        return false;
        break;
    }
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


class Application;

class BaseActivity: public BufferProducer {

  bool deletionMarker = false;
  
public:

  Application* parentApplication;

  BaseActivity(Application* parentApplication)
  : parentApplication(parentApplication)
  {

  }

  virtual ~BaseActivity() {

  }

  using CompletionCallback = std::function<void()>;

  void setCompletionCallback(CompletionCallback callback) {
    completionCallback = std::move(callback);
  }

  void activityComplete() {
    if (completionCallback && !complete) {
      completionCallback();
    }
    complete = true;
  }

  void invisible() {
    if (visibility) {
      exitVisibility();
    }
    visibility = false;
    if (deletionMarker) {
      onDestroy();
      delete this;
      return;
    }
  }

  void markForDeletion() {
    deletionMarker = true;
  }

  virtual bool getPixel(int x, int y) = 0;

  virtual bool ensureBufferValidity(bool includeInactive = false) = 0;

  virtual bool handleInput(InputEventType inputEventType) = 0;

private:
  CompletionCallback completionCallback;
  bool complete = false;
};

class ActivityManager: public SurfaceScrollerImproved {
private:
  std::map<Application*, std::stack<BaseActivity*>> appActivityStacks;
  Application* currentStack;

  Application* launcher;

public:

  ActivityManager()
  : SurfaceScrollerImproved(false)
  {

  }

  void startActivity(BaseActivity* activity) {
    auto& activityStack = appActivityStacks[activity->parentApplication];

    if (!activityStack.empty()) {
      activityStack.top()->unfocussed();
    }

    setupCompletionCallback(activity);
    activityStack.push(activity);
    if (currentStack == activity->parentApplication) {
      ScrollInstruction scrollInstruction;
      scrollInstruction.buffer = activityStack.top();
      scrollInstruction.distance = DISPLAY_WIDTH;
      scrollInstruction.direction = true;
      addScrollInstrction(scrollInstruction); 
      currentStack = activity->parentApplication;
    } else {
      goToStack(activity->parentApplication);
    }
  }

  void setLauncher(Application* application) {
    launcher = application;
  }

  bool goToStack(Application* appStack) {
    if (!appStack) {
      appStack = launcher;
    }

    if(appActivityStacks[appStack].empty()) {
      return false;
    }

    currentStack = appStack;
    ScrollInstruction scrollInstruction;
    scrollInstruction.buffer = appActivityStacks[appStack].top();
    scrollInstruction.distance = DISPLAY_WIDTH;
    scrollInstruction.direction = !(appStack == launcher);
    addScrollInstrction(scrollInstruction); 

    return true;
  }

  void closeActivity(BaseActivity* activity) {
    Application* parentApplication = activity->parentApplication;
    auto& activityStack = appActivityStacks[parentApplication];

    bool requireAnimation = (activity == activityStack.top());

    activity->markForDeletion();
    activityStack.pop();

    if (activityStack.empty()) {
      goToStack(launcher);
      appActivityStacks.erase(parentApplication);
    }
    else if (requireAnimation){
      ScrollInstruction scrollInstruction;
      scrollInstruction.buffer = activityStack.top();
      scrollInstruction.distance = DISPLAY_WIDTH;
      scrollInstruction.direction = false;
      addScrollInstrction(scrollInstruction);
    }
  }

  bool handleInput(InputEventType inputEventType) {
    if (appActivityStacks[currentStack].top()->handleInput(inputEventType)) {
      return true;
    }
    if (inputEventType == LEFT_SINGLE && currentStack != launcher) {
      closeActivity(appActivityStacks[currentStack].top());
      return true;
    }
    return false;
  }

private:
  void setupCompletionCallback(BaseActivity* activity) {
    activity->setCompletionCallback([this, activity]() {
      closeActivity(activity);
    });
  }
};

ActivityManager activityManager;

class Application {
public:

  String name = "App tmplt";

  Application() {

  }
};

class Clock: public Application {

};

class CountdownTimer: public Application {

  int32_t timer = -1;

  TaskHandle_t countdownTaskHandle;

  static void countdownTask(void* pvParameters) {
    CountdownTimer* countdownApp = (CountdownTimer*)pvParameters;
    while (countdownApp->timer >= 0) {
      countdownApp->timer -= 1;
      vTaskDelay(1000/portTICK_PERIOD_MS);
    }
    countdownApp->timer = -1;
    vTaskDelete(NULL);
  }

  public:

  class AlarmActivity: public BaseActivity {
    CountdownTimer* parentTimer;
  public:
    AlarmActivity(CountdownTimer* parentApplicationPointer)
    : BaseActivity(parentApplicationPointer)
    , parentTimer(parentApplicationPointer)
    {

    }

    bool ensureBufferValidity(bool includeInactive) {
      return true;
    }

    bool handleInput(InputEventType inputEventType) {
      return false;
    }

    bool getPixel(int x, int y) {
      return (millis()/250)%2;
    }

    void enterFocus() {
      //activityComplete();
    }
  };

  class CountdownActivity: public BaseActivity {
    TextSurface countdown;
    int32_t timer;

    CountdownTimer* parentTimer;

    public: 
      CountdownActivity(CountdownTimer* parentApplicationPointer) 
      : BaseActivity(parentApplicationPointer)
      , parentTimer(parentApplicationPointer)
      , countdown("--:--:--")
      {

      }

      String intToFormattedStr(int number) {
        if (number > 9) {
          return String(number);
        }
        else {
          return "0" + String(number);
        }
      }

      void updateCountdown() {
        String seconds = intToFormattedStr(timer % 60);
        String minutes = intToFormattedStr((timer/60)%60);
        String hours = intToFormattedStr(timer/(60*60));

        countdown.setText(hours + ":" + minutes + ":" + seconds);
      }

      bool startedthing = false;

      bool ensureBufferValidity(bool includeInactive) {
        timer = parentTimer->timer;
        if (timer == 0) {
          updateCountdown();
          //activityComplete();
          if (!startedthing) {
            activityManager.startActivity(new CountdownTimer::AlarmActivity(parentTimer));
          }
          startedthing = true;
        }
        else if (timer > 0) {
          updateCountdown();
        }
        return true;
      }

      bool handleInput(InputEventType inputEventType) {
        return false;
      }
  
      bool getPixel(int x, int y) {
        return countdown.getPixel(x, y);
      }
  };

  class timerSetupActivity: public BaseActivity {
    CountdownTimer* parentTimer;

    int selectionPosition = 0;

    TextSurface background;

    NumberInput hoursMinorScroller;
    NumberInput hoursMajorScroller;

    NumberInput minutesMinorScroller;
    NumberInput minutesMajorScroller;

    NumberInput secondsMinorScroller;
    NumberInput secondsMajorScroller;

  public:
    timerSetupActivity(CountdownTimer* parentApplicationPointer) 
    : BaseActivity(parentApplicationPointer)
    , parentTimer(parentApplicationPointer)
    , hoursMajorScroller()
    , hoursMinorScroller()
    , minutesMajorScroller(5)
    , minutesMinorScroller()
    , secondsMajorScroller(5)
    , secondsMinorScroller()
    , background("--:--:--")
    {
      
    }

    bool timerStarted = false;

    void startTimer() {
      int32_t time = secondsMinorScroller.value + (10*secondsMajorScroller.value) + (60*minutesMinorScroller.value) + (600*minutesMajorScroller.value) + (3600*hoursMinorScroller.value) + (36000*hoursMajorScroller.value);
      if (!timerStarted) {
        parentTimer->TimerSet(time);
        activityManager.startActivity(new CountdownTimer::CountdownActivity(parentTimer));
      }
      timerStarted = true;
    }

    bool ensureBufferValidity(bool includeInactive) {
      return true;
    }

    bool handleInput(InputEventType inputEventType) {
      switch(inputEventType){
        case LEFT_SINGLE:
          if (selectionPosition == 0) {
            return false;
          }
          else {
            selectionPosition--;
            return true;
          }
          break;
        case RIGHT_SINGLE:
          selectionPosition++;
          break;
        case CENTER_SINGLE:
          startTimer();
          return true;
          break;
        default:
          switch(selectionPosition) {
            case 0:
              return hoursMajorScroller.handleInput(inputEventType);
              break;
            case 1:
              return hoursMinorScroller.handleInput(inputEventType);
              break;
            case 2:
              return minutesMajorScroller.handleInput(inputEventType);
              break;
            case 3:
              return minutesMinorScroller.handleInput(inputEventType);
              break;
            case 4:
              return secondsMajorScroller.handleInput(inputEventType);
              break;
            case 5:
              return secondsMinorScroller.handleInput(inputEventType);
              break;
          }
          break;
        }
      return false;
    }

    bool getPixel(int x, int y) {
      if (x >= 0 && x <= 3) {
        return hoursMajorScroller.getPixel(x, y);
      }
      if (x >=4 && x <= 7) {
        return hoursMinorScroller.getPixel(x - 4, y);
      }
      if (x >= 10 && x <= 13) {
        return minutesMajorScroller.getPixel(x-10, y);
      }
      if (x >=14 && x <= 17) {
        return minutesMinorScroller.getPixel(x - 14, y);
      }
      if (x >= 20 && x <= 23) {
        return secondsMajorScroller.getPixel(x - 20, y);
      }
      if (x >=24 && x <= 27) {
        return secondsMinorScroller.getPixel(x - 24, y);
      }
      return background.getPixel(x, y);
    }

  };

    enum activities {
      unspecified,
      setup,
      countdown,
      alarm
    };

    void TimerSet(int32_t seconds) {
      if (timer == -1) {
        timer = seconds;
        xTaskCreatePinnedToCore (
          countdownTask,
          "Countdown timer task",
          1000,
          this,
          1,
          &countdownTaskHandle,
          1
        );
      }
    }

    CountdownTimer() {
    }

    ~CountdownTimer() {
      vTaskDelete(countdownTaskHandle);
    }
};

CountdownTimer countdownTimer;

class Launcher: public Application {
public:

  Application* appList = {
    &countdownTimer
  };

  class HomeScreen: public BaseActivity {
    Menu menu;
    BufferProducer *menuItems[5] = {
      new TextSurface("Timer"),
      new TextSurface("Stopwatch"),
      new TextSurface("Snake"),
      new TextSurface("Tetris"),
      new TextSurface("Settings")
    };

  public:

    HomeScreen(Launcher* parentApplication)
    : BaseActivity(parentApplication)
    {
      for (int i = 0; i < 5; i++) 
        menu.menuItems.push_back(menuItems[i]);
      menu.setFrame(menuItems[0]);
    }

    bool getPixel(int x, int y) {
      return menu.getPixel(x, y);
    }

    bool ensureBufferValidity(bool inclueInactive = false) {
      return menu.ensureBufferValidity(inclueInactive);
    }

    bool handleInput(InputEventType inputEventType) {
      if (menu.handleInput(inputEventType)) {
        return true;
      }
      activityManager.startActivity(new CountdownTimer::timerSetupActivity(&countdownTimer));
      return true;
    }

  };
};

Launcher launcher;

FlipDisplay display;

StaticBuffer updateScreen("Updating");

void setup() {
  display.begin();

  
  #ifdef OLED_DISPLAY
    oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    oled.clearDisplay();
    oled.display();
  #endif

  Wire.begin();

  usb.begin();


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

  activityManager.setLauncher(&launcher);
  activityManager.startActivity(new Launcher::HomeScreen(&launcher));
  display.frameBuffer.bindToProducer(&activityManager);

  pinMode(INPUT_CENTER, INPUT);
  pinMode(INPUT_LEFT, INPUT);
  pinMode(INPUT_RIGHT, INPUT);
  pinMode(INPUT_UP, INPUT);
  pinMode(INPUT_DOWN, INPUT);

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