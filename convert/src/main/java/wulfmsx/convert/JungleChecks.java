package wulfmsx.convert;

import java.lang.reflect.Field;
import java.util.List;
import wulf.Content;
import wulf.engine.Fixed;
import wulf.sim.Beast;
import wulf.sim.Creature;
import wulf.sim.Player;
import wulf.sim.Quest;
import wulf.sim.Simulation;
import wulf.sim.Wulf;
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
    private static final Field WULF_SPEED = field(Wulf.class, "speedFp");
    private static final Field WULF_HERE = field(Wulf.class, "here");

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

    /**
     * The quest and the Wulf (M7): {@code Q SET SLOTS TAKEN LAIR FLASH FLY KEEPER_HERE KEEPER_STATE
     * KEEPER_X KEEPER_MOVE}, then the guardian ({@code G X_FP Y_FP PHASE TIMER AUX FACE HURT MOVE},
     * or {@code G -} out of a lair), then the Wulf ({@code W STATE STATE_TICK AT HEADING SPEED LATCH
     * HERE X_FP Y_FP MOVE}).
     */
    static String questLine(Simulation sim) {
        Quest q = sim.quest();
        int taken = 0;
        for (int i = 0; i < sim.landmarks().lairs().size(); i++) {
            taken |= q.taken(i) ? 1 << i : 0;
        }
        StringBuilder sb = new StringBuilder("Q ");
        sb.append(sim.landmarks().amuletSet()).append(' ').append(q.slotMask()).append(' ').append(taken).append(' ')
                .append(q.lair()).append(' ').append(q.pickupFlash()).append(' ').append(q.flyTicks()).append(' ')
                .append(q.keeperHere() ? 1 : 0).append(' ').append(q.keeperState().ordinal()).append(' ')
                .append(q.keeperHere() ? String.valueOf(q.keeper().xFp()) : "-").append(' ')
                .append(q.keeper().moveTicks());
        if (q.inLair()) {
            Creature g = q.guardian();
            sb.append(" G ").append(g.xFp()).append(' ').append(g.yFp()).append(' ').append(g.phase()).append(' ')
                    .append(g.timer()).append(' ').append(g.aux()).append(' ').append(g.faceX()).append(' ')
                    .append(g.hurtTicks()).append(' ').append(g.moveTicks());
        } else {
            sb.append(" G -");
        }
        Wulf w = sim.wulf();
        boolean here;
        try {
            here = WULF_HERE.getBoolean(w);
        } catch (IllegalAccessException e) {
            throw new IllegalStateException(e);
        }
        sb.append(" W ").append(w.state().ordinal()).append(' ').append(w.stateTick()).append(' ')
                .append(w.at().index()).append(' ').append(w.heading()).append(' ').append(get(WULF_SPEED, w))
                .append(' ').append(w.latch().ordinal()).append(' ').append(here ? 1 : 0).append(' ')
                .append(w.body().xFp()).append(' ').append(w.body().yFp()).append(' ').append(w.body().moveTicks());
        return sb.toString();
    }

    private static final Field FLAME_WAIT = field(Simulation.class, "flameWait");
    private static final Field ORCHID_START = field(Simulation.class, "orchidCycleStart");
    private static final Field ORCHID_BASE = field(Simulation.class, "orchidBase");
    private static final Field ROOM_ORCHIDS = field(Simulation.class, "roomOrchids");

    /**
     * The rest of the game (M8): {@code M SCORE BEST CONTINUES_USED EFFECT ORCHID REMAINING
     * TREASURES MAP EYES GLASSES WAY HINT HINT_DIR HINT_EXIT FLAME_WAIT ORCHID_START}: the effect as
     * EffectKind's ordinal, the eyes and glasses taken as bits, the room's orchid's cycle start
     * ({@code -} when it has none).
     */
    static String gameLine(Simulation sim) {
        Quest q = sim.quest();
        int eyes = 0;
        for (int i = 0; i < sim.landmarks().items().eyes().size(); i++) {
            eyes |= q.eyeTaken(i) ? 1 << i : 0;
        }
        int glasses = 0;
        for (int i = 0; i < sim.landmarks().items().glass().all().size(); i++) {
            glasses |= q.glassTaken(i) ? 1 << i : 0;
        }
        String orchid;
        try {
            int[] anchors = (int[]) ROOM_ORCHIDS.get(sim);
            orchid = anchors.length == 0 ? "-"
                    : String.valueOf(((long[]) ORCHID_START.get(sim))[ORCHID_BASE.getInt(sim)]);
        } catch (IllegalAccessException e) {
            throw new IllegalStateException(e);
        }
        return "M " + sim.score() + ' ' + sim.bestScore() + ' ' + sim.continuesUsed() + ' '
                + sim.effect().kind().ordinal() + ' ' + sim.effect().orchid() + ' ' + sim.effect().remainingTicks() + ' '
                + sim.treasuresTaken() + ' ' + (q.mapTaken() ? 1 : 0) + ' ' + eyes + ' ' + glasses + ' ' + q.wayTicks()
                + ' ' + q.hintTicks() + ' ' + q.hintDirection().ordinal() + ' ' + (q.hintToExit() ? 1 : 0) + ' '
                + get(FLAME_WAIT, sim) + ' ' + orchid;
    }

    /** Plays the scenario in the Java game and fails unless its check shows. */
    static void hold(Content c, WorldGrid grid, MoveTraces.Scenario s) {
        if (s.check().equals("-")) {
            return;
        }
        Simulation sim = MoveTraces.simulation(c, grid, s);
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
        java.util.Set<String> seen = new java.util.HashSet<>();
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
                case "wulf-sits" -> shown |= sim.wulf().state() == Wulf.State.SITTING;
                case "guardian-parry" -> shown |= sim.quest().inLair()
                        && sim.quest().guardian().aux() == c.questRules().orbit().stunTicks();
                case "win" -> shown |= p.mode() == Player.Mode.WON;
                case "flame" -> shown |= sim.creatures().stream().anyMatch(x -> x.speciesIndex()
                        == sim.ecosystem().creatures().creatures().size());
                case "treasure" -> shown |= sim.treasuresTaken() > 0;
                case "item" -> shown |= sim.quest().mapTaken() || sim.quest().wayTicks() > 0
                        || sim.quest().eyeTaken(0) || sim.quest().eyeTaken(1);
                case "hint" -> shown |= sim.quest().hintTicks() > 0;
                case "continue" -> shown |= sim.continuesUsed() > 0;
                default -> {
                    if (s.check().startsWith("effects:")) {
                        // Every one named, at some tick: "effects:TORPOR+HASTE".
                        seen.add(sim.effect().kind().name());
                        shown = seen.containsAll(List.of(s.check().substring("effects:".length()).split("\\+")));
                    } else if (s.check().startsWith("effect:")) {
                        shown |= sim.effect().kind().name().equals(s.check().substring("effect:".length()));
                    } else if (s.check().startsWith("set:")) {
                        shown = sim.landmarks().amuletSet() == Integer.parseInt(s.check().substring("set:".length()));
                    } else if (s.check().startsWith("pieces:")) {
                        shown = sim.quest().piecesHeld() >= Integer.parseInt(s.check().substring("pieces:".length()));
                    } else if (s.check().startsWith("deaths:")) {
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
