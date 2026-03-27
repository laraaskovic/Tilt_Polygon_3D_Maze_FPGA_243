# random_solver.py
# Very simple "bad" agent: picks random valid moves each step

import random

# action mapping (same as everywhere else)
# 0 = up, 1 = down, 2 = left, 3 = right

def compute_random_action(grid, ac, ar):
    actions = [0, 1, 2, 3]
    random.shuffle(actions)

    for action in actions:
        nac, nar = ac, ar

        if action == 0:   nar -= 1
        elif action == 1: nar += 1
        elif action == 2: nac -= 1
        elif action == 3: nac += 1

        # only return VALID moves (prevents getting stuck constantly)
        if 0 <= nac < len(grid[0]) and 0 <= nar < len(grid) and grid[nar][nac] == 0:
            return action

    return 0  # fallback (should rarely happen)