"""
agent.py  —  reads game state from FPGA over JTAG/serial, sends back moves
Run:  python agent.py [COM_PORT]

Expects dqn_model.pth in the same folder (produced by train.py).
Falls back to random moves if no model file is found.
"""

import sys
import struct
import time
import random
import numpy as np

import serial
import serial.tools.list_ports

import torch
import torch.nn as nn

from maze_env import STATE_DIM, N_ACTIONS, SCREEN_W, SCREEN_H, COLS, ROWS, NUM_MAPS

MODEL_PATH = "dqn_model.pth"

# ── network (must match train.py) ─────────────────────────────────────────────

class DQN(nn.Module):
    def __init__(self, state_dim, n_actions, hidden=128):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(state_dim, hidden),
            nn.ReLU(),
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Linear(hidden, n_actions),
        )

    def forward(self, x):
        return self.net(x)


# ── model loading ─────────────────────────────────────────────────────────────

def load_model():
    try:
        model = DQN(STATE_DIM, N_ACTIONS)
        model.load_state_dict(torch.load(MODEL_PATH, map_location="cpu"))
        model.eval()
        print(f"Loaded model from {MODEL_PATH}")
        return model
    except FileNotFoundError:
        print(f"WARNING: {MODEL_PATH} not found — using random agent")
        return None
    except Exception as e:
        print(f"WARNING: Could not load model ({e}) — using random agent")
        return None


# ── serial helpers ────────────────────────────────────────────────────────────

def find_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if any(k in p.description for k in ["USB", "JTAG", "Altera", "Intel"]):
            print(f"Auto-detected: {p.device} — {p.description}")
            return p.device
    print("Could not auto-detect port. Available ports:")
    for p in ports:
        print(f"  {p.device} — {p.description}")
    return input("Enter port (e.g. COM3 or /dev/ttyUSB0): ").strip()


def read_packet(conn):
    """
    Packet from FPGA: 0xFF | px(4) | py(4) | tcol(4) | trow(4) | cm(4) = 21 bytes
    Returns dict or None on timeout.
    """
    deadline = time.time() + 1.0
    while time.time() < deadline:
        b = conn.read(1)
        if b and b[0] == 0xFF:
            break
    else:
        return None

    data = conn.read(20)
    if len(data) < 20:
        return None

    px, py, tcol, trow, cm = struct.unpack(">iiiii", data)
    return {"px": px, "py": py, "tcol": tcol, "trow": trow, "map": cm}


def send_move(conn, move: int):
    conn.write(bytes([move & 0xFF]))


# ── state construction (mirrors maze_env._get_state but from packet) ──────────

BALL_SIZE   = 8
TILE        = 20
MAP_OFFSET_X = (SCREEN_W - COLS * TILE) // 2
MAP_OFFSET_Y = (SCREEN_H - ROWS * TILE) // 2

from maze_env import MAPS

def wall_at(grid, col, row):
    col = max(0, min(COLS - 1, col))
    row = max(0, min(ROWS - 1, row))
    return float(grid[row][col])

def state_from_packet(pkt):
    px   = pkt["px"]
    py   = pkt["py"]
    tcol = pkt["tcol"]
    trow = pkt["trow"]
    midx = max(0, min(NUM_MAPS - 1, pkt["map"]))
    grid = MAPS[midx]

    col = max(0, min(COLS - 1, (px + BALL_SIZE // 2 - MAP_OFFSET_X) // TILE))
    row = max(0, min(ROWS - 1, (py + BALL_SIZE // 2 - MAP_OFFSET_Y) // TILE))

    dx = (tcol - col) / COLS
    dy = (trow - row) / ROWS

    return np.array([
        px / SCREEN_W,
        py / SCREEN_H,
        dx,
        dy,
        midx / NUM_MAPS,
        wall_at(grid, col,     row - 1),   # up
        wall_at(grid, col,     row + 1),   # down
        wall_at(grid, col - 1, row),        # left
        wall_at(grid, col + 1, row),        # right
        wall_at(grid, col,     row - 2),   # up 2
        wall_at(grid, col,     row + 2),   # down 2
        wall_at(grid, col - 2, row),        # left 2
        wall_at(grid, col + 2, row),        # right 2
    ], dtype=np.float32)


# ── main loop ─────────────────────────────────────────────────────────────────

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    model = load_model()

    conn = serial.Serial(port, 115200, timeout=0.5)
    print(f"Connected to {port}. Waiting for game state packets…\n")

    packet_count = 0
    t_start = time.time()

    while True:
        pkt = read_packet(conn)
        if pkt is None:
            continue

        packet_count += 1
        state = state_from_packet(pkt)

        if model is not None:
            with torch.no_grad():
                s_t    = torch.FloatTensor(state).unsqueeze(0)
                action = int(model(s_t).argmax(dim=1).item())
        else:
            action = random.randint(0, N_ACTIONS - 1)

        send_move(conn, action)

        if packet_count % 20 == 0:
            elapsed = time.time() - t_start
            pps = packet_count / elapsed
            print(
                f"[{packet_count:6d} pkts | {pps:5.1f} pkt/s] "
                f"pos:({pkt['px']:3d},{pkt['py']:3d})  "
                f"target:({pkt['tcol']},{pkt['trow']})  "
                f"map:{pkt['map']}  action:{action}"
            )


if __name__ == "__main__":
    main()