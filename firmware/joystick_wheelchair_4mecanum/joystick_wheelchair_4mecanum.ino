/*
 * Joystick-Controlled Wheelchair - 4 MECANUM WHEELS (slides sideways)
 * HW-504 joystick + Arduino Uno + 2x L298N + 4 motors. No extra mode button.
 *
 * Everything is done with the joystick's own push button (D2):
 *   Short press while DRIVING  -> STOP at once (motors off)
 *   Short press while STOPPED  -> motors ON
 *   HOLD 1 second while STOPPED -> switch TURN <-> SLIDE mode
 * The mode can only change while the chair is stopped.
 * The Arduino's built-in LED (pin 13) is ON in SLIDE mode, OFF in TURN mode.
 *
 *   TURN mode  -> stick left/right turns the chair
 *   SLIDE mode -> stick left/right slides the chair sideways, no turning
 * Up/down is always forward/back. Up + right in SLIDE mode = diagonal.
 *
 * Wheel speeds (FL = front-left, FR = front-right, RL = rear-left, RR = rear-right):
 *   FL = forward + side + turn      FR = forward - side - turn
 *   RL = forward - side + turn      RR = forward + side - turn
 *
 * Mount the wheels so FRONT-LEFT and REAR-RIGHT are the same roller type,
 * and FRONT-RIGHT and REAR-LEFT are the other type.
 * If "slide right" goes left, set INVERT_SIDE = true.
 *
 * Built by Sujail Bin Abdullah (github.com/SujailXD) / NebuCoders
 */

// ---------- Pins ----------
const int JOY_X = A0, JOY_Y = A1, JOY_SW = 2;   // joystick
const int MODE_LED = 13;                        // built-in LED: on = SLIDE mode

// Front L298N
const int FL_EN = 5, FL_A = 7,  FL_B = 8;       // ENA, IN1, IN2  -> front-left motor
const int FR_EN = 6, FR_A = 3,  FR_B = 4;       // ENB, IN3, IN4  -> front-right motor
// Rear L298N
const int RL_EN = 9,  RL_A = 11, RL_B = 12;     // ENA, IN1, IN2  -> rear-left motor
const int RR_EN = 10, RR_A = A2, RR_B = A3;     // ENB, IN3, IN4  -> rear-right motor

// ---------- Settings ----------
const int  MAX_SPEED   = 200;
const int  TURN_LIMIT  = 70;     // % turning strength in TURN mode
const int  DEADZONE    = 60;
const int  RAMP_STEP   = 4;
const int  MIN_PWM     = 70;
const bool INVERT_X    = false;
const bool INVERT_Y    = true;
const bool INVERT_SIDE = false;  // flip if sliding goes the wrong way
const unsigned long HOLD_MS     = 1000;  // hold this long (while stopped) to change mode
const unsigned long DEBOUNCE_MS = 25;

int centerX = 512, centerY = 512;
int now[4] = { 0, 0, 0, 0 };     // FL, FR, RL, RR (ramped)
bool enabled = false;
bool slideMode = false;

void setMotor(int en, int inA, int inB, int speed) {
  speed = constrain(speed, -255, 255);
  int pwm = abs(speed);
  if (pwm > 0) pwm = map(pwm, 1, 255, MIN_PWM, 255);
  digitalWrite(inA, speed > 0);
  digitalWrite(inB, speed < 0);
  analogWrite(en, pwm);
}

void driveAll() {
  setMotor(FL_EN, FL_A, FL_B, now[0]);
  setMotor(FR_EN, FR_A, FR_B, now[1]);
  setMotor(RL_EN, RL_A, RL_B, now[2]);
  setMotor(RR_EN, RR_A, RR_B, now[3]);
}

void stopAll() {
  for (int i = 0; i < 4; i++) now[i] = 0;
  driveAll();
}

int readAxis(int pin, int center, bool invert) {
  int v = analogRead(pin) - center;
  if (abs(v) < DEADZONE) return 0;
  v = (v > 0) ? map(v, DEADZONE, 511, 0, 255)
              : map(v, -512, -DEADZONE, -255, 0);
  v = constrain(v, -255, 255);
  return invert ? -v : v;
}

int rampTo(int cur, int target) {
  bool slowing = cur != 0 && (abs(target) < abs(cur) || (target > 0) != (cur > 0));
  int step = slowing ? RAMP_STEP * 2 : RAMP_STEP;
  if (target > cur) return min(cur + step, target);
  if (target < cur) return max(cur - step, target);
  return cur;
}

// Joystick button: short press = stop / start, hold while stopped = change mode
void handleButton() {
  static bool rawLast = HIGH, stable = HIGH, handled = true;
  static unsigned long changedAt = 0, pressedAt = 0;

  bool raw = digitalRead(JOY_SW);
  if (raw != rawLast) { rawLast = raw; changedAt = millis(); }
  if (millis() - changedAt < DEBOUNCE_MS || raw == stable) {
    // no new stable edge; check for a long hold
    if (stable == LOW && !handled && !enabled && millis() - pressedAt >= HOLD_MS) {
      slideMode = !slideMode;
      digitalWrite(MODE_LED, slideMode);
      handled = true;                          // releasing now won't start the motors
      Serial.println(slideMode ? F("SLIDE mode") : F("TURN mode"));
    }
    return;
  }

  stable = raw;
  if (stable == LOW) {                         // button pressed
    pressedAt = millis();
    handled = false;
    if (enabled) {                             // driving: stop at once
      enabled = false;
      stopAll();
      handled = true;
      Serial.println(F("STOPPED"));
    }
  } else {                                     // button released
    if (!handled && !enabled) {                // short press while stopped: start
      enabled = true;
      Serial.println(slideMode ? F("ON (SLIDE mode)") : F("ON (TURN mode)"));
    }
    handled = true;
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(MODE_LED, OUTPUT);
  digitalWrite(MODE_LED, LOW);                 // start in TURN mode
  int outs[] = { FL_EN, FL_A, FL_B, FR_EN, FR_A, FR_B, RL_EN, RL_A, RL_B, RR_EN, RR_A, RR_B };
  for (int p : outs) pinMode(p, OUTPUT);
  stopAll();

  long sx = 0, sy = 0;
  for (int i = 0; i < 32; i++) { sx += analogRead(JOY_X); sy += analogRead(JOY_Y); delay(5); }
  centerX = sx / 32;
  centerY = sy / 32;
  Serial.println(F("Ready. Press = start/stop. Hold 1 s while stopped = turn/slide mode."));
}

void loop() {
  handleButton();

  if (!enabled) { stopAll(); delay(20); return; }

  int fwd = readAxis(JOY_Y, centerY, INVERT_Y);
  int x   = readAxis(JOY_X, centerX, INVERT_X);
  int side = slideMode ? (INVERT_SIDE ? -x : x) : 0;
  int turn = slideMode ? 0 : (long)x * TURN_LIMIT / 100;

  long w[4] = { (long)fwd + side + turn,    // FL
                (long)fwd - side - turn,    // FR
                (long)fwd - side + turn,    // RL
                (long)fwd + side - turn };  // RR

  // scale all four together so the direction stays right when one goes over 255
  long big = 255;
  for (int i = 0; i < 4; i++) big = max(big, labs(w[i]));
  for (int i = 0; i < 4; i++) {
    int target = (w[i] * 255 / big) * MAX_SPEED / 255;   // all long math: no 16-bit overflow
    now[i] = rampTo(now[i], target);
  }
  driveAll();
  delay(20);
}
