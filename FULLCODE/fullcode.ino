#include <Servo.h>

// ---- Motor Driver (L298N) Pins ----
#define ENA 3  // PWM for Left Motor (Speed A)
#define IN1 4  // Left Motor Direction 1
#define IN2 2  // Left Motor Direction 2
#define ENB 5  // PWM for Right Motor (Speed B)
#define IN3 8  // Right Motor Direction 1
#define IN4 12 // Right Motor Direction 2

// ---- Servo Pins (4 DOF Arm) ----
#define SERVO_BASE 6
#define SERVO_SHOULDER 9
#define SERVO_ELBOW 10
#define SERVO_GRIPPER 11

Servo sBase, sShoulder, sElbow, sGrip;

// ---- Configuration Variables ----
// Default Motor Speed (0-255)
const int DRIVE_SPEED = 160;

// Servo Angle Calibration (These values MUST be tuned for YOUR arm's geometry)
const int BASE_HOME_ANGLE = 90;
const int SHOULDER_HOME_ANGLE = 100;
const int ELBOW_HOME_ANGLE = 60;
const int GRIPPER_OPEN = 40;
const int GRIPPER_CLOSE = 90;

const int SHOULDER_PICK_ANGLE = 130;
const int ELBOW_PICK_ANGLE = 80;

// =========================================================
// ---- Motor Control Functions ----
// =========================================================

void motorInit()
{
    pinMode(ENA, OUTPUT);
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(ENB, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);
    stopMotors();
}

// Drive one motor (speed can be negative for reverse)
void driveOneMotor(int motor_ena, int motor_in1, int motor_in2, int spd)
{
    spd = constrain(spd, -255, 255);
    if (spd >= 0)
    {
        digitalWrite(motor_in1, HIGH);
        digitalWrite(motor_in2, LOW);
        analogWrite(motor_ena, spd);
    }
    else
    {
        digitalWrite(motor_in1, LOW);
        digitalWrite(motor_in2, HIGH);
        analogWrite(motor_ena, -spd);
    }
}

void drive(int left, int right)
{
    driveOneMotor(ENA, IN1, IN2, left);  // Left motor
    driveOneMotor(ENB, IN3, IN4, right); // Right motor
}

void stopMotors()
{
    analogWrite(ENA, 0); // ENA, ENB low for quick stop
    analogWrite(ENB, 0);
}

// =========================================================
// ---- Servo / Arm Control Functions ----
// =========================================================

void servoInit()
{
    sBase.attach(SERVO_BASE);
    sShoulder.attach(SERVO_SHOULDER);
    sElbow.attach(SERVO_ELBOW);
    sGrip.attach(SERVO_GRIPPER);
}

void openGripper()
{
    sGrip.write(GRIPPER_OPEN);
    delay(300);
}
void closeGripper()
{
    sGrip.write(GRIPPER_CLOSE);
    delay(300);
}

void armHome()
{
    // Forward direction, medium height (safe/ready position)
    sBase.write(BASE_HOME_ANGLE);
    sShoulder.write(SHOULDER_HOME_ANGLE);
    sElbow.write(ELBOW_HOME_ANGLE);
    openGripper();
    delay(600);
}

// Lowers arm to pick up object from ground level
void armPickPoseDown()
{
    sShoulder.write(SHOULDER_PICK_ANGLE);
    sElbow.write(ELBOW_PICK_ANGLE);
    delay(700);
}

// Lifts object to a safe height for driving
void armCarryPose()
{
    sShoulder.write(SHOULDER_HOME_ANGLE);
    sElbow.write(ELBOW_HOME_ANGLE);
    delay(700);
}

void pickSequence()
{
    Serial.println("Starting PICK sequence...");
    stopMotors();
    armHome();

    // 1. Move to picking pose (down)
    openGripper();
    armPickPoseDown();

    // 2. Grasp object
    closeGripper();

    // 3. Lift to carry pose
    armCarryPose();
    Serial.println("PICK sequence complete.");
}

void dropSequence()
{
    Serial.println("Starting DROP sequence...");
    stopMotors();

    // 1. Turn base servo slightly to side (assuming dustbin is to the right)
    sBase.write(140); // Tune angle for right-side drop
    delay(500);

    // 2. Lower arm over dustbin
    sShoulder.write(120);
    sElbow.write(80);
    delay(700);

    // 3. Release object
    openGripper();
    delay(400);

    // 4. Return to home position
    armHome();
    Serial.println("DROP sequence complete.");
}

// =========================================================
// ---- Setup & Loop ----
// =========================================================

void setup()
{
    Serial.begin(115200);
    motorInit();
    servoInit();
    armHome(); // Arm goes to default position on start

    Serial.println("Car + Arm Ready. Commands: F,B,L,R,S,P,D,O,C,H");
}

void loop()
{
    if (Serial.available())
    {
        char c = Serial.read();

        // Drive Commands
        if (c == 'F')
        {
            drive(DRIVE_SPEED, DRIVE_SPEED);
            Serial.println("Forward");
        }
        else if (c == 'B')
        {
            drive(-DRIVE_SPEED, -DRIVE_SPEED);
            Serial.println("Backward");
        }
        else if (c == 'L')
        {
            drive(-DRIVE_SPEED, DRIVE_SPEED);
            Serial.println("Turn Left");
        } // Spin turn
        else if (c == 'R')
        {
            drive(DRIVE_SPEED, -DRIVE_SPEED);
            Serial.println("Turn Right");
        } // Spin turn
        else if (c == 'S')
        {
            stopMotors();
            Serial.println("STOP");
        }

        // Arm Commands
        else if (c == 'P')
        {
            pickSequence();
        }
        else if (c == 'D')
        {
            dropSequence();
        }
        else if (c == 'O')
        {
            openGripper();
            Serial.println("Gripper OPEN");
        }
        else if (c == 'C')
        {
            closeGripper();
            Serial.println("Gripper CLOSE");
        }
        else if (c == 'H')
        {
            armHome();
            Serial.println("Arm HOME");
        }
    }
}