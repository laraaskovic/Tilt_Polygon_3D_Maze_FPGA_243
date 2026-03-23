#include <stdlib.h>

#define SCREEN_W   320
#define SCREEN_H   240
#define BLACK 0x0000
#define TILE 20
#define COLS 10
#define ROWS 10
#define NUM_MAPS 6
#define BALL_SIZE 8
#define SPEED 8

#define MAP_OFFSET_X  ((SCREEN_W - COLS*TILE) / 2)
#define MAP_OFFSET_Y  ((SCREEN_H - ROWS*TILE) / 2)

// colors
#define COL_BG         0x0000
#define COL_WALL_OUTER 0x4208
#define COL_WALL_CORE  0xFFFF
#define COL_WALL_SHINE 0xAD55
#define COL_BALL_OUT   0xF800
#define COL_BALL_MID   0xFC10
#define COL_BALL_IN    0xFFF0
#define COL_TGT_OUT    0x07FF
#define COL_TGT_MID    0x07E0
#define COL_TGT_IN     0xFFFF
#define COL_TGT_PULSE  0x03EF
#define COL_AGENT      0x07E0  // green agent ball

// JTAG UART
#define JTAG_UART_BASE 0xFF201000

short int Buffer1[240][512];
int target_col, target_row;

// agent position
int agent_px, agent_py;

int maps[NUM_MAPS][ROWS][COLS] = {
{   {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,0,1,1,0,1},
    {1,0,1,0,0,0,0,1,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,1,0,0,0,0,1,0,1},
    {1,0,1,1,0,0,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1},
},{  {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,1,0,0,0,1},
    {1,0,1,1,0,1,0,1,0,1},
    {1,0,1,0,0,0,0,1,0,1},
    {1,0,1,0,1,1,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,0,1,0,1,1,1},
    {1,0,0,0,0,1,0,0,0,1},
    {1,0,1,1,1,1,0,1,0,1},
    {1,1,1,1,1,1,1,1,1,1},
},{  {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,1,1,1,0,1},
    {1,0,1,0,0,0,0,0,0,1},
    {1,0,1,0,1,1,0,1,0,1},
    {1,0,1,0,1,1,0,1,0,1},
    {1,0,0,0,0,0,0,1,0,1},
    {1,0,1,1,1,1,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1},
},{  {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,1,0,0,0,0,1},
    {1,0,1,0,1,0,1,1,0,1},
    {1,0,1,0,0,0,0,1,0,1},
    {1,1,1,0,1,0,0,1,0,1},
    {1,0,0,0,1,0,1,1,0,1},
    {1,0,1,1,1,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,0,1},
    {1,0,1,1,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1},
},{  {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,1,0,0,1,0,0,1},
    {1,0,0,0,0,0,1,0,0,1},
    {1,0,0,1,0,0,0,0,0,1},
    {1,1,0,1,1,1,1,0,1,1},
    {1,1,0,1,1,1,1,0,1,1},
    {1,0,0,0,0,0,1,0,0,1},
    {1,0,0,1,0,0,0,0,0,1},
    {1,0,0,1,0,0,1,0,0,1},
    {1,1,1,1,1,1,1,1,1,1},
},{  {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,0,1,0,0,1,0,0,1,1},
    {1,0,0,1,0,0,0,0,0,1},
    {1,0,0,1,0,0,0,1,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,1,0,0,0,0,0,0,1},
    {1,1,0,0,1,0,0,1,0,1},
    {1,0,0,0,1,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1},
},};

// ── JTAG UART ────────────────────────────────────────────

void jtag_putchar(char c) {
    volatile int *uart = (int *)JTAG_UART_BASE;
    // wait until transmit FIFO has space
    while (!(*(uart + 1) & 0xFFFF0000));
    *uart = c;
}

void jtag_putint(int val) {
    jtag_putchar((val >> 24) & 0xFF);
    jtag_putchar((val >> 16) & 0xFF);
    jtag_putchar((val >>  8) & 0xFF);
    jtag_putchar((val      ) & 0xFF);
}

int jtag_getchar() {
    volatile int *uart = (int *)JTAG_UART_BASE;
    int val = *uart;
    if (val & 0x8000) return val & 0xFF;
    return -1;
}

// send: start marker + px + py + target_col + target_row + map
// = 1 + 20 bytes = 21 bytes total
void send_game_state(int px, int py, int tcol, int trow, int cm) {
    jtag_putchar(0xFF);
    jtag_putint(px);
    jtag_putint(py);
    jtag_putint(tcol);
    jtag_putint(trow);
    jtag_putint(cm);
}

// returns 0-3 for a valid move, -1 if nothing received
int recv_agent_move() {
    int b = jtag_getchar();
    if (b >= 0 && b <= 3) return b;
    return -1;
}

// ── drawing ──────────────────────────────────────────────

void plot_pixel(int x, int y, short int color) {
    if (x < 0 || x > 319 || y < 0 || y > 239) return;
    Buffer1[y][x] = color;
}

void draw_rect(int x, int y, int w, int h, short int color) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            plot_pixel(x+c, y+r, color);
}

void draw_circle(int cx, int cy, int r, short int color) {
    int x = 0, y = r, d = 1 - r;
    while (x <= y) {
        plot_pixel(cx+x,cy+y,color); plot_pixel(cx-x,cy+y,color);
        plot_pixel(cx+x,cy-y,color); plot_pixel(cx-x,cy-y,color);
        plot_pixel(cx+y,cy+x,color); plot_pixel(cx-y,cy+x,color);
        plot_pixel(cx+y,cy-x,color); plot_pixel(cx-y,cy-x,color);
        if (d < 0) d += 2*x+3;
        else { d += 2*(x-y)+5; y--; }
        x++;
    }
}

void fill_circle(int cx, int cy, int r, short int color) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                plot_pixel(cx+dx, cy+dy, color);
}

// 3D projection
#define CAMERA_DISTANCE 135
#define HALF_BOX_WIDTH  16

struct TwoDPoint { int x; int y; };
struct BoxPoints {
    struct TwoDPoint ftl, ftr, fbl, fbr;
    struct TwoDPoint btl, btr, bbl, bbr;
};

int abs(int x) { return x > 0 ? x : -x; }
void swap(int *x, int *y) { int t = *x; *x = *y; *y = t; }

struct TwoDPoint projectPoint(int x, int y, int z) {
    struct TwoDPoint p;
    int d = CAMERA_DISTANCE;
    p.x = (-(x)*d)/(z-d)+160;
    p.y = (-(y)*d)/(z-d)+120;
    return p;
}

struct BoxPoints getBoxPoints(int x, int y, int z) {
    struct BoxPoints b;
    int w = HALF_BOX_WIDTH;
    b.ftl = projectPoint(x-w,y+w,z+w); b.ftr = projectPoint(x+w,y+w,z+w);
    b.fbl = projectPoint(x-w,y-w,z+w); b.fbr = projectPoint(x+w,y-w,z+w);
    b.btl = projectPoint(x-w,y+w,z-w); b.btr = projectPoint(x+w,y+w,z-w);
    b.bbl = projectPoint(x-w,y-w,z-w); b.bbr = projectPoint(x+w,y-w,z-w);
    return b;
}

void line(int x0, int y0, int x1, int y1, short color) {
    int steep = abs(y1-y0) > abs(x1-x0);
    if (steep)  { swap(&x0,&y0); swap(&x1,&y1); }
    if (x0>x1)  { swap(&x0,&x1); swap(&y0,&y1); }
    int dx = x1-x0, dy = abs(y1-y0);
    int err = -(dx/2), y = y0, ys = y0<y1 ? 1 : -1;
    for (int i = x0; i <= x1; i++) {
        if (steep) plot_pixel(y,i,color); else plot_pixel(i,y,color);
        err += dy;
        if (err > 0) { y += ys; err -= dx; }
    }
}

void drawBox(int x, int y, int z, short color) {
    struct BoxPoints b = getBoxPoints(x,y,z);
    line(b.ftl.x,b.ftl.y,b.ftr.x,b.ftr.y,color);
    line(b.ftr.x,b.ftr.y,b.fbr.x,b.fbr.y,color);
    line(b.fbr.x,b.fbr.y,b.fbl.x,b.fbl.y,color);
    line(b.fbl.x,b.fbl.y,b.ftl.x,b.ftl.y,color);
    if(y<0) line(b.btl.x,b.btl.y,b.btr.x,b.btr.y,color);
    if(x<0) line(b.btr.x,b.btr.y,b.bbr.x,b.bbr.y,color);
    if(y>0) line(b.bbr.x,b.bbr.y,b.bbl.x,b.bbl.y,color);
    if(x>0) line(b.bbl.x,b.bbl.y,b.btl.x,b.btl.y,color);
    if((y<0)||(x>0)) line(b.ftl.x,b.ftl.y,b.btl.x,b.btl.y,color);
    if((y<0)||(x<0)) line(b.ftr.x,b.ftr.y,b.btr.x,b.btr.y,color);
    if((y>0)||(x>0)) line(b.fbl.x,b.fbl.y,b.bbl.x,b.bbl.y,color);
    if((y>0)||(x<0)) line(b.fbr.x,b.fbr.y,b.bbr.x,b.bbr.y,color);
}

void draw_wall_tile(int col, int row) {
    int x = col * 3*TILE - 260;
    int y = row * 3*TILE - 270;
    drawBox(x, y, -250, 0x9534);
}

void draw_ball(int x, int y, short int color) {
    int cx = x + BALL_SIZE/2;
    int cy = y + BALL_SIZE/2;
    if (color == BLACK) {
        draw_rect(x-1, y-1, BALL_SIZE+2, BALL_SIZE+2, BLACK);
        return;
    }
    fill_circle(cx, cy, BALL_SIZE/2,     color == COL_AGENT ? COL_AGENT : COL_BALL_OUT);
    fill_circle(cx, cy, BALL_SIZE/2 - 2, color == COL_AGENT ? 0x0410   : COL_BALL_MID);
    fill_circle(cx, cy, BALL_SIZE/2 - 3, color == COL_AGENT ? 0xFFFF   : COL_BALL_IN);
    plot_pixel(cx-1, cy-2, 0xFFFF);
}

void draw_target(int col, int row, short int color) {
    int cx = MAP_OFFSET_X + col * TILE + TILE/2;
    int cy = MAP_OFFSET_Y + row * TILE + TILE/2;
    if (color == BLACK) {
        draw_rect(cx - TILE/2, cy - TILE/2, TILE, TILE, BLACK);
        return;
    }
    draw_circle(cx, cy, 6, COL_TGT_PULSE);
    fill_circle(cx, cy, 4, COL_TGT_OUT);
    fill_circle(cx, cy, 2, COL_TGT_MID);
    plot_pixel(cx,   cy,   COL_TGT_IN);
    plot_pixel(cx+1, cy,   COL_TGT_IN);
    plot_pixel(cx,   cy+1, COL_TGT_IN);
    plot_pixel(cx+1, cy+1, COL_TGT_IN);
}

void draw_map(int m) {
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            Buffer1[y][x] = BLACK;
    for (int row = 0; row < ROWS; row++)
        for (int col = 0; col < COLS; col++)
            if (maps[m][row][col] == 1)
                draw_wall_tile(col, row);
}

void spawn_target(int m, int bc, int br) {
    int col, row;
    do {
        col = 1 + rand() % (COLS-2);
        row = 1 + rand() % (ROWS-2);
    } while (maps[m][row][col] != 0 || (col==bc && row==br));
    target_col = col;
    target_row = row;
    draw_target(target_col, target_row, COL_TGT_OUT);
}

int hits_wall(int m, int px, int py) {
    int pts[4][2] = {
        {px, py},
        {px + BALL_SIZE - 1, py},
        {px, py + BALL_SIZE - 1},
        {px + BALL_SIZE - 1, py + BALL_SIZE - 1},
    };
    for (int i = 0; i < 4; i++) {
        int col = (pts[i][0] - MAP_OFFSET_X) / TILE;
        int row = (pts[i][1] - MAP_OFFSET_Y) / TILE;
        if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return 1;
        if (maps[m][row][col] == 1) return 1;
    }
    return 0;
}

int reached_target(int px, int py) {
    int bc = (px + BALL_SIZE/2 - MAP_OFFSET_X) / TILE;
    int br = (py + BALL_SIZE/2 - MAP_OFFSET_Y) / TILE;
    return (bc == target_col && br == target_row);
}

void wait_for_vsync(void) {
    volatile int *ctrl = (int *)0xFF203020;
    *ctrl = 1;
    while ((*(ctrl+3) & 0x01) != 0);
}

volatile int *ps2 = (volatile int *)0xFF200100;

int ps2_read() {
    int val = *ps2;
    if (val & 0x8000) return val & 0xFF;
    return -1;
}

// ── reset helper ─────────────────────────────────────────

void reset_round(int *cm, int *px, int *py) {
    *cm = rand() % NUM_MAPS;
    draw_map(*cm);
    *px = MAP_OFFSET_X + TILE + (TILE-BALL_SIZE)/2;
    *py = MAP_OFFSET_Y + TILE + (TILE-BALL_SIZE)/2;
    agent_px = MAP_OFFSET_X + 2*TILE + (TILE-BALL_SIZE)/2;
    agent_py = MAP_OFFSET_Y + TILE   + (TILE-BALL_SIZE)/2;
    draw_ball(*px, *py, COL_BALL_OUT);
    draw_ball(agent_px, agent_py, COL_AGENT);
    spawn_target(*cm, 1, 1);
}

// ── main ─────────────────────────────────────────────────

int main(void) {
    volatile int *pixel_ctrl = (int *)0xFF203020;
    *(pixel_ctrl+1) = (int)&Buffer1;
    *pixel_ctrl = 1;
    while ((*(pixel_ctrl+3) & 0x01) != 0);
    *(pixel_ctrl+1) = (int)&Buffer1;

    int cm = 0;
    draw_map(cm);

    // player start
    int px = MAP_OFFSET_X + TILE + (TILE-BALL_SIZE)/2;
    int py = MAP_OFFSET_Y + TILE + (TILE-BALL_SIZE)/2;
    draw_ball(px, py, COL_BALL_OUT);

    // agent start — one tile to the right of player
    agent_px = MAP_OFFSET_X + 2*TILE + (TILE-BALL_SIZE)/2;
    agent_py = MAP_OFFSET_Y + TILE   + (TILE-BALL_SIZE)/2;
    draw_ball(agent_px, agent_py, COL_AGENT);

    spawn_target(cm, 1, 1);

    int e0 = 0, skip = 0;

    while (1) {

        // ── send game state to Python agent ──
        send_game_state(px, py, target_col, target_row, cm);

        // ── receive and apply agent move ──
        int agent_move = recv_agent_move();
        if (agent_move >= 0) {
            int ax = agent_px, ay = agent_py;
            if      (agent_move == 0) ay -= SPEED;
            else if (agent_move == 1) ay += SPEED;
            else if (agent_move == 2) ax -= SPEED;
            else if (agent_move == 3) ax += SPEED;

            if (!hits_wall(cm, ax, ay)) {
                draw_ball(agent_px, agent_py, BLACK);
                agent_px = ax;
                agent_py = ay;
                draw_target(target_col, target_row, COL_TGT_OUT);
                draw_ball(agent_px, agent_py, COL_AGENT);
            }

            // agent wins round
            if (reached_target(agent_px, agent_py)) {
                reset_round(&cm, &px, &py);
                continue;
            }
        }

        // ── player keyboard input ──
        int b;
        while ((b = ps2_read()) >= 0) {
            if (skip) { skip=0; e0=0; continue; }
            if (b == 0xF0) { skip=1; continue; }
            if (b == 0xE0) { e0=1; continue; }

            int nx = px, ny = py;
            if (e0) {
                if      (b==0x6B) nx -= SPEED;
                else if (b==0x74) nx += SPEED;
                else if (b==0x75) ny -= SPEED;
                else if (b==0x72) ny += SPEED;
                e0 = 0;
            } else {
                if      (b==0x1D) ny -= SPEED;
                else if (b==0x1B) ny += SPEED;
                else if (b==0x1C) nx -= SPEED;
                else if (b==0x23) nx += SPEED;
            }

            if (!hits_wall(cm, nx, ny)) {
                draw_ball(px, py, BLACK);
                px = nx; py = ny;
                draw_target(target_col, target_row, COL_TGT_OUT);
                draw_ball(agent_px, agent_py, COL_AGENT);
                draw_ball(px, py, COL_BALL_OUT);
            }

            // player wins round
            if (reached_target(px, py)) {
                reset_round(&cm, &px, &py);
            }
        }
    }
    return 0;
}