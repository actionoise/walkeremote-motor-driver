/*
------------------------------------------------------------
Control Signal Decoder for L298N Motor Driver
------------------------------------------------------------

This sketch reads digital control sequences originally used
to drive stepper motors through a ULN2803 driver and converts
them into motor control signals for an L298N motor driver.

The Arduino monitors two groups of input pins:

UP / DOWN signals
U1 → pin 2
U2 → pin 3
U3 → pin 4
U4 → pin 5

LEFT / RIGHT signals
L1 → pin 6
L2 → pin 7
L3 → pin 8
L4 → pin 9

The code detects specific bit transition sequences that
represent movement commands:

UP      → forward movement
DOWN    → reverse movement
RIGHT   → rotate right
LEFT    → rotate left

Once a sequence is recognized, the sketch generates the
corresponding control signals for the L298N driver:

IN1 → pin 10
IN2 → pin 11
IN3 → pin 12
IN4 → pin A0

Motor control logic:

• UP activates a latch mode that keeps the motors running forward.
• DOWN drives the motors in reverse while the command is active.
• When the input state returns to 0000, the motors stop.

The sketch therefore acts as a translation layer between
the original stepper motor control signals and a dual-motor
drive system using an L298N driver.

Future updates:
The code will be extended to support additional command
combinations in order to add new functions such as:

• relay control
• speed regulation
• additional motion behaviors

Author: Michele Tavolacci
Project: Walkeremote
*/

const byte U1 = 2;
const byte U2 = 3;
const byte U3 = 4;
const byte U4 = 5;

const byte L1 = 6;
const byte L2 = 7;
const byte L3 = 8;
const byte L4 = 9;

// L298N
const byte M1A = 10;   // IN1
const byte M1B = 11;   // IN2
const byte M2A = 12;   // IN3
const byte M2B = A0;   // IN4

byte lastUpState = 255;
byte lastLeftState = 255;

unsigned long lastUpChangeMs = 0;
unsigned long lastLeftChangeMs = 0;

bool upStoppedPrinted = true;
bool leftStoppedPrinted = true;

// forward latch mode only
enum DriveMode {
  MODE_STOP,
  MODE_FORWARD
};

DriveMode driveMode = MODE_STOP;

// gesture flags
bool upGestureSeen = false;
bool downGestureSeen = false;
bool reverseActive = false;

byte readState(byte p1, byte p2, byte p3, byte p4) {
  return (digitalRead(p1) << 3) |
         (digitalRead(p2) << 2) |
         (digitalRead(p3) << 1) |
          digitalRead(p4);
}

// --------------------
// Motor functions
// --------------------
void stopMotors() {
  digitalWrite(M1A, LOW);
  digitalWrite(M1B, LOW);
  digitalWrite(M2A, LOW);
  digitalWrite(M2B, LOW);
}

void goUp() {
  digitalWrite(M1A, HIGH);
  digitalWrite(M1B, LOW);

  digitalWrite(M2A, LOW);
  digitalWrite(M2B, HIGH);
}

void goDown() {
  digitalWrite(M1A, LOW);
  digitalWrite(M1B, HIGH);

  digitalWrite(M2A, HIGH);
  digitalWrite(M2B, LOW);
}

void goRight() {
  digitalWrite(M1A, HIGH);
  digitalWrite(M1B, LOW);

  digitalWrite(M2A, HIGH);
  digitalWrite(M2B, LOW);
}

void goLeft() {
  digitalWrite(M1A, LOW);
  digitalWrite(M1B, HIGH);

  digitalWrite(M2A, LOW);
  digitalWrite(M2B, HIGH);
}

void applyDriveMode() {
  if (driveMode == MODE_FORWARD) {
    goUp();
  } else {
    stopMotors();
  }
}

void handleUpClick() {
  Serial.println("CLICK_UP");
  reverseActive = false;
  driveMode = MODE_FORWARD;
  applyDriveMode();
  Serial.println("FORWARD_ON");
}

void handleDownClick() {
  Serial.println("CLICK_DOWN");
  driveMode = MODE_STOP;   // disable possible forward latch
  reverseActive = true;    // reverse active only while gesture is active
  goDown();
  Serial.println("REVERSE_ON");
}

void setup() {

  Serial.begin(115200);

  pinMode(U1, INPUT);
  pinMode(U2, INPUT);
  pinMode(U3, INPUT);
  pinMode(U4, INPUT);

  pinMode(L1, INPUT);
  pinMode(L2, INPUT);
  pinMode(L3, INPUT);
  pinMode(L4, INPUT);

  pinMode(M1A, OUTPUT);
  pinMode(M1B, OUTPUT);
  pinMode(M2A, OUTPUT);
  pinMode(M2B, OUTPUT);

  stopMotors();

  Serial.println("1 click UP = forward latch");
  Serial.println("1 click DOWN = reverse while gesture is active");
  Serial.println("DOWN -> 0000 = STOP");
}

void loop() {

  unsigned long now = millis();

  // ---------------- UP / DOWN ----------------
  byte upState = readState(U1, U2, U3, U4);

  if (upState != lastUpState) {

    lastUpChangeMs = now;
    upStoppedPrinted = false;

    // UP sequence: 6 -> 10 -> 9 -> 5 -> 6
    if (
      (lastUpState == 6  && upState == 10) ||
      (lastUpState == 10 && upState == 9 ) ||
      (lastUpState == 9  && upState == 5 ) ||
      (lastUpState == 5  && upState == 6 )
    ) {
      Serial.println("UP");
      upGestureSeen = true;
    }

    // DOWN sequence: 5 -> 9 -> 10 -> 6 -> 5
    else if (
      (lastUpState == 5  && upState == 9 ) ||
      (lastUpState == 9  && upState == 10) ||
      (lastUpState == 10 && upState == 6 ) ||
      (lastUpState == 6  && upState == 5 )
    ) {
      Serial.println("DOWN");
      goDown();

      downGestureSeen = true;
    }

    lastUpState = upState;
  }

  // close UP/DOWN gesture
  if (!upStoppedPrinted && upState == 0 && (now - lastUpChangeMs > 250)) {

    upStoppedPrinted = true;

    if (upGestureSeen) {
      upGestureSeen = false;
      handleUpClick();
    }

    if (downGestureSeen) {
      downGestureSeen = false;
      handleDownClick();
    }

    // if reverse was active, stop when signal returns to zero
    if (reverseActive) {
      reverseActive = false;
      stopMotors();
      Serial.println("STOP");
    }
  }

  // ---------------- RIGHT / LEFT ----------------
  byte lrState = readState(L1, L2, L3, L4);

  if (lrState != lastLeftState) {

    lastLeftChangeMs = now;
    leftStoppedPrinted = false;

    if (
      (lastLeftState == 13 && lrState == 9) ||
      (lastLeftState == 3  && lrState == 2) ||
      (lastLeftState == 2  && lrState == 6) ||
      (lastLeftState == 6  && lrState == 4)
    ) {
      Serial.println("RIGHT");
      goRight();
    }

    else if (
      (lastLeftState == 14 && lrState == 6) ||
      (lastLeftState == 3  && lrState == 9) ||
      (lastLeftState == 9  && lrState == 8) ||
      (lastLeftState == 6  && lrState == 7)
    ) {
      Serial.println("LEFT");
      goLeft();
    }

    lastLeftState = lrState;
  }

  // when releasing left/right return to active mode
  if (!leftStoppedPrinted && lrState == 0 && (now - lastLeftChangeMs > 250)) {

    leftStoppedPrinted = true;

    if (reverseActive) {
      goDown();
    } else {
      applyDriveMode();
    }
  }
}