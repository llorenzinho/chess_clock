#include <Arduino.h>
#include <TM1637Display.h>

// Signatures
void setup();
void loop();
void setupPins();
void readButtons();
void resetButtons();
void handleButtonIdle();
void handleButtonSelectMinutes();
void handleButtonSelectSeconds();
void handleButtonSelectIncrement();
void handleButtonWaitingToStart();
void handleStateRunning();
void countDown();
void handleButtonFinished();
void setupTimer();
void setupDisplays();
void displayDigit(TM1637Display display, unsigned long digit);
void displayMillis(TM1637Display display, unsigned long milliseconds);
void _displayMs(TM1637Display* display, unsigned long milliseconds);

#define STATE_IDLE 0
#define STATE_SELECT_MINUTES 1	// Select minutes
#define STATE_SELECT_SECONDS 2	// Select seconds
#define STATE_SELECT_INCREMENT 3	// Select increment
#define STATE_WAITING_TO_START 4
#define STATE_RUNNING 5

#define STATE_PAUSED 6
#define STATE_FINISHED 7

#define BLACK_TURN false
#define WHITE_TURN true

#define CLK_1 26
#define DIO_1 27
#define CLK_2 16
#define DIO_2 17

// Buttons pins
#define BUTTON_1 18 // (left button, white) remember PULLDOWN (button 1)
#define BUTTON_2 19 // (right button, black) remember PULLDOWN (button 2)

#define RESET_PRESS_COUNT 5

const uint8_t SEG_S_INC[] = {
  0x0,   // O
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,           // S
  0x0,   // O
  SEG_B | SEG_C                                    // I
};

const uint8_t SEG_S_MIN[] = {
  0x0,   // O
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,           // S
  0x0,   // O
  SEG_E | SEG_C | SEG_G                            // n
};

const uint8_t SEG_S_SEC[] = {
  0x0,   // O
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,           // S
  0x0,   // O
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,           // S
};

const uint8_t SEG_PRESS[] = {
  SEG_A | SEG_B | SEG_G | SEG_F | SEG_E,
  SEG_E | SEG_G,
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,
  SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,
};

const uint8_t SEG_ANY[] = {
  0x0,
  SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
  SEG_E | SEG_C | SEG_G ,
  SEG_B | SEG_C | SEG_F | SEG_G,
};

uint8_t state = STATE_IDLE;
hw_timer_t *clockTimer = NULL;

TM1637Display whiteDisplay(CLK_1, DIO_1);
TM1637Display blackDisplay(CLK_2, DIO_2);

// Available times
uint8_t availableSecondsSelection[] = { 0, 10, 15, 20, 30};
uint8_t* secondsPointer = availableSecondsSelection;              // Pointer to the current selection

uint8_t availableMinutesSelection[] = { 0, 1, 3, 5, 10, 15, 20, 30, 45, 60};
uint8_t* minutesPointer = availableMinutesSelection;              // Pointer to the current selection

uint8_t availableSecondsIncrement[] = { 0, 1, 2, 3, 5, 10, 15, 30};
uint8_t* incrementPointer = availableSecondsIncrement;              // Pointer to the current selection

uint8_t whitePressCounter = 0;
uint8_t blackPressCounter = 0;

static bool turn;
bool isDone = false;

// Minute selection
bool isSecondSelection = false;
bool isMinuteSelection = false;
bool isGameSelected = false;

// White
unsigned long whiteMillis = 0;          // Calculated when game starts

// Black
unsigned long blackMillis = 0;          // Calculated when game starts

// Buttons utils
unsigned long lastTimePressed = 0;   // used to debounce
bool btn1Pressed = false;
bool btn2Pressed = false;

void setupPins() {
    pinMode(BUTTON_1, INPUT_PULLDOWN);
    pinMode(BUTTON_2, INPUT_PULLDOWN);
}

void readButtons() {
    // debounce
    if (millis() - lastTimePressed < 300) {
        return;
    }
    if (digitalRead(BUTTON_1) == HIGH) {
        lastTimePressed = millis();
        btn1Pressed = true;
    }
    if (digitalRead(BUTTON_2) == HIGH) {
        lastTimePressed = millis();
        btn2Pressed = true;
    }
}

void resetButtons() {
    btn1Pressed = false;
    btn2Pressed = false;
}

void handleButtonIdle() {
  readButtons();
  if (btn1Pressed || btn2Pressed) {
    // Serial.println("Handle idle");
    state = STATE_SELECT_MINUTES;
  }
  resetButtons();
}

void handleButtonSelectMinutes() {
  readButtons();
  if (btn1Pressed) {
    if (++minutesPointer >= availableMinutesSelection + (sizeof(availableMinutesSelection) / sizeof(availableMinutesSelection[0]))) {
      minutesPointer = availableMinutesSelection;
    }
    // Serial.printf("Minutes selected: %d\n", *minutesPointer);
  };
  if (btn2Pressed) {
    // Serial.printf("Minutes selected: %d\n", *minutesPointer);
    state = STATE_SELECT_SECONDS;
  }
  resetButtons();
}

void handleButtonSelectSeconds() {
  readButtons();
  if (btn1Pressed) {
    if (++secondsPointer >= availableSecondsSelection + (sizeof(availableSecondsSelection) / sizeof(availableSecondsSelection[0]))) {
      secondsPointer = availableSecondsSelection;
    }
    // Serial.printf("Seconds selected: %d\n", *secondsPointer);
  }
  if (btn2Pressed) {
    // Serial.printf("Seconds selected: %d\n", *secondsPointer);
    whiteMillis = *secondsPointer * 1000 + *minutesPointer * 60000;
    blackMillis = whiteMillis;
    state = STATE_SELECT_INCREMENT;
  }
  resetButtons();
}

void handleButtonSelectIncrement() {
  readButtons();
  if (btn1Pressed) {
    if (++incrementPointer >= availableSecondsIncrement + (sizeof(availableSecondsIncrement) / sizeof(availableSecondsIncrement[0]))) {
      incrementPointer = availableSecondsIncrement;
    }
    // Serial.printf("Increment selected: %d\n", *incrementPointer);
  }
  if (btn2Pressed) {
    // Serial.printf("Increment selected: %d\n", *incrementPointer);
    state = STATE_WAITING_TO_START;
  }
  resetButtons();
}

// Interrupt to change turn
void whiteTurnPress(void) {
  whitePressCounter++;
  blackPressCounter = 0;
  if (turn == BLACK_TURN) {
    return;
  }
  whiteMillis += *incrementPointer * 1000;
  // _displayMs(&whiteDisplay, whiteMillis);
  turn = BLACK_TURN;
}
void blackTurnPress(void) {
  whitePressCounter = 0;
  blackPressCounter++;
  if (turn == WHITE_TURN) {
    return;
  }
  blackMillis += *incrementPointer * 1000;
  // _displayMs(&blackDisplay, blackMillis);
  turn = WHITE_TURN;
}

void handleButtonWaitingToStart() {
  readButtons();
  if (btn2Pressed) {
    // Serial.printf("Starting game with %d minutes and %d seconds\n", *minutesPointer, *secondsPointer);
    turn = WHITE_TURN;
    attachInterrupt(digitalPinToInterrupt(BUTTON_2), &blackTurnPress, RISING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_1), &whiteTurnPress, RISING);
    timerAlarmEnable(clockTimer);
    state = STATE_RUNNING;
  }
  resetButtons();
}

void handleStateRunning() {
  // Print the timer value of the curtrent turn
  // unsigned long milliseconds;
  // TM1637Display* display;
  // if (turn == WHITE_TURN) {
  //   // convert milliseconds to minutes:seconds:milliseconds format and print
  //   milliseconds = whiteMillis;
  //   display = &whiteDisplay;
  // } else {
  //   milliseconds = blackMillis;
  //   display = &blackDisplay;
  // }

  // _displayMs(display, milliseconds);
  _displayMs(&whiteDisplay, whiteMillis);
  _displayMs(&blackDisplay, blackMillis);
}

void _displayMs(TM1637Display* display, unsigned long milliseconds) {
  // calculate minutes, seconds and centiseconds to show mm:ss:cc
  uint8_t minutes = milliseconds / 60000;
  uint8_t seconds = (milliseconds % 60000) / 1000;
  uint8_t centis = (milliseconds % 60000) % 1000 / 10;
  if (minutes <= 0 && seconds <=  10) {
    displayDigit(*display, seconds * 100 + centis);
  } else {
    displayDigit(*display, minutes * 100 + seconds);
  }
}

void countDown() {
  if (turn == WHITE_TURN) {
    whiteMillis -= 10;
    if (whiteMillis <= 0) {
      state = STATE_FINISHED;
      detachInterrupt(digitalPinToInterrupt(BUTTON_2));
      detachInterrupt(digitalPinToInterrupt(BUTTON_1));
      displayDigit(whiteDisplay, 0);
      isDone = true;
    }
  } else {
    if (blackMillis <= 0) {
      timerAlarmDisable(clockTimer);
      state = STATE_FINISHED;
      detachInterrupt(digitalPinToInterrupt(BUTTON_2));
      detachInterrupt(digitalPinToInterrupt(BUTTON_1));
      displayDigit(blackDisplay, 0);
      isDone = true;
    }
    blackMillis -= 10;
  }
}

void handleButtonFinished() {
  // disable buttons for 3 seconds
  if (isDone) {
    lastTimePressed = millis();
    isDone = false;
  }
  if (millis() - lastTimePressed < 5000) {
    return;
  }
  readButtons();
  if (btn1Pressed || btn2Pressed) {
    state = STATE_SELECT_MINUTES;
  }
  resetButtons();
}

void setupTimer() {
  clockTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(clockTimer, &countDown, true);
  timerAlarmWrite(clockTimer, 10000, true);
}

// Setup displays
void setupDisplays() {
  uint16_t digits = *minutesPointer * 100 + *secondsPointer;
  whiteDisplay.setBrightness(0x0f);
  whiteDisplay.showNumberDecEx(digits, 0b01000000, true);

  blackDisplay.setBrightness(0x0f);
  blackDisplay.showNumberDecEx(digits, 0b01000000, true);
}

void displayDigit(TM1637Display display, unsigned long digit) {
  display.showNumberDecEx(digit, 0b01000000, true);
}

void handleDisplaySelect(const uint8_t seg_digit[]) {
  // blink every 500 ms
  if (millis() % 800 < 400) {
    whiteDisplay.setSegments(seg_digit);
  } else {
    whiteDisplay.clear();
  }
  displayDigit(blackDisplay, *minutesPointer * 100 + *secondsPointer);
}

void handleDisplaySelectIncrement(const uint8_t seg_digit[]) {
  // blink every 300 ms
  if (millis() % 800 < 400) {
    whiteDisplay.setSegments(seg_digit);
  } else {
    whiteDisplay.clear();
  }
  displayDigit(blackDisplay, *minutesPointer * 100 + *incrementPointer);
}

void handleDisplayGameDone() {
  // blink every 300 ms on the screen of the player who lost
  TM1637Display* display;
  if (turn == WHITE_TURN) {
    display = &whiteDisplay;
  } else {
    display = &blackDisplay;
  }
  if (millis() % 600 < 300) {
    displayDigit(*display, 0);
  } else {
    display->clear();
  }
}

void handleDisplayIdle() {
  // display press any
  if (millis() % 1000 < 500) {
    whiteDisplay.setSegments(SEG_PRESS);
    blackDisplay.clear();
  } else {
    blackDisplay.setSegments(SEG_ANY);
    whiteDisplay.clear();
  }
}

void handleReset() {
  if (whitePressCounter >= RESET_PRESS_COUNT || blackPressCounter >= RESET_PRESS_COUNT) {
    whitePressCounter = 0;
    blackPressCounter = 0;
    detachInterrupt(digitalPinToInterrupt(BUTTON_2));
    detachInterrupt(digitalPinToInterrupt(BUTTON_1));
    timerAlarmDisable(clockTimer);
    state = STATE_IDLE;
  }
}

void setup() {
  // Serial.begin(115200);
  setupPins();
  setupTimer();
  setupDisplays();
}

void loop() {
  switch (state)
  {
  case STATE_IDLE:
    handleButtonIdle();
    handleDisplayIdle();
    break;
  case STATE_SELECT_MINUTES:
    handleButtonSelectMinutes();
    handleDisplaySelect(SEG_S_MIN);
    break;
  case STATE_SELECT_SECONDS:
    handleButtonSelectSeconds();
    handleDisplaySelect(SEG_S_SEC);
    break;
  case STATE_SELECT_INCREMENT:
    handleDisplaySelectIncrement(SEG_S_INC);
    handleButtonSelectIncrement();
    break;
  case STATE_WAITING_TO_START:
    handleButtonWaitingToStart();
    displayDigit(whiteDisplay, *minutesPointer * 100 + *secondsPointer);
    displayDigit(blackDisplay, *minutesPointer * 100 + *secondsPointer);
    break;
  case STATE_RUNNING:
    handleReset();
    handleStateRunning();
    break;
  case STATE_FINISHED:
    timerAlarmDisable(clockTimer);
    handleButtonFinished();
    handleDisplayGameDone();
    break;
  default:
    break;
  }
}