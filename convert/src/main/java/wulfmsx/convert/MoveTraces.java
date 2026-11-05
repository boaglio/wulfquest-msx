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
import wulf.sim.Simulation;
import wulf.world.CollisionMask;
import wulf.world.CollisionWorld;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * The test oracle for M3 (AGENTS.md §10): Ranger Vale as the Java game moves
 * him, tick by tick, for test/moves.sh to compare with the MSX2.
 *
 * <pre>
 *   MoveTraces DATA_DIR SCENARIOS_JSON OUT_PREFIX
 * </pre>
 *
 * Each scenario in the JSON is a start and a list of keys held for so many
 * ticks. It runs in the Java game's own {@link Simulation}, with no creatures,
 * in one of two worlds: a room of the map with everything outside it solid
 * (the MSX2 has only the room on screen until M4), or the open world, where
 * only the map's edge is. Writes:
 *
 * <ul>
 *   <li>{@code OUT_PREFIX_plan.txt}, for test/moves.tcl: per scenario
 *       {@code scenario NAME ROOM OPEN X_FP Y_FP}, then {@code keys} and a
 *       key byte a tick (up 1, down 2, left 4, right 8, fire 16);
 *   <li>{@code OUT_PREFIX_expected.txt}: per scenario {@code scenario NAME},
 *       then after each tick {@code X_FP Y_FP FACING SWING WALK SX SY SW SH}:
 *       the feet, the facing (Direction8's order), the tick into the stroke
 *       (-1 none), ticks walked, and the sabre's hit box (all 0 when none).
 * </ul>
 */
public final class MoveTraces {

    private MoveTraces() {
    }

    public static void main(String[] args) throws IOException {
        if (args.length != 3) {
            System.err.println("usage: MoveTraces DATA_DIR SCENARIOS_JSON OUT_PREFIX");
            System.exit(2);
        }
        Content c = Content.load(Path.of(args[0]));
        JsonNode list = JsonMapper.builder().build().readTree(Files.readString(Path.of(args[1])));
        StringBuilder plan = new StringBuilder();
        StringBuilder expected = new StringBuilder();
        for (JsonNode s : list.get("scenarios")) {
            run(c, s, plan, expected);
        }
        Gen.write(Path.of(args[2] + "_plan.txt"), plan.toString());
        Gen.write(Path.of(args[2] + "_expected.txt"), expected.toString());
    }

    static final CollisionWorld OPEN = (gx, gy) -> gx < 0 || gy < 0 || gx >= WorldGrid.COLS || gy >= WorldGrid.ROWS;

    static void run(Content c, JsonNode s, StringBuilder plan, StringBuilder expected) {
        String name = s.get("name").asString();
        PlayerData rules = c.player();
        boolean open = s.has("open") && s.get("open").asBoolean();
        String[] cr = s.get("room").asString().split(",");
        RoomAddress room = new RoomAddress(Integer.parseInt(cr[0]), Integer.parseInt(cr[1]));
        CollisionWorld world = open ? OPEN : roomOnly(new WorldGrid(c.rooms()), room);

        int xFp;
        int yFp;
        JsonNode at = s.get("at");
        if (at.isString() && at.asString().equals("start")) {
            Simulation spot = Simulation.startingIn(rules, world, 0, room);
            xFp = spot.player().xFp();
            yFp = spot.player().yFp();
        } else {
            xFp = Fixed.fp(at.get(0).asInt());
            yFp = Fixed.fp(at.get(1).asInt());
        }

        List<Integer> keys = new ArrayList<>();
        for (JsonNode k : s.get("keys")) {
            int bits = bits(name, k.get(0).asString());
            for (int i = k.get(1).asInt(); i > 0; i--) {
                keys.add(bits);
            }
        }

        plan.append("scenario ").append(name).append(' ').append(room.index()).append(' ').append(open ? 1 : 0)
                .append(' ').append(xFp).append(' ').append(yFp).append("\nkeys");
        for (int k : keys) {
            plan.append(' ').append(k);
        }
        plan.append('\n');

        expected.append("scenario ").append(name).append('\n');
        Simulation sim = Simulation.at(rules, world, 0, xFp, yFp);
        boolean fireWas = false;
        for (int k : keys) {
            boolean fire = (k & 16) != 0;
            sim.tick(new InputState((k & 1) != 0, (k & 2) != 0, (k & 4) != 0, (k & 8) != 0,
                    fire, fire && !fireWas, false, false, false, false, false));
            fireWas = fire;
            Player p = sim.player();
            PixelRect b = sim.sabre();
            expected.append(p.xFp()).append(' ').append(p.yFp()).append(' ').append(p.facing().ordinal())
                    .append(' ').append(p.swingTick()).append(' ').append(p.walkTicks()).append(' ')
                    .append(b.isEmpty() ? "0 0 0 0" : b.x() + " " + b.y() + " " + b.w() + " " + b.h()).append('\n');
        }
    }

    /** The map with everything outside one room solid: the MSX2 until M4 flips rooms. */
    static CollisionWorld roomOnly(CollisionWorld map, RoomAddress room) {
        int x0 = room.col() * CollisionMask.COLS;
        int y0 = room.row() * CollisionMask.ROWS;
        return (gx, gy) -> gx < x0 || gy < y0 || gx >= x0 + CollisionMask.COLS || gy >= y0 + CollisionMask.ROWS
                || map.isSolid(gx, gy);
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
