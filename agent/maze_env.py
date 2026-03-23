import numpy as np
import random

# ── constants (must match C) ──────────────────────────────────────────────────
COLS       = 10
ROWS       = 10
TILE       = 20
BALL_SIZE  = 8
SPEED      = 8
SCREEN_W   = 320
SCREEN_H   = 240
MAP_OFFSET_X = (SCREEN_W - COLS * TILE) // 2
MAP_OFFSET_Y = (SCREEN_H - ROWS * TILE) // 2
NUM_MAPS   = 6
MAX_STEPS  = 100   # was 200

MAPS = [
    [[1,1,1,1,1,1,1,1,1,1],
     [1,0,0,0,0,0,0,0,0,1],
     [1,0,1,1,0,0,1,1,0,1],
     [1,0,1,0,0,0,0,1,0,1],
     [1,0,0,0,1,1,0,0,0,1],
     [1,0,0,0,1,1,0,0,0,1],
     [1,0,1,0,0,0,0,1,0,1],
     [1,0,1,1,0,0,1,1,0,1],
     [1,0,0,0,0,0,0,0,0,1],
     [1,1,1,1,1,1,1,1,1,1]],

    [[1,1,1,1,1,1,1,1,1,1],
     [1,0,0,0,0,1,0,0,0,1],
     [1,0,1,1,0,1,0,1,0,1],
     [1,0,1,0,0,0,0,1,0,1],
     [1,0,1,0,1,1,1,1,0,1],
     [1,0,0,0,0,0,0,0,0,1],
     [1,1,1,1,0,1,0,1,1,1],
     [1,0,0,0,0,1,0,0,0,1],
     [1,0,1,1,1,1,0,1,0,1],
     [1,1,1,1,1,1,1,1,1,1]],

    [[1,1,1,1,1,1,1,1,1,1],
     [1,0,0,0,0,0,0,0,0,1],
     [1,0,1,1,1,1,1,1,0,1],
     [1,0,1,0,0,0,0,0,0,1],
     [1,0,1,0,1,1,0,1,0,1],
     [1,0,1,0,1,1,0,1,0,1],
     [1,0,0,0,0,0,0,1,0,1],
     [1,0,1,1,1,1,1,1,0,1],
     [1,0,0,0,0,0,0,0,0,1],
     [1,1,1,1,1,1,1,1,1,1]],

    [[1,1,1,1,1,1,1,1,1,1],
     [1,0,0,0,1,0,0,0,0,1],
     [1,0,1,0,1,0,1,1,0,1],
     [1,0,1,0,0,0,0,1,0,1],
     [1,1,1,0,1,0,0,1,0,1],
     [1,0,0,0,1,0,1,1,0,1],
     [1,0,1,1,1,0,0,0,0,1],
     [1,0,0,0,0,0,1,1,0,1],
     [1,0,1,1,0,0,0,0,0,1],
     [1,1,1,1,1,1,1,1,1,1]],

    [[1,1,1,1,1,1,1,1,1,1],
     [1,0,0,1,0,0,1,0,0,1],
     [1,0,0,0,0,0,1,0,0,1],
     [1,0,0,1,0,0,0,0,0,1],
     [1,1,0,1,1,1,1,0,1,1],
     [1,1,0,1,1,1,1,0,1,1],
     [1,0,0,0,0,0,1,0,0,1],
     [1,0,0,1,0,0,0,0,0,1],
     [1,0,0,1,0,0,1,0,0,1],
     [1,1,1,1,1,1,1,1,1,1]],

    [[1,1,1,1,1,1,1,1,1,1],
     [1,0,0,0,0,0,0,0,0,1],
     [1,0,1,0,0,1,0,0,1,1],
     [1,0,0,1,0,0,0,0,0,1],
     [1,0,0,1,0,0,0,1,0,1],
     [1,0,0,0,1,1,0,0,0,1],
     [1,0,1,0,0,0,0,0,0,1],
     [1,1,0,0,1,0,0,1,0,1],
     [1,0,0,0,1,0,0,0,0,1],
     [1,1,1,1,1,1,1,1,1,1]],
]

# ── state dimensions ──────────────────────────────────────────────────────────
# [px_norm, py_norm, dx_norm, dy_norm, map_idx_norm,
#  wall_up, wall_down, wall_left, wall_right,
#  wall_up2, wall_down2, wall_left2, wall_right2]
STATE_DIM  = 13
N_ACTIONS  = 4   # up, down, left, right


# ── legacy tabular helpers (kept so agent.py can still import them) ───────────
Q = {}

def discretize(state):
    """Kept for backward-compat with agent.py; not used by DQN."""
    px_n = float(state[0]); py_n = float(state[1])
    tc_n = float(state[2]); tr_n = float(state[3])
    m_n  = float(state[4])
    px_b = min(int(px_n * 16), 15)
    py_b = min(int(py_n * 16), 15)
    tc_b = min(int(tc_n * 10),  9)
    tr_b = min(int(tr_n * 10),  9)
    m_b  = min(int(m_n  *  6),  5)
    return (px_b, py_b, tc_b, tr_b, m_b)

def get_q(s, a):
    return Q.get((s, a), 0.0)

def best_action(s):
    vals = [get_q(s, a) for a in range(N_ACTIONS)]
    return int(np.argmax(vals))


# ── environment ───────────────────────────────────────────────────────────────

class MazeEnv:
    def __init__(self):
        self.map_idx = 0
        self.grid    = MAPS[0]
        self.px = self.py = 0
        self.target_col = self.target_row = 0
        self.steps = 0
        self.reset()

    # ------------------------------------------------------------------
    def reset(self, map_idx=None):
        if map_idx is not None:
            self.map_idx = map_idx % NUM_MAPS
        else:
            self.map_idx = random.randint(0, NUM_MAPS - 1)

        self.grid = MAPS[self.map_idx]

        # player always starts at tile (1,1)
        self.px = MAP_OFFSET_X + TILE + (TILE - BALL_SIZE) // 2
        self.py = MAP_OFFSET_Y + TILE + (TILE - BALL_SIZE) // 2

        # random open target tile, not the start tile
        while True:
            tc = random.randint(1, COLS - 2)
            tr = random.randint(1, ROWS - 2)
            if self.grid[tr][tc] == 0 and not (tc == 1 and tr == 1):
                break
        self.target_col = tc
        self.target_row = tr
        self.steps = 0
        return self._get_state()

    # ------------------------------------------------------------------
    def _current_tile(self):
        col = (self.px + BALL_SIZE // 2 - MAP_OFFSET_X) // TILE
        row = (self.py + BALL_SIZE // 2 - MAP_OFFSET_Y) // TILE
        col = max(0, min(COLS - 1, col))
        row = max(0, min(ROWS - 1, row))
        return col, row

    def _wall_at(self, col, row):
        col = max(0, min(COLS - 1, col))
        row = max(0, min(ROWS - 1, row))
        return float(self.grid[row][col])

    def _get_state(self):
        col, row = self._current_tile()

        # direction to target (normalised, signed)
        dx = (self.target_col - col) / COLS
        dy = (self.target_row - row) / ROWS

        # immediate neighbours
        w_up    = self._wall_at(col, row - 1)
        w_down  = self._wall_at(col, row + 1)
        w_left  = self._wall_at(col - 1, row)
        w_right = self._wall_at(col + 1, row)

        # two-tile lookahead
        w_up2    = self._wall_at(col, row - 2)
        w_down2  = self._wall_at(col, row + 2)
        w_left2  = self._wall_at(col - 2, row)
        w_right2 = self._wall_at(col + 2, row)

        return np.array([
            self.px / SCREEN_W,          # 0  absolute x
            self.py / SCREEN_H,          # 1  absolute y
            dx,                          # 2  signed x to target
            dy,                          # 3  signed y to target
            self.map_idx / NUM_MAPS,     # 4  which map
            w_up, w_down,                # 5-6
            w_left, w_right,             # 7-8
            w_up2, w_down2,              # 9-10
            w_left2, w_right2,           # 11-12
        ], dtype=np.float32)

    # ------------------------------------------------------------------
    def _hits_wall(self, px, py):
        corners = [
            (px,                   py),
            (px + BALL_SIZE - 1,   py),
            (px,                   py + BALL_SIZE - 1),
            (px + BALL_SIZE - 1,   py + BALL_SIZE - 1),
        ]
        for cx, cy in corners:
            col = (cx - MAP_OFFSET_X) // TILE
            row = (cy - MAP_OFFSET_Y) // TILE
            if col < 0 or col >= COLS or row < 0 or row >= ROWS:
                return True
            if self.grid[row][col] == 1:
                return True
        return False

    def _reached_target(self):
        bc = (self.px + BALL_SIZE // 2 - MAP_OFFSET_X) // TILE
        br = (self.py + BALL_SIZE // 2 - MAP_OFFSET_Y) // TILE
        return bc == self.target_col and br == self.target_row

    # ------------------------------------------------------------------
    def step(self, action):
        nx, ny = self.px, self.py
        if   action == 0: ny -= SPEED
        elif action == 1: ny += SPEED
        elif action == 2: nx -= SPEED
        elif action == 3: nx += SPEED

        moved = False
        if not self._hits_wall(nx, ny):
            self.px, self.py = nx, ny
            moved = True

        self.steps += 1
        done   = False
        reward = -0.02          # small step penalty

        if not moved:
            reward = -0.5       # discourage wall-bashing

        if self._reached_target():
            reward = 10.0
            done   = True
        elif self.steps >= MAX_STEPS:
            reward = -2.0
            done   = True

        return self._get_state(), reward, done