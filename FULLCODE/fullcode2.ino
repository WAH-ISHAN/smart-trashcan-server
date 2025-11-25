#include <Arduino.h>
#include <Servo.h>

// --- COMMUNICATION CONFIGURATION ---
// The main robot board expects to receive data from the ESP32-CAM over Serial1.
// Connect TX of Camera ESP32 to RX1 (GPIO 9 on some ESP32) of Control ESP32
// Connect RX of Camera ESP32 to TX1 (GPIO 10 on some ESP32) of Control ESP32
#define CAMERA_SERIAL Serial1
const long COMMUNICATION_BAUD = 115200;

// --- CHASSIS (L298N) MOTOR DRIVER PINS (ESP32) ---
#define ENA_L 25 // PWM Pin for Left Motor Speed
#define IN1_L 26 // Left Motor Forward
#define IN2_L 27 // Left Motor Backward

#define ENB_R 14 // PWM Pin for Right Motor Speed
#define IN3_R 12 // Right Motor Forward
#define IN4_R 13 // Right Motor Backward

const int DRIVE_SPEED = 180;            // Motor speed (0-255)
const long MOVE_FORWARD_TIME_MS = 1500; // Time to approach object (TUNE THIS)
const long ROTATE_TIME_MS = 2000;       // Time for 360-degree rotation (TUNE THIS)

// --- ROBOT ARM (4 SERVOS) PINS (ESP32) ---
#define BASE_SERVO_PIN 15    // Yaw
#define SHOULDER_SERVO_PIN 4 // Pitch 1
#define ELBOW_SERVO_PIN 16   // Pitch 2
#define GRIPPER_SERVO_PIN 17 // Gripper

// Servo Objects
Servo baseServo;
Servo shoulderServo;
Servo elbowServo;
Servo gripperServo;

// --- TIMING AND STATE MANAGEMENT ---
enum RobotState
{
    STATE_SCANNING,     // Waiting for signal or scan interval timeout
    STATE_MOVE_START,   // Initiate movement
    STATE_MOVING,       // Non-blocking movement phase
    STATE_PICKING_UP,   // Execute arm pickup sequence
    STATE_DROPPING,     // Execute arm drop sequence
    STATE_ROTATE_START, // Initiate 360 rotation
    STATE_ROTATING      // Non-blocking rotation phase
};

RobotState currentState = STATE_SCANNING;
unsigned long lastScanTime = 0;
unsigned long stateStartTime = 0;
const long SCAN_INTERVAL_MS = 3 * 60 * 1000; // 3 minutes in milliseconds

// Object detection data
int targetX = -1; // -1 means no object detected/processed
int targetY = -1;
String detectedLabel = "";

/* --- MOTOR CONTROL FUNCTIONS (L298N) --- */

/**
 * @brief Sets the direction and speed of the chassis motors.
 * @param leftDir -1 (Backward), 0 (Stop), 1 (Forward)
 * @param rightDir -1 (Backward), 0 (Stop), 1 (Forward)
 * @param speedL PWM speed (0-255)
 * @param speedR PWM speed (0-255)
 */
void setChassis(int leftDir, int rightDir, int speedL, int speedR)
{
    // Left Motor Control
    digitalWrite(IN1_L, leftDir == 1);
    digitalWrite(IN2_L, leftDir == -1);
    analogWrite(ENA_L, speedL);

    // Right Motor Control
    digitalWrite(IN3_R, rightDir == 1);
    digitalWrite(IN4_R, rightDir == -1);
    analogWrite(ENB_R, speedR);
}

void moveForward()
{
    setChassis(1, 1, DRIVE_SPEED, DRIVE_SPEED);
    Serial.println("CHASSIS: Moving Forward");
}

void rotateInPlace()
{
    // Rotate Right in place (Left forward, Right backward)
    setChassis(1, -1, DRIVE_SPEED, DRIVE_SPEED);
    Serial.println("CHASSIS: Starting rotation...");
}

void stopChassis()
{
    setChassis(0, 0, 0, 0);
    Serial.println("CHASSIS: Stopped");
}

/* --- ROBOT ARM CONTROL FUNCTIONS (PREDEFINED POSES) --- */

/**
 * @brief Sets the arm to the stable initial position.
 */
void arm_initial_pose()
{
    baseServo.write(90);
    shoulderServo.write(10);
    elbowServo.write(170);
    gripperServo.write(10); // Open
    delay(500);             // Give time for arm to move (necessary mechanical delay)
    Serial.println("ARM: Initial Pose Ready.");
}

/**
 * @brief Executes the sequence to pick up the object.
 */
void arm_to_pickup_pose()
{
    Serial.print("ARM: Attempting to pick up ");
    Serial.println(detectedLabel);

    // 1. Point to object (use targetX to adjust Base angle)
    // Map camera X-coordinate (0-320) to servo angle (e.g., 45-135 degrees)
    int base_angle = map(targetX, 0, 320, 45, 135);
    baseServo.write(base_angle);
    delay(400);

    // 2. Reach down (TUNE THESE ANGLES)
    shoulderServo.write(90);
    elbowServo.write(90);
    gripperServo.write(10); // Open
    delay(600);

    // 3. Grab
    gripperServo.write(80); // Close (TUNE FOR GRIP)
    delay(400);

    // 4. Lift up slightly
    shoulderServo.write(45);
    elbowServo.write(150);
    delay(500);

    Serial.println("ARM: Object Picked Up.");
}

/**
 * @brief Executes the sequence to deposit the object into the bucket.
 */
void arm_to_bucket_pose()
{
    Serial.println("ARM: Dropping object into bucket.");

    // 1. Rotate base towards the bucket position (assuming bucket is behind the arm)
    baseServo.write(170);
    delay(600);

    // 2. Position arm over the bucket (TUNE THESE ANGLES)
    shoulderServo.write(100);
    elbowServo.write(100);
    delay(500);

    // 3. Drop
    gripperServo.write(10); // Open
    delay(500);

    // 4. Return to safe position and close gripper
    gripperServo.write(80);
    Serial.println("ARM: Object Dropped.");
}

/* --- COMMUNICATION FUNCTION --- */

// Reads the serial port for detection data in the format: "LABEL,X,Y"
// Example: "bottle,160,120"
void readCameraData()
{
    while (CAMERA_SERIAL.available())
    {
        String data = CAMERA_SERIAL.readStringUntil('\n');
        data.trim();

        int firstComma = data.indexOf(',');
        int secondComma = data.indexOf(',', firstComma + 1);

        if (firstComma == -1 || secondComma == -1)
            continue;

        String label = data.substring(0, firstComma);
        String xStr = data.substring(firstComma + 1, secondComma);
        String yStr = data.substring(secondComma + 1);

        // Check if the detected label is one we care about ("cup" or "bottle")
        if (label.equalsIgnoreCase("bottle") || label.equalsIgnoreCase("cup"))
        {
            targetX = xStr.toInt();
            targetY = yStr.toInt();
            detectedLabel = label;

            Serial.print("COMM: Detected ");
            Serial.print(detectedLabel);
            Serial.print(" at X=");
            Serial.println(targetX);

            // If we are currently scanning and an object is found, switch state
            if (currentState == STATE_SCANNING || currentState == STATE_ROTATING)
            {
                currentState = STATE_MOVE_START;
            }
        }
    }
}

/* --- ARDUINO SETUP AND LOOP --- */

void setup()
{
    // Initialize standard Serial for debugging
    Serial.begin(115200);
    while (!Serial)
        ;
    Serial.println("--- Robot Control System Initialized ---");

    // Initialize Camera Serial Port
    CAMERA_SERIAL.begin(COMMUNICATION_BAUD);
    Serial.println("Listening for Camera data on Serial1...");

    // Motor Pins Setup (L298N)
    pinMode(ENA_L, OUTPUT);
    pinMode(IN1_L, OUTPUT);
    pinMode(IN2_L, OUTPUT);
    pinMode(ENB_R, OUTPUT);
    pinMode(IN3_R, OUTPUT);
    pinMode(IN4_R, OUTPUT);

    // Servo Pins Setup
    baseServo.attach(BASE_SERVO_PIN);
    shoulderServo.attach(SHOULDER_SERVO_PIN);
    elbowServo.attach(ELBOW_SERVO_PIN);
    gripperServo.attach(GRIPPER_SERVO_PIN);

    // Set initial arm pose
    arm_initial_pose();

    // Set initial scan time
    lastScanTime = millis();
}

void loop()
{
    // 1. Always check for camera data, regardless of the current state
    readCameraData();

    // 2. State Machine for control flow
    switch (currentState)
    {

    case STATE_SCANNING:
        // Check the 3-minute interval for automatic rotation/scan
        if (millis() - lastScanTime >= SCAN_INTERVAL_MS)
        {
            currentState = STATE_ROTATE_START;
            Serial.println("STATE: 3 minute scan interval reached. Starting scan rotation.");
        }
        // If targetX != -1, readCameraData() already transitioned the state to STATE_MOVE_START
        break;

    case STATE_MOVE_START:
        // Non-blocking movement initiated
        moveForward();
        stateStartTime = millis();
        currentState = STATE_MOVING;
        break;

    case STATE_MOVING:
        // Check if movement time has elapsed (non-blocking)
        if (millis() - stateStartTime >= MOVE_FORWARD_TIME_MS)
        {
            stopChassis();
            currentState = STATE_PICKING_UP;
            Serial.println("STATE: Reached object. Transition to pickup.");
        }
        break;

    case STATE_PICKING_UP:
        // This entire sequence is run once and contains the necessary mechanical delays
        arm_to_pickup_pose();

        currentState = STATE_DROPPING;
        break;

    case STATE_DROPPING:
        // This entire sequence is run once and contains the necessary mechanical delays
        arm_to_bucket_pose();

        // Reset target data after successful drop
        targetX = -1;
        targetY = -1;
        detectedLabel = "";

        currentState = STATE_ROTATE_START; // Immediately start rotation
        break;

    case STATE_ROTATE_START:
        // Initiate 360-degree rotation
        rotateInPlace();
        stateStartTime = millis();
        currentState = STATE_ROTATING;
        Serial.println("STATE: Executing 360-degree rotation...");
        break;

    case STATE_ROTATING:
        // Check if rotation time has elapsed (non-blocking)
        if (millis() - stateStartTime >= ROTATE_TIME_MS)
        {
            stopChassis();
            arm_initial_pose();      // Return arm to stable position
            lastScanTime = millis(); // Reset 3-minute timer

            currentState = STATE_SCANNING;
            Serial.println("STATE: Rotation complete. Scanning started (3 minute timer reset).");
        }
        break;
    }
}
