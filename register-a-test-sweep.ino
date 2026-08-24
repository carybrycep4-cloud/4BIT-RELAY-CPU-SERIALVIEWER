
// ==========================================================
// ARDUINO ALU A-REGISTER RANGE CHECKER (0 -> F Test)
// ==========================================================

// -------- 74HC595 PINS (Driving 4-bit A Register: Q0–Q3) --------
const int DATA_PIN = 12; // SER / DS (Data Input)
const int LATCH_PIN = 5; // RCK / ST_CP (Latch Clock)
const int CLOCK_PIN = 13; // SCK / SH_CP (Shift Clock)

// -------- BIT OUTPUT PINS (Arduino Direct -> ALU B Register) --------
const int B_pins[4] = {8, 9, 10, 11}; // B0–B3 (Held at 0)

// -------- SUM INPUT PINS (ALU Relays -> Arduino) --------
const int S_pins[4] = {2, 3, 4, 6}; // S0–S3 (4-bit sum bus)

// -------- 7-SEGMENT OUTPUT PINS (Common Cathode) --------
const int segPins[7] = {A0, A1, A2, A3, A4, A5, 7};

// -------- STANDARD HEX MAP (Common Cathode) --------
const byte hexMap[16][7] = {
  {1,1,1,1,1,1,0}, // 0
  {0,1,1,0,0,0,0}, // 1
  {1,1,0,1,1,0,1}, // 2
  {1,1,1,1,0,0,1}, // 3
  {0,1,1,0,0,1,1}, // 4
  {1,0,1,1,0,1,1}, // 5
  {1,0,1,1,1,1,1}, // 6
  {1,1,1,0,0,0,0}, // 7
  {1,1,1,1,1,1,1}, // 8
  {1,1,1,1,0,1,1}, // 9
  {1,1,1,0,1,1,1}, // A
  {0,0,1,1,1,1,1}, // b
  {1,0,0,1,1,1,0}, // C
  {0,1,1,1,1,0,1}, // d
  {1,0,0,1,1,1,1}, // E
  {1,0,0,0,1,1,1} // F
};

// -------- TIMING CONTROL --------
const unsigned long stepDelay = 1000; // 1 second per step
const unsigned long settleDelay = 50; // 50ms relay bounce settling time

// -------- TEST STATE --------
unsigned long lastStepTime = 0;
uint8_t test_A_value = 0; // Steps from 0 to 15

// Helper function to shift out 4 bits to A Register
void updateARegister(uint8_t value) {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, value & 0x0F);
  digitalWrite(LATCH_PIN, HIGH);
}

// ==========================================================
// SETUP
// ==========================================================
void setup() {

  // Configure Shift Register Control Pins
  pinMode(DATA_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);

  // Configure ALU B output register pins
  for (int i = 0; i < 4; i++) {
    pinMode(B_pins[i], OUTPUT);
    digitalWrite(B_pins[i], LOW); // Lock B Register permanently at 0
  }

  // Configure ALU input bus
  for (int i = 0; i < 4; i++) {
    pinMode(S_pins[i], INPUT);
  }

  // Configure 7-segment display outputs
  for (int i = 0; i < 7; i++) {
    pinMode(segPins[i], OUTPUT);
  }
}

// ==========================================================
// LOOP
// ==========================================================
void loop() {

  unsigned long currentTime = millis();

  if (currentTime - lastStepTime >= stepDelay) {
    lastStepTime = currentTime;

    // --------------------------------------------------
    // 1. Force B=0 and output current test value to A
    // --------------------------------------------------
    for (int i = 0; i < 4; i++) {
      digitalWrite(B_pins[i], LOW); // Always 0
    }

    updateARegister(test_A_value);

    // --------------------------------------------------
    // 2. Relay Settle Time (Wait for contacts to settle)
    // --------------------------------------------------
    delay(settleDelay);

    // --------------------------------------------------
    // 3. Read REAL ALU Sum Bus from Relay Contacts
    // --------------------------------------------------
    uint8_t sum = 0;
    for (int i = 0; i < 4; i++) {
      sum |= (digitalRead(S_pins[i]) << i);
    }

    // --------------------------------------------------
    // 4. Update 7-Segment Display with Sum Readout
    // --------------------------------------------------
    for (int i = 0; i < 7; i++) {
      digitalWrite(segPins[i], hexMap[sum & 0x0F][i]);
    }

    // --------------------------------------------------
    // 5. Step A from 0 -> 15 then roll over back to 0
    // --------------------------------------------------
    test_A_value = (test_A_value + 1) % 16;
  }
}

