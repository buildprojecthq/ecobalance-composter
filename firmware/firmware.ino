#include <Servo.h>
#include <EEPROM.h>

Servo gateServo;

// Pins
const int SERVO_PIN      = 9;
const int MQ135_PIN      = A0;
const int GREEN_LED_PIN  = 4;
const int YELLOW_LED_PIN = 3;
const int RED_LED_PIN    = 2;

// Verified SG90 gate angles (EEPROM-backed)
const int CLOSED_POS    = 110;
const int OPEN_POS      = 170;
const int STEP_DELAY_MS = 25;

// 5-minute MQ-135 warm-up (300,000 ms)
const unsigned long WARMUP_MS = 300000UL;

// MQ-135 thresholds (calibrated around ~145 settled baseline)
const int YELLOW_THRESHOLD = 165;
const int RED_THRESHOLD    = 185;

// Timing (milliseconds)
const unsigned long SAMPLE_INTERVAL_MS   = 500;
const unsigned long OPEN_HOLD_MS         = 1000;   // Brief open time (~1 sec)
const unsigned long DISPENSE_COOLDOWN_MS = 15000;  // Cooldown between leaf doses

int currentPos;
unsigned long bootTime = 0;
unsigned long lastSampleTime = 0;
unsigned long lastDispenseTime = 0;
unsigned long lastWarmupLogTime = 0;
bool hasDispensedOnce = false;
bool warmupDonePrinted = false;

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

void setLeds(bool greenOn, bool yellowOn, bool redOn) {
  digitalWrite(GREEN_LED_PIN,  greenOn  ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, yellowOn ? HIGH : LOW);
  digitalWrite(RED_LED_PIN,    redOn    ? HIGH : LOW);
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
  setLeds(false, false, false); // All LEDs OFF during warm-up

  // Restore last saved servo angle without boot jerk, then ensure closed
  currentPos = EEPROM.read(0);
  if (currentPos < CLOSED_POS || currentPos > OPEN_POS) {
    currentPos = CLOSED_POS;
  }
  gateServo.write(currentPos);
  gateServo.attach(SERVO_PIN);
  closeGate();

  bootTime = millis();
  Serial.println("MQ-135 warming up for 5 minutes (LEDs OFF)...");
}

void loop() {
  unsigned long now = millis();

  // Warm-up phase: keep all LEDs off and skip active triggers until 5 min elapse
  if (now - bootTime < WARMUP_MS) {
    setLeds(false, false, false);
    if (now - lastWarmupLogTime >= 10000UL) {
      lastWarmupLogTime = now;
      unsigned long remSec = (WARMUP_MS - (now - bootTime)) / 1000UL;
      int rawWarm = analogRead(MQ135_PIN);
      Serial.print("Warming up... ");
      Serial.print(remSec);
      Serial.print("s left | Raw: ");
      Serial.println(rawWarm);
    }
    return;
  }

  if (!warmupDonePrinted) {
    warmupDonePrinted = true;
    Serial.println("Warm-up complete. Starting MQ-135 monitoring.");
  }

  if (now - lastSampleTime < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleTime = now;

  int rawVal = analogRead(MQ135_PIN);
  float volts = rawVal * (5.0 / 1023.0);

  Serial.print("MQ135 Raw: ");
  Serial.print(rawVal);
  Serial.print(" | V: ");
  Serial.print(volts, 2);

  if (rawVal >= RED_THRESHOLD) {
    setLeds(false, false, true);
    bool cooldownReady = !hasDispensedOnce || (now - lastDispenseTime >= DISPENSE_COOLDOWN_MS);
    if (cooldownReady) {
      Serial.println(" | State: RED (Wet/High) -> Dispensing dry leaves");
      dispenseDryLeavesOnce();
    } else {
      Serial.println(" | State: RED (Wet/High) -> Cooldown active");
    }
  } else if (rawVal >= YELLOW_THRESHOLD) {
    setLeds(false, true, false);
    Serial.println(" | State: YELLOW (Moderate)");
  } else {
    setLeds(true, false, false);
    Serial.println(" | State: GREEN (Balanced)");
  }
}
