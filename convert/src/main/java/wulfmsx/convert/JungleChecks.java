package wulfmsx.convert;

import java.lang.reflect.Field;
import java.util.List;
import wulf.Content;
import wulf.engine.Fixed;
import wulf.sim.Beast;
import wulf.sim.Creature;
import wulf.sim.Player;
import wulf.sim.Simulation;
import wulf.world.WorldGrid;

/**
 * The jungle in MoveTraces's traces (AGENTS.md §10, M6): a line a tick of
 * what lives in it, and the checks a jungle scenario is there to show, which
 * the Java game must show first.
 *
 * <p>The line: {@code J MODE MODE_TICK LIVES INVULN N}, then for each of the
 * room's creatures, in the Java game's list order, {@code SPECIES MODE
 * MODE_TICK X_FP Y_FP TIMER PHASE AUX FACE HOME}, then {@code B} and for each
 * beast {@code KIND AT X_FP Y_FP VX VY FACE ASLEEP PAUSE MOVE_TICKS}. Some of
 * these are the Java game's package-private fields, read by reflection.
 *
 * <p>The checks: {@code burst-facing} (a creature burst by the blade, Vale
 * fencing and facing it, and on his feet after); {@code touch-kills} (a
 * creature's touch takes a life); {@code rhino-turns} (a rhino in his room
 * turns about); {@code follow-flip} (after a flip a tribesman is set down
 * near him); {@code deaths:N} (he falls N times); {@code game-over}.
 */
final class JungleChecks {

    private static final Field HOME = field(Creature.class, "homeRoom");
    private static final Field KIND = field(Beast.class, "kindIndex");
    private static final Field VX = field(Beast.class, "vxFp");
    private static final Field VY = field(Beast.class, "vyFp");
    private static final Field PAUSE = field(Beast.class, "pauseTicks");

    private JungleChecks() {
    }

    private static Field field(Class<?> c, String name) {
        try {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            return f;
        } catch (NoSuchFieldException e) {
            throw new IllegalStateException("wulfquest's " + c.getSimpleName() + " has no " + name + " now", e);
        }
    }

    private static int get(Field f, Object o) {
        try {
            return f.getInt(o);
        } catch (IllegalAccessException e) {
            throw new IllegalStateException(e);
        }
    }

    static String line(Simulation sim) {
        Player p = sim.player();
        StringBuilder sb = new StringBuilder("J ");
        sb.append(p.mode().ordinal()).append(' ').append(p.modeTick()).append(' ').append(p.lives()).append(' ')
                .append(p.invulnTicks()).append(' ').append(sim.creatures().size());
        for (Creature c : sim.creatures()) {
            sb.append(' ').append(c.speciesIndex()).append(' ').append(c.mode().ordinal()).append(' ')
                    .append(c.modeTick()).append(' ').append(c.xFp()).append(' ').append(c.yFp()).append(' ')
                    .append(c.timer()).append(' ').append(c.phase()).append(' ').append(c.aux()).append(' ')
                    .append(c.faceX()).append(' ').append(get(HOME, c));
        }
        sb.append(" B");
        for (Beast b : sim.beasts()) {
            sb.append(' ').append(get(KIND, b)).append(' ').append(b.at().index()).append(' ').append(b.xFp())
                    .append(' ').append(b.yFp()).append(' ').append(get(VX, b)).append(' ').append(get(VY, b))
                    .append(' ').append(b.faceX()).append(' ').append(b.asleep() ? 1 : 0).append(' ')
                    .append(get(PAUSE, b)).append(' ').append(b.moveTicks());
        }
        return sb.toString();
    }

    /** Plays the scenario in the Java game and fails unless its check shows. */
    static void hold(Content c, WorldGrid grid, MoveTraces.Scenario s) {
        if (s.check().equals("-")) {
            return;
        }
        Simulation sim = MoveTraces.simulation(c, grid, false, s.xFp(), s.yFp(), s.seed());
        boolean fireWas = false;
        int deaths = 0;
        boolean shown = false;
        int lastFlip = -1;
        int[] rhinoVx = new int[sim.beasts().size()];
        int[] beastX = new int[sim.beasts().size()];
        int[] beastY = new int[sim.beasts().size()];
        for (int i = 0; i < rhinoVx.length; i++) {
            rhinoVx[i] = get(VX, sim.beasts().get(i));
            beastX[i] = sim.beasts().get(i).xFp();
            beastY[i] = sim.beasts().get(i).yFp();
        }
        int t = 0;
        for (int k : s.keys()) {
            List<Creature> before = List.copyOf(sim.creatures());
            int[] modesBefore = before.stream().mapToInt(x -> x.mode().ordinal()).toArray();
            Player.Mode was = sim.player().mode();
            int roomWas = sim.room().index();
            sim.tick(MoveTraces.input(k, fireWas));
            fireWas = (k & 16) != 0;
            t++;
            Player p = sim.player();
            if (sim.room().index() != roomWas) {
                lastFlip = t;
            }
            boolean fell = was == Player.Mode.ALIVE && p.mode() == Player.Mode.DYING;
            if (fell) {
                deaths++;
            }
            int bursts = 0;
            for (int i = 0; i < before.size(); i++) {
                if (modesBefore[i] != Creature.Mode.DYING.ordinal() && before.get(i).mode() == Creature.Mode.DYING) {
                    bursts++;
                }
            }
            switch (s.check()) {
                case "burst-facing" -> shown |= bursts > 0 && !fell && p.mode() == Player.Mode.ALIVE && p.swinging();
                case "touch-kills" -> shown |= bursts > 0 && fell;
                case "rhino-turns" -> {
                    for (int i = 0; i < rhinoVx.length; i++) {
                        Beast b = sim.beasts().get(i);
                        int vx = get(VX, b);
                        if (b.kind() == Beast.Kind.RHINO && b.at().equals(sim.room()) && Integer.signum(vx) != 0
                                && Integer.signum(vx) == -Integer.signum(rhinoVx[i])) {
                            shown = true;
                        }
                        rhinoVx[i] = vx;
                    }
                }
                case "follow-flip" -> {
                    for (int i = 0; i < beastX.length; i++) {
                        Beast b = sim.beasts().get(i);
                        boolean jumped = Math.abs(Fixed.px(b.xFp()) - Fixed.px(beastX[i])) > 16
                                || Math.abs(Fixed.px(b.yFp()) - Fixed.px(beastY[i])) > 16;
                        if (b.kind() == Beast.Kind.TRIBESMAN && jumped && lastFlip > 0 && lastFlip < t
                                && Math.abs(b.at().row() - sim.room().row()) <= 1) {
                            shown = true;
                        }
                        beastX[i] = b.xFp();
                        beastY[i] = b.yFp();
                    }
                }
                case "game-over" -> shown |= p.mode() == Player.Mode.GAME_OVER;
                default -> {
                    if (s.check().startsWith("deaths:")) {
                        shown = deaths >= Integer.parseInt(s.check().substring("deaths:".length()));
                    } else {
                        throw new ConvertException(s.name() + ": no check '" + s.check() + "'");
                    }
                }
            }
        }
        if (!shown) {
            throw new ConvertException("test/jungle.json: scenario " + s.name() + " (seed " + s.seed()
                    + ") does not show '" + s.check() + "' in the Java game; it fell " + deaths + " times");
        }
    }
}
