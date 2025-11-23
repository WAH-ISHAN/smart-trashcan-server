// =========================================
// Smart Trash Car - Arduino (UNO/Nano) + L9110 + ESP32-CAM
// - L9110: 2 DC motors (left + right)
// - ESP32-CAM sends over Serial: 
//     BOX,cx=..,w=..,iw=..,label=..,score=..
// - This sketch: car auto moves to CUP, then
//   triggers arm controller (D8 pulse) to pick+drop.
// =========================================

// ----- PIN CONFIG (Arduino -> L9110) -----
const int LEFT_IN1  = 2;   // L9110 A-1A  (Left motor)
const int LEFT_IN2  = 3;   // L9110 A-1B  (Left motor)
const int RIGHT_IN1 = 4;   // L9110 B-1A  (Right motor)
const int RIGHT_IN2 = 5;   // L9110 B-1B  (Right motor)

// ----- ARM TRIGGER PIN -----
const int ARM_TRIGGER_PIN = 8; // Connected to Arm Arduino D2

// ----- MODES -----
enum Mode {
  MODE_SCAN,      // keep rotating (360° scan)
  MODE_APPROACH   // move toward detected object
};

Mode mode = MODE_SCAN;

// ----- TIMING / THRESHOLDS -----
unsigned long lastDetectionTime = 0;
const unsigned long lostTimeout = 1500;    // 1.5s no detection -> back to SCAN

const float scoreThreshold      = 0.6;     // minimum confidence
const float stopWidthRatio      = 0.7;     // w > 0.7 * iw -> consider "close"
const float centerDeadbandRatio = 0.15;    // ±15% of width -> "center" zone

// Only follow cup label
String targetLabel = "cup";   // Edge Impulse label name (change if needed)

String line = "";

// --------- MOTOR CONTROL (L9110) ---------

void setupMotors() {
  pinMode(LEFT_IN1,  OUTPUT);
  pinMode(LEFT_IN2,  OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);

  pinMode(ARM_TRIGGER_PIN, OUTPUT);
  digitalWrite(ARM_TRIGGER_PIN, LOW);

  stopMotors();
}

void stopMotors() {
  digitalWrite(LEFT_IN1,  LOW);
  digitalWrite(LEFT_IN2,  LOW);
  digitalWrite(RIGHT_IN1, LOW);
  digitalWrite(RIGHT_IN2, LOW);
}

// Car forward
void forward() {
  // Left motor forward
  digitalWrite(LEFT_IN1, HIGH);
  digitalWrite(LEFT_IN2, LOW);
  // Right motor forward
  digitalWrite(RIGHT_IN1, HIGH);
  digitalWrite(RIGHT_IN2, LOW);
}

// Car backward (optional)
void backward() {
  digitalWrite(LEFT_IN1, LOW);
  digitalWrite(LEFT_IN2, HIGH);
  digitalWrite(RIGHT_IN1, LOW);
  digitalWrite(RIGHT_IN2, HIGH);
}

// Turn left on the spot (360 rotate)
void turnLeft() {
  // Left motor backward, right motor forward
  digitalWrite(LEFT_IN1, LOW);
  digitalWrite(LEFT_IN2, HIGH);
  digitalWrite(RIGHT_IN1, HIGH);
  digitalWrite(RIGHT_IN2, LOW);
}

// Turn right on the spot
void turnRight() {
  // Left motor forward, right motor backward
  digitalWrite(LEFT_IN1, HIGH);
  digitalWrite(LEFT_IN2, LOW);
  digitalWrite(RIGHT_IN1, LOW);
  digitalWrite(RIGHT_IN2, HIGH);
}

// --------- STRING PARSE HELPERS ---------

int getIntValue(String s, String key) {
  int idx = s.indexOf(key);
  if (idx < 0) return -1;
  idx += key.length();
  int end = s.indexOf(',', idx);
  if (end < 0) end = s.length();
  return s.substring(idx, end).toInt();
}

float getFloatValue(String s, String key) {
  int idx = s.indexOf(key);
  if (idx < 0) return 0;
  idx += key.length();
  int end = s.indexOf(',', idx);
  if (end < 0) end = s.length();
  return s.substring(idx, end).toFloat();
}

String getStringValue(String s, String key) {
  int idx = s.indexOf(key);
  if (idx < 0) return "";
  idx += key.length();
  int end = s.indexOf(',', idx);
  if (end < 0) end = s.length();
  return s.substring(idx, end);
}

// --------- HANDLE ONE ML RESULT LINE ---------

void processLine(String s) {
  s.trim();
  if (!s.startsWith("BOX")) {
    return; // not our message
  }

  Serial.println("DBG: " + s);

  if (s == "BOX,none") {
    // no object; timeout logic uses lastDetectionTime
    return;
  }

  // Parse values from ESP32-CAM
  int cx   = getIntValue(s, "cx=");
  int iw   = getIntValue(s, "iw=");
  int w    = getIntValue(s, "w=");
  float score = getFloatValue(s, "score=");
  String label = getStringValue(s, "label=");

  if (cx < 0 || iw <= 0) {
    return; // parsing error
  }

  lastDetectionTime = millis();

  // 1) Confidence check
  if (score < scoreThreshold) return;

  // 2) Label filter (only cup)
  if (targetLabel.length() > 0 && label != targetLabel) return;

  // 3) center zone
  int center   = iw / 2;
  int deadband = (int)(iw * centerDeadbandRatio);
  int left_th  = center - deadband;
  int right_th = center + deadband;

  // 4) close enough to cup? -> stop + trigger arm
  if (w > (int)(iw * stopWidthRatio)) {
    stopMotors();

    // ----- trigger arm pickup+drop -----
    Serial.println("Cup reached, triggering arm...");
    digitalWrite(ARM_TRIGGER_PIN, HIGH);
    delay(100);                 // short pulse
    digitalWrite(ARM_TRIGGER_PIN, LOW);

    // give time for arm to finish (tune as needed)
    delay(5000);

    mode = MODE_SCAN;           // after that, go back to scanning
    return;
  }

  // 5) normal follow logic
  if (mode == MODE_SCAN) {
    if (cx < left_th) {
      turnLeft();
    }
    else if (cx > right_th) {
      turnRight();
    }
    else {
      mode = MODE_APPROACH;
      forward();
    }
  }
  else if (mode == MODE_APPROACH) {
    if (cx < left_th) {
      turnLeft();
    }
    else if (cx > right_th) {
      turnRight();
    }
    else {
      forward();
    }
  }
}

// --------- SETUP / LOOP ---------

void setup() {
  setupMotors();
  Serial.begin(115200);   // must match ESP32-CAM baud
  mode = MODE_SCAN;
}

void loop() {
  // Read Serial line from ESP32-CAM
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      if (line.length() > 0) {
        processLine(line);
        line = "";
      }
    }
    else if (c != '\r') {
      line += c;
    }
  }

  unsigned long now = millis();

  // mode handling
  if (mode == MODE_SCAN) {
    // keep rotating until we see cup
    turnLeft();
  }

  if (mode == MODE_APPROACH) {
    // if we lost detection for some time, back to scan
    if (now - lastDetectionTime > lostTimeout) {
      mode = MODE_SCAN;
    }
  }
}
