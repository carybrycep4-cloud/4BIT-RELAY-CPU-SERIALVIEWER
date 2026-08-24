import sys
import serial
import pygame

SYNC0 = 0xFF
SYNC1 = 0x00
COLS = 64
MAX_H = 48 # expected 0..47

# Extended payload limits (viewer-side sanity checks)
MAX_SPRITES = 16

# Sprite types (optional)
SPR_ENEMY = 1
SPR_HEART = 2
SPR_FOLIAGE = 3


def open_serial(port, baud):
    # timeout makes reads "non-blocking-ish"; we still use _read_exact to avoid partial-frame display.
    return serial.Serial(port, baudrate=baud, timeout=0.1)


def _read_exact(ser, n):
    """Read exactly n bytes or return None (uses serial timeout)."""
    data = ser.read(n)
    if len(data) != n:
        return None
    return data


def read_frame(ser):
    """
    Returns dict:
      {
        "cols": [64 ints],
        "health": int (0..255),
        "sprites": list of {"type","sx","sh","flags"}
      }
    or None if no full frame available yet.
    """
    # Find sync bytes (streaming parser)
    while True:
        b = ser.read(1)
        if not b:
            return None
        if b[0] != SYNC0:
            continue

        b2 = ser.read(1)
        if not b2:
            return None
        if b2[0] != SYNC1:
            continue

        # Base payload: 64 columns
        data = _read_exact(ser, COLS)
        if data is None:
            return None
        cols = list(data)

        # Extended payload is REQUIRED for your current Uno sketch
        ext = _read_exact(ser, 2) # health + spriteCount
        if ext is None:
            return None

        health = ext[0]
        n = ext[1]

        # sanity clamp / resync aid
        if n > MAX_SPRITES:
            # If desynced, drop buffered junk and wait for a clean sync
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            return None

        sprites = []
        if n:
            blob = _read_exact(ser, n * 4)
            if blob is None:
                return None
            for i in range(n):
                t = blob[i * 4 + 0]
                sx = blob[i * 4 + 1]
                sh = blob[i * 4 + 2]
                fl = blob[i * 4 + 3]
                sprites.append({"type": t, "sx": sx, "sh": sh, "flags": fl})

        return {"cols": cols, "health": health, "sprites": sprites}


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def draw_health_bar(screen, w, h, health, font):
    # Supports 0..15 (intended), but also handles other ranges gracefully.
    if health <= 15:
        frac = health / 15.0
        label = f"HP {health:02d}/15"
    elif health <= 100:
        frac = health / 100.0
        label = f"HP {health:03d}/100"
    else:
        frac = health / 255.0
        label = f"HP {health:03d}/255"

    frac = clamp(frac, 0.0, 1.0)

    bar_w = 170
    bar_h = 14
    pad = 12
    x = w - pad - bar_w
    y = pad

    # frame
    pygame.draw.rect(screen, (10, 10, 12), (x - 2, y - 2, bar_w + 4, bar_h + 4))
    pygame.draw.rect(screen, (70, 70, 80), (x, y, bar_w, bar_h))

    # fill
    fill_w = int(bar_w * frac)
    pygame.draw.rect(screen, (180, 40, 40), (x, y, fill_w, bar_h))

    # text
    txt = font.render(label, True, (235, 235, 235))
    screen.blit(txt, (x, y + bar_h + 6))


def draw_weapon(screen, w, h, swing_t):
    """
    swing_t: 0..1 where 0 means idle, 1 means end of swing.
    Simple knife block + highlight; no image files needed.
    """
    base_x = int(w * 0.62)
    base_y = int(h * 0.78)

    t = clamp(swing_t, 0.0, 1.0)
    punch = (1 - (2 * t - 1) ** 2) # 0..1..0
    dx = int(22 * punch)
    dy = int(16 * punch)

    # handle
    handle = pygame.Rect(base_x + dx, base_y + dy, 26, 56)
    pygame.draw.rect(screen, (70, 55, 45), handle)
    pygame.draw.rect(screen, (110, 90, 70), handle, 2)

    # blade
    blade = pygame.Rect(base_x + dx + 16, base_y + dy - 42, 12, 90)
    pygame.draw.rect(screen, (170, 170, 175), blade)
    pygame.draw.rect(screen, (220, 220, 230), blade, 2)

    # blade highlight
    pygame.draw.line(
        screen,
        (245, 245, 245),
        (blade.x + 3, blade.y + 6),
        (blade.x + 3, blade.y + blade.height - 6),
        1,
    )


def draw_sprites(screen, w, h, sprites):
    """Draw simple billboard rectangles."""
    for sp in sprites:
        t = sp["type"]
        sx = sp["sx"]
        sh = sp["sh"]

        x = int((sx / 255.0) * w)
        height = int((sh / 255.0) * (h * 0.95))
        height = clamp(height, 4, int(h * 0.95))

        width = max(6, height // 4)
        y = (h - height) // 2

        if t == SPR_ENEMY:
            color = (90, 190, 120)
            outline = (30, 60, 40)
        elif t == SPR_HEART:
            color = (200, 60, 60)
            outline = (80, 20, 20)
        elif t == SPR_FOLIAGE:
            color = (60, 140, 70)
            outline = (20, 50, 25)
        else:
            color = (200, 200, 200)
            outline = (60, 60, 60)

        rect = pygame.Rect(x - width // 2, y, width, height)
        pygame.draw.rect(screen, color, rect)
        pygame.draw.rect(screen, outline, rect, 2)


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 viewer3.py /dev/ttyACM0 115200")
        print(" or: python viewer3.py COM3 115200")
        sys.exit(1)

    port = sys.argv[1]
    baud = int(sys.argv[2])

    ser = open_serial(port, baud)

    pygame.init()
    w, h = 960, 540
    screen = pygame.display.set_mode((w, h))
    pygame.display.set_caption("Relay Ray Viewer (Serial)")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont(None, 20)

    running = True

    last_cols = [0] * COLS
    last_health = 15
    last_sprites = []

    # Attack key edge detect + swing animation
    prev_attack_down = False
    swing_timer = 0.0
    SWING_DUR = 0.18 # seconds

    while running:
        # Read at most one frame per loop; the loop runs fast enough at 60fps.
        frame = read_frame(ser)
        if frame is not None:
            last_cols = frame["cols"]
            last_health = frame.get("health", last_health)
            last_sprites = frame.get("sprites", [])

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

        keys = pygame.key.get_pressed()

        # Controls to Arduino (bitmask)
        ctrl = 0
        if keys[pygame.K_w] or keys[pygame.K_UP]:
            ctrl |= 1
        if keys[pygame.K_s] or keys[pygame.K_DOWN]:
            ctrl |= 2
        if keys[pygame.K_a] or keys[pygame.K_LEFT]:
            ctrl |= 4
        if keys[pygame.K_d] or keys[pygame.K_RIGHT]:
            ctrl |= 8

        attack_down = keys[pygame.K_f] or keys[pygame.K_SPACE]
        if attack_down:
            ctrl |= 16

        # Start swing animation on rising edge
        if attack_down and not prev_attack_down:
            swing_timer = SWING_DUR
        prev_attack_down = attack_down

        try:
            ser.write(bytes([ctrl]))
        except Exception:
            pass

        # Update swing timer
        dt = clock.get_time() / 1000.0
        if swing_timer > 0.0:
            swing_timer = max(0.0, swing_timer - dt)

        swing_t = 0.0
        if SWING_DUR > 0.0 and swing_timer > 0.0:
            swing_t = 1.0 - (swing_timer / SWING_DUR)

        # Draw background
        screen.fill((20, 20, 28))
        pygame.draw.rect(screen, (28, 28, 40), (0, 0, w, h // 2)) # ceiling
        pygame.draw.rect(screen, (12, 12, 16), (0, h // 2, w, h // 2)) # floor

        # Walls
        col_w = max(1, w // COLS)
        for i, val in enumerate(last_cols):
            v = min(val, MAX_H - 1)
            hh = int((v / (MAX_H - 1)) * (h * 0.95))
            x = i * col_w
            y = (h - hh) // 2

            shade = max(40, min(255, 255 - (MAX_H - 1 - v) * 4))
            color = (shade, shade, shade)
            pygame.draw.rect(screen, color, (x, y, col_w - 1, hh))

        # Sprites on top of walls
        if last_sprites:
            draw_sprites(screen, w, h, last_sprites)

        # UI overlays
        draw_health_bar(screen, w, h, last_health, font)
        draw_weapon(screen, w, h, swing_t)

        txt = font.render(f"{port} @ {baud} | ctrl: {ctrl:02x}", True, (220, 220, 220))
        screen.blit(txt, (10, 10))

        pygame.display.flip()
        clock.tick(60)

    try:
        ser.close()
    except Exception:
        pass
    pygame.quit()


if __name__ == "__main__":
    main()

