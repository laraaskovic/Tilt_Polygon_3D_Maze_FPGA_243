// accel_dot.c
//
// Moves a dot on VGA based on MPU-9250 accelerometer tilt.
// Tilt = acceleration, dot builds up speed and bounces off walls.
//
// KEY FIX: accelerometer X and Y are now read in one burst transaction
// instead of 4 separate reads. This ensures XH/XL/YH/YL all come from
// the same sample — previously the chip could update between reads and
// give you corrupted values.

#include <stdlib.h>

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

#define SCL_BIT  0   // GPIO bit 0 = SCL wire
#define SDA_BIT  1   // GPIO bit 1 = SDA wire

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 I2C address and registers
// ─────────────────────────────────────────────────────────────────────────────

// AD0 tied to GND → address 0x68
#define MPU_ADDR         0x68

#define REG_PWR_MGMT_1   0x6B   // write 0x00 to wake chip from sleep
#define REG_ACCEL_XOUT_H 0x3B   // burst read starts here: XH, XL, YH, YL
#define REG_ACCEL_XOUT_L 0x3C
#define REG_ACCEL_YOUT_H 0x3D
#define REG_ACCEL_YOUT_L 0x3E

// ─────────────────────────────────────────────────────────────────────────────
// I2C bit-bang — pin wiggling
// ─────────────────────────────────────────────────────────────────────────────

// Delay between pin changes — increase to 500 if getting garbage reads
void i2c_delay() { volatile int i; for (i = 0; i < 100; i++); }

// I2C is open-drain: never drive HIGH, just release and let pull-up do it
void scl_high() { *jp1_dir &= ~(1 << SCL_BIT); i2c_delay(); }
void scl_low()  { *jp1_data &= ~(1 << SCL_BIT); *jp1_dir |= (1 << SCL_BIT); i2c_delay(); }
void sda_high() { *jp1_dir &= ~(1 << SDA_BIT); i2c_delay(); }
void sda_low()  { *jp1_data &= ~(1 << SDA_BIT); *jp1_dir |= (1 << SDA_BIT); i2c_delay(); }

int sda_read() {
    *jp1_dir &= ~(1 << SDA_BIT);  // release so slave can drive it
    i2c_delay();
    return (*jp1_data >> SDA_BIT) & 1;
}

// START: SDA falls while SCL is high
void i2c_start() { sda_high(); scl_high(); sda_low(); scl_low(); }

// STOP: SDA rises while SCL is high
void i2c_stop()  { sda_low(); scl_high(); sda_high(); }

// Send 8 bits MSB first, return 0=ACK (good) 1=NACK (bad)
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

// Read 8 bits. ack=1 means "send ACK, keep going". ack=0 means "NACK, last byte"
unsigned char i2c_recv_byte(int ack) {
    unsigned char byte = 0;
    int i;
    sda_high();  // release SDA so slave can drive it
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
// MPU-9250 single register write/read (used for init only)
// ─────────────────────────────────────────────────────────────────────────────

void mpu_write_reg(unsigned char reg, unsigned char val) {
    i2c_start();
    i2c_send_byte(MPU_ADDR << 1);  // address + WRITE
    i2c_send_byte(reg);
    i2c_send_byte(val);
    i2c_stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 init
// ─────────────────────────────────────────────────────────────────────────────

void mpu_init() {
    // release both pins — pull-ups will hold them high
    *jp1_dir &= ~((1 << SCL_BIT) | (1 << SDA_BIT));

    // chip boots in sleep mode — write 0 to PWR_MGMT_1 to wake it
    mpu_write_reg(REG_PWR_MGMT_1, 0x00);

    // let chip stabilize after waking
    volatile int i;
    for (i = 0; i < 1000000; i++);
}

// ─────────────────────────────────────────────────────────────────────────────
// MPU-9250 burst read — reads XH, XL, YH, YL in ONE transaction
//
// WHY THIS MATTERS:
//   The chip updates all accel registers at the same time (one snapshot).
//   If you read each byte in a separate I2C transaction, the chip can
//   update between reads — XH comes from sample N, XL from sample N+1.
//   That gives you a garbage 16-bit value.
//
//   Burst read sets the register pointer once at ACCEL_XOUT_H, then
//   reads all 4 bytes without releasing the bus. The chip auto-increments
//   its pointer after each byte, so you get XH→XL→YH→YL all from the
//   same snapshot.
//
//   ACK after each byte except the last (NACK tells chip we're done).
// ─────────────────────────────────────────────────────────────────────────────

void mpu_read_accel(short *ax, short *ay) {
    // phase 1: write mode — tell chip which register to start from
    i2c_start();
    i2c_send_byte(MPU_ADDR << 1);       // address + WRITE (bit0=0)
    i2c_send_byte(REG_ACCEL_XOUT_H);    // start at XH — chip will auto-increment

    // phase 2: repeated START, switch to read mode
    i2c_start();
    i2c_send_byte((MPU_ADDR << 1) | 1); // address + READ (bit0=1)

    // read all 4 bytes in one go — ACK each except the last
    unsigned char xh = i2c_recv_byte(1);  // ACCEL_XOUT_H — ACK, more coming
    unsigned char xl = i2c_recv_byte(1);  // ACCEL_XOUT_L — ACK, more coming
    unsigned char yh = i2c_recv_byte(1);  // ACCEL_YOUT_H — ACK, more coming
    unsigned char yl = i2c_recv_byte(0);  // ACCEL_YOUT_L — NACK, we're done
    i2c_stop();

    // combine high and low bytes into signed 16-bit values
    // flat on table: ax≈0, ay≈0
    // tilted 90°:    ax or ay ≈ ±16384
    // gentle tilt:   roughly ±200 to ±4000 depending on angle
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

    // position stored in fixed-point (*16) for sub-pixel precision
    // without this, small velocities get truncated to 0 by integer division
    int pos_x = 160 * 16;
    int pos_y = 120 * 16;

    // velocity accumulates each frame based on tilt
    int vel_x = 0;
    int vel_y = 0;

    int old_x = 160, old_y = 120;
    short ax, ay;

    while (1) {
        // ── 1. read accelerometer (burst read, all 4 bytes one transaction) ──
        mpu_read_accel(&ax, &ay);

        // ── 2. tilt → acceleration ───────────────────────────────────────────
        // ax/ay are ±16384 at full 1g tilt
        // dividing by 32 maps a gentle tilt (~1000 counts) to ~31 units/frame
        // raise divisor (64, 128) if too twitchy
        // lower divisor (16) if too sluggish
        vel_x += (int)ax / 32;
        vel_y -= (int)ay / 32;  // negate: forward tilt = positive ay = up on screen

        // ── 3. friction ───────────────────────────────────────────────────────
        // multiplies velocity by 15/16 = 0.9375 each frame
        // dot gradually slows when board is flat
        // change 15→14 for more damping, 15→16 for no friction (ice)
        vel_x = (vel_x * 15) / 16;
        vel_y = (vel_y * 15) / 16;

        // ── 4. cap max speed ──────────────────────────────────────────────────
        // 96 fixed-point units = 6 pixels/frame max
        // raise if you want faster movement
        if (vel_x >  96) vel_x =  96;
        if (vel_x < -96) vel_x = -96;
        if (vel_y >  96) vel_y =  96;
        if (vel_y < -96) vel_y = -96;

        // ── 5. update position ────────────────────────────────────────────────
        pos_x += vel_x;
        pos_y += vel_y;

        // ── 6. bounce off walls with half velocity ────────────────────────────
        if (pos_x < 8*16)   { pos_x = 8*16;   vel_x = -vel_x / 2; }
        if (pos_x > 311*16) { pos_x = 311*16;  vel_x = -vel_x / 2; }
        if (pos_y < 8*16)   { pos_y = 8*16;    vel_y = -vel_y / 2; }
        if (pos_y > 231*16) { pos_y = 231*16;  vel_y = -vel_y / 2; }

        // ── 7. convert fixed-point back to screen pixels ──────────────────────
        int dot_x = pos_x / 16;
        int dot_y = pos_y / 16;

        // ── 8. erase old position, draw new ──────────────────────────────────
        fill_circle(old_x, old_y, 8, BLACK);
        fill_circle(dot_x, dot_y, 8, CYAN);

        old_x = dot_x;
        old_y = dot_y;

        wait_for_vsync();
    }

    return 0;
}