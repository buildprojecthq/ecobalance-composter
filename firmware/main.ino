#include <Servo.h>
#include <EEPROM.h>

Servo gateServo;

// Pins (matches your pasted sketch)
const int SERVO_PIN      = 9;
const int MQ135_PIN      = A0;
const int GREEN_LED_PIN  = 4;
const int YELLOW_LED_PIN = 3;
const int RED_LED_PIN    = 2;

// Verified SG90 gate angles (EEPROM-backed)
const int CLOSED_POS    = 110;
const int OPEN_POS      = 170;
const int STEP_DELAY_MS = 25;

// Warm-up time: use 300000UL (5 min) for full run, or e.g. 15000UL for quick demo boot
const unsigned long WARMUP_MS = 300000UL;

// Balanced around ~145 settled baseline with hysteresis to prevent LED flicker
const int YELLOW_ENTER = 162;
const int YELLOW_EXIT  = 156;
const int RED_ENTER    = 180;
const int RED_EXIT     = 172;

// Timing (milliseconds)
const unsigned long SAMPLE_INTERVAL_MS   = 250;
const unsigned long OPEN_HOLD_MS         = 1000;
const unsigned long DISPENSE_COOLDOWN_MS = 12000;

enum CompostState { STATE_GREEN, STATE_YELLOW, STATE_RED };
CompostState currentState = STATE_GREEN;

int currentPos;
unsigned long bootTime = 0;
unsigned long lastSampleTime = 0;
unsigned long lastDispenseTime = 0;
unsigned long lastWarmupLogTime = 0;
bool hasDispensedOnce = false;
bool warmupDonePrinted = false;

int readSmoothedMq135() {
  const int SAMPLES = 5;
  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(MQ135_PIN);
  }
  return (int)(sum / SAMPLES);
}

void moveGate(int target) {
  int step = (target > currentPos) ? 1 : -1;
  while (currentPos != target) {
    currentPos += step;
    gateServo.write(currentPos);
    delay(STEP_DELAY_MS);
  }
  EEPROM.update(0, currentPos);
}

void openGate()  { moveGate(OPEN_POS); }
void closeGate() { moveGate(CLOSED_POS); }

void applyStateLeds(CompostState s) {
  digitalWrite(GREEN_LED_PIN,  (s == STATE_GREEN)  ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, (s == STATE_YELLOW) ? HIGH : LOW);
  digitalWrite(RED_LED_PIN,    (s == STATE_RED)    ? HIGH : LOW);
}

void dispenseDryLeavesOnce() {
  openGate();
  delay(OPEN_HOLD_MS);
  closeGate();
  lastDispenseTime = millis();
  hasDispensedOnce = true;
}

void setup() {
  Serial.begin(9600);

  pinMode(MQ135_PIN, INPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);

  currentPos = EEPROM.read(0);
  if (currentPos < CLOSED_POS || currentPos > OPEN_POS) {
    currentPos = CLOSED_POS;
  }
  gateServo.write(currentPos);
  gateServo.attach(SERVO_PIN);
  closeGate();

  bootTime = millis();
  Serial.println("MQ-135 warming up... LEDs OFF");
}

void loop() {
  unsigned long now = millis();

  if (now - bootTime < WARMUP_MS) {
    if (now - lastWarmupLogTime >= 10000UL) {
      lastWarmupLogTime = now;
      unsigned long remSec = (WARMUP_MS - (now - bootTime)) / 1000UL;
      Serial.print("Warming up... ");
      Serial.print(remSec);
      Serial.print("s left | Raw: ");
      Serial.println(readSmoothedMq135());
    }
    return;
  }

  if (!warmupDonePrinted) {
    warmupDonePrinted = true;
    Serial.println("Warm-up complete. Monitoring active.");
  }

  if (now - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = now;

  int rawVal = readSmoothedMq135();
  float volts = rawVal * (5.0 / 1023.0);

  // State machine with hysteresis for crisp, flicker-free LEDs
  if (currentState == STATE_RED) {
    if (rawVal < RED_EXIT) {
      currentState = (rawVal >= YELLOW_ENTER) ? STATE_YELLOW : STATE_GREEN;
    }
  } else if (currentState == STATE_YELLOW) {
    if (rawVal >= RED_ENTER) {
      currentState = STATE_RED;
    } else if (rawVal < YELLOW_EXIT) {
      currentState = STATE_GREEN;
    }
  } else { // STATE_GREEN
    if (rawVal >= RED_ENTER) {
      currentState = STATE_RED;
    } else if (rawVal >= YELLOW_ENTER) {
      currentState = STATE_YELLOW;
    }
  }

  applyStateLeds(currentState);

  Serial.print("Raw: ");
  Serial.print(rawVal);
  Serial.print(" | V: ");
  Serial.print(volts, 2);

  if (currentState == STATE_RED) {
    Serial.print(" | RED");
    bool cooldownReady = !hasDispensedOnce || (now - lastDispenseTime >= DISPENSE_COOLDOWN_MS);
    if (cooldownReady) {
      Serial.println(" -> Dispensing dry leaves");
      dispenseDryLeavesOnce();
    } else {
      Serial.println(" -> Cooldown");
    }
  } else if (currentState == STATE_YELLOW) {
    Serial.println(" | YELLOW");
  } else {
    Serial.println(" | GREEN");
  }
}