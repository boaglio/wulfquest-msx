package wulfmsx.convert;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.PriorityQueue;
import wulf.data.LandmarksData;
import wulf.data.PlayerData;
import wulf.sim.Simulation;
import wulf.world.CollisionMask;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * The looking glass's trail on the map (AGENTS.md §10, M12; the Java game's
 * {@code WayPainter.route} and {@code MapScreen.paintTrail}), as the ROM finds it.
 *
 * <p>The Java game floods the whole jungle, 512x352 cells, from the quarters still out
 * there, and follows the flood down from Vale's feet. The Z80 has neither the RAM nor the
 * time for that. So the converter cuts the jungle into its rooms' <em>openings</em>: a run
 * of cells along a room's edge where the feet fit on both sides (468 of them, 936 sides, at
 * most six a room), and for each room the steps between its openings' sides, the least
 * over their cells, within the room. The ROM then works out each side's steps to the
 * quarters (D: the least, over the room's sides, of the steps to it, one across, and the
 * other side's D), and goes room by room from Vale: each room flooded within itself from its
 * sides, each cell of a side starting at one across plus the other side's D (and from the
 * quarters there, at 0), followed down from where he stands, or came in, the Java game's way
 * (its neighbours' order), and across where it ends. The side he came in by is left out, so
 * an opening whose cells differ in steps (they share one D) cannot send him back.
 *
 * <p>This class builds the openings and their steps (the {@code trail_N.bin} pieces, cut to
 * fit the space the other blobs leave) and is the reference of the ROM's search, step for
 * step as {@code src/trail.c}, for the tests to hold the ROM to.
 */
final class Trail {

    static final int COLS = CollisionMask.COLS;
    static final int ROWS = CollisionMask.ROWS;
    static final int ROOMS = RoomAddress.GRID_W * RoomAddress.GRID_H;
    /** WayPainter.NEAR_CELLS: how far from a point to look for somewhere the feet fit. */
    static final int NEAR = 3;
    /** The most sides a room may have: the ROM's buffers. */
    static final int MAX_SIDES = 6;
    /** The trail's pixels the ROM keeps (TrailTest: the longest is within it). */
    static final int MAX_PX = 2176;
    /** A piece's offsets are halved into a byte. */
    static final int PIECE_MAX = 510;
    /** Steps unknown, or no way. */
    static final int NONE = 0xFFFF;
    /** A seed that is a quarter, or the arch: its kind in the flood's cells (a side's is its index + 1). */
    static final int KIND_TARGET = 7;

    private static final int[] DX = {1, -1, 0, 0};
    private static final int[] DY = {0, 0, 1, -1};

    /** A side of an opening: its room, its edge (0 n, 1 e, 2 s, 3 w), its first cell along the edge, its cells. */
    record Side(int room, int edge, int start, int len) {
        int[] cell(int i) {
            int a = start + i;
            return edge == 0 ? new int[] {a, 0} : edge == 1 ? new int[] {COLS - 1, a}
                    : edge == 2 ? new int[] {a, ROWS - 1} : new int[] {0, a};
        }
    }

    private final WorldGrid grid;
    private final LandmarksData marks;
    private final PlayerData.Box feet;
    final List<Side> sides = new ArrayList<>();
    /** Each room's first side; first[ROOMS] is the count. */
    final int[] first = new int[ROOMS + 1];
    final int[] partner;
    /** Each room's steps between its sides, i * k + j; NONE where there is no way. */
    final int[][] steps = new int[ROOMS][];
    int maxSteps;

    Trail(WorldGrid grid, LandmarksData marks, PlayerData.Box feet) {
        this.grid = grid;
        this.marks = marks;
        this.feet = feet;
        for (int room = 0; room < ROOMS; room++) {
            first[room] = sides.size();
            for (int edge = 0; edge < 4; edge++) {
                sides.addAll(runs(room, edge));
            }
            if (sides.size() - first[room] > MAX_SIDES) {
                throw new ConvertException("room " + room + " has " + (sides.size() - first[room])
                        + " openings; the ROM's trail takes " + MAX_SIDES);
            }
        }
        first[ROOMS] = sides.size();
        partner = new int[sides.size()];
        for (int s = 0; s < sides.size(); s++) {
            Side a = sides.get(s);
            int other = neighbour(a.room(), a.edge());
            partner[s] = -1;
            for (int t = first[other]; t < first[other + 1]; t++) {
                Side b = sides.get(t);
                if (b.edge() == (a.edge() + 2) % 4 && b.start() == a.start() && b.len() == a.len()) {
                    partner[s] = t;
                }
            }
            if (partner[s] < 0) {
                throw new ConvertException("an opening of room " + a.room() + " has no other side");
            }
        }
        for (int room = 0; room < ROOMS; room++) {
            int k = first[room + 1] - first[room];
            steps[room] = new int[k * k];
            for (int i = 0; i < k; i++) {
                List<int[]> seeds = new ArrayList<>();
                Side a = sides.get(first[room] + i);
                for (int c = 0; c < a.len(); c++) {
                    int[] xy = a.cell(c);
                    seeds.add(new int[] {xy[0], xy[1], 0, 0});
                }
                Flood f = flood(room, seeds);
                for (int j = 0; j < k; j++) {
                    Side b = sides.get(first[room] + j);
                    int best = NONE;
                    for (int c = 0; c < b.len(); c++) {
                        int[] xy = b.cell(c);
                        int v = f.dist[xy[1] * COLS + xy[0]];
                        best = v >= 0 ? Math.min(best, v) : best;
                    }
                    steps[room][i * k + j] = best;
                    if (best != NONE) {
                        maxSteps = Math.max(maxSteps, best);
                    }
                }
            }
        }
        if (maxSteps > 254) {
            throw new ConvertException("a room's openings are " + maxSteps + " steps apart; the ROM keeps a byte");
        }
    }

    static int neighbour(int room, int edge) {
        return edge == 0 ? room - RoomAddress.GRID_W : edge == 1 ? room + 1 : edge == 2 ? room + RoomAddress.GRID_W
                : room - 1;
    }

    /** Whether the feet fit at a cell of the room, its edges' neighbours included (WorldGrid.blockPassable). */
    boolean fits(int room, int x, int y) {
        return grid.blockPassable((room % RoomAddress.GRID_W) * COLS + x, (room / RoomAddress.GRID_W) * ROWS + y);
    }

    /** The openings along an edge of the room: runs of its edge's cells the feet fit in, and the next room's too. */
    private List<Side> runs(int room, int edge) {
        List<Side> out = new ArrayList<>();
        int col = room % RoomAddress.GRID_W;
        int row = room / RoomAddress.GRID_W;
        if ((edge == 0 && row == 0) || (edge == 1 && col == RoomAddress.GRID_W - 1)
                || (edge == 2 && row == RoomAddress.GRID_H - 1) || (edge == 3 && col == 0)) {
            return out;
        }
        int n = edge == 0 || edge == 2 ? COLS : ROWS;
        int start = -1;
        for (int a = 0; a <= n; a++) {
            boolean open = false;
            if (a < n) {
                int[] in = new Side(room, edge, a, 1).cell(0);
                int ox = in[0] + (edge == 1 ? 1 : edge == 3 ? -1 : 0);
                int oy = in[1] + (edge == 2 ? 1 : edge == 0 ? -1 : 0);
                open = fits(room, in[0], in[1]) && fits(room, ox, oy);
            }
            if (open && start < 0) {
                start = a;
            }
            if (!open && start >= 0) {
                out.add(new Side(room, edge, start, a - start));
                start = -1;
            }
        }
        return out;
    }

    // ------------------------------------------------------------------ the ROM's pieces

    /**
     * The openings in pieces, each at most the size of a space left in the ROM's segments
     * (in that order): each piece its rooms' record offsets (halved, a byte each, padded to
     * even), then the records: the room's side count, its first side (2 bytes), each side's
     * other side (10 bits) and edge (bits 12-13 of the two bytes), the steps between its sides
     * (i < j, a byte each, 255 none), padded to even.
     */
    List<byte[]> pieces(List<Integer> holes) {
        List<byte[]> records = new ArrayList<>();
        for (int room = 0; room < ROOMS; room++) {
            records.add(record(room));
        }
        List<byte[]> out = new ArrayList<>();
        room0.clear();
        int room = 0;
        int h = 0;
        while (room < ROOMS) {
            if (h == holes.size()) {
                throw new ConvertException("the trail's openings do not fit the space the ROM's segments leave");
            }
            int limit = Math.min(PIECE_MAX, holes.get(h++));
            int n = 0;
            while (room + n < ROOMS && size(records, room, n + 1) <= limit) {
                n++;
            }
            if (n == 0) {
                continue;   // too small a space
            }
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            int head = (n + 1) & ~1;
            int at = head;
            for (int i = 0; i < n; i++) {
                b.write(at >> 1);
                at += records.get(room + i).length;
            }
            if ((n & 1) != 0) {
                b.write(0);
            }
            for (int i = 0; i < n; i++) {
                b.writeBytes(records.get(room + i));
            }
            out.add(b.toByteArray());
            room0.add(room);
            room += n;
        }
        return out;
    }

    /** The first room of each piece, after {@link #pieces}. */
    final List<Integer> room0 = new ArrayList<>();

    private static int size(List<byte[]> records, int from, int n) {
        int s = (n + 1) & ~1;
        for (int i = 0; i < n; i++) {
            s += records.get(from + i).length;
        }
        return s;
    }

    private byte[] record(int room) {
        int k = first[room + 1] - first[room];
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        b.write(k);
        b.write(first[room] & 0xFF);
        b.write(first[room] >> 8);
        for (int i = 0; i < k; i++) {
            int s = first[room] + i;
            int p = partner[s];
            b.write(p & 0xFF);
            b.write(p >> 8 | sides.get(s).edge() << 4);
        }
        for (int i = 0; i < k; i++) {
            for (int j = i + 1; j < k; j++) {
                int v = steps[room][i * k + j];
                b.write(v == NONE ? 255 : v);
            }
        }
        if ((b.size() & 1) != 0) {
            b.write(0);
        }
        return b.toByteArray();
    }

    /** The biggest record, for the ROM's buffer. */
    int recordMax() {
        int m = 0;
        for (int room = 0; room < ROOMS; room++) {
            m = Math.max(m, record(room).length);
        }
        return m;
    }

    /** trail_data.h, after {@link #pieces}. */
    String header() {
        StringBuilder h = new StringBuilder(Gen.banner("data/world/rooms (the rooms' openings, for the glass's trail)"));
        h.append("#pragma once\n\n");
        h.append("// The looking glass's trail on the map (Trail.java, trail.c): the rooms' openings, their\n");
        h.append("// sides in each room (n, e, s, w, along the edge), the steps between them, in trail_N.bin.\n");
        h.append("#define TRAIL_SIDES\t").append(sides.size()).append('\n');
        h.append("#define TRAIL_SIDES_MAX\t").append(MAX_SIDES).append('\n');
        h.append("#define TRAIL_RUN_MAX\t").append(sides.stream().mapToInt(Side::len).max().orElse(0)).append('\n');
        h.append("#define TRAIL_RECORD_MAX\t").append(recordMax()).append('\n');
        h.append("#define TRAIL_NEAR\t").append(NEAR).append('\n');
        h.append("#define TRAIL_MAX_PX\t").append(MAX_PX).append('\n');
        h.append("#define TRAIL_NONE\t0x").append(Integer.toHexString(NONE).toUpperCase()).append('\n');
        h.append("#define TRAIL_KIND_TARGET\t").append(KIND_TARGET).append('\n');
        h.append("// The pieces (rom.h's g_TrailPieceSeg, g_TrailPieceRel) and the first room of each.\n");
        h.append("#define TRAIL_PIECES\t").append(room0.size()).append('\n');
        h.append("#define TRAIL_ROOM0\t");
        for (int i = 0; i < room0.size(); i++) {
            h.append(i == 0 ? "" : ", ").append(room0.get(i));
        }
        h.append('\n');
        return h.toString();
    }

    // ------------------------------------------------------------------ the ROM's search

    /** The cells of the quarters still out there in the room (the lairs' order), or the arch's mouth, room-local. */
    List<int[]> targets(int set, int done, int room) {
        LandmarksData placed = marks.withAmuletSet(set);
        List<int[]> out = new ArrayList<>();
        int lairs = placed.lairs().size();
        for (int i = 0; i < lairs; i++) {
            LandmarksData.Lair l = placed.lairs().get(i);
            if ((done & 1 << i) == 0 && LandmarksData.room(l.room()).index() == room) {
                int[] c = nearest(room, null, Math.floorDiv(l.pedestal().x() + feet.x(), Simulation.CELL_PX),
                        Math.floorDiv(l.pedestal().y() + feet.y(), Simulation.CELL_PX));
                if (c != null) {
                    out.add(c);
                }
            }
        }
        if (done == (1 << lairs) - 1 && placed.exitRoom().index() == room) {
            LandmarksData.Rect z = placed.exit().zone();
            int[] c = nearest(room, null, Math.floorDiv(z.x() + z.w() / 2 + feet.x(), Simulation.CELL_PX),
                    Math.floorDiv(z.y() + z.h() + feet.y(), Simulation.CELL_PX));
            if (c != null) {
                out.add(c);
            }
        }
        return out;
    }

    /** Wayfinder.nearest within the room: by steps on both axes, the first in rows, then columns; reached, given a flood. */
    int[] nearest(int room, Flood f, int lx, int ly) {
        int[] best = null;
        int bestD = Integer.MAX_VALUE;
        for (int y = ly - NEAR; y <= ly + NEAR; y++) {
            for (int x = lx - NEAR; x <= lx + NEAR; x++) {
                if (x < 0 || y < 0 || x >= COLS || y >= ROWS || !fits(room, x, y)) {
                    continue;
                }
                if (f != null && f.dist[y * COLS + x] < 0) {
                    continue;
                }
                int d = Math.abs(x - lx) + Math.abs(y - ly);
                if (d < bestD) {
                    bestD = d;
                    best = new int[] {x, y};
                }
            }
        }
        return best;
    }

    /** A room's flood: each cell's steps (-1 where not reached), and the seed that started it (its kind). */
    record Flood(int[] dist, int[] kind) {
    }

    /**
     * The room flooded from its seeds ({x, y, steps, kind}), in steps' order (stable): the
     * seeds of a number of steps join as the flood comes to it; a cell is the first's to reach it.
     */
    Flood flood(int room, List<int[]> seeds) {
        List<int[]> sorted = new ArrayList<>(seeds);
        sorted.sort((a, b) -> Integer.compare(a[2], b[2]));
        int[] dist = new int[COLS * ROWS];
        int[] kind = new int[COLS * ROWS];
        Arrays.fill(dist, -1);
        int[] queue = new int[COLS * ROWS];
        int head = 0;
        int tail = 0;
        int si = 0;
        if (sorted.isEmpty()) {
            return new Flood(dist, kind);
        }
        int val = sorted.get(0)[2];
        while (true) {
            while (si < sorted.size() && sorted.get(si)[2] <= val) {
                int[] s = sorted.get(si++);
                int c = s[1] * COLS + s[0];
                if (dist[c] < 0 && fits(room, s[0], s[1])) {
                    dist[c] = val;
                    kind[c] = s[3];
                    queue[tail++] = c;
                }
            }
            if (head == tail) {
                if (si == sorted.size()) {
                    break;
                }
                val = sorted.get(si)[2];
                continue;
            }
            int end = tail;
            while (head < end) {
                int c = queue[head++];
                int x = c % COLS;
                int y = c / COLS;
                for (int k = 0; k < 4; k++) {
                    int nx = x + DX[k];
                    int ny = y + DY[k];
                    if (nx < 0 || ny < 0 || nx >= COLS || ny >= ROWS) {
                        continue;
                    }
                    int n = ny * COLS + nx;
                    if (dist[n] < 0 && fits(room, nx, ny)) {
                        dist[n] = val + 1;
                        queue[tail++] = n;
                    }
                }
            }
            val++;
        }
        return new Flood(dist, kind);
    }

    /** Each side's steps to the quarters (or the arch) for set and the lairs done: the ROM's D. */
    int[] distances(int set, int done) {
        int n = sides.size();
        int[] d = new int[n];
        Arrays.fill(d, NONE);
        PriorityQueue<int[]> pq = new PriorityQueue<>((a, b) -> Integer.compare(a[0], b[0]));
        for (int room = 0; room < ROOMS; room++) {
            List<int[]> t = targets(set, done, room);
            if (t.isEmpty()) {
                continue;
            }
            List<int[]> seeds = new ArrayList<>();
            for (int[] c : t) {
                seeds.add(new int[] {c[0], c[1], 0, KIND_TARGET});
            }
            Flood f = flood(room, seeds);
            for (int s = first[room]; s < first[room + 1]; s++) {
                Side a = sides.get(s);
                for (int c = 0; c < a.len(); c++) {
                    int[] xy = a.cell(c);
                    int v = f.dist[xy[1] * COLS + xy[0]];
                    if (v >= 0 && v < d[s]) {
                        d[s] = v;
                    }
                }
                if (d[s] != NONE) {
                    pq.add(new int[] {d[s], s});
                }
            }
        }
        // D[i] = min(its own, over j of the room: steps(i, j) + 1 + D[other side of j]).
        while (!pq.isEmpty()) {
            int[] e = pq.poll();
            int p = e[1];
            if (e[0] != d[p]) {
                continue;
            }
            int j = partner[p];
            int room = sides.get(j).room();
            int k = first[room + 1] - first[room];
            int jj = j - first[room];
            for (int i = 0; i < k; i++) {
                int w = i == jj ? 0 : steps[room][i * k + jj];
                if (w == NONE) {
                    continue;
                }
                int v = w + 1 + d[p];
                int s = first[room] + i;
                if (v < d[s]) {
                    d[s] = v;
                    pq.add(new int[] {v, s});
                }
            }
        }
        return d;
    }

    /**
     * The trail's cells, global (gx, gy), from the feet of Vale at room-global pixel (x, y)
     * in {@code room}; empty when there is none. {@code limit}: the most cells it follows.
     */
    List<int[]> route(int set, int done, int room, int x, int y, int limit) {
        whole = false;
        int[] d = distances(set, done);
        List<int[]> out = new ArrayList<>();
        int col = room % RoomAddress.GRID_W;
        int row = room / RoomAddress.GRID_W;
        int sx = Math.floorDiv(x + feet.x(), Simulation.CELL_PX) - col * COLS;
        int sy = Math.floorDiv(y + feet.y(), Simulation.CELL_PX) - row * ROWS;
        boolean start = true;
        int entered = -1;
        for (int hop = 0; hop < 256; hop++) {
            int ox = (room % RoomAddress.GRID_W) * COLS;
            int oy = (room / RoomAddress.GRID_W) * ROWS;
            List<int[]> seeds = new ArrayList<>();
            for (int[] c : targets(set, done, room)) {
                seeds.add(new int[] {c[0], c[1], 0, KIND_TARGET});
            }
            for (int s = first[room]; s < first[room + 1]; s++) {
                int p = partner[s];
                if (s == entered || d[p] == NONE) {
                    continue;
                }
                Side a = sides.get(s);
                for (int c = 0; c < a.len(); c++) {
                    int[] xy = a.cell(c);
                    seeds.add(new int[] {xy[0], xy[1], d[p] + 1, s - first[room] + 1});
                }
            }
            Flood f = flood(room, seeds);
            if (start) {
                int[] c = nearest(room, f, sx, sy);
                if (c == null) {
                    return out;
                }
                sx = c[0];
                sy = c[1];
                start = false;
            } else if (f.dist[sy * COLS + sx] < 0) {
                return out;
            }
            while (true) {
                if (out.size() == limit) {
                    return out;
                }
                out.add(new int[] {ox + sx, oy + sy});
                int v = f.dist[sy * COLS + sx];
                boolean moved = false;
                for (int k = 0; k < 4 && !moved; k++) {
                    int nx = sx + DX[k];
                    int ny = sy + DY[k];
                    if (nx >= 0 && ny >= 0 && nx < COLS && ny < ROWS && f.dist[ny * COLS + nx] == v - 1) {
                        sx = nx;
                        sy = ny;
                        moved = true;
                    }
                }
                if (!moved) {
                    break;
                }
            }
            int kind = f.kind[sy * COLS + sx];
            if (kind == KIND_TARGET || kind == 0) {
                whole = kind == KIND_TARGET;
                return out;
            }
            int s = first[room] + kind - 1;
            int edge = sides.get(s).edge();
            sx += edge == 1 ? 1 : edge == 3 ? -1 : 0;
            sy += edge == 2 ? 1 : edge == 0 ? -1 : 0;
            sx = Math.floorMod(sx, COLS);
            sy = Math.floorMod(sy, ROWS);
            entered = partner[s];
            room = neighbour(room, edge);
        }
        return out;
    }

    /** Whether the last {@link #route} got to a quarter, or the arch: MapScreen's cross. */
    boolean whole;

    /** The trail's pixels on the map (a pixel for 2x2 cells), packed y * 256 + x, two cells one pixel. */
    static List<Integer> pixels(List<int[]> cells) {
        List<Integer> out = new ArrayList<>();
        for (int[] g : cells) {
            int u = g[0] * 16 / COLS;
            int v = g[1] * 11 / ROWS;
            int px = v * 256 + u;
            if (out.isEmpty() || out.get(out.size() - 1) != px) {
                out.add(px);
            }
        }
        return out;
    }
}
