/*
------------------------------------------------------------
Control Signal Decoder for L298N Motor Driver
------------------------------------------------------------

Update:
A relay control feature has been added to allow switching
between two operating modes.

Using the LEFT → RIGHT → LEFT command combination, the
Arduino toggles an auxiliary output (A1) that can drive a
relay. This relay can be used to select whether the original
stepper motors of the camera head remain active or if the
control signals are redirected to the brushless motors used
to move the rover platform.

When the relay is activated, the control signals coming from
the camera are routed to the brushless motor driver,
allowing the PTZ camera controls to move the rover.

When the relay is deactivated, the original stepper motors
of the camera head can operate normally again.

------------------------------------------------------------

This sketch reads digital control sequences originally used
to drive stepper motors through a ULN2803 driver and converts
them into motor control signals for an L298N motor driver.

The Arduino monitors two groups of input pins:

UP / DOWN signals
U1 -> pin 2
U2 -> pin 3
U3 -> pin 4
U4 -> pin 5

LEFT / RIGHT signals
L1 -> pin 6
L2 -> pin 7
L3 -> pin 8
L4 -> pin 9

The code detects specific bit transition sequences that
represent movement commands:

UP      -> forward movement
DOWN    -> reverse movement
RIGHT   -> rotate right
LEFT    -> rotate left

Once a sequence is recognized, the sketch generates the
corresponding control signals for the L298N driver:

IN1 -> pin 10
IN2 -> pin 11
IN3 -> pin 12
IN4 -> pin A0

Motor control logic:

- UP activates a latch mode that keeps the motors running forward.
- DOWN drives the motors in reverse while the command is active.
- When the input state returns to 0000, the motors stop.

The sketch therefore acts as a translation layer between
the original stepper motor control signals and a dual-motor
drive system using an L298N driver.

Future updates:
The code will be extended to support additional command
combinations in order to add new functions such as:

- relay control
- speed regulation
- additional motion behaviors

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

// Auxiliary output
const byte AUX_PIN = A1;

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

// LEFT/RIGHT combo detection for AUX toggle
unsigned long lastLRGestureMs = 0;
bool auxState = false;
const unsigned long comboWindow = 1000;

byte gesture1 = 0;
byte gesture2 = 0;
byte gesture3 = 0;

// this prevents multiple LEFT/RIGHT detections during one single physical gesture
bool lrGestureLocked = false;
byte currentLRGesture = 0; // 1 = LEFT, 2 = RIGHT

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

void toggleAux() {
  auxState = !auxState;

  if (auxState) {
    digitalWrite(AUX_PIN, HIGH);
    Serial.println("A1 ON");
  } else {
    digitalWrite(AUX_PIN, LOW);
    Serial.println("A1 OFF");
  }
}

void pushLRGesture(byte g, unsigned long now) {
  if (now - lastLRGestureMs > comboWindow) {
    gesture1 = 0;
    gesture2 = 0;
    gesture3 = 0;
  }

  gesture1 = gesture2;
  gesture2 = gesture3;
  gesture3 = g;

  lastLRGestureMs = now;

  Serial.print("SEQ: ");
  Serial.print(gesture1);
  Serial.print(" ");
  Serial.print(gesture2);
  Serial.print(" ");
  Serial.println(gesture3);

  // 1 = LEFT
  // 2 = RIGHT
  if ((gesture1 == 1 && gesture2 == 2 && gesture3 == 1) ||
      (gesture1 == 2 && gesture2 == 1 && gesture3 == 2)) {
    toggleAux();
    gesture1 = 0;
    gesture2 = 0;
    gesture3 = 0;
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
  driveMode = MODE_STOP;
  reverseActive = true;
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

  pinMode(AUX_PIN, OUTPUT);
  digitalWrite(AUX_PIN, LOW);

  stopMotors();

  Serial.println("1 click UP = forward latch");
  Serial.println("1 click DOWN = reverse while gesture is active");
  Serial.println("DOWN -> 0000 = STOP");
  Serial.println("LEFT-RIGHT-LEFT or RIGHT-LEFT-RIGHT = toggle A1");
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

    // detect RIGHT only once per physical gesture
    if (!lrGestureLocked && (
      (lastLeftState == 13 && lrState == 9) ||
      (lastLeftState == 3  && lrState == 2) ||
      (lastLeftState == 2  && lrState == 6) ||
      (lastLeftState == 6  && lrState == 4)
    )) {
      Serial.println("RIGHT");
      goRight();

      lrGestureLocked = true;
      currentLRGesture = 2;
      pushLRGesture(2, now);
    }

    // detect LEFT only once per physical gesture
    else if (!lrGestureLocked && (
      (lastLeftState == 14 && lrState == 6) ||
      (lastLeftState == 3  && lrState == 9) ||
      (lastLeftState == 9  && lrState == 8) ||
      (lastLeftState == 6  && lrState == 7)
    )) {
      Serial.println("LEFT");
      goLeft();

      lrGestureLocked = true;
      currentLRGesture = 1;
      pushLRGesture(1, now);
    }

    lastLeftState = lrState;
  }

  // when releasing left/right return to active mode
  if (!leftStoppedPrinted && lrState == 0 && (now - lastLeftChangeMs > 250)) {
    leftStoppedPrinted = true;

    // unlock LR gesture only when the axis returns to zero
    lrGestureLocked = false;
    currentLRGesture = 0;

    if (reverseActive) {
      goDown();
    } else {
      applyDriveMode();
    }
  }
}