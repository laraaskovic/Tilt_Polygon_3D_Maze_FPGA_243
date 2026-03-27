"""
visualize.py — watch the trained Q-table agent navigate the maze
Run:  python visualize.py
Requires: pip install pygame
"""

import pygame
import random
import time
import maze_env
from maze_env import MAPS, COLS, ROWS, NUM_MAPS, N_ACTIONS
import json

from dfs_solver import compute_dfs_path
from astar_solver import compute_astar_path

# ── display settings ──────────────────────────────────────────────────────────
TILE     = 48
W        = COLS * TILE
H        = ROWS * TILE
FPS      = 8

# colors
C_BG        = (10,  10,  20)
C_WALL      = (60,  80, 120)
C_WALL_EDGE = (100, 130, 180)
C_AGENT     = (0,   220, 150)
C_TARGET    = (0,   200, 255)
C_PATH      = (40,  60,  40)
C_TEXT      = (255, 255, 255)


def load_q():
    try:
        with open("q_table.json", "r") as f:
            raw = json.load(f)
        maze_env.Q = {eval(k): v for k, v in raw.items()}
        print(f"Loaded Q-table: {len(maze_env.Q)} entries")
        return True
    except FileNotFoundError:
        print("No q_table.json found — run train.py first")
        return False


def best_action(state):
    from maze_env import get_q
    vals = [get_q(state, a) for a in range(N_ACTIONS)]
    return int(__import__('numpy').argmax(vals))


def random_action():
    return random.randint(0, N_ACTIONS - 1)


def draw_maze(screen, grid, path, agent_col, agent_row, target_col, target_row, font, wins, ep, map_idx, mode):
    screen.fill(C_BG)

    for row in range(ROWS):
        for col in range(COLS):
            rect = pygame.Rect(col*TILE, row*TILE, TILE, TILE)
            if grid[row][col] == 1:
                pygame.draw.rect(screen, C_WALL, rect)
                pygame.draw.rect(screen, C_WALL_EDGE, rect, 2)
            else:
                if (col, row) in path:
                    pygame.draw.rect(screen, C_PATH, rect)

    tx = target_col * TILE + TILE // 2
    ty = target_row * TILE + TILE // 2
    pygame.draw.circle(screen, C_TARGET, (tx, ty), TILE // 3)
    pygame.draw.circle(screen, (255,255,255), (tx, ty), TILE // 6)

    ax = agent_col * TILE + TILE // 2
    ay = agent_row * TILE + TILE // 2
    pygame.draw.circle(screen, C_AGENT, (ax, ay), TILE // 3)
    pygame.draw.circle(screen, (255,255,255), (ax, ay), TILE // 8)

    if mode == 1:
        mode_str = "RL"
    elif mode == 2:
        mode_str = "DFS"
    elif mode == 3:
        mode_str = "ASTAR"
    else:
        mode_str = "RANDOM"

    txt = font.render(f"Mode: {mode_str} | Map: {map_idx}   Wins: {wins}   Episode: {ep}", True, C_TEXT)
    screen.blit(txt, (4, 4))

    pygame.display.flip()


def run():
    if not load_q():
        return

    pygame.init()
    screen = pygame.display.set_mode((W, H + 30))
    pygame.display.set_caption("Q-Table Agent Visualizer")
    clock = pygame.time.Clock()
    font  = pygame.font.SysFont("monospace", 14)

    wins = 0
    ep   = 0

    mode = 1

    while True:
        ep += 1
        map_idx = random.randint(0, NUM_MAPS - 1)
        grid    = MAPS[map_idx]

        ac, ar = 1, 1
        while True:
            tc = random.randint(1, COLS-2)
            tr = random.randint(1, ROWS-2)
            if grid[tr][tc] == 0 and not (tc == 1 and tr == 1):
                break

        path   = set()
        steps  = 0
        won    = False

        dfs_path = compute_dfs_path(grid, (ac, ar), (tc, tr))
        astar_path = compute_astar_path(grid, (ac, ar), (tc, tr))

        dfs_index = 0
        astar_index = 0

        while steps < 300:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    pygame.quit(); return
                if event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        pygame.quit(); return
                    if event.key == pygame.K_SPACE:
                        steps = 9999
                    if event.key == pygame.K_1:
                        mode = 1
                    if event.key == pygame.K_2:
                        mode = 2
                    if event.key == pygame.K_3:
                        mode = 3
                    if event.key == pygame.K_4:
                        mode = 4

            if mode == 1:
                state  = (ac, ar, tc, tr, map_idx)
                action = best_action(state)

            elif mode == 2:
                if dfs_index < len(dfs_path):
                    action = dfs_path[dfs_index]
                    dfs_index += 1
                else:
                    action = 0

            elif mode == 3:
                if astar_index < len(astar_path):
                    action = astar_path[astar_index]
                    astar_index += 1
                else:
                    action = 0

            else:
                action = random_action()

            nac, nar = ac, ar
            if   action == 0: nar -= 1
            elif action == 1: nar += 1
            elif action == 2: nac -= 1
            elif action == 3: nac += 1

            if 0 <= nac < COLS and 0 <= nar < ROWS and grid[nar][nac] == 0:
                path.add((ac, ar))
                ac, ar = nac, nar

            draw_maze(screen, grid, path, ac, ar, tc, tr, font, wins, ep, map_idx, mode)
            clock.tick(FPS)
            steps += 1

            if ac == tc and ar == tr:
                wins += 1
                break

        time.sleep(0.3)


if __name__ == "__main__":
    run()