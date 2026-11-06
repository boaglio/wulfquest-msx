package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import java.util.PriorityQueue;
import wulf.Content;
import wulf.data.PlayerData;
import wulf.engine.Fixed;
import wulf.sim.Simulation;
import wulf.tools.MapAudit;
import wulf.world.CollisionMask;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * A long walk for test/moves.sh (AGENTS.md §10, M4): from a start, to a room
 * at least so many rooms away, as keys a tick.
 *
 * <p>The path is found on the map's cells at the player's clearance (the
 * blocks {@link WorldGrid#blockPassable} lets the feet box into), with turns
 * made dear so it runs straight where it can. Then the Java game's own
 * {@link Simulation} walks it: each tick the keys are chosen from where Vale
 * is and how fast he is going, braking against his momentum (§11.3) to stop
 * at each corner. The keys are what the ROM replays; the trace they make is
 * the Java game's, as for every other scenario.
 */
record Route(List<Integer> keys, RoomAddress target) {

    private static final int TURN_COST = 6;
    private static final int STUCK_TICKS = 400;
    private static final int[][] STEPS = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    static Route plan(Content c, WorldGrid grid, int xFp, int yFp, int rooms) {
        PlayerData rules = c.player();
        PlayerData.Box box = rules.collisionBox();
        int cell = Simulation.CELL_PX;
        int left = Fixed.px(xFp) + box.x();
        int top = Fixed.px(yFp) + box.y();
        int gx0 = Math.floorDiv(left, cell);
        int gy0 = Math.floorDiv(top, cell);
        if (!grid.blockPassable(gx0, gy0) || left - gx0 * cell + box.w() > 2 * cell
                || top - gy0 * cell + box.h() > 2 * cell) {
            throw new ConvertException("route: the start " + Fixed.px(xFp) + "," + Fixed.px(yFp)
                    + " is not in a clear block of cells");
        }
        RoomAddress from = roomOf(box, Fixed.px(xFp), Fixed.px(yFp));
        int[] distance = MapAudit.roomDistances(grid, from);
        RoomAddress target = null;
        for (int i = 0; i < distance.length && target == null; i++) {
            if (distance[i] == rooms) {
                target = new RoomAddress(i % RoomAddress.GRID_W, i / RoomAddress.GRID_W);
            }
        }
        if (target == null) {
            throw new ConvertException("route: no room is " + rooms + " rooms from " + from);
        }

        // Where the feet are with the box centred in a block.
        int inX = (2 * cell - box.w()) / 2 - box.x();
        int inY = (2 * cell - box.h()) / 2 - box.y();
        List<int[]> corners = new ArrayList<>();
        corners.add(new int[] {gx0 * cell + inX, gy0 * cell + inY});
        for (int[] b : corners(path(grid, gx0, gy0, target, box, inX, inY))) {
            corners.add(new int[] {b[0] * cell + inX, b[1] * cell + inY});
        }

        Simulation sim = Simulation.at(rules, grid, c.game().transition().freezeTicks(), xFp, yFp);
        List<Integer> keys = new ArrayList<>();
        int on = 0;
        int ticks = 0;
        while (on < corners.size()) {
            int[] w = corners.get(on);
            int dx = w[0] - Fixed.px(sim.player().xFp());
            int dy = w[1] - Fixed.px(sim.player().yFp());
            int vx = sim.player().vx();
            int vy = sim.player().vy();
            int step = rules.momentum().step();
            if (Math.abs(dx) <= 1 && Math.abs(dy) <= 1 && Math.abs(vx) < step && Math.abs(vy) < step
                    && sim.freezeRemaining() == 0) {
                on++;
                ticks = 0;
                continue;
            }
            if (++ticks > STUCK_TICKS) {
                throw new ConvertException("route: stuck on the way to " + w[0] + "," + w[1] + " at "
                        + Fixed.px(sim.player().xFp()) + "," + Fixed.px(sim.player().yFp()));
            }
            int k = axis(rules.momentum(), dx, vx, 8, 4) | axis(rules.momentum(), dy, vy, 2, 1);
            keys.add(k);
            sim.tick(MoveTraces.input(k, false));
        }
        if (!sim.room().equals(target)) {
            throw new ConvertException("route: ends in " + sim.room() + ", not " + target);
        }
        for (int i = 0; i < 20; i++) {
            keys.add(0);
        }
        return new Route(List.copyOf(keys), target);
    }

    static RoomAddress roomOf(PlayerData.Box box, int x, int y) {
        return new RoomAddress(Math.floorDiv(x + box.x() + box.w() / 2, Simulation.ROOM_W_PX),
                Math.floorDiv(y + box.y() + box.h() / 2, Simulation.ROOM_H_PX));
    }

    /**
     * One axis: the key toward the target ({@code plus} or {@code minus}), or the
     * other way to brake when he would glide past it, or none when there.
     */
    static int axis(PlayerData.Momentum m, int d, int v, int plus, int minus) {
        int level = v / m.step();
        int toward = d > 0 ? plus : minus;
        int against = v > 0 ? minus : plus;
        if (Math.abs(d) <= 1) {
            return level != 0 ? against : 0;
        }
        if (level != 0 && (level > 0) == (d > 0)) {
            return brakingDistance(m, v) >= Math.abs(d) ? against : toward;
        }
        return toward;
    }

    /** Pixels he still goes, the other way held from now (§11.3: a speed level is a pixel a tick). */
    static int brakingDistance(PlayerData.Momentum m, int v) {
        int sign = v > 0 ? 1 : -1;
        int px = 0;
        for (int i = 0; i < 64; i++) {
            v = Math.max(-m.max(), Math.min(m.max(), v - sign * m.accel()));
            int level = v / m.step();
            if (level == 0 || (level > 0) != (sign > 0)) {
                break;
            }
            px += Math.abs(level);
            v = v > 0 ? Math.max(0, v - m.decay()) : Math.min(0, v + m.decay());
        }
        return px;
    }

    /** The blocks from (gx0, gy0) to the first one in the target room, fewest steps and turns. */
    static List<int[]> path(WorldGrid grid, int gx0, int gy0, RoomAddress target, PlayerData.Box box,
            int inX, int inY) {
        int cols = WorldGrid.COLS;
        int cells = cols * WorldGrid.ROWS;
        int[] cost = new int[cells * 4];
        int[] came = new int[cells * 4];
        java.util.Arrays.fill(cost, Integer.MAX_VALUE);
        PriorityQueue<long[]> queue = new PriorityQueue<>((a, b) -> Long.compare(a[0], b[0]));
        for (int d = 0; d < 4; d++) {
            int s = (gy0 * cols + gx0) * 4 + d;
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
            int x = i % cols;
            int y = i / cols;
            if (roomOf(box, x * Simulation.CELL_PX + inX, y * Simulation.CELL_PX + inY).equals(target)) {
                end = s;
                break;
            }
            for (int d = 0; d < 4; d++) {
                int nx = x + STEPS[d][0];
                int ny = y + STEPS[d][1];
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
            throw new ConvertException("route: no way to " + target);
        }
        List<int[]> blocks = new ArrayList<>();
        for (int s = end; s >= 0; s = came[s]) {
            blocks.add(0, new int[] {(s / 4) % cols, (s / 4) / cols});
        }
        return blocks;
    }

    /** The blocks where the path turns, and its last. */
    static List<int[]> corners(List<int[]> blocks) {
        List<int[]> out = new ArrayList<>();
        for (int i = 1; i < blocks.size(); i++) {
            int[] a = blocks.get(i - 1);
            int[] b = blocks.get(i);
            boolean last = i == blocks.size() - 1;
            if (last) {
                out.add(b);
                break;
            }
            int[] n = blocks.get(i + 1);
            if (b[0] - a[0] != n[0] - b[0] || b[1] - a[1] != n[1] - b[1]) {
                out.add(b);
            }
        }
        return out;
    }
}
