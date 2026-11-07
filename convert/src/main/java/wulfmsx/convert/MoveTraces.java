package wulfmsx.convert;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import wulf.Content;
import wulf.data.PlayerData;
import wulf.engine.Fixed;
import wulf.input.InputState;
import wulf.sim.Player;
import wulf.sim.PixelRect;
import wulf.data.FlameData;
import wulf.data.OrchidData;
import wulf.sim.Ecosystem;
import wulf.sim.QuestRules;
import wulf.sim.RoomPopulator;
import wulf.sim.Simulation;
import wulf.sim.WulfRules;
import wulf.tools.MapAudit;
import wulf.world.CollisionMask;
import wulf.world.CollisionWorld;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * The test oracle for M3 and M4 (AGENTS.md §10): Ranger Vale as the Java game
 * moves him, tick by tick, for test/moves.sh to compare with the MSX2.
 *
 * <pre>
 *   MoveTraces DATA_DIR SCENARIOS_JSON OUT_PREFIX
 * </pre>
 *
 * Each scenario in the JSON is a start and a list of keys held for so many
 * ticks. It runs in the Java game's own {@link Simulation}, with no creatures,
 * on the whole map, flips and their frozen ticks included; or, with
 * {@code "open": true}, in a world with only the map's edge solid and no
 * frozen ticks, where the movement numbers are measured. Two entries are
 * made here instead of written out:
 *
 * <ul>
 *   <li>{@code "route": N}: a walk from the start across at least N rooms,
 *       steered tick by tick along a path through the map ({@link Route});
 *   <li>{@code "seams": true}: one scenario per edge between two rooms that
 *       {@link MapAudit} finds the player's box fits through: Vale stands at
 *       the crossing, walks into the next room and back again.
 * </ul>
 *
 * Writes:
 *
 * <ul>
 *   <li>{@code OUT_PREFIX_plan.txt}, for test/moves.tcl: per scenario
 *       {@code scenario NAME ROOM OPEN X_FP Y_FP CHECK SEED}, then {@code keys} and
 *       a key byte a tick (up 1, down 2, left 4, right 8, fire 16);
 *   <li>{@code OUT_PREFIX_expected.txt}: per scenario {@code scenario NAME},
 *       then after each tick {@code X_FP Y_FP FACING SWING WALK SX SY SW SH ROOM FREEZE FRAME BLADE}:
 *       the feet, the facing (Direction8's order), the tick into the stroke
 *       (-1 none), ticks walked, the sabre's hit box (all 0 when none), the
 *       room (row * 16 + col), the frozen ticks left, the frame he is drawn in
 *       (player.h) and the blade's shape (blade.h; -1 none), as the Java
 *       game's PlayerPainter picks them; with the jungle ({@code "jungle": SEED}),
 *       a second line after it, {@link JungleChecks#line}.
 * </ul>
 */
public final class MoveTraces {

    private MoveTraces() {
    }

    /** A start, a key byte a tick, and what test/check_moves.py checks on top of equality ("-" nothing). */
    record Scenario(String name, RoomAddress room, boolean open, int xFp, int yFp, List<Integer> keys, String check,
                    long seed) {
        /** With the jungle: the swarm and the big beasts, seeded (else, -1, the empty jungle). */
        boolean jungle() {
            return seed >= 0;
        }
    }

    public static void main(String[] args) throws IOException {
        if (args.length != 3) {
            System.err.println("usage: MoveTraces DATA_DIR SCENARIOS_JSON OUT_PREFIX");
            System.exit(2);
        }
        Content c = Content.load(Path.of(args[0]));
        JsonNode list = JsonMapper.builder().build().readTree(Files.readString(Path.of(args[1])));
        WorldGrid grid = new WorldGrid(c.rooms());
        List<Scenario> scenarios = new ArrayList<>();
        for (JsonNode s : list.get("scenarios")) {
            if (s.has("seams")) {
                scenarios.addAll(seams(c, grid));
            } else {
                scenarios.add(scenario(c, grid, s));
            }
        }
        StringBuilder plan = new StringBuilder();
        StringBuilder expected = new StringBuilder();
        for (Scenario s : scenarios) {
            plan(s, plan);
            trace(c, grid, s, expected);
        }
        Gen.write(Path.of(args[2] + "_plan.txt"), plan.toString());
        Gen.write(Path.of(args[2] + "_expected.txt"), expected.toString());
    }

    static final CollisionWorld OPEN = (gx, gy) -> gx < 0 || gy < 0 || gx >= WorldGrid.COLS || gy >= WorldGrid.ROWS;

    static Simulation simulation(Content c, WorldGrid grid, boolean open, int xFp, int yFp) {
        return simulation(c, grid, open, xFp, yFp, -1);
    }

    static Simulation simulation(Content c, WorldGrid grid, boolean open, int xFp, int yFp, long seed) {
        if (seed >= 0) {
            return Simulation.at(c.player(), grid, c.game().transition().freezeTicks(), xFp, yFp, jungle(c), seed);
        }
        return open ? Simulation.at(c.player(), OPEN, 0, xFp, yFp)
                : Simulation.at(c.player(), grid, c.game().transition().freezeTicks(), xFp, yFp);
    }

    /**
     * M6's jungle (AGENTS.md §10): the game's own swarm and big beasts, and nothing else yet —
     * no Wulf, no quest, no orchids, no flame, no score for a first visit.
     */
    static Ecosystem jungle(Content c) {
        return new Ecosystem(c.originalCreatures(), RoomPopulator.NONE, 0, WulfRules.NONE, QuestRules.NONE,
                OrchidData.NONE, FlameData.NONE, c.swarmRules(), c.beastRules());
    }

    static Scenario scenario(Content c, WorldGrid grid, JsonNode s) {
        String name = s.get("name").asString();
        PlayerData rules = c.player();
        boolean open = s.has("open") && s.get("open").asBoolean();
        String[] cr = s.get("room").asString().split(",");
        RoomAddress room = new RoomAddress(Integer.parseInt(cr[0]), Integer.parseInt(cr[1]));

        int xFp;
        int yFp;
        JsonNode at = s.get("at");
        if (at.isString() && at.asString().equals("start")) {
            Simulation spot = Simulation.startingIn(rules, open ? OPEN : grid, 0, room);
            xFp = spot.player().xFp();
            yFp = spot.player().yFp();
        } else {
            xFp = Fixed.fp(at.get(0).asInt());
            yFp = Fixed.fp(at.get(1).asInt());
        }
        if (!Route.roomOf(rules.collisionBox(), Fixed.px(xFp), Fixed.px(yFp)).equals(room)) {
            throw new ConvertException("scenario " + name + ": " + Fixed.px(xFp) + "," + Fixed.px(yFp)
                    + " is not in room " + room.col() + "," + room.row());
        }
        String check = s.has("check") ? s.get("check").asString() : "-";
        long seed = s.has("jungle") ? s.get("jungle").asLong() : -1;

        if (s.has("route")) {
            int rooms = s.get("route").asInt();
            Route route = Route.plan(c, grid, xFp, yFp, rooms);
            return new Scenario(name, room, false, xFp, yFp, route.keys(),
                    "route:" + rooms + ":" + route.target().index(), -1);
        }
        List<Integer> keys = new ArrayList<>();
        for (JsonNode k : s.get("keys")) {
            int bits = bits(name, k.get(0).asString());
            for (int i = k.get(1).asInt(); i > 0; i--) {
                keys.add(bits);
            }
        }
        Scenario sc = new Scenario(name, room, open, xFp, yFp, keys, check, seed);
        if (sc.jungle()) {
            JungleChecks.hold(c, grid, sc);
        }
        return sc;
    }

    /**
     * Every edge between two rooms the player's box fits through ({@link MapAudit}'s
     * crossings, stranded ones included): from a crossing in the middle of it, walk
     * into the next room and back.
     */
    static List<Scenario> seams(Content c, WorldGrid grid) {
        List<Scenario> out = new ArrayList<>();
        PlayerData.Box box = c.player().collisionBox();
        for (MapAudit.Seam seam : MapAudit.run(c.rooms()).seams()) {
            if (seam.crossings() == 0) {
                continue;
            }
            boolean eastWest = seam.to().col() != seam.from().col();
            List<int[]> cells = new ArrayList<>();
            for (int i = 0; i < (eastWest ? CollisionMask.ROWS : CollisionMask.COLS) - 1; i++) {
                int gx = eastWest ? seam.to().col() * CollisionMask.COLS - 1 : seam.from().col() * CollisionMask.COLS + i;
                int gy = eastWest ? seam.from().row() * CollisionMask.ROWS + i : seam.to().row() * CollisionMask.ROWS - 1;
                int nx = eastWest ? gx + 1 : gx;
                int ny = eastWest ? gy : gy + 1;
                if (grid.blockPassable(gx, gy) && grid.blockPassable(nx, ny)) {
                    cells.add(new int[] {gx, gy});
                }
            }
            if (cells.size() != seam.crossings()) {
                throw new ConvertException("seam " + seam.from() + "-" + seam.to() + ": " + cells.size()
                        + " crossings here, MapAudit has " + seam.crossings());
            }
            int[] cell = cells.get(cells.size() / 2);
            // The box's top-left in the crossing's last cell on this side; across the seam, centred in it.
            int left = cell[0] * Simulation.CELL_PX + (eastWest ? 0 : 3);
            int top = cell[1] * Simulation.CELL_PX + (eastWest ? 3 : 0);
            int xFp = Fixed.fp(left - box.x());
            int yFp = Fixed.fp(top - box.y());
            int there = eastWest ? 8 : 2;
            int back = eastWest ? 4 : 1;
            String name = "seam-" + seam.from().col() + "-" + seam.from().row() + (eastWest ? "-E" : "-S");
            List<Integer> keys = crossAndBack(c, grid, name, xFp, yFp, there, back, seam.to().index(),
                    seam.from().index());
            out.add(new Scenario(name, seam.from(), false, xFp, yFp, keys,
                    "seam:" + seam.from().index() + ":" + seam.to().index(), -1));
        }
        return out;
    }

    /** Hold one way until the Java game is in the next room, the other way until it is back, then rest. */
    static List<Integer> crossAndBack(Content c, WorldGrid grid, String name, int xFp, int yFp, int there, int back,
            int to, int from) {
        Simulation sim = simulation(c, grid, false, xFp, yFp);
        List<Integer> keys = new ArrayList<>();
        for (int[] leg : new int[][] {{there, to}, {back, from}}) {
            int ticks = 0;
            while (sim.room().index() != leg[1]) {
                if (++ticks > 120) {
                    throw new ConvertException(name + ": the Java game does not reach room " + leg[1]);
                }
                keys.add(leg[0]);
                sim.tick(input(leg[0], false));
            }
        }
        for (int i = 0; i < c.game().transition().freezeTicks() + 4; i++) {
            keys.add(0);
        }
        return keys;
    }

    static void plan(Scenario s, StringBuilder plan) {
        plan.append("scenario ").append(s.name()).append(' ').append(s.room().index()).append(' ')
                .append(s.open() ? 1 : 0).append(' ').append(s.xFp()).append(' ').append(s.yFp()).append(' ')
                .append(s.check()).append(' ').append(s.seed()).append("\nkeys");
        for (int k : s.keys()) {
            plan.append(' ').append(k);
        }
        plan.append('\n');
    }

    static void trace(Content c, WorldGrid grid, Scenario s, StringBuilder expected) {
        expected.append("scenario ").append(s.name()).append('\n');
        Simulation sim = simulation(c, grid, s.open(), s.xFp(), s.yFp(), s.seed());
        List<String> frames = PlayerSprite.frameIds(c.player());
        boolean fireWas = false;
        for (int k : s.keys()) {
            boolean fire = (k & 16) != 0;
            sim.tick(input(k, fireWas));
            fireWas = fire;
            Player p = sim.player();
            PixelRect b = sim.sabre();
            expected.append(p.xFp()).append(' ').append(p.yFp()).append(' ').append(p.facing().ordinal())
                    .append(' ').append(p.swingTick()).append(' ').append(p.walkTicks()).append(' ')
                    .append(b.isEmpty() ? "0 0 0 0" : b.x() + " " + b.y() + " " + b.w() + " " + b.h())
                    .append(' ').append(sim.room().index()).append(' ').append(sim.freezeRemaining())
                    .append(' ').append(frame(frames, p, c.player(), s))
                    .append(' ').append(Painter.bladeShape(p, c.player(), !b.isEmpty(), sim.tick())).append('\n');
            if (s.jungle()) {
                expected.append(JungleChecks.line(sim)).append('\n');
            }
        }
    }

    /** The frame of player_frames.bin the Java game draws him in. */
    static int frame(List<String> frames, Player p, PlayerData rules, Scenario s) {
        String f = Painter.frameFor(p, rules);
        int i = frames.indexOf(f);
        if (i < 0) {
            throw new ConvertException(s.name() + ": the Java game draws Vale as '" + f + "', a frame the ROM has not got");
        }
        return i;
    }

    /** The keys as the Java game's input; fire is pressed when it was not held the tick before. */
    static InputState input(int k, boolean fireWas) {
        boolean fire = (k & 16) != 0;
        return new InputState((k & 1) != 0, (k & 2) != 0, (k & 4) != 0, (k & 8) != 0,
                fire, fire && !fireWas, false, false, false, false, false);
    }

    /** "UL", "RF", "": the keys held, as the bits test/moves.tcl writes to the ROM. */
    static int bits(String scenario, String held) {
        int b = 0;
        for (char ch : held.toCharArray()) {
            int i = "UDLRF".indexOf(ch);
            if (i < 0) {
                throw new ConvertException("test/moves.json: scenario " + scenario + " holds '" + ch
                        + "'; the keys are U D L R F");
            }
            b |= 1 << i;
        }
        return b;
    }
}
