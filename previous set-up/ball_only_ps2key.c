#include <stdlib.h>

#define SCREEN_W  320
#define SCREEN_H  240
#define BLOCK 12
#define SPEED 2

#define BLACK 0x0000
#define RED 0xF800

short int Buffer1[240][512];

void plot_pixel(int x, int y, short int color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    Buffer1[y][x] = color;
}

void draw_rect(int x, int y, int w, int h, short int color) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            plot_pixel(x+c, y+r, color);
}

/* PS2 keyboard register */
volatile int *ps2 = (volatile int *)0xFF200100;

/* Read one byte from PS2. Returns -1 if the buffer is empty. */
int ps2_read(void) {
    int val = *ps2;
    if (val & 0x8000) return val & 0xFF;  // bit 15 = data valid
    return -1;
}

int main(void) {
    /* Point the pixel controller at our buffer and wait for it to be ready */
    volatile int *pixel_ctrl = (int *)0xFF203020;
    *(pixel_ctrl+1) = (int)&Buffer1;
    *pixel_ctrl = 1;
	
    while ((*(pixel_ctrl+3) & 0x01) != 0);
	
    *(pixel_ctrl+1) = (int)&Buffer1;

    /* Clear screen and draw the block in the center */
    draw_rect(0, 0, SCREEN_W, SCREEN_H, BLACK);

    int px = SCREEN_W/2 - BLOCK/2;
    int py = SCREEN_H/2 - BLOCK/2;
    draw_rect(px, py, BLOCK, BLOCK, RED);

    /*
     * PS2 scan code notes:
     *   0xF0 = break code prefix, the next byte is a key release
     *   0xE0 = extended key prefix, the next byte is an arrow key
     *   arrow up = E0 75
     *   arrow down = E0 72
     *   arrow left = E0 6B
     *   arrow right = E0 74
     *   W = 1D, S = 1B, A = 1C, D = 23
     *
     * We only move on make codes (key press), not break codes (key release).
     */
	
    int e0 = 0;  // set when we see 0xE0, so we know next byte is an arrow
    int skip = 0;  // set when we see 0xF0, so we can throw away the next byte

    while (1) {
        int b;
		
        while ((b = ps2_read()) >= 0) { //interupt like

            /* 0xF0 means the next byte is a release code, just discard it */
            if (b == 0xF0) {
                skip = 1;
                continue;
            }

            /* throw away the release byte that followed 0xF0 */
            if (skip) {
                skip = 0;
                e0 = 0;
                continue;
            }

            /* 0xE0 is a prefix for arrow keys, wait for the actual key byte */
            if (b == 0xE0) {
                e0 = 1;
                continue;
            }

            /* figure out how far to move */
            int dx = 0, dy = 0;

            if (e0) {
                /* arrow keys come after the 0xE0 prefix */
                if (b == 0x6B) dx = -SPEED;  // left
                else if (b == 0x74) dx = +SPEED;  // right
                else if (b == 0x75) dy = -SPEED;  // up
                else if (b == 0x72) dy = +SPEED;  // down
                e0 = 0;
            } else {
                /* WASD */
                if (b == 0x1D) dy = -SPEED;  // W
                else if (b == 0x1B) dy = +SPEED;  // S
                else if (b == 0x1C) dx = -SPEED;  // A
                else if (b == 0x23) dx = +SPEED;  // D
            }

            /* clamp to screen edges so the block never flies off */
            int nx = px + dx;
            int ny = py + dy;
			
            if (nx < 0) nx = 0;
            if (nx > SCREEN_W - BLOCK) nx = SCREEN_W - BLOCK;
            if (ny < 0) ny = 0;
            if (ny > SCREEN_H - BLOCK) ny = SCREEN_H - BLOCK;

            /* erase old position, draw at new position */
            draw_rect(px, py, BLOCK, BLOCK, BLACK); //erase
            px = nx;
            py = ny;
            draw_rect(px, py, BLOCK, BLOCK, RED); //draw
        }
    }

    return 0;
}