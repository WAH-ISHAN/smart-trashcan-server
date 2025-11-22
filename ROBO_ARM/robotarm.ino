#include <Servo.h>

// ---- Servo Pins (4 DOF Arm) ----
#define SERVO_BASE 6
#define SERVO_SHOULDER 9
#define SERVO_ELBOW 10
#define SERVO_GRIPPER 11

Servo sBase, sShoulder, sElbow, sGrip;

// ---- Configuration Variables ----
// Servo Angle Calibration (Tune these for your arm's geometry)
const int BASE_HOME_ANGLE = 90;
const int SHOULDER_HOME_ANGLE = 100;
const int ELBOW_HOME_ANGLE = 60;
const int GRIPPER_OPEN = 40;
const int GRIPPER_CLOSE = 90;

const int SHOULDER_PICK_ANGLE = 130;
const int ELBOW_PICK_ANGLE = 80;

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
    servoInit();
    armHome(); // Arm goes to default position on start

    Serial.println("Arm Ready. Commands: P, D, O, C, H");
}

void loop()
{
    if (Serial.available())
    {
        char c = Serial.read();

        // Arm Commands
        if (c == 'P')
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