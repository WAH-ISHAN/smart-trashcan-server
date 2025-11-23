// =========================================
// Smart Trash Car - ESP32-CAM + Arduino + L9110
// - L9110: 2 DC motors (left + right)
// - Arduino UNO / Nano
// - ESP32-CAM sends: BOX,cx=..,w=..,iw=..,label=..,score=..
// =========================================

// ----- PIN CONFIG (Arduino -> L9110) -----
const int LEFT_IN1  = 2;   // L9110 A-1A  (Left motor)
const int LEFT_IN2  = 3;   // L9110 A-1B  (Left motor)
const int RIGHT_IN1 = 4;   // L9110 B-1A  (Right motor)
const int RIGHT_IN2 = 5;  // L9110 B-1B  (Right motor)

// ----- MODES -----
enum Mode {
  MODE_SCAN,      // keep rotating (360° scan)
  MODE_APPROACH   // move toward detected object
};

Mode mode = MODE_SCAN;

// ----- TIMING / THRESHOLDS -----
unsigned long lastDetectionTime = 0;
const unsigned long lostTimeout = 1500;    // 1.5s no detection -> back to SCAN

const float scoreThreshold     = 0.6;      // minimum confidence
const float stopWidthRatio     = 0.7;      // w > 0.7 * iw -> consider "close"
const float centerDeadbandRatio = 0.15;    // ±15% of width -> "center" zone

// Only follow specific label? put name here (e.g. "bottle").
// If empty string -> accept all labels.
String targetLabel = "";   // example: "bottle";

String line = "";

// --------- MOTOR CONTROL (L9110) ---------

void setupMotors() {
  pinMode(LEFT_IN1,  OUTPUT);
  pinMode(LEFT_IN2,  OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
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

// Car backward (nijama ona nam)
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
    // Not our message; ignore
    return;
  }

  // Debug to PC serial monitor (TX only)
  Serial.println("DBG: " + s);

  if (s == "BOX,none") {
    // No object in this frame - just rely on timeout logic
    return;
  }

  // Parse values from ESP32-CAM
  int cx   = getIntValue(s, "cx=");
  int iw   = getIntValue(s, "iw=");
  int w    = getIntValue(s, "w=");
  float score = getFloatValue(s, "score=");
  String label = getStringValue(s, "label=");

  if (cx < 0 || iw <= 0) {
    // Parsing error
    return;
  }

  lastDetectionTime = millis();

  // Confidence threshold
  if (score < scoreThreshold) {
    return;
  }

  // Label filter (if targetLabel set)
  if (targetLabel.length() > 0 && label != targetLabel) {
    return;
  }

  // "Center" zone calculation
  int center = iw / 2;
  int deadband = (int)(iw * centerDeadbandRatio);
  int left_th  = center - deadband;
  int right_th = center + deadband;

  // If object already very close -> stop & back to scan mode
  if (w > (int)(iw * stopWidthRatio)) {
    stopMotors();
    mode = MODE_SCAN;    // re-scan after reaching object
    return;
  }

  if (mode == MODE_SCAN) {
    // We are rotating 360. Once we see object:
    if (cx < left_th) {
      // object on left -> continue turning left
      turnLeft();
    }
    else if (cx > right_th) {
      // object on right -> rotate right to align
      turnRight();
    }
    else {
      // object roughly in center -> start going forward
      mode = MODE_APPROACH;
      forward();
    }
  }
  else if (mode == MODE_APPROACH) {
    // Move toward object, with small steering corrections
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

// --------- ARDUINO STANDARD FUNCTIONS ---------

void setup() {
  setupMotors();
  Serial.begin(115200);  // must match ESP32-CAM baud
  mode = MODE_SCAN;      // start by scanning (360 rotate)
}

void loop() {
  // --- Read serial from ESP32-CAM line by line ---
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

  // --- Mode handling ---

  if (mode == MODE_SCAN) {
    // keep rotating (simple 360 scan)
    turnLeft();
  }

  if (mode == MODE_APPROACH) {
    // If no detection for some time, fall back to scan mode
    if (now - lastDetectionTime > lostTimeout) {
      mode = MODE_SCAN;
    }
  }
}
