#include <stdlib.h>

/* Screen and grid settings */
#define SCREEN_W  320
#define SCREEN_H  240
#define TILE      16
#define COLS      10
#define ROWS      10
#define BLOCK     12
#define SPEED     2

/* Center the grid on screen */
#define OFFSET_X  ((SCREEN_W - COLS*TILE) / 2)
#define OFFSET_Y  ((SCREEN_H - ROWS*TILE) / 2)

/* Colors (RGB565) */
#define BLACK     0x0000
#define WHITE     0xFFFF
#define GRAY      0x4208
#define RED       0xF800

/* Frame buffer */
short int Buffer1[240][512];

/* Simple map: 1 = wall, 0 = open */
int map[ROWS][COLS] = {
    {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,0,1,1,0,1},
    {1,0,1,0,0,0,0,1,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,1,0,0,0,0,1,0,1},
    {1,0,1,1,0,0,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1},
};

/* Draw a single pixel safely */
void plot_pixel(int x, int y, short int color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    Buffer1[y][x] = color;
}

/* Fill a rectangle with a solid color */
void draw_rect(int x, int y, int w, int h, short int color) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            plot_pixel(x+c, y+r, color);
}

/* Draw the full map: black background, then white tiles with a gray border */
void draw_map(void) {
    draw_rect(0, 0, SCREEN_W, SCREEN_H, BLACK);

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            if (map[row][col] == 1) {
                int x = OFFSET_X + col * TILE;
                int y = OFFSET_Y + row * TILE;

                /* Outer dark border */
                draw_rect(x, y, TILE, TILE, GRAY);

                /* White fill inset by 1px */
                draw_rect(x+1, y+1, TILE-2, TILE-2, WHITE);
            }
        }
    }
}

/* Draw the player block. Pass BLACK to erase it. */
void draw_block(int x, int y, short int color) {
    draw_rect(x, y, BLOCK, BLOCK, color);
}

/* Check if the block at (px, py) would overlap any wall tile */
int hits_wall(int px, int py) {
    /* Test all four corners of the block */
    int corners[4][2] = {
        {px,          py},
        {px+BLOCK-1,  py},
        {px,          py+BLOCK-1},
        {px+BLOCK-1,  py+BLOCK-1},
    };

    for (int i = 0; i < 4; i++) {
        int col = (corners[i][0] - OFFSET_X) / TILE;
        int row = (corners[i][1] - OFFSET_Y) / TILE;

        if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return 1;
        if (map[row][col] == 1) return 1;
    }
    return 0;
}

/* Wait for vertical sync so drawing stays smooth */
void wait_for_vsync(void) {
    volatile int *ctrl = (int *)0xFF203020;
    *ctrl = 1;
    while ((*(ctrl+3) & 0x01) != 0);
}

/* Try to read one byte from the PS2 keyboard. Returns -1 if nothing ready. */
volatile int *ps2 = (volatile int *)0xFF200100;

int ps2_read(void) {
    int val = *ps2;
    if (val & 0x8000) return val & 0xFF;
    return -1;
}

int main(void) {
    /* Point the pixel controller at our buffer */
    volatile int *pixel_ctrl = (int *)0xFF203020;
    *(pixel_ctrl+1) = (int)&Buffer1;
    *pixel_ctrl = 1;
    while ((*(pixel_ctrl+3) & 0x01) != 0);
    *(pixel_ctrl+1) = (int)&Buffer1;

    draw_map();

    /* Start the block one tile in from the top-left corner */
    int px = OFFSET_X + TILE + (TILE-BLOCK)/2;
    int py = OFFSET_Y + TILE + (TILE-BLOCK)/2;
    draw_block(px, py, RED);

    /* Track key state: skip=1 means next byte completes a break code */
    int e0 = 0, skip = 0;

    while (1) {
        int b;
        while ((b = ps2_read()) >= 0) {

            /* Break code: 0xF0 means the next byte is a key release, ignore it */
            if (skip) { skip=0; e0=0; continue; }
            if (b == 0xF0) { skip=1; continue; }

            /* 0xE0 prefix means an arrow key is coming next */
            if (b == 0xE0) { e0=1; continue; }

            int nx = px, ny = py;

            if (e0) {
                /* Arrow keys */
                if      (b == 0x6B) nx -= SPEED;  // left
                else if (b == 0x74) nx += SPEED;  // right
                else if (b == 0x75) ny -= SPEED;  // up
                else if (b == 0x72) ny += SPEED;  // down
                e0 = 0;
            } else {
                /* WASD */
                if      (b == 0x1D) ny -= SPEED;  // W
                else if (b == 0x1B) ny += SPEED;  // S
                else if (b == 0x1C) nx -= SPEED;  // A
                else if (b == 0x23) nx += SPEED;  // D
            }

            /* Only move if the new position is clear */
            if (!hits_wall(nx, ny)) {
                draw_block(px, py, BLACK);
                px = nx;
                py = ny;
                draw_block(px, py, RED);
            }
        }
    }

    return 0;
}