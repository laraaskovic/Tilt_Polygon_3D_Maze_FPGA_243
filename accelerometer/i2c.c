#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240
short int Buffer1[240][512];

// ── GPIO ─────────────────────────────────────────────────────────────────────
// FIX 1: DATA is at base+0, DDR (direction) is at base+4 — was swapped before
typedef struct {
    volatile unsigned int DATA;   // base+0  = Data Register
    volatile unsigned int DDR;    // base+4  = Direction Register
} GPIO_t;

#define GPIO_BASE ((GPIO_t *)0xFF200060)
#define SCL_PIN 0
#define SDA_PIN 1

// ── MPU6050 ──────────────────────────────────────────────────────────────────
#define MPU_ADDR      0x68
#define REG_PWR_MGMT  0x6B
#define REG_ACCEL_X_H 0x3B

// ── Dot movement tuning ───────────────────────────────────────────────────────
// MPU6050 outputs ~16384 counts per g at default ±2g range.
// SENSITIVITY: divide raw value to get pixels/frame. Lower = faster.
// DEADZONE: ignore tiny values (sensor noise when flat).
#define SENSITIVITY 2000
#define DEADZONE    800

// ── Pixel / screen helpers ────────────────────────────────────────────────────
void plot_pixel(int x, int y, short color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    Buffer1[y][x] = color;
}

// 6×6 debug square in top-left corner
void dbg_color(short color) {
    for (int dy = 0; dy < 6; dy++)
        for (int dx = 0; dx < 6; dx++)
            plot_pixel(2 + dx, 2 + dy, color);
}

void wait_for_vsync(void) {
    volatile int *ctrl = (int *)0xFF203020;
    *ctrl = 1;
    while ((*(ctrl + 3) & 0x01) != 0);
}

void clear_screen(void) {
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            Buffer1[y][x] = 0x0000;
}

void draw_dot(int x, int y, short color) {
    int r = 5;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                plot_pixel(x + dx, y + dy, color);
}

// Cyan bar = ax, Magenta bar = ay, centred at screen midpoint
void draw_debug_bar(short ax, short ay) {
    // clear bar area
    for (int y = 20; y < 36; y++)
        for (int x = 0; x < SCREEN_W; x++)
            plot_pixel(x, y, 0x0000);

    int cx = SCREEN_W / 2;

    // ax bar — cyan, top row — divide by 50 so small tilts are visible
    int bar_ax = cx + (int)(ax / 50);
    if (bar_ax < 0)          bar_ax = 0;
    if (bar_ax >= SCREEN_W)  bar_ax = SCREEN_W - 1;
    int x = cx;
    while (x != bar_ax) {
        for (int y = 20; y < 28; y++) plot_pixel(x, y, 0x07FF);
        x += (bar_ax > cx ? 1 : -1);
    }

    // ay bar — magenta, bottom row
    int bar_ay = cx + (int)(ay / 50);
    if (bar_ay < 0)          bar_ay = 0;
    if (bar_ay >= SCREEN_W)  bar_ay = SCREEN_W - 1;
    x = cx;
    while (x != bar_ay) {
        for (int y = 28; y < 36; y++) plot_pixel(x, y, 0xF81F);
        x += (bar_ay > cx ? 1 : -1);
    }
}

// ── I2C (software / bit-bang, open-drain) ────────────────────────────────────
//
// Open-drain rule: NEVER drive a line HIGH — release it and let the
// pull-up resistor pull it up.  Drive low by setting pin as output=0.
// Release high by setting pin as input (high-Z).

void i2c_delay(void) { volatile int i; for (i = 0; i < 200; i++); }

// SDA
void sda_high(GPIO_t *g) {
    g->DDR &= ~(1 << SDA_PIN);          // input → released → pulled high
}
void sda_low(GPIO_t *g) {
    g->DATA &= ~(1 << SDA_PIN);         // ensure output data is 0
    g->DDR  |=  (1 << SDA_PIN);         // output → drives low
}
int sda_read(GPIO_t *g) {
    g->DDR &= ~(1 << SDA_PIN);          // make sure it's an input first
    return (g->DATA >> SDA_PIN) & 1;
}

// SCL
// FIX 2: scl_high releases the line (open-drain), was wrongly driving it high
void scl_high(GPIO_t *g) {
    g->DDR &= ~(1 << SCL_PIN);          // input → released → pulled high
}
void scl_low(GPIO_t *g) {
    g->DATA &= ~(1 << SCL_PIN);         // ensure output data is 0
    g->DDR  |=  (1 << SCL_PIN);         // output → drives low
}

// ── I2C protocol ─────────────────────────────────────────────────────────────
void i2c_start(GPIO_t *g) {
    sda_high(g); i2c_delay();
    scl_high(g); i2c_delay();
    sda_low(g);  i2c_delay();   // SDA falls while SCL high = START
    scl_low(g);  i2c_delay();
}

void i2c_stop(GPIO_t *g) {
    sda_low(g);  i2c_delay();
    scl_high(g); i2c_delay();
    sda_high(g); i2c_delay();   // SDA rises while SCL high = STOP
}

// Returns 0 on ACK, 1 on NACK
int i2c_write_byte(GPIO_t *g, unsigned char byte) {
    for (int i = 7; i >= 0; i--) {
        if ((byte >> i) & 1) sda_high(g);
        else                 sda_low(g);
        i2c_delay();
        scl_high(g); i2c_delay();
        scl_low(g);  i2c_delay();
    }
    // Read ACK bit
    sda_high(g);             // release SDA so slave can pull it low for ACK
    i2c_delay();
    scl_high(g); i2c_delay();
    int nack = sda_read(g);  // 0 = ACK, 1 = NACK
    scl_low(g);  i2c_delay();
    return nack;
}

// ack=1 → send ACK after byte (more bytes coming), ack=0 → send NACK (last byte)
unsigned char i2c_read_byte(GPIO_t *g, int ack) {
    unsigned char byte = 0;
    sda_high(g);             // release SDA for slave to drive
    for (int i = 7; i >= 0; i--) {
        scl_high(g); i2c_delay();
        if (sda_read(g)) byte |= (1 << i);
        scl_low(g);  i2c_delay();
    }
    // Send ACK/NACK
    if (ack) sda_low(g);     // ACK: pull low
    else     sda_high(g);    // NACK: release high
    i2c_delay();
    scl_high(g); i2c_delay();
    scl_low(g);  i2c_delay();
    sda_high(g);
    return byte;
}

// ── MPU6050 helpers ───────────────────────────────────────────────────────────
void mpu_write_reg(GPIO_t *g, unsigned char reg, unsigned char val) {
    i2c_start(g);
    i2c_write_byte(g, MPU_ADDR << 1);   // write address
    i2c_write_byte(g, reg);
    i2c_write_byte(g, val);
    i2c_stop(g);
}

void mpu_read_regs(GPIO_t *g, unsigned char reg, unsigned char *buf, int len) {
    i2c_start(g);
    i2c_write_byte(g, MPU_ADDR << 1);   // write address (set register pointer)
    i2c_write_byte(g, reg);
    i2c_start(g);                        // repeated start
    i2c_write_byte(g, (MPU_ADDR << 1) | 1);  // read address
    for (int i = 0; i < len; i++)
        buf[i] = i2c_read_byte(g, i < len - 1);  // ACK all but last
    i2c_stop(g);
}

void mpu_init(GPIO_t *g) {
    // Make sure both lines start released
    sda_high(g);
    scl_high(g);
    i2c_delay();

    // Ping the MPU to check wiring/address
    i2c_start(g);
    int nack = i2c_write_byte(g, MPU_ADDR << 1);
    i2c_stop(g);

    if (nack) {
        dbg_color(0xF800);  // RED  — not found, check wiring/address
    } else {
        dbg_color(0x07FF);  // CYAN — found, about to wake up
        mpu_write_reg(g, REG_PWR_MGMT, 0x00);   // clear sleep bit, wake up
        // Optional: set DLPF to reduce noise (register 0x1A, value 0x03 = 44Hz)
        mpu_write_reg(g, 0x1A, 0x03);
    }
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(void) {
    // Point pixel controller at our back-buffer and wait for first vsync
    volatile int *pixel_ctrl = (int *)0xFF203020;
    *(pixel_ctrl + 1) = (int)&Buffer1;
    *pixel_ctrl = 1;
    while ((*(pixel_ctrl + 3) & 0x01) != 0);
    *(pixel_ctrl + 1) = (int)&Buffer1;

    GPIO_t *gpio = GPIO_BASE;

    clear_screen();
    mpu_init(gpio);
    wait_for_vsync();

    // If debug square is RED  → sensor not found (check wiring / pull-ups)
    // If debug square is CYAN → sensor found, entering main loop

    int dot_x = SCREEN_W / 2;
    int dot_y = SCREEN_H / 2;

    while (1) {
        // Read 6 bytes: ACCEL_X_H, ACCEL_X_L, ACCEL_Y_H, ACCEL_Y_L, ACCEL_Z_H, ACCEL_Z_L
        unsigned char buf[6];
        mpu_read_regs(gpio, REG_ACCEL_X_H, buf, 6);

        short ax = (short)((buf[0] << 8) | buf[1]);
        short ay = (short)((buf[2] << 8) | buf[3]);
        // az = (short)((buf[4] << 8) | buf[5]);  // not used for movement

        // Show raw sensor values as coloured bars — tilt the board and
        // watch the cyan (ax) and magenta (ay) bars move left/right.
        // If bars don't move at all, there is still an I2C problem.
        draw_debug_bar(ax, ay);

        // Erase old dot position
        draw_dot(dot_x, dot_y, 0x0000);

        // Move dot proportionally to tilt, ignoring noise inside DEADZONE
        if (ax >  DEADZONE) dot_x += (ax / SENSITIVITY) + 1;
        if (ax < -DEADZONE) dot_x -= ((-ax) / SENSITIVITY) + 1;
        // ay positive = tilting board forward = dot moves up (subtract)
        if (ay >  DEADZONE) dot_y -= (ay / SENSITIVITY) + 1;
        if (ay < -DEADZONE) dot_y += ((-ay) / SENSITIVITY) + 1;

        // Clamp dot to screen edges (leave room for radius=5)
        if (dot_x < 5)            dot_x = 5;
        if (dot_x >= SCREEN_W-5)  dot_x = SCREEN_W - 6;
        if (dot_y < 5)            dot_y = 5;
        if (dot_y >= SCREEN_H-5)  dot_y = SCREEN_H - 6;

        // Draw dot in white
        draw_dot(dot_x, dot_y, 0xFFFF);

        // Green square = read completed OK
        dbg_color(0x07E0);

        wait_for_vsync();
    }

    return 0;
}