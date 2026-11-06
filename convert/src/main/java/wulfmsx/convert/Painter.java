package wulfmsx.convert;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.List;
import wulf.data.PlayerData;
import wulf.render.PlayerPainter;
import wulf.sim.Player;

/**
 * The Java game's own choices of what to draw Vale as, for the oracles: its
 * {@code PlayerPainter.frameFor} and {@code pose} are package-private, so
 * they are called by reflection rather than copied.
 */
final class Painter {

    private static final Method FRAME_FOR;
    private static final Method POSE;

    static {
        try {
            FRAME_FOR = PlayerPainter.class.getDeclaredMethod("frameFor", Player.class, PlayerData.class);
            POSE = PlayerPainter.class.getDeclaredMethod("pose", PlayerData.Fence.class, int.class, long.class);
            FRAME_FOR.setAccessible(true);
            POSE.setAccessible(true);
        } catch (NoSuchMethodException e) {
            throw new IllegalStateException("wulfquest's PlayerPainter changed: " + e.getMessage(), e);
        }
    }

    private Painter() {
    }

    /** The frame the Java game draws him in. */
    static String frameFor(Player p, PlayerData rules) {
        return (String) call(FRAME_FOR, p, rules);
    }

    /** The blade's pose the Java game draws, {forward, side}. */
    @SuppressWarnings("unchecked")
    static List<Integer> pose(PlayerData.Fence fence, int intoBlade, long tick) {
        return (List<Integer>) call(POSE, fence, intoBlade, tick);
    }

    /** The blade shape (blade.h) the Java game draws him with: facing * poses + pose; -1 with none out. */
    static int bladeShape(Player p, PlayerData rules, boolean fighting, long tick) {
        if (!fighting) {
            return -1;
        }
        PlayerData.Sabre s = rules.sabre();
        int intoBlade = p.swingTick() - s.windupTicks();
        List<Integer> pose = pose(s.fence(), intoBlade, tick);
        int guards = s.fence().guards().size();
        int i = s.fence().thrusting(intoBlade) ? guards + s.fence().thrusts().indexOf(pose) : s.fence().guards().indexOf(pose);
        return p.facing().ordinal() * 2 * guards + i;
    }

    private static Object call(Method m, Object... args) {
        try {
            return m.invoke(null, args);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new IllegalStateException(e);
        }
    }
}
