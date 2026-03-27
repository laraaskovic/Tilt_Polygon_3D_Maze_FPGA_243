# astar_solver.py
# A* shortest path solver
# Returns a list of actions: 0=up,1=down,2=left,3=right

import heapq
from maze_env import COLS, ROWS

DC = [0, 0, -1, 1]
DR = [-1, 1, 0, 0]

def heuristic(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])

def compute_astar_path(grid, start, target):
    pq = []
    heapq.heappush(pq, (0, start))

    came_from = {}
    cost = {start: 0}

    while pq:
        _, current = heapq.heappop(pq)

        if current == target:
            break

        for a in range(4):
            nc = current[0] + DC[a]
            nr = current[1] + DR[a]

            if 0 <= nc < COLS and 0 <= nr < ROWS and grid[nr][nc] == 0:
                nxt = (nc, nr)
                new_cost = cost[current] + 1

                if nxt not in cost or new_cost < cost[nxt]:
                    cost[nxt] = new_cost
                    priority = new_cost + heuristic(nxt, target)
                    heapq.heappush(pq, (priority, nxt))
                    came_from[nxt] = (current, a)

    path = []
    cur = target

    while cur != start:
        if cur not in came_from:
            return []
        prev, action = came_from[cur]
        path.append(action)
        cur = prev

    path.reverse()
    return path