// ===============
== == == == == == == == == == == == ==
// Smart Trash Arm - Arduino + 4 Servos
// - Gets trigger pulse on D2 from car Arduino
// - Sequence: pick cup in front, rotate to bin, drop, go home
// =========================================
#include <Servo.h>

    // ---- Pins ----
    const int TRIGGER_PIN = 2; // from car Arduino D8
const int BASE_PIN = 9;
const int SHOULDER_PIN = 10;
const int ELBOW_PIN = 11;
const int GRIPPER_PIN = 6;

// ---- Servos ----
Servo baseServo;
Servo shoulderServo;
Servo elbowServo;
Servo gripperServo;

// ---- Angles (TUNE these for your arm) ----
// Home pose
int BASE_HOME = 90;
int SHOULDER_HOME = 90;
int ELBOW_HOME = 90;
int GRIPPER_OPEN = 40;  // open gripper
int GRIPPER_CLOSE = 90; // close gripper

// Pick position (cup in front of car)
int SHOULDER_PICK = 120; // arm down
int ELBOW_PICK = 60;     // adjust so gripper touches ground

// Bin position (dustbin fixed on right side)
int BASE_BIN = 30;      // rotate base to bin side
int SHOULDER_BIN = 100; // slightly down
int ELBOW_BIN = 80;     // adjust for bin height

int lastTriggerState = LOW;

// smooth move helper
void moveServoSmooth(Servo &s, int from, int to, int stepDelay = 10)
{
  int step = (to > from) ? 1 : -1;
  for (int pos = from; pos != to; pos += step)
  {
    s.write(pos);
    delay(stepDelay);
  }
  s.write(to);
}

void goHome()
{
  baseServo.write(BASE_HOME);
  shoulderServo.write(SHOULDER_HOME);
  elbowServo.write(ELBOW_HOME);
  gripperServo.write(GRIPPER_OPEN);
}

// full sequence: pick cup and drop to bin
void pickupAndDrop()
{
  Serial.println("Arm: sequence start");

  // 1) ensure home + gripper open
  goHome();
  delay(500);

  // 2) go down to cup
  moveServoSmooth(shoulderServo, SHOULDER_HOME, SHOULDER_PICK, 15);
  moveServoSmooth(elbowServo, ELBOW_HOME, ELBOW_PICK, 15);
  delay(300);

  // 3) close gripper (grab cup)
  moveServoSmooth(gripperServo, GRIPPER_OPEN, GRIPPER_CLOSE, 10);
  delay(400);

  // 4) lift up
  moveServoSmooth(elbowServo, ELBOW_PICK, ELBOW_HOME, 15);
  moveServoSmooth(shoulderServo, SHOULDER_PICK, SHOULDER_HOME, 15);
  delay(300);

  // 5) rotate to bin side
  moveServoSmooth(baseServo, BASE_HOME, BASE_BIN, 15);
  delay(300);

  // 6) move to bin-dump pose
  moveServoSmooth(shoulderServo, SHOULDER_HOME, SHOULDER_BIN, 15);
  moveServoSmooth(elbowServo, ELBOW_HOME, ELBOW_BIN, 15);
  delay(300);

  // 7) open gripper -> drop cup
  moveServoSmooth(gripperServo, GRIPPER_CLOSE, GRIPPER_OPEN, 10);
  delay(500);

  // 8) back to home pose
  moveServoSmooth(elbowServo, ELBOW_BIN, ELBOW_HOME, 15);
  moveServoSmooth(shoulderServo, SHOULDER_BIN, SHOULDER_HOME, 15);
  moveServoSmooth(baseServo, BASE_BIN, BASE_HOME, 15);

  Serial.println("Arm: sequence done");
}

void setup()
{
  Serial.begin(115200);

  pinMode(TRIGGER_PIN, INPUT); // connected from car D8 (no pullup here; use series resistor if needed)

  baseServo.attach(BASE_PIN);
  shoulderServo.attach(SHOULDER_PIN);
  elbowServo.attach(ELBOW_PIN);
  gripperServo.attach(GRIPPER_PIN);

  goHome();
  delay(1000);
}

void loop()
{
  int state = digitalRead(TRIGGER_PIN);

  // detect rising edge: LOW → HIGH
  if (state == HIGH && lastTriggerState == LOW)
  {
    // got trigger from car -> run sequence
    pickupAndDrop();
  }

  lastTriggerState = state;

  // small delay to avoid jitter
  delay(20);
}
