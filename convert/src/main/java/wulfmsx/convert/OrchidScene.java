package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import wulf.Content;
import wulf.data.PlayerData;
import wulf.engine.Fixed;
import wulf.sim.Player;
import wulf.sim.Simulation;
import wulf.sim.effects.EffectKind;
import wulf.world.WorldGrid;

/**
 * Keys for test/moves.sh's orchid scene (AGENTS.md §10, M8): Vale picks the
 * room's orchid each time it blooms, and walks with whatever it gave him.
 *
 * <p>The Java game's own {@link Simulation} is played: with no effect on him,
 * Vale is steered to the orchid's spot and waits there for the bloom; with one,
 * he walks left and right, so a slowed, hastened or reversed walk shows in his
 * track, and a knockdown in his standing still. The keys are what the ROM
 * replays; the trace they make is the Java game's.
 */
final class OrchidScene {

    private static final int SWAY_TICKS = 24;

    private OrchidScene() {
    }

    static List<Integer> keys(Content c, WorldGrid grid, MoveTraces.Scenario s, int ticks) {
        Simulation sim = MoveTraces.simulation(c, grid, s);
        PlayerData rules = c.player();
        int[] anchors = sim.orchidAnchors();
        if (anchors.length == 0) {
            throw new ConvertException("scenario " + s.name() + ": room " + s.room() + " grows no orchid");
        }
        int roomX = sim.room().col() * Simulation.ROOM_W_PX;
        int roomY = sim.room().row() * Simulation.ROOM_H_PX;
        int spotX = roomX + anchors[0];
        int spotY = roomY + anchors[1];
        List<Integer> keys = new ArrayList<>();
        int sway = 0;
        for (int t = 0; t < ticks; t++) {
            Player p = sim.player();
            int k = 0;
            if (p.mode() == Player.Mode.ALIVE && sim.freezeRemaining() == 0) {
                if (sim.effect().kind() != EffectKind.NONE) {
                    k = (sway++ / SWAY_TICKS) % 2 == 0 ? 4 : 8;
                } else {
                    sway = 0;
                    k = Route.axis(rules.momentum(), spotX - Fixed.px(p.xFp()), p.vx(), 8, 4)
                            | Route.axis(rules.momentum(), spotY - Fixed.px(p.yFp()), p.vy(), 2, 1);
                }
            }
            keys.add(k);
            sim.tick(MoveTraces.input(k, false));
        }
        return keys;
    }
}
