import numpy as np
import random

COLS      = 10
ROWS      = 10
NUM_MAPS  = 6
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

Q = {}

def get_q(s, a):
    return Q.get((s, a), 0.0)

def best_action(s):
    return int(np.argmax([get_q(s, a) for a in range(N_ACTIONS)]))


class MazeEnv:
    def __init__(self):
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
        self.visited = {}   # tile -> visit count
        self.visited[(self.col, self.row)] = 1
        self.last_col, self.last_row = self.col, self.row
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
            self.last_col, self.last_row = self.col, self.row
            self.col, self.row = nc, nr

        done = False

        if not moved:
            reward = -1.0                          # wall bump
        else:
            visit_count = self.visited.get((self.col, self.row), 0)
            if visit_count == 0:
                reward = -0.05                     # fresh tile, small penalty
            else:
                reward = -0.3 * visit_count        # revisit penalty scales up
            self.visited[(self.col, self.row)] = visit_count + 1

        if self.col == self.target_col and self.row == self.target_row:
            reward, done = 10.0, True
        elif self.steps >= MAX_STEPS:
            reward, done = -2.0, True

        return self._state(), reward, done