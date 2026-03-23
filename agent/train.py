"""
train.py  —  DQN trainer for the FPGA maze agent
Run:  python train.py
Saves:  dqn_model.pth   (PyTorch weights, used by agent.py)
        train_log.txt   (reward log so you can plot progress)
"""

import random
import numpy as np
from collections import deque
import torch
import torch.nn as nn
import torch.optim as optim

import maze_env
from maze_env import MazeEnv, STATE_DIM, N_ACTIONS

# ── hyper-parameters ──────────────────────────────────────────────────────────
EPISODES        = 3_000
BATCH_SIZE      = 64
GAMMA           = 0.97
LR              = 1e-3
REPLAY_SIZE     = 20_000
TARGET_UPDATE   = 200
EPSILON_START   = 1.0
EPSILON_END     = 0.05
EPSILON_DECAY   = 0.9985
HIDDEN          = 64
SAVE_PATH       = "dqn_model.pth"
LOG_PATH        = "train_log.txt"
PRINT_EVERY     = 500


# ── network ───────────────────────────────────────────────────────────────────

class DQN(nn.Module):
    def __init__(self, state_dim, n_actions, hidden=HIDDEN):
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


# ── replay buffer ─────────────────────────────────────────────────────────────

class ReplayBuffer:
    def __init__(self, capacity):
        self.buf = deque(maxlen=capacity)

    def push(self, s, a, r, s2, done):
        self.buf.append((s, a, r, s2, done))

    def sample(self, n):
        batch = random.sample(self.buf, n)
        s, a, r, s2, d = zip(*batch)
        return (
            torch.FloatTensor(np.array(s)),
            torch.LongTensor(a),
            torch.FloatTensor(r),
            torch.FloatTensor(np.array(s2)),
            torch.FloatTensor(d),
        )

    def __len__(self):
        return len(self.buf)


# ── training loop ─────────────────────────────────────────────────────────────

def train():
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on {device}  |  state_dim={STATE_DIM}  n_actions={N_ACTIONS}")

    env        = MazeEnv()
    policy_net = DQN(STATE_DIM, N_ACTIONS).to(device)
    target_net = DQN(STATE_DIM, N_ACTIONS).to(device)
    target_net.load_state_dict(policy_net.state_dict())
    target_net.eval()

    optimizer  = optim.Adam(policy_net.parameters(), lr=LR)
    buffer     = ReplayBuffer(REPLAY_SIZE)
    epsilon    = EPSILON_START
    rewards_log = []

    log_file = open(LOG_PATH, "w")
    log_file.write("episode,total_reward,epsilon\n")

    for ep in range(EPISODES):
        state = env.reset()
        total_reward = 0.0

        while True:
            # ε-greedy action
            if random.random() < epsilon:
                action = random.randint(0, N_ACTIONS - 1)
            else:
                with torch.no_grad():
                    s_t = torch.FloatTensor(state).unsqueeze(0).to(device)
                    action = int(policy_net(s_t).argmax(dim=1).item())

            next_state, reward, done = env.step(action)
            buffer.push(state, action, reward, next_state, float(done))
            state = next_state
            total_reward += reward

            # learn once we have enough samples
            if len(buffer) >= BATCH_SIZE:
                s, a, r, s2, d = buffer.sample(BATCH_SIZE)
                s  = s.to(device);  a  = a.to(device)
                r  = r.to(device);  s2 = s2.to(device)
                d  = d.to(device)

                # current Q values
                q_vals = policy_net(s).gather(1, a.unsqueeze(1)).squeeze(1)

                # target Q values (Double DQN style)
                with torch.no_grad():
                    next_actions = policy_net(s2).argmax(dim=1)
                    next_q = target_net(s2).gather(1, next_actions.unsqueeze(1)).squeeze(1)
                    target_q = r + GAMMA * next_q * (1 - d)

                loss = nn.SmoothL1Loss()(q_vals, target_q)
                optimizer.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(policy_net.parameters(), 1.0)
                optimizer.step()

            if done:
                break

        epsilon = max(EPSILON_END, epsilon * EPSILON_DECAY)
        rewards_log.append(total_reward)
        log_file.write(f"{ep},{total_reward:.4f},{epsilon:.5f}\n")

        # copy to target net
        if ep % TARGET_UPDATE == 0 and ep > 0:
            target_net.load_state_dict(policy_net.state_dict())

        # per-episode live update (overwrites same line)
        window = rewards_log[-100:] if len(rewards_log) >= 100 else rewards_log
        avg  = np.mean(window)
        wins = sum(1 for r in window if r > 5.0)
        print(f"\rEp {ep:6d}/{EPISODES} | avg: {avg:7.2f} | "
              f"wins/100: {wins:3d} | ε: {epsilon:.4f} | "
              f"buf: {len(buffer):6d}", end="", flush=True)

        # full newline every PRINT_EVERY episodes
        if ep % PRINT_EVERY == 0 and ep > 0:
            print()

    log_file.close()
    torch.save(policy_net.state_dict(), SAVE_PATH)
    print(f"\nDone! Model saved to {SAVE_PATH}")
    print(f"Training log saved to {LOG_PATH}")


if __name__ == "__main__":
    train()