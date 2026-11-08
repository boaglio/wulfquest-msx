package wulfmsx.convert;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.PriorityQueue;
import wulf.Content;
import wulf.data.LandmarksData;
import wulf.data.PlayerData;
import wulf.engine.Fixed;
import wulf.sim.Player;
import wulf.sim.Simulation;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * A winning route for test/moves.sh's quest plan (AGENTS.md §10, M7): the
 * whole quest, every quarter taken and out through the arch, as keys a tick.
 *
 * <p>A bot plays the Java game's own {@link Simulation} with the quest on and
 * the Java game's infiniteLives (the ROM's g_TestEndless): a death costs time,
 * not the run. It walks to the nearest lair's pedestal until it holds every
 * quarter, then to the arch. The way is found on the map's cell blocks, as
 * {@link Route} finds it, with turns made dear; it is found again after each
 * death, each quarter, and whenever Vale stops getting nearer his next corner.
 * The keys are what the ROM replays; the trace they make is the Java game's.
 * It is a test's route, not good play: Vale never fights.
 */
final class QuestRoute {

    private static final int TURN_COST = 6;
    private static final int STUCK_TICKS = 150;
    private static final int MAX_TICKS = 90_000;
    private static final int[][] STEPS = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    private QuestRoute() {
    }

    static List<Integer> keys(Content c, WorldGrid grid, MoveTraces.Scenario s) {
        return keys(c, grid, s, 0, 0);
    }

    /**
     * The same route, as far as {@code limit} ticks (0: to the win) or game over, a press of
     * fire every {@code swingEvery} ticks (0: none) while he walks: the attract demo's (M10).
     */
    static List<Integer> keys(Content c, WorldGrid grid, MoveTraces.Scenario s, int limit, int swingEvery) {
        Simulation sim = MoveTraces.simulation(c, grid, s);
        PlayerData rules = c.player();
        PlayerData.Box box = rules.collisionBox();
        int cell = Simulation.CELL_PX;
        int inX = (2 * cell - box.w()) / 2 - box.x();
        int inY = (2 * cell - box.h()) / 2 - box.y();
        LandmarksData marks = sim.landmarks();
        List<Integer> keys = new ArrayList<>();
        List<int[]> corners = null;
        int on = 0;
        int sinceProgress = 0;
        int best = Integer.MAX_VALUE;
        int heldBefore = -1;
        Player.Mode modeBefore = sim.player().mode();
        while (sim.player().mode() != Player.Mode.WON) {
            if (limit > 0 && (keys.size() >= limit || sim.player().mode() == Player.Mode.GAME_OVER)) {
                return keys;
            }
            if (keys.size() > MAX_TICKS) {
                throw new ConvertException("quest route " + s.name() + ": not won in " + MAX_TICKS + " ticks; it holds "
                        + sim.quest().piecesHeld() + " quarters, in room " + sim.room());
            }
            Player p = sim.player();
            int held = sim.quest().piecesHeld();
            boolean back = modeBefore != Player.Mode.ALIVE && p.mode() == Player.Mode.ALIVE;
            modeBefore = p.mode();
            if (p.mode() != Player.Mode.ALIVE || sim.freezeRemaining() > 0) {
                step(sim, keys, 0);
                continue;
            }
            if (corners == null || back || held != heldBefore || sinceProgress > STUCK_TICKS) {
                int[] goal = goal(sim, marks, box);
                corners = path(grid, box, cell, inX, inY, Fixed.px(p.xFp()), Fixed.px(p.yFp()), goal);
                corners.add(goal);
                on = 0;
                sinceProgress = 0;
                best = Integer.MAX_VALUE;
                heldBefore = held;
            }
            int[] w = corners.get(on);
            int dx = w[0] - Fixed.px(p.xFp());
            int dy = w[1] - Fixed.px(p.yFp());
            int step = rules.momentum().step();
            if (Math.abs(dx) <= 1 && Math.abs(dy) <= 1 && Math.abs(p.vx()) < step && Math.abs(p.vy()) < step) {
                if (on + 1 < corners.size()) {
                    on++;
                    best = Integer.MAX_VALUE;
                    sinceProgress = 0;
                    continue;
                }
                // At the goal and nothing taken (a quarter's box missed): look again.
                sinceProgress = STUCK_TICKS + 1;
            }
            int d = Math.abs(dx) + Math.abs(dy);
            if (d < best) {
                best = d;
                sinceProgress = 0;
            } else {
                sinceProgress++;
            }
            int fire = swingEvery > 0 && keys.size() % swingEvery == 0 ? 16 : 0;
            step(sim, keys, fire | Route.axis(rules.momentum(), dx, p.vx(), 8, 4)
                    | Route.axis(rules.momentum(), dy, p.vy(), 2, 1));
        }
        for (int i = 0; i < 25; i++) {
            step(sim, keys, 0);
        }
        return keys;
    }

    private static void step(Simulation sim, List<Integer> keys, int k) {
        keys.add(k);
        sim.tick(MoveTraces.input(k, false));
    }

    /** The feet to make for, world pixels: the nearest quarter still lying, or the way out. */
    private static int[] goal(Simulation sim, LandmarksData marks, PlayerData.Box box) {
        RoomAddress here = sim.room();
        int bestLair = -1;
        int bestDistance = Integer.MAX_VALUE;
        for (int i = 0; i < marks.lairs().size(); i++) {
            if (sim.quest().taken(i)) {
                continue;
            }
            RoomAddress r = LandmarksData.room(marks.lairs().get(i).room());
            int d = Math.abs(r.col() - here.col()) + Math.abs(r.row() - here.row());
            if (d < bestDistance) {
                bestDistance = d;
                bestLair = i;
            }
        }
        if (bestLair >= 0) {
            LandmarksData.Lair lair = marks.lairs().get(bestLair);
            RoomAddress r = LandmarksData.room(lair.room());
            return new int[] {r.col() * Simulation.ROOM_W_PX + lair.pedestal().x(),
                r.row() * Simulation.ROOM_H_PX + lair.pedestal().y()};
        }
        LandmarksData.Rect zone = marks.exit().zone();
        RoomAddress exit = marks.exitRoom();
        // Inside the zone, the box's top a few pixels above its foot.
        return new int[] {exit.col() * Simulation.ROOM_W_PX + zone.x() + zone.w() / 2,
            exit.row() * Simulation.ROOM_H_PX + zone.y() + zone.h() - 4 - box.y()};
    }

    /** The corners of a way from the feet (x, y) to the block nearest the goal, as feet positions. */
    private static List<int[]> path(WorldGrid grid, PlayerData.Box box, int cell, int inX, int inY, int x, int y,
            int[] goal) {
        int[] from = nearestBlock(grid, Math.floorDiv(x + box.x(), cell), Math.floorDiv(y + box.y(), cell));
        int[] to = nearestBlock(grid, Math.floorDiv(goal[0] - inX, cell), Math.floorDiv(goal[1] - inY, cell));
        int cols = WorldGrid.COLS;
        int cells = cols * WorldGrid.ROWS;
        int[] cost = new int[cells * 4];
        int[] came = new int[cells * 4];
        Arrays.fill(cost, Integer.MAX_VALUE);
        PriorityQueue<long[]> queue = new PriorityQueue<>((a, b) -> Long.compare(a[0], b[0]));
        for (int d = 0; d < 4; d++) {
            int s = (from[1] * cols + from[0]) * 4 + d;
            cost[s] = 0;
            came[s] = -1;
            queue.add(new long[] {0, s});
        }
        int end = -1;
        while (!queue.isEmpty()) {
            long[] at = queue.poll();
            int s = (int) at[1];
            if (at[0] > cost[s]) {
                continue;
            }
            int i = s / 4;
            if (i % cols == to[0] && i / cols == to[1]) {
                end = s;
                break;
            }
            for (int d = 0; d < 4; d++) {
                int nx = i % cols + STEPS[d][0];
                int ny = i / cols + STEPS[d][1];
                if (nx < 0 || ny < 0 || nx >= cols || ny >= WorldGrid.ROWS || !grid.blockPassable(nx, ny)) {
                    continue;
                }
                int n = (ny * cols + nx) * 4 + d;
                int nc = cost[s] + 1 + (d == s % 4 ? 0 : TURN_COST);
                if (nc < cost[n]) {
                    cost[n] = nc;
                    came[n] = s;
                    queue.add(new long[] {nc, n});
                }
            }
        }
        if (end < 0) {
            throw new ConvertException("quest route: no way from block " + from[0] + "," + from[1] + " to "
                    + to[0] + "," + to[1]);
        }
        List<int[]> blocks = new ArrayList<>();
        for (int s = end; s >= 0; s = came[s]) {
            blocks.add(0, new int[] {(s / 4) % cols, (s / 4) / cols});
        }
        List<int[]> out = new ArrayList<>();
        out.add(new int[] {from[0] * cell + inX, from[1] * cell + inY});
        for (int[] b : Route.corners(blocks)) {
            out.add(new int[] {b[0] * cell + inX, b[1] * cell + inY});
        }
        return out;
    }

    /** The passable block nearest (gx, gy), by rings. */
    private static int[] nearestBlock(WorldGrid grid, int gx, int gy) {
        for (int r = 0; r < 8; r++) {
            for (int dy = -r; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (Math.max(Math.abs(dx), Math.abs(dy)) == r && grid.blockPassable(gx + dx, gy + dy)) {
                        return new int[] {gx + dx, gy + dy};
                    }
                }
            }
        }
        throw new ConvertException("quest route: no free block near " + gx + "," + gy);
    }
}
