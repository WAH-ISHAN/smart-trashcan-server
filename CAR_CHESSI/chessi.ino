// ---- Motor Driver (L298N) Pins ----
#define ENA 3  // PWM for Left Motor (Speed A)
#define IN1 4  // Left Motor Direction 1
#define IN2 2  // Left Motor Direction 2
#define ENB 5  // PWM for Right Motor (Speed B)
#define IN3 8  // Right Motor Direction 1
#define IN4 12 // Right Motor Direction 2

// ---- Configuration Variables ----
// Default Motor Speed (0-255)
const int DRIVE_SPEED = 160;

// =========================================================
// ---- Motor Control Functions ----
// =========================================================

void motorInit()
{
  // Set all motor control pins as OUTPUT
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
    // Forward direction
    digitalWrite(motor_in1, HIGH);
    digitalWrite(motor_in2, LOW);
    analogWrite(motor_ena, spd); // Set speed using PWM
  }
  else
  {
    // Reverse direction
    digitalWrite(motor_in1, LOW);
    digitalWrite(motor_in2, HIGH);
    analogWrite(motor_ena, -spd); // Use absolute speed value
  }
}

// Drive both motors simultaneously
void drive(int left, int right)
{
  driveOneMotor(ENA, IN1, IN2, left);  // Left motor
  driveOneMotor(ENB, IN3, IN4, right); // Right motor
}

// Stop both motors instantly
void stopMotors()
{
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}

// =========================================================
// ---- Setup & Loop ----
// =========================================================

void setup()
{
  Serial.begin(115200);
  motorInit();

  Serial.println("Car Motor Ready. Commands: F,B,L,R,S");
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
    // Spin Turn Left (Left motor backwards, Right motor forwards)
    else if (c == 'L')
    {
      drive(-DRIVE_SPEED, DRIVE_SPEED);
      Serial.println("Turn Left");
    }
    // Spin Turn Right (Left motor forwards, Right motor backwards)
    else if (c == 'R')
    {
      drive(DRIVE_SPEED, -DRIVE_SPEED);
      Serial.println("Turn Right");
    }
    else if (c == 'S')
    {
      stopMotors();
      Serial.println("STOP");
    }
  }
}