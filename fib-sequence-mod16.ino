
// ==========================================================
// ARDUINO ALU FIBONACCI SEQUENCER (Updated: A via 74HC595)
// Uses D12=DATA, D13=CLOCK, D5=LATCH to create 8 outputs.
// Q0..Q3 of 74HC595 drive A0..A3 (4-bit A register).
// B register remains direct on D8..D11.
// ==========================================================

// -------- BIT OUTPUT PINS (Arduino -> ALU Registers) --------
const int B_pins[4] = {8, 9, 10, 11}; // B0–B3 (4-bit bus)

// -------- 74HC595 PINS (Arduino -> 74HC595) --------
// Wiring:
// 74HC595 pin 14 (DS/SER) -> D12
// 74HC595 pin 11 (SH_CP) -> D13
// 74HC595 pin 12 (ST_CP) -> D5
const int SR_DATA = 12;
const int SR_CLOCK = 13;
const int SR_LATCH = 5;

// -------- SUM INPUT PINS (ALU Relays -> Arduino) --------
const int S_pins[4] = {2, 3, 4, 6}; // S0–S3 (4-bit sum bus)

// -------- 7-SEGMENT OUTPUT PINS (Common Cathode) --------
// Pins: a, b, c, d, e, f, g
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
const unsigned long stepDelay = 1200; // 1.2s per step
const unsigned long settleDelay = 50; // 50ms relay bounce settling time

// -------- FIBONACCI STATE --------
unsigned long lastStepTime = 0;
uint8_t A_value = 0; // 0..15 goes out via 74HC595 (Q0..Q3)
uint8_t B_value = 1; // 0..15 goes out via B_pins

// ==========================================================
// 74HC595 HELPER
// ==========================================================
// Writes 8 bits to the 74HC595.
// With LSBFIRST: bit0 -> Q0, bit1 -> Q1, ... bit7 -> Q7
void write595(uint8_t v) {
  digitalWrite(SR_LATCH, LOW);
  shiftOut(SR_DATA, SR_CLOCK, LSBFIRST, v);
  digitalWrite(SR_LATCH, HIGH);
}

// ==========================================================
// SETUP
// ==========================================================
void setup() {

  // Configure B register outputs (direct GPIO)
  for (int i = 0; i < 4; i++) {
    pinMode(B_pins[i], OUTPUT);
  }

  // Configure 74HC595 control pins
  pinMode(SR_DATA, OUTPUT);
  pinMode(SR_CLOCK, OUTPUT);
  pinMode(SR_LATCH, OUTPUT);

  // Configure ALU input bus (sum)
  for (int i = 0; i < 4; i++) {
    pinMode(S_pins[i], INPUT);
  }

  // Configure 7-segment display outputs
  for (int i = 0; i < 7; i++) {
    pinMode(segPins[i], OUTPUT);
  }

  // Initialize outputs
  for (int i = 0; i < 4; i++) {
    digitalWrite(B_pins[i], LOW);
  }
  write595(0x00);
}

// ==========================================================
// LOOP
// ==========================================================
void loop() {

  unsigned long currentTime = millis();

  if (currentTime - lastStepTime >= stepDelay) {
    lastStepTime = currentTime;

    // --------------------------------------------------
    // 1. Drive ALU Inputs (A and B Registers)
    // --------------------------------------------------

    // B register via direct pins D8..D11
    for (int i = 0; i < 4; i++) {
      digitalWrite(B_pins[i], (B_value >> i) & 0x01);
    }

    // A register via 74HC595: Q0..Q3 = A0..A3
    write595(A_value & 0x0F);

    // --------------------------------------------------
    // 2. Relay Settle Time
    // --------------------------------------------------
    delay(settleDelay);

    // --------------------------------------------------
    // 3. Read REAL ALU Sum Bus from Relay Contacts
    // --------------------------------------------------
    uint8_t sum = 0;
    for (int i = 0; i < 4; i++) {
      sum |= (digitalRead(S_pins[i]) << i);
    }
    sum &= 0x0F;

    // --------------------------------------------------
    // 4. Update 7-Segment Display
    // --------------------------------------------------
    for (int i = 0; i < 7; i++) {
      digitalWrite(segPins[i], hexMap[sum][i]);
    }

    // --------------------------------------------------
    // 5. Fibonacci Update (mod 16)
    // This will cycle through 4-bit Fibonacci behavior.
    // --------------------------------------------------
    B_value = A_value & 0x0F;
    A_value = sum & 0x0F;
  }
}
