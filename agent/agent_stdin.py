"""
agent_stdin.py  —  pipe version, no serial port needed
Run as:   nios2-terminal | python agent_stdin.py

The nios2-terminal tool bridges the JTAG UART to stdout/stdin,
so this script just reads/writes sys.stdin and sys.stdout.
"""

import sys
import struct
import random
import numpy as np
import torch
import torch.nn as nn

from maze_env import STATE_DIM, N_ACTIONS, SCREEN_W, SCREEN_H
from maze_env import COLS, ROWS, NUM_MAPS, MAPS, BALL_SIZE, TILE
from maze_env import MAP_OFFSET_X, MAP_OFFSET_Y

MODEL_PATH = "dqn_model.pth"

# ── network (must match train.py) ─────────────────────────────────────────────

class DQN(nn.Module):
    def __init__(self, state_dim, n_actions, hidden=64):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(state_dim, hidden), nn.ReLU(),
            nn.Linear(hidden, hidden),   nn.ReLU(),
            nn.Linear(hidden, n_actions),
        )
    def forward(self, x):
        return self.net(x)


def load_model():
    try:
        model = DQN(STATE_DIM, N_ACTIONS)
        model.load_state_dict(torch.load(MODEL_PATH, map_location="cpu"))
        model.eval()
        sys.stderr.write(f"Loaded model from {MODEL_PATH}\n")
        return model
    except Exception as e:
        sys.stderr.write(f"WARNING: could not load model ({e}) — random agent\n")
        return None


# ── state helpers ─────────────────────────────────────────────────────────────

def wall_at(grid, col, row):
    col = max(0, min(COLS - 1, col))
    row = max(0, min(ROWS - 1, row))
    return float(grid[row][col])

def state_from_packet(px, py, tcol, trow, midx):
    midx = max(0, min(NUM_MAPS - 1, midx))
    grid = MAPS[midx]
    col = max(0, min(COLS-1, (px + BALL_SIZE//2 - MAP_OFFSET_X) // TILE))
    row = max(0, min(ROWS-1, (py + BALL_SIZE//2 - MAP_OFFSET_Y) // TILE))
    dx = (tcol - col) / COLS
    dy = (trow - row) / ROWS
    return np.array([
        px / SCREEN_W, py / SCREEN_H, dx, dy, midx / NUM_MAPS,
        wall_at(grid, col,     row - 1), wall_at(grid, col,     row + 1),
        wall_at(grid, col - 1, row),     wall_at(grid, col + 1, row),
        wall_at(grid, col,     row - 2), wall_at(grid, col,     row + 2),
        wall_at(grid, col - 2, row),     wall_at(grid, col + 2, row),
    ], dtype=np.float32)


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    # use binary mode for stdin/stdout
    stdin  = sys.stdin.buffer
    stdout = sys.stdout.buffer

    model = load_model()
    sys.stderr.write("Waiting for packets...\n")

    buf = bytearray()
    packet_count = 0

    while True:
        # read one byte at a time looking for 0xFF header
        byte = stdin.read(1)
        if not byte:
            break

        if byte[0] == 0xFF:
            # read remaining 20 bytes of packet
            rest = stdin.read(20)
            if len(rest) < 20:
                continue
            px, py, tcol, trow, cm = struct.unpack(">iiiii", rest)

            state  = state_from_packet(px, py, tcol, trow, cm)

            if model is not None:
                with torch.no_grad():
                    s_t    = torch.FloatTensor(state).unsqueeze(0)
                    action = int(model(s_t).argmax(dim=1).item())
            else:
                action = random.randint(0, N_ACTIONS - 1)

            stdout.write(bytes([action & 0xFF]))
            stdout.flush()

            packet_count += 1
            if packet_count % 20 == 0:
                sys.stderr.write(
                    f"[{packet_count:5d}] pos:({px},{py}) "
                    f"target:({tcol},{trow}) map:{cm} action:{action}\n"
                )

if __name__ == "__main__":
    main()