#include <Keyboard.h>
#include <TimerOne.h>

#define SPEED_PIN       A6
#define SIFA_PIN        5
#define PANTOGRAPH_PIN  6
#define LIGHT_PIN       7
#define HORN_PIN        8
#define FORWARD_PIN     9
#define REVERSE_PIN     10
#define SIFA_LED_PIN    16
#define LIGHT_LED_PIN   14
#define BRAKE_PIN       15
#define SPEED_ZERO_PIN  18

// Calibrated from potentiometer
#define SPEED_RAW_MIN       10
#define SPEED_RAW_SLOW_MIN  390
#define SPEED_RAW_MID       535
#define SPEED_RAW_SLOW_MAX  660
#define SPEED_RAW_MAX       1010
#define SPEED_HYSTERESIS    10

// Blink interval in case of SIFA warning (in 100ms steps)
#define SIFA_BLINK_INTERVAL 5

// Wait time until SIFA is released after an emergency braking (in 100ms steps)
#define SIFA_BRAKE_DELAY 300

typedef enum direction {
  D_FORWARD,
  D_NONE,
  D_REVERSE
} direction_t;

// Speed and direction variables
int curSpeedRaw = 0;
int prevSpeedRaw = SPEED_RAW_MID;
int prevSpeedSteps = 0;
direction_t curDirection = D_NONE;

// Old button press states
bool prevSifaButtonState = 0;
bool prevPantographButtonState = 0;
bool prevLightButtonState = 0;
bool prevHornButtonState = 0;
bool prevBrakeButtonState = 0;

// States
bool curLightState = 0;
bool emergencyBrakeOccurred = 0;

// SIFA variables
volatile unsigned long curSifaTimer = 0;
volatile bool sifaWarningActive = 0;
volatile bool sifaBrakeActive = 0;
volatile bool sifaBrakeOccurred = 0;

// Delay until SIFA warning (in 100ms steps)
int sifaTimerWarning = 100;

// Delay until SIFA emergency braking (in 100ms steps)
int sifaTimerBrake = 200;

// Speed step configuration
int numSpeedSteps = 20; // TODO: Anpassbar machen
int numSpeedStepsSlow = 1;

int getSpeedRaw() {
  // TODO: Calibration factors
  return analogRead(SPEED_PIN);
}

direction_t getDirection() {
  if (!digitalRead(FORWARD_PIN)) {
    return D_FORWARD;
  } else if (!digitalRead(REVERSE_PIN)) {
    return D_REVERSE;
  }
  return D_NONE;
}

bool getSifaButton() {
  bool state = !digitalRead(SIFA_PIN);
  bool pressed = !prevSifaButtonState && state;
  prevSifaButtonState = state;
  return pressed;
}

bool getPantographButton() {
  bool state = !digitalRead(PANTOGRAPH_PIN);
  bool pressed = !prevPantographButtonState && state;
  prevPantographButtonState = state;
  return pressed;
}

bool getLightButton() {
  bool state = !digitalRead(LIGHT_PIN);
  bool pressed = !prevLightButtonState && state;
  prevLightButtonState = state;
  return pressed;
}

bool getHornButton() {
  return !digitalRead(HORN_PIN);
}

bool getBrakeButton() {
  bool state = !digitalRead(BRAKE_PIN);
  bool pressed = !prevBrakeButtonState && state;
  prevBrakeButtonState = state;
  return pressed;
}

bool getSpeedZeroButton() {
  return !digitalRead(SPEED_ZERO_PIN);
}

void decelerate() {
  Keyboard.print("x");
}

void accelerate() {
  Keyboard.print("w");
}

void stop() {
  Keyboard.print("s");
}

void togglePantograph() {
  // Numpad 1
  Keyboard.press(225);
  Keyboard.release(225);
}

void toggleLight() {
  Keyboard.print("l");
  curLightState = !curLightState;
  setLightLED(curLightState);
}

void setHorn(bool state) {
  if (state) {
    Keyboard.press('h');
  } else {
    Keyboard.release('h');
  }
}

void setSifaLED(bool state) {
  digitalWrite(SIFA_LED_PIN, state);
}

void toggleSifaLED() {
  digitalWrite(SIFA_LED_PIN, !digitalRead(SIFA_LED_PIN));
}

void setLightLED(bool state) {
  digitalWrite(LIGHT_LED_PIN, state);
}

int rawSpeedToSpeedSteps(int rawSpeed) {
  // Calculate amount of speed steps given a raw speed
  if (rawSpeed < SPEED_RAW_SLOW_MIN) {
    return map(rawSpeed, SPEED_RAW_MIN, SPEED_RAW_SLOW_MIN, -numSpeedSteps, -numSpeedStepsSlow);
  } else if (rawSpeed >= SPEED_RAW_SLOW_MIN && rawSpeed < SPEED_RAW_MID) {
    return map(rawSpeed, SPEED_RAW_SLOW_MIN, SPEED_RAW_MID, -numSpeedStepsSlow, 0);
  } else if (rawSpeed == SPEED_RAW_MID) {
    return 0;
  } else if (rawSpeed > SPEED_RAW_MID && rawSpeed <= SPEED_RAW_SLOW_MAX) {
    return map(rawSpeed, SPEED_RAW_MID, SPEED_RAW_SLOW_MAX, 0, numSpeedStepsSlow);
  } else if (rawSpeed > SPEED_RAW_SLOW_MAX) {
    return map(rawSpeed, SPEED_RAW_SLOW_MAX, SPEED_RAW_MAX, numSpeedStepsSlow, numSpeedSteps);
  }
}

void setSpeed(int newSpeed, direction_t direction) {
  // Limit speed to allowable range
  if (newSpeed > 0) {
    newSpeed = min(numSpeedSteps, newSpeed);
  } else if (newSpeed < 0) {
    newSpeed = max(-numSpeedSteps, newSpeed);
  }

  int speedStepsDiff = newSpeed - prevSpeedSteps;
  if (((speedStepsDiff > 0) && (direction == D_FORWARD)) || ((speedStepsDiff < 0) && (direction == D_REVERSE))) {
    for (int i = 0; i < abs(speedStepsDiff); i++) {
      accelerate();
    }
  } else if (((speedStepsDiff < 0) && (direction == D_FORWARD)) || ((speedStepsDiff > 0) && (direction == D_REVERSE))) {
    for (int i = 0; i < abs(speedStepsDiff); i++) {
      decelerate();
    }
  }
  prevSpeedSteps = newSpeed;
}

void sifaTimerInterrupt() {
  if (!getSpeedZeroButton() || sifaBrakeOccurred) {
    curSifaTimer++;
  }

  // Wait for defined time after braking
  if (sifaBrakeOccurred && (curSifaTimer < SIFA_BRAKE_DELAY)) {
    return;
  }

  if (curSifaTimer >= sifaTimerBrake) {
    sifaWarningActive = 0;
    sifaBrakeActive = 1;
  } else if (curSifaTimer >= sifaTimerWarning) {
    sifaWarningActive = 1;
  }

  if (sifaBrakeActive) {
    if (!sifaBrakeOccurred) {
      stop();
      sifaBrakeOccurred = 1;
      curSifaTimer = 0;
      setSifaLED(1);
    }
    if (getSpeedZeroButton()) {
      sifaBrakeActive = 0;
      sifaBrakeOccurred = 0;
      prevSpeedSteps = 0;
      curSifaTimer = 0;
    }
  } else if (sifaWarningActive) {
    if (curSifaTimer % SIFA_BLINK_INTERVAL == 0) {
      toggleSifaLED();
    }
  } else {
    setSifaLED(0);
  }
}

void resetSifa() {
  curSifaTimer = 0;
  sifaWarningActive = 0;
  sifaBrakeActive = 0;
}

void setup() {
  pinMode(SPEED_PIN, INPUT);
  pinMode(SIFA_PIN, INPUT_PULLUP);
  pinMode(PANTOGRAPH_PIN, INPUT_PULLUP);
  pinMode(LIGHT_PIN, INPUT_PULLUP);
  pinMode(HORN_PIN, INPUT_PULLUP);
  pinMode(FORWARD_PIN, INPUT_PULLUP);
  pinMode(REVERSE_PIN, INPUT_PULLUP);
  pinMode(SIFA_LED_PIN, OUTPUT);
  pinMode(LIGHT_LED_PIN, OUTPUT);
  pinMode(BRAKE_PIN, INPUT_PULLUP);
  pinMode(SPEED_ZERO_PIN, INPUT_PULLUP);

  setSifaLED(1);
  setLightLED(1);

  Keyboard.begin();
  delay(5000);

  setSifaLED(0);
  setLightLED(0);

  Timer1.initialize(100000); // 100ms intervals
  Timer1.attachInterrupt(sifaTimerInterrupt);
}

void loop() {
  if (getSpeedZeroButton()) {
    curSpeedRaw = SPEED_RAW_MID;
  } else {
    curSpeedRaw = getSpeedRaw();
  }
  curDirection = getDirection();

  if (emergencyBrakeOccurred && getSpeedZeroButton()) {
    emergencyBrakeOccurred = 0;
    prevSpeedSteps = 0;
  }

  if (!sifaBrakeOccurred && !emergencyBrakeOccurred) {
    if (getBrakeButton()) {
      stop();
      emergencyBrakeOccurred = 1;
    } else {
      if (abs(curSpeedRaw - prevSpeedRaw) > SPEED_HYSTERESIS) {
        setSpeed(rawSpeedToSpeedSteps(curSpeedRaw), curDirection);
        prevSpeedRaw = curSpeedRaw;
      }
    }

    if (getSifaButton() || getSpeedZeroButton()) {
      resetSifa();
    }
  }

  if (getPantographButton()) {
    togglePantograph();
  }

  if (getLightButton()) {
    toggleLight();
  }

  if (getHornButton()) {
    setHorn(1);
  } else {
    setHorn(0);
  }
}
