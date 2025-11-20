
# Smart Trash Sorting Robot with ESP32‑CAM, Arduino Nano & Edge Impulse

IoT project to detect plastic trash (bottles, cups, wrappers), drive a smart car chassis towards the object, pick it up with a robotic arm, and drop it into a trash bin.

Computer vision runs **on-board** using an **ESP32‑CAM (AI Thinker)** and an **Edge Impulse** object detection model.  
Motion (car + arm) is handled by an **Arduino Nano**. A simple Wi‑Fi web interface from the ESP32‑CAM shows the live camera feed and bounding boxes.

> Wi‑Fi AP details (default):
> - SSID: `ESP32CAM_TRASH`  
> - Password: `12345678`  
> - IP: `http://192.168.4.1`

---

## Table of Contents

- [Features](#features)
- [System Architecture](#system-architecture)
- [Hardware](#hardware)
  - [Main Components](#main-components)
  - [Power Architecture](#power-architecture)
  - [Pin Mapping](#pin-mapping)
- [Edge Impulse Model](#edge-impulse-model)
- [Firmware](#firmware)
  - [ESP32‑CAM (Vision + Wi‑Fi)](#esp32cam-vision--wifi)
  - [Arduino Nano (Car + Arm)](#arduino-nano-car--arm)
- [How to Build & Run](#how-to-build--run)
  - [1. Mechanical Assembly](#1-mechanical-assembly)
  - [2. ESP32‑CAM Setup](#2-esp32cam-setup)
  - [3. Training and Deploying the ML Model](#3-training-and-deploying-the-ml-model)
  - [4. Nano Firmware & Testing the Car + Arm](#4-nano-firmware--testing-the-car--arm)
  - [5. Full System Test](#5-full-system-test)
- [Serial Protocol (ESP32‑CAM → Nano)](#serial-protocol-esp32cam--nano)
- [Troubleshooting](#troubleshooting)
- [Future Improvements](#future-improvements)
- [License](#license)

---

## Features

- **On‑device object detection** with Edge Impulse (FOMO or SSD):
  - Detects: `bottle`, `cup`, `wrapper` (or your own labels).
- **ESP32‑CAM AI Thinker**:
  - Captures frames at 320×240, resizes to model input (e.g. 96×96).
  - Runs Edge Impulse inference on the camera frames.
  - Acts as a **Wi‑Fi Access Point**:
    - `ESP32CAM_TRASH / 12345678 / 192.168.4.1`
  - Simple **web interface**:
    - Live still image stream (auto‑refresh).
    - Bounding box overlay drawn in browser (JavaScript).
    - Text view of last detection (`/last` endpoint).
- **Arduino Nano**:
  - Drives a 2‑wheel **smart car chassis** via **L298N** motor driver.
  - Controls a **4‑DOF servo robotic arm** (base, shoulder, elbow, gripper).
  - Parses serial messages from ESP32‑CAM and:
    - Turns towards object using bounding box center.
    - Approaches the object.
    - Executes pick & drop sequences into a trash bin.
- **Flash LED** on ESP32‑CAM always ON (configurable).
- Simple, extendable **serial protocol** (`BOX,cx=...,w=...,label=...`).

---

## System Architecture

High‑level block diagram:

```text
 +---------------------+          +--------------------+
 |  ESP32-CAM AI Thinker|  UART   |   Arduino Nano     |
 |                     |<-------->|                    |
 |  - OV2640 Camera    |          |  - L298N Motor     |
 |  - Edge Impulse ML  |          |  - 4x Servo Arm    |
 |  - Wi-Fi AP + Web   |          |  - Car Logic       |
 +----------+----------+          +-----+--------------+
            |                           |
      Live video & bbox                |
     (phone/PC browser)                |
                                       |
                                 +-----v--------------+
                                 |  Smart Car Chassis |
                                 |  + Robotic Arm     |
                                 +--------------------+
```

ESP32‑CAM is the **brain for vision**, Nano is the **brain for motion**.

---

## Hardware

### Main Components

- **ESP32‑CAM AI Thinker** (+ ESP32‑CAM MB or USB‑to‑TTL programmer)
- **Arduino Nano** (original or clone)
- **Smart Car Chassis** (2WD):
  - 2x DC motors + wheels
  - 1x front caster / ball wheel
- **Motor Driver**: L298N (or TB6612FNG)
- **Robotic Arm** (4 DOF):
  - Servo1: Base (MG996R/MG995)
  - Servo2: Shoulder
  - Servo3: Elbow
  - Servo4: Gripper (SG90/MG90S)
- **Power**:
  - Battery pack: 2S Li‑ion / LiPo (7.4 V) or 2×18650
  - DC–DC Buck converter 5 V, 3–5 A (for logic + servos)
- Jumper wires, breadboard / PCB, chassis mounting hardware.

### Power Architecture

- **Battery (7.4 V)**:
  - → L298N `12V` / `Vmot` (motor power).
  - → Buck converter `VIN` (step down to 5 V).
- **Buck 5 V output**:
  - → Arduino Nano `5V`.
  - → All servo VCC (red wires).
  - → ESP32‑CAM `5V` pin (through regulator).
- **Ground (GND)**:
  - Battery GND, Buck GND, Nano GND, ESP32‑CAM GND, L298N GND, servo GND **all common**.

> Important: Do **not** power servos directly from the Nano 5 V pin – use the dedicated 5 V buck output (3–5 A).

### Pin Mapping

#### Arduino Nano → L298N (2WD car)

| L298N Pin | Nano Pin | Function         |
|----------:|:--------:|-----------------|
| ENA       | D3 (PWM) | Left motor speed|
| IN1       | D4       | Left dir 1      |
| IN2       | D2       | Left dir 2      |
| ENB       | D5 (PWM) | Right motor speed|
| IN3       | D8       | Right dir 1     |
| IN4       | D12      | Right dir 2     |

#### Arduino Nano → Servos (roboto arm)

| Servo       | Nano Pin |
|------------:|:--------:|
| Base        | D6       |
| Shoulder    | D9       |
| Elbow       | D10      |
| Gripper     | D11      |

Servos: Signal → Nano pin, 5 V → Buck 5 V, GND → Buck GND.

#### ESP32‑CAM AI Thinker

Camera pins are wired as in `camera_config_t` and standard for AI Thinker variant.

Additional:

| Function   | ESP32‑CAM Pin |
|-----------:|:--------------|
| Flash LED  | GPIO 4        |
| 5 V input  | 5V            |
| GND        | GND           |

> UART between ESP32‑CAM and Nano (when you connect them):
> - ESP32‑CAM TX0 (GPIO1) → Nano RX (D0)
> - ESP32‑CAM RX0 (GPIO3) ← Nano TX (D1) (via level shifter or resistor divider)

---

## Edge Impulse Model

This project uses an **Edge Impulse Camera model** (object detection).

Typical configuration:

- **Impulse**:
  - Input: Image (grayscale or RGB)
  - Resolution: **96×96** (recommended for ESP32‑CAM)
  - Processing: Image (resize, normalize)
  - Learning Block: **FOMO** (Object Detection) or SSD
- **Labels** (example):
  - `bottle`, `cup`, `wrapper`, `background`
- **Deployment**:
  - Quantized (int8)
  - EON Compiler ON
  - Export → **Arduino Library**

After exporting, add the ZIP via Arduino IDE:

```text
Sketch → Include Library → Add .ZIP Library... → ML_IOT_inferencing.zip
```

`ML_IOT_inferencing.h` is then used in the ESP32‑CAM sketch.

---

## Firmware

### ESP32‑CAM (Vision + Wi‑Fi)

Main responsibilities:

- Initialize camera (320×240 JPEG).
- Convert JPEG → RGB888 → resize to model input size.
- Run `run_classifier()` from Edge Impulse.
- Keep “best” bounding box information:
  - `label`, `score`, `x`, `y`, `w`, `h`, `cx`.
- Start a **Wi‑Fi Access Point**:
  - SSID: `ESP32CAM_TRASH`
  - Password: `12345678`
  - Default IP: `192.168.4.1`
- Start HTTP server:
  - `GET /` → Simple webpage with `<img src="/jpg">` and JavaScript auto‑refresh.
  - `GET /jpg` → Current camera frame (JPEG).
  - `GET /last` → Last detection info (plain text).
- Print bounding boxes and a short line on serial:
  ```text
  BOX,cx=56,w=16,iw=96,label=wrapper,score=0.80
  BOX,none
  ```

Flash LED is always ON:

```cpp
pinMode(FLASH_LED_PIN, OUTPUT);
digitalWrite(FLASH_LED_PIN, HIGH);
```

### Arduino Nano (Car + Arm)

> Place Nano firmware in `nano/` folder (not shown here in full to keep README short).

Responsibilities:

- Drive motors via L298N.
- Control 4 servos (base, shoulder, elbow, gripper).
- Implement:
  - `forward()`, `backward()`, `turnLeft()`, `turnRight()`, `stop()`.
  - `pickSequence()` – lower arm, close gripper, lift.
  - `dropSequence()` – move to bin, lower, open gripper, return home.
- Read serial lines from ESP32‑CAM:
  - Parse `BOX,cx=...,w=...,iw=...,label=...,score=...`.
  - If `label` and `score` above threshold:
    - Compute horizontal error: `ex = cx - iw/2`.
    - Adjust left/right motor speeds to center object.
    - Use `w` (bbox width) as a proxy for distance. When big enough → call `pickSequence()` + `dropSequence()`.

Basic Nano algorithm (pseudo):

```cpp
if (line.startsWith("BOX,none")) {
    // rotate slowly to search
} else if (line.startsWith("BOX,")) {
    // parse cx, w, iw
    int ex = cx - iw/2;
    int turn = Kp * ex;
    int left  = baseSpeed - turn;
    int right = baseSpeed + turn;
    drive(left, right);

    if (w >= pickThreshold) {
        stopMotors();
        pickSequence();
        dropSequence();
    }
}
```

---

## How to Build & Run

### 1. Mechanical Assembly

- Assemble the 2‑wheel chassis:
  - Motors at rear, caster at front.
- Mount L298N and Arduino Nano on the top plate.
- Mount base servo (Servo1) vertically at the front center of chassis.
- Assemble robotic arm on top of the base servo:
  - Servo2 (shoulder) at end of first link.
  - Servo3 (elbow) at end of second link.
  - Servo4 (gripper) at end.
- Mount ESP32‑CAM so that the camera sees in front of the car and the arm.

### 2. ESP32‑CAM Setup

1. Install **ESP32 board support** in Arduino IDE:
   - File → Preferences → Additional Boards Manager URLs:
     ```text
     https://dl.espressif.com/dl/package_esp32_index.json
     ```
   - Tools → Board → Boards Manager… → Search `esp32` → Install.
2. Select:
   - Board: `AI Thinker ESP32-CAM`
   - Partition: `Huge APP (3MB No OTA/1MB SPIFFS)`
   - Upload speed: 115200
3. Add your Edge Impulse library (`ML_IOT_inferencing`).
4. Upload this ESP32‑CAM sketch.
5. Open Serial Monitor @ 115200 and reset the board.
6. Look for:
   ```text
   Edge Impulse Inferencing Demo
   Camera initialized
   Access Point started.
     SSID: ESP32CAM_TRASH
     PASS: 12345678
     IP: 192.168.4.1
   HTTP server started
   ```
7. On your phone/PC:
   - Connect Wi‑Fi → `ESP32CAM_TRASH` (password `12345678`).
   - Open a browser → `http://192.168.4.1`.
   - You should see:
     - The camera image updating ~3 fps.
     - When an object is detected, red bounding box + label/score.

### 3. Training and Deploying the ML Model

1. In Edge Impulse:
   - Create a new project.
   - **Data acquisition**:
     - Capture images of your trash objects (bottle/cup/wrapper).
   - **Label** bounding boxes for each object in the Studio UI.
2. Configure your **Impulse**:
   - Image data, size e.g. 96×96.
   - Image processing block (color or grayscale).
   - Learning block: FOMO Object Detection.
3. Train the model until accuracy is acceptable.
4. Deploy → **Arduino Library**.
5. Replace the current `ML_IOT_inferencing` library with the new exported one if needed.

### 4. Nano Firmware & Testing the Car + Arm

1. Write / upload Nano firmware (separate `.ino` sketch) implementing:
   - Motors: ENA/IN1/IN2 and ENB/IN3/IN4.
   - Servos: `Servo` library on pins 6, 9, 10, 11.
   - Serial parsing as described above.
2. Connect Nano via USB and test manually:
   - `F`, `B`, `L`, `R`, `S` commands → car movement.
   - `P`, `D`, `O`, `C` → arm pick/drop/open/close.
3. Tune servo angles:
   - Adjust `write()` values to align gripper with ground object and trash bin.

### 5. Full System Test

1. Connect ESP32‑CAM TX/RX to Nano RX/TX (with voltage divider or shifter).
2. Power everything from the main 7.4 V battery + buck 5 V.
3. Start ESP32‑CAM (AP mode).
4. Open browser (`192.168.4.1`) and place a known object in front.
5. Observe:
   - ESP32‑CAM detection lines in Serial:
     ```text
     BOX,cx=56,w=16,iw=96,label=wrapper,score=0.80
     ```
   - Nano interpreting this and:
     - turning car towards object,
     - approaching,
     - executing pick & drop.

---

## Serial Protocol (ESP32‑CAM → Nano)

Lines sent over UART at 115200 baud:

- No object:
  ```text
  BOX,none
  ```
- Object detected:
  ```text
  BOX,cx=<center_x>,w=<bbox_width>,iw=<image_width>,label=<label>,score=<0..1>
  ```
  Example:
  ```text
  BOX,cx=84,w=8,iw=96,label=cup,score=0.59
  ```

Meanings:

- `cx`: center x of the best bounding box in model coordinate space (0..image_width-1).
- `w`: bbox width (in model pixels).
- `iw`: model input width (e.g., 96).
- `label`: object class string (`bottle`, `cup`, `wrapper`, etc.).
- `score`: confidence.

Nano firmware uses `cx` vs `iw/2` to steer, `w` as distance cue.

---

## Troubleshooting

- **Camera init failed**:
  - Check `AI Thinker ESP32-CAM` board selected.
  - Power supply ≥ 5 V / 1 A to ESP32‑CAM.
- **Only one frame, no “live” effect**:
  - Make sure you open `http://192.168.4.1/` (root), not `/jpg`.
  - Ensure JavaScript is enabled in browser.
- **No AP visible**:
  - Check Serial Monitor for messages.
  - Try power cycling.
- **Servo jitter / resets**:
  - Use a separate 5 V buck (3–5 A) for servos.
  - Keep grounds common.
- **Serial noise / wrong data**:
  - Use 3.3 V‑safe level shifter or resistor divider Nano TX → ESP32 RX.

---

## Future Improvements

- Add NodeMCU / ESP8266 dashboard with:
  - Firebase logging (accuracy, detection history).
  - Remote manual control of car + arm.
- Add ultrasonic / IR sensors to avoid obstacles.
- Separate “recyclable / non‑recyclable” bins with different labels.

---

## License

This project is based on Edge Impulse Arduino examples (MIT License).  
All additional code, wiring diagrams, and documentation in this repository are released under the MIT License unless otherwise specified.
```

You can now:

1. Create a GitHub repo.
2. Add your `esp32cam` sketch file and `nano` sketch file.
3. Save this text as `README.md` in the root of the repo.

If you want, I can also generate a shorter Sinhala‑only README for a report/assignment, while keeping this detailed English version for GitHub.
