// accel_dot.c
//
// Moves a dot on VGA based on MPU-9250 accelerometer tilt.
// SIMPLE VERSION: raw ax/ay directly control velocity.
// No angle math, no dead zone — just tilt and it moves.

#include <stdlib.h>

// ─────────────────────────────────────────────────────────────────────────────
// VGA / screen constants
// ─────────────────────────────────────────────────────────────────────────────

#define SCREEN_W  320
#define SCREEN_H  240

#define BLACK   0x0000
#define CYAN    0x07FF

short int Buffer1[240][512];

// ─────────────────────────────────────────────────────────────────────────────
// JP1 GPIO registers
// ─────────────────────────────────────────────────────────────────────────────

volatile int *jp1_data = (volatile int *)0xFF200060;
volatile int *jp1_dir  = (volatile int *)0xFF200064;

#define SCL_BIT  0
#define SDA_BIT  1

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 registers
// ─────────────────────────────────────────────────────────────────────────────

#define MPU_ADDR         0x68
#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_CFG2   0x1D   // low pass filter register
#define REG_ACCEL_XOUT_H 0x3B

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
// MPU-9250 write / init
// ─────────────────────────────────────────────────────────────────────────────

void mpu_write_reg(unsigned char reg, unsigned char val) {
    i2c_start();
    i2c_send_byte(MPU_ADDR << 1);
    i2c_send_byte(reg);
    i2c_send_byte(val);
    i2c_stop();
}

void mpu_init() {
    *jp1_dir &= ~((1 << SCL_BIT) | (1 << SDA_BIT));

    // wake chip from sleep
    mpu_write_reg(REG_PWR_MGMT_1, 0x00);

    // enable hardware low pass filter — smooths noise
    // 0x05 = 10Hz bandwidth (stable, minimal lag for tilt)
    mpu_write_reg(REG_ACCEL_CFG2, 0x05);

    volatile int i;
    for (i = 0; i < 1000000; i++);
}

// ─────────────────────────────────────────────────────────────────────────────
// Burst read X and Y (4 bytes, one transaction)
// ─────────────────────────────────────────────────────────────────────────────

void mpu_read_accel(short *ax, short *ay) {
    i2c_start();
    i2c_send_byte(MPU_ADDR << 1);
    i2c_send_byte(REG_ACCEL_XOUT_H);   // start at XH, chip auto-increments
    i2c_start();
    i2c_send_byte((MPU_ADDR << 1) | 1);

    unsigned char xh = i2c_recv_byte(1);  // XH — ACK
    unsigned char xl = i2c_recv_byte(1);  // XL — ACK
    unsigned char yh = i2c_recv_byte(1);  // YH — ACK
    unsigned char yl = i2c_recv_byte(0);  // YL — NACK (done)
    i2c_stop();

    *ax = (short)((xh << 8) | xl);
    *ay = (short)((yh << 8) | yl);
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
    short ax, ay;

    while (1) {
        // 1. read accelerometer (burst, both axes one transaction)
        mpu_read_accel(&ax, &ay);

        // 2. CALIBRATION STEP:
        //    Hold the board flat and note what ax/ay read.
        //    They won't be exactly 0 — subtract that offset here.
        //    For now set both to 0 and adjust once you see behaviour.
        int offset_x = 0;   // <-- if dot drifts left/right when flat, put that value here
        int offset_y = 0;   // <-- if dot drifts up/down when flat, put that value here

        int ix = (int)ax - offset_x;
        int iy = (int)ay - offset_y;

        // 3. raw tilt drives velocity directly
        //    flat board: ax≈0, ay≈0 → no velocity added
        //    tilted:     ax or ay changes → velocity builds up
        //    raise divisor if too twitchy, lower if too sluggish
        vel_x += ix / 32;
        vel_y -= iy / 32;   // negate: forward tilt = positive ay = up on screen

        // 4. friction — dot slows when flat
        vel_x = (vel_x * 15) / 16;
        vel_y = (vel_y * 15) / 16;

        // 5. cap speed
        if (vel_x >  64) vel_x =  64;
        if (vel_x < -64) vel_x = -64;
        if (vel_y >  64) vel_y =  64;
        if (vel_y < -64) vel_y = -64;

        // 6. move
        pos_x += vel_x;
        pos_y += vel_y;

        // 7. bounce off walls
        if (pos_x < 8*16)   { pos_x = 8*16;   vel_x = -vel_x / 2; }
        if (pos_x > 311*16) { pos_x = 311*16;  vel_x = -vel_x / 2; }
        if (pos_y < 8*16)   { pos_y = 8*16;    vel_y = -vel_y / 2; }
        if (pos_y > 231*16) { pos_y = 231*16;  vel_y = -vel_y / 2; }

        // 8. fixed-point to pixels
        int dot_x = pos_x / 16;
        int dot_y = pos_y / 16;

        // 9. draw
        fill_circle(old_x, old_y, 8, BLACK);
        fill_circle(dot_x, dot_y, 8, CYAN);

        old_x = dot_x;
        old_y = dot_y;

        wait_for_vsync();
    }

    return 0;
}