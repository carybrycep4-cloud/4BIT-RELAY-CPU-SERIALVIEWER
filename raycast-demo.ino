
// ==========================================================
// RELAY-ALU CONTROLLED PLAYER STATE + UNO RAYCAST + SERIAL FRAME (EXTENDED)
// - Relay ALU: authoritative for x,y,angle updates (4-bit each)
// - UNO: raycasts 16x16 map into 64 columns and streams to PC
// - Serial protocol (extended, backward compatible on the viewer side):
// 0xFF 0x00 + 64 bytes (heights 0..47) + health + spriteCount + sprites...
// - Control input: 1 byte bitmask from PC: W=1 S=2 A=4 D=8 F=16
// ==========================================================

#include <Arduino.h>

// -------- BIT OUTPUT PINS (Arduino -> ALU Registers) --------
const int B_pins[4] = {8, 9, 10, 11}; // B0–B3

// -------- 74HC595 PINS (Arduino -> 74HC595) --------
const int SR_DATA = 12;
const int SR_CLOCK = 13;
const int SR_LATCH = 5;

// -------- SUM INPUT PINS (ALU Relays -> Arduino) --------
const int S_pins[4] = {2, 3, 4, 6}; // S0–S3

// -------- TIMING CONTROL --------
const unsigned long settleDelay = 50; // relay settle/bounce time (ms)

// -------- SERIAL / FRAME --------
static const uint8_t SYNC0 = 0xFF;
static const uint8_t SYNC1 = 0x00;
static const uint8_t COLS = 64;
static const uint8_t MAX_H = 48; // heights 0..47

// Control bits from PC
static const uint8_t CTRL_W = 1;
static const uint8_t CTRL_S = 2;
static const uint8_t CTRL_A = 4;
static const uint8_t CTRL_D = 8;
static const uint8_t CTRL_F = 16; // attack

// -------- GAME TIMING --------
const unsigned long moveTickMs = 200; // movement/turn/ALU tick
const unsigned long renderTickMs = 50; // 20 FPS target

// -------- WORLD MAP 16x16 (1=wall, 0=empty) --------
const uint8_t map16[16][16] PROGMEM = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

// -------- PLAYER STATE (4-bit each) --------
uint8_t px = 8; // 0..15 tile X
uint8_t py = 8; // 0..15 tile Y
uint8_t angle = 0; // 0..15 (16 directions)

// 4-bit health (0..15)
uint8_t health4 = 15;

// Latest control byte from PC
volatile uint8_t ctrl = 0;
uint8_t prevCtrl = 0;

// Frame buffer (64 column heights)
uint8_t frame[COLS];

// -------- Optional sprites to send to PC --------
static const uint8_t MAX_SPRITES = 8;
static const uint8_t SPR_ENEMY = 1;
static const uint8_t SPR_HEART = 2;

struct SpriteOut {
  uint8_t type;
  uint8_t sx; // 0..255 screen x
  uint8_t sh; // 0..255 sprite height
  uint8_t flags; // reserved
};

SpriteOut sprites[MAX_SPRITES];
uint8_t spriteCount = 0;

// -------- Simple entities (tile-based) --------
struct Ent {
  uint8_t x, y; // 0..15
  uint8_t alive; // 0/1
};

Ent enemy = { 10, 10, 1 };

static const uint8_t MAX_HEARTS = 3;
Ent hearts[MAX_HEARTS] = {
  { 3, 3, 1 },
  { 12, 4, 1 },
  { 4, 12, 1 }
};

// ==========================================================
// 74HC595 HELPER
// ==========================================================
void write595(uint8_t v) {
  digitalWrite(SR_LATCH, LOW);
  shiftOut(SR_DATA, SR_CLOCK, MSBFIRST, v);
  digitalWrite(SR_LATCH, HIGH);
}

// ==========================================================
// RELAY ALU OPS
// ==========================================================
uint8_t aluAdd4(uint8_t a, uint8_t b) {
  for (int i = 0; i < 4; i++) {
    digitalWrite(B_pins[i], (b >> i) & 0x01);
  }
  write595(a & 0x0F);

  delay(settleDelay);

  uint8_t sum = 0;
  for (int i = 0; i < 4; i++) {
    sum |= (digitalRead(S_pins[i]) << i);
  }
  return sum & 0x0F;
}

// ==========================================================
// MAP ACCESS
// ==========================================================
uint8_t mapAt(uint8_t x, uint8_t y) {
  x &= 0x0F;
  y &= 0x0F;
  return pgm_read_byte(&(map16[y][x]));
}

// ==========================================================
// Direction tables (shared)
// step values are encoded as: +1=0x01, 0=0x00, -1=0x0F
// ==========================================================
static const uint8_t stepX16[16] = {
  0x01,0x01,0x01,0x00,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x00,0x01,0x01,0x01,0x01
};
static const uint8_t stepY16[16] = {
  0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x00,0x00
};

static inline int8_t convStep(uint8_t v) {
  if (v == 0x01) return 1;
  if (v == 0x0F) return -1;
  return 0;
}

// ==========================================================
// MOVEMENT (Relay ALU owns x/y/angle updates)
// ==========================================================
void updatePlayerFromCtrl() {
  if (ctrl & CTRL_A) angle = aluAdd4(angle, 0x0F); // -1
  if (ctrl & CTRL_D) angle = aluAdd4(angle, 0x01); // +1

  int8_t move = 0;
  if (ctrl & CTRL_W) move += 1;
  if (ctrl & CTRL_S) move -= 1;
  if (move == 0) return;

  uint8_t sx = stepX16[angle & 0x0F];
  uint8_t sy = stepY16[angle & 0x0F];

  if (move < 0) {
    if (sx == 0x01) sx = 0x0F; else if (sx == 0x0F) sx = 0x01;
    if (sy == 0x01) sy = 0x0F; else if (sy == 0x0F) sy = 0x01;
  }

  uint8_t nx = aluAdd4(px, sx);
  uint8_t ny = aluAdd4(py, sy);

  if (mapAt(nx, ny) == 0) {
    px = nx & 0x0F;
    py = ny & 0x0F;
  }
}

// ==========================================================
// Simple gameplay: enemy wandering, hearts pickup, melee attack
// ==========================================================
void tryPickupHearts() {
  for (uint8_t i = 0; i < MAX_HEARTS; i++) {
    if (!hearts[i].alive) continue;
    if (hearts[i].x == px && hearts[i].y == py) {
      hearts[i].alive = 0;
      // heal +4 (clamp to 15)
      uint8_t newHp = health4 + 4;
      if (newHp > 15) newHp = 15;
      health4 = newHp;
    }
  }
}

uint8_t tileDistManhattan(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by) {
  int8_t dx = (int8_t)ax - (int8_t)bx;
  int8_t dy = (int8_t)ay - (int8_t)by;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  return (uint8_t)(dx + dy);
}

void enemyWanderTick() {
  if (!enemy.alive) return;

  // Very simple random-ish move: use millis() low bits as pseudo-rand
  uint8_t r = (uint8_t)millis();

  int8_t dx = 0, dy = 0;
  switch (r & 0x03) {
    case 0: dx = 1; break;
    case 1: dx = -1; break;
    case 2: dy = 1; break;
    default: dy = -1; break;
  }

  uint8_t nx = (uint8_t)((int8_t)enemy.x + dx) & 0x0F;
  uint8_t ny = (uint8_t)((int8_t)enemy.y + dy) & 0x0F;

  // avoid walls and avoid stepping onto player tile
  if (mapAt(nx, ny) == 0 && !(nx == px && ny == py)) {
    enemy.x = nx;
    enemy.y = ny;
  }

  // If enemy is adjacent, damage player (slowly)
  if (tileDistManhattan(enemy.x, enemy.y, px, py) == 1) {
    if (health4 > 0) health4--;
  }
}

void meleeAttack() {
  if (!enemy.alive) return;

  // Attack hits if enemy is within 1 tile AND roughly in front of player.
  // "In front" = enemy tile equals player tile + facing step.
  int8_t fx = convStep(stepX16[angle & 0x0F]);
  int8_t fy = convStep(stepY16[angle & 0x0F]);

  uint8_t tx = (uint8_t)((int8_t)px + fx) & 0x0F;
  uint8_t ty = (uint8_t)((int8_t)py + fy) & 0x0F;

  if (enemy.x == tx && enemy.y == ty) {
    enemy.alive = 0;
  }
}

// ==========================================================
// RAYCAST (UNO-only, integer grid stepping)
// ==========================================================
void raycastFrame() {
  for (uint8_t i = 0; i < COLS; i++) {
    int8_t off = (int8_t)((int16_t)i * 9 / (int16_t)(COLS - 1)) - 4; // -4..+4
    uint8_t dir = (angle + (uint8_t)(off & 0x0F)) & 0x0F;

    int8_t rx = convStep(stepX16[dir]);
    int8_t ry = convStep(stepY16[dir]);
    if (rx == 0 && ry == 0) rx = 1;

    uint8_t x = px;
    uint8_t y = py;
    uint8_t dist = 0;
    const uint8_t MAX_DIST = 15;

    while (dist < MAX_DIST) {
      x = (uint8_t)((int8_t)x + rx) & 0x0F;
      y = (uint8_t)((int8_t)y + ry) & 0x0F;
      dist++;
      if (mapAt(x, y) != 0) break;
    }

    uint8_t h;
    if (dist <= 1) h = 47;
    else if (dist == 2) h = 34;
    else if (dist == 3) h = 26;
    else if (dist == 4) h = 20;
    else if (dist == 5) h = 16;
    else if (dist == 6) h = 13;
    else if (dist == 7) h = 11;
    else if (dist == 8) h = 9;
    else if (dist == 9) h = 8;
    else if (dist == 10) h = 7;
    else if (dist == 11) h = 6;
    else if (dist == 12) h = 5;
    else if (dist == 13) h = 4;
    else if (dist == 14) h = 4;
    else h = 3;

    frame[i] = (h < MAX_H) ? h : (MAX_H - 1);
  }
}

// ==========================================================
// Sprite projection helper (very simple)
// Produces screenX and height from tile-space dx/dy
// ==========================================================
bool projectSprite(uint8_t ex, uint8_t ey, uint8_t &outSx, uint8_t &outSh) {
  // Compute delta in signed range [-8..+7] (wrap-aware)
  int8_t dx = (int8_t)((ex - px) & 0x0F);
  int8_t dy = (int8_t)((ey - py) & 0x0F);
  if (dx > 7) dx -= 16;
  if (dy > 7) dy -= 16;

  // crude "distance"
  uint8_t dist = (uint8_t)(abs(dx) + abs(dy));
  if (dist == 0) dist = 1;
  if (dist > 15) return false;

  // crude "angle center test":
  // Use facing vector and side vector to get a left/right offset.
  int8_t fx = convStep(stepX16[angle & 0x0F]);
  int8_t fy = convStep(stepY16[angle & 0x0F]);
  // side vector (rotate 90°)
  int8_t sxv = -fy;
  int8_t syv = fx;

  int8_t forward = dx * fx + dy * fy;
  int8_t side = dx * sxv + dy * syv;

  if (forward <= 0) return false; // behind you

  // Map side to screen x. Clamp to a reasonable FOV.
  // side range about [-8..+8] -> x 0..255
  int16_t sx = 128 + (int16_t)side * 18; // tweak 18 for "FOV feel"
  sx = constrain(sx, 0, 255);

  // Height from distance: near => tall
  // dist 1..15 => sh ~ 240..30
  int16_t sh = 255 - (int16_t)(dist * 14);
  sh = constrain(sh, 20, 255);

  outSx = (uint8_t)sx;
  outSh = (uint8_t)sh;
  return true;
}

void buildSpriteList() {
  spriteCount = 0;

  // Enemy
  if (enemy.alive && spriteCount < MAX_SPRITES) {
    uint8_t sx, sh;
    if (projectSprite(enemy.x, enemy.y, sx, sh)) {
      sprites[spriteCount++] = { SPR_ENEMY, sx, sh, 0 };
    }
  }

  // Hearts
  for (uint8_t i = 0; i < MAX_HEARTS && spriteCount < MAX_SPRITES; i++) {
    if (!hearts[i].alive) continue;
    uint8_t sx, sh;
    if (projectSprite(hearts[i].x, hearts[i].y, sx, sh)) {
      sprites[spriteCount++] = { SPR_HEART, sx, (uint8_t)(sh * 3 / 5), 0 };
    }
  }
}

// ==========================================================
// SERIAL SEND
// ==========================================================
void sendFrame() {
  Serial.write(SYNC0);
  Serial.write(SYNC1);
  Serial.write(frame, COLS);

  // Extended payload
  Serial.write(health4); // 0..15 recommended
  Serial.write(spriteCount); // number of sprites
  for (uint8_t i = 0; i < spriteCount; i++) {
    Serial.write(sprites[i].type);
    Serial.write(sprites[i].sx);
    Serial.write(sprites[i].sh);
    Serial.write(sprites[i].flags);
  }
}

// ==========================================================
// SETUP / LOOP
// ==========================================================
unsigned long lastMove = 0;
unsigned long lastRender = 0;

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) pinMode(B_pins[i], OUTPUT);

  pinMode(SR_DATA, OUTPUT);
  pinMode(SR_CLOCK, OUTPUT);
  pinMode(SR_LATCH, OUTPUT);

  for (int i = 0; i < 4; i++) pinMode(S_pins[i], INPUT);

  for (int i = 0; i < 4; i++) digitalWrite(B_pins[i], LOW);
  write595(0x00);

  for (uint8_t i = 0; i < COLS; i++) frame[i] = 0;
}

void loop() {
  while (Serial.available() > 0) {
    ctrl = (uint8_t)Serial.read();
  }

  unsigned long now = millis();

  if (now - lastMove >= moveTickMs) {
    lastMove = now;

    // Movement/turn via relay ALU
    updatePlayerFromCtrl();

    // Pickups
    tryPickupHearts();

    // Enemy wander (slowly)
    enemyWanderTick();

    // Attack on rising edge of F
    uint8_t fNow = (ctrl & CTRL_F) ? 1 : 0;
    uint8_t fPrev = (prevCtrl & CTRL_F) ? 1 : 0;
    if (fNow && !fPrev) {
      meleeAttack();
    }
    prevCtrl = ctrl;
  }

  if (now - lastRender >= renderTickMs) {
    lastRender = now;
    raycastFrame();
    buildSpriteList();
    sendFrame();
  }
}
