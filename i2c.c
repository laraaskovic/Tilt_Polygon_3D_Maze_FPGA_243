// accel_dot.c
//
// Moves a dot on VGA based on MPU-9250 accelerometer tilt.
// Tilt = acceleration, dot builds up speed and bounces off walls.
//
// FIXES APPLIED:
//   1. Enabled MPU-9250's built-in low pass filter (register 0x1D)
//      — cuts out high frequency noise so readings are stable
//   2. Read all 6 bytes (X, Y, Z) in one burst — need Z to compute tilt
//   3. Use proper tilt math (pitch/roll from atan2) instead of raw ax/ay
//      — raw ax/ay mix in gravity incorrectly and cause drift patterns
//      — pitch/roll give you the actual angle of tilt regardless of orientation

#include <stdlib.h>

// ─────────────────────────────────────────────────────────────────────────────
// Integer square root (no floating point, no math.h needed)
// ─────────────────────────────────────────────────────────────────────────────

// We need sqrt to compute tilt angles without float.
// This is a simple Newton's method integer square root.
static int isqrt(int n) {
    if (n <= 0) return 0;
    int x = n;
    int y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return x;
}

// ─────────────────────────────────────────────────────────────────────────────
// Integer atan2 approximation — returns value scaled to ±512
// (replaces floating point atan2 which we can't use easily on bare metal)
// Input: y, x as raw accelerometer counts
// Output: roughly proportional to the angle, range ±512
// ─────────────────────────────────────────────────────────────────────────────

static int iatan2_scaled(int y, int x) {
    // avoid divide by zero
    if (x == 0 && y == 0) return 0;
    // scale down to avoid overflow (values up to 16384)
    // result is angle * 512 / (pi/2) approximately
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    int angle;
    if (ax >= ay)
        angle = (512 * ay) / (ax + 1);   // 0..512 range
    else
        angle = 512 - (512 * ax) / (ay + 1); // 0..512 range
    if (x < 0) angle = 1024 - angle;
    if (y < 0) angle = -angle;
    return angle;  // roughly ±1024 = ±180 degrees
}

// ─────────────────────────────────────────────────────────────────────────────
// VGA / screen constants
// ─────────────────────────────────────────────────────────────────────────────

#define SCREEN_W  320
#define SCREEN_H  240

#define BLACK   0x0000
#define CYAN    0x07FF

// pixel buffer — 512 wide because DE1-SoC needs power-of-2 row stride
short int Buffer1[240][512];

// ─────────────────────────────────────────────────────────────────────────────
// JP1 GPIO registers (JP1_BASE = 0xFF200060)
// ─────────────────────────────────────────────────────────────────────────────

volatile int *jp1_data = (volatile int *)0xFF200060;
volatile int *jp1_dir  = (volatile int *)0xFF200064;

#define SCL_BIT  0
#define SDA_BIT  1

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 registers
// ─────────────────────────────────────────────────────────────────────────────

#define MPU_ADDR         0x68
#define REG_PWR_MGMT_1   0x6B   // write 0x00 to wake chip
#define REG_ACCEL_CFG2   0x1D   // accelerometer low pass filter config
#define REG_ACCEL_XOUT_H 0x3B   // burst read starts here: XH XL YH YL ZH ZL

// ─────────────────────────────────────────────────────────────────────────────
// I2C bit-bang
// ─────────────────────────────────────────────────────────────────────────────

void i2c_delay() { volatile int i; for (i = 0; i < 100; i++); }

void scl_high() { *jp1_dir &= ~(1 << SCL_BIT); i2c_delay(); }
void scl_low()  { *jp1_data &= ~(1 << SCL_BIT); *jp1_dir |= (1 << SCL_BIT); i2c_delay(); }
void sda_high() { *jp1_dir &= ~(1 << SDA_BIT); i2c_delay(); }
void sda_low()  { *jp1_data &= ~(1 << SDA_BIT); *jp1_dir |= (1 << SDA_BIT); i2c_delay(); }

int sda_read() {
    *jp1_dir &= ~(1 << SDA_BIT);
    i2c_delay();
    return (*jp1_data >> SDA_BIT) & 1;
}

void i2c_start() { sda_high(); scl_high(); sda_low(); scl_low(); }
void i2c_stop()  { sda_low(); scl_high(); sda_high(); }

int i2c_send_byte(unsigned char byte) {
    int i;
    for (i = 7; i >= 0; i--) {
        if ((byte >> i) & 1) sda_high(); else sda_low();
        scl_high(); scl_low();
    }
    sda_high(); scl_high();
    int nack = sda_read();
    scl_low();
    return nack;
}

unsigned char i2c_recv_byte(int ack) {
    unsigned char byte = 0;
    int i;
    sda_high();
    for (i = 7; i >= 0; i--) {
        scl_high();
        if (sda_read()) byte |= (1 << i);
        scl_low();
    }
    if (ack) sda_low(); else sda_high();
    scl_high(); scl_low(); sda_high();
    return byte;
}

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 register write (used for init)
// ─────────────────────────────────────────────────────────────────────────────

void mpu_write_reg(unsigned char reg, unsigned char val) {
    i2c_start();
    i2c_send_byte(MPU_ADDR << 1);
    i2c_send_byte(reg);
    i2c_send_byte(val);
    i2c_stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 init
// ─────────────────────────────────────────────────────────────────────────────

void mpu_init() {
    // release both I2C pins — pull-ups hold them high
    *jp1_dir &= ~((1 << SCL_BIT) | (1 << SDA_BIT));

    // wake chip from sleep
    mpu_write_reg(REG_PWR_MGMT_1, 0x00);

    // enable built-in low pass filter on accelerometer
    // register 0x1D = ACCEL_CONFIG_2
    // value 0x05 = enable LPF, bandwidth=10Hz — smooths out noise
    // this stops the "moving in a pattern" behaviour from raw noise
    // 0x04 = 20Hz (less smooth), 0x06 = 5Hz (very smooth, more lag)
    mpu_write_reg(REG_ACCEL_CFG2, 0x05);

    // let chip stabilize
    volatile int i;
    for (i = 0; i < 1000000; i++);
}

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 burst read — reads X, Y, Z all in ONE transaction (6 bytes)
//
// We need Z now because the tilt angle formula requires it:
//   pitch = atan2(ax, sqrt(ay² + az²))   ← tilt left/right
//   roll  = atan2(ay, sqrt(ax² + az²))   ← tilt forward/back
//
// Without az, using raw ax/ay directly mixes gravity in wrong
// and gives the "moving in a pattern" problem you saw.
// ─────────────────────────────────────────────────────────────────────────────

void mpu_read_accel(short *ax, short *ay, short *az) {
    // set register pointer to ACCEL_XOUT_H
    i2c_start();
    i2c_send_byte(MPU_ADDR << 1);
    i2c_send_byte(REG_ACCEL_XOUT_H);

    // repeated start, switch to read, get all 6 bytes
    i2c_start();
    i2c_send_byte((MPU_ADDR << 1) | 1);

    unsigned char xh = i2c_recv_byte(1);  // XH — ACK
    unsigned char xl = i2c_recv_byte(1);  // XL — ACK
    unsigned char yh = i2c_recv_byte(1);  // YH — ACK
    unsigned char yl = i2c_recv_byte(1);  // YL — ACK
    unsigned char zh = i2c_recv_byte(1);  // ZH — ACK
    unsigned char zl = i2c_recv_byte(0);  // ZL — NACK (last byte)
    i2c_stop();

    *ax = (short)((xh << 8) | xl);
    *ay = (short)((yh << 8) | yl);
    *az = (short)((zh << 8) | zl);
}

// ─────────────────────────────────────────────────────────────────────────────
// VGA drawing
// ─────────────────────────────────────────────────────────────────────────────

void plot_pixel(int x, int y, short color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    Buffer1[y][x] = color;
}

void fill_circle(int cx, int cy, int r, short color) {
    int dx, dy;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                plot_pixel(cx+dx, cy+dy, color);
}

void clear_screen() {
    int x, y;
    for (y = 0; y < SCREEN_H; y++)
        for (x = 0; x < SCREEN_W; x++)
            Buffer1[y][x] = BLACK;
}

void wait_for_vsync() {
    volatile int *ctrl = (volatile int *)0xFF203020;
    *ctrl = 1;
    while ((*(ctrl + 3) & 0x01) != 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(void) {
    volatile int *pixel_ctrl = (volatile int *)0xFF203020;
    *(pixel_ctrl + 1) = (int)Buffer1;
    *pixel_ctrl = 1;
    while ((*(pixel_ctrl + 3) & 0x01) != 0);
    *(pixel_ctrl + 1) = (int)Buffer1;

    clear_screen();
    mpu_init();

    // fixed-point position (*16 for sub-pixel precision)
    int pos_x = 160 * 16;
    int pos_y = 120 * 16;

    int vel_x = 0;
    int vel_y = 0;

    int old_x = 160, old_y = 120;
    short ax, ay, az;

    while (1) {
        // ── 1. read all 3 axes in one burst ──────────────────────────────────
        mpu_read_accel(&ax, &ay, &az);

        // ── 2. compute tilt angles using pitch/roll formula ───────────────────
        //
        // pitch = atan2(ax,  sqrt(ay² + az²))  → tilt around Y axis (left/right)
        // roll  = atan2(ay,  sqrt(ax² + az²))  → tilt around X axis (fwd/back)
        //
        // iatan2_scaled returns ±1024 for ±180 degrees
        // so a gentle 15° tilt ≈ ±85 units out of 1024
        //
        // We divide by 8 to convert to velocity units — tune this:
        //   lower = more sensitive, higher = needs bigger tilt

        int denom_pitch = isqrt((int)ay*(int)ay + (int)az*(int)az);
        int denom_roll  = isqrt((int)ax*(int)ax + (int)az*(int)az);

        int pitch = iatan2_scaled((int)ax,  denom_pitch); // left/right tilt
        int roll  = iatan2_scaled((int)ay,  denom_roll);  // forward/back tilt

        // ── 3. tilt angle drives velocity ─────────────────────────────────────
        // pitch positive = tilted right → dot moves right (vel_x increases)
        // roll  positive = tilted forward → dot moves down (vel_y increases)
        // divide by 8 to scale angle to reasonable velocity — tune this
        vel_x += pitch / 8;
        vel_y += roll  / 8;

        // ── 4. friction — slows dot when board is flat ────────────────────────
        vel_x = (vel_x * 15) / 16;
        vel_y = (vel_y * 15) / 16;

        // ── 5. cap max speed ──────────────────────────────────────────────────
        if (vel_x >  96) vel_x =  96;
        if (vel_x < -96) vel_x = -96;
        if (vel_y >  96) vel_y =  96;
        if (vel_y < -96) vel_y = -96;

        // ── 6. update position ────────────────────────────────────────────────
        pos_x += vel_x;
        pos_y += vel_y;

        // ── 7. bounce off walls ───────────────────────────────────────────────
        if (pos_x < 8*16)   { pos_x = 8*16;   vel_x = -vel_x / 2; }
        if (pos_x > 311*16) { pos_x = 311*16;  vel_x = -vel_x / 2; }
        if (pos_y < 8*16)   { pos_y = 8*16;    vel_y = -vel_y / 2; }
        if (pos_y > 231*16) { pos_y = 231*16;  vel_y = -vel_y / 2; }

        // ── 8. fixed-point to pixels ──────────────────────────────────────────
        int dot_x = pos_x / 16;
        int dot_y = pos_y / 16;

        // ── 9. draw ───────────────────────────────────────────────────────────
        fill_circle(old_x, old_y, 8, BLACK);
        fill_circle(dot_x, dot_y, 8, CYAN);

        old_x = dot_x;
        old_y = dot_y;

        wait_for_vsync();
    }

    return 0;
}