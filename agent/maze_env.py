import numpy as np
import random

# ── constants — must match ball.c exactly ─────────────────────────────────────
COLS     = 10
ROWS     = 10
NUM_MAPS = 6
MAX_STEPS = 300
N_ACTIONS = 4  # 0=up 1=down 2=left 3=right

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

# ── Q table ───────────────────────────────────────────────────────────────────
# state = (agent_col, agent_row, target_col, target_row, map_idx)
Q = {}

def get_q(s, a):
    return Q.get((s, a), 0.0)

def best_action(s):
    return int(np.argmax([get_q(s, a) for a in range(N_ACTIONS)]))


# ── environment ───────────────────────────────────────────────────────────────

class MazeEnv:
    def __init__(self):
        self.map_idx = 0
        self.grid = MAPS[0]
        self.col = self.row = 1
        self.target_col = self.target_row = 0
        self.steps = 0
        self.reset()

    def reset(self, map_idx=None):
        self.map_idx = (map_idx % NUM_MAPS) if map_idx is not None else random.randint(0, NUM_MAPS-1)
        self.grid = MAPS[self.map_idx]
        self.col, self.row = 1, 1
        while True:
            tc = random.randint(1, COLS-2)
            tr = random.randint(1, ROWS-2)
            if self.grid[tr][tc] == 0 and not (tc == 1 and tr == 1):
                break
        self.target_col, self.target_row = tc, tr
        self.steps = 0
        return self._state()

    def _state(self):
        return (self.col, self.row, self.target_col, self.target_row, self.map_idx)

    def step(self, action):
        nc, nr = self.col, self.row
        if   action == 0: nr -= 1
        elif action == 1: nr += 1
        elif action == 2: nc -= 1
        elif action == 3: nc += 1

        self.steps += 1
        moved = (0 <= nc < COLS and 0 <= nr < ROWS and self.grid[nr][nc] == 0)
        if moved:
            self.col, self.row = nc, nr

        done = False
        reward = -0.05 if moved else -0.5

        if self.col == self.target_col and self.row == self.target_row:
            reward, done = 10.0, True
        elif self.steps >= MAX_STEPS:
            reward, done = -2.0, True

        return self._state(), reward, done