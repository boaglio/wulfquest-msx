package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.DisplayConfig;
import wulf.data.PlayerData;
import wulf.data.SpriteData;
import wulf.engine.Fixed;
import wulf.render.Framebuffer;
import wulf.render.PlayerPainter;
import wulf.render.Sprite;
import wulf.render.SpriteBank;
import wulf.sim.Simulation;

/** M5: the blade's shapes are what the Java game's PlayerPainter draws, and the ROM's hash picks its pose. */
class BladeTest {

    static final Content C = Content.load(ConvertTest.DATA);

    @Test
    void theRomsThirtyTwoBitHashPicksTheJavaGamesPose() {
        PlayerData.Fence fence = C.player().sabre().fence();
        int n = fence.guards().size();
        for (long tick = 0; tick < 50_000; tick++) {
            for (int intoBlade : new int[] {0, fence.everyTicks()}) {
                List<List<Integer>> list = fence.thrusting(intoBlade) ? fence.thrusts() : fence.guards();
                assertThat(list.get(Blade.poseByHash(tick, fence.everyTicks(), n)))
                        .as("tick %d", tick).isEqualTo(Painter.pose(fence, intoBlade, tick));
            }
        }
    }

    @Test
    void everyBladeTheJavaGameDrawsIsItsShapeAtTheHand() {
        PlayerData rules = C.player();
        VdpPalette p = VdpPalette.forGame(C);
        Blade blade = new Blade(rules, C.palette(), p);
        assertThat(blade.dropped()).isEmpty();
        DisplayConfig display = C.display();
        DisplayConfig.Playfield field = display.playfield();
        SpriteBank bank = new SpriteBank(C.sprites());
        Sprite sprite = bank.get(rules.sprite());
        SpriteData data = C.sprites().get(rules.sprite());
        Map<String, Map<String, SpriteData.Point>> anchors = data.anchors();
        PlayerPainter painter = new PlayerPainter(display, bank, rules, C.palette());
        int guards = rules.sabre().fence().guards().size();
        int[] keys = {1, 9, 8, 10, 2, 6, 4, 5};   // up, up-right, ... as test/moves.tcl's bits
        int seen = 0;
        boolean[] shapes = new boolean[blade.shapes()];
        for (int k : keys) {
            // In the middle of an open room, fencing for a while, on the spot and walking.
            Simulation sim = Simulation.at(rules, MoveTraces.OPEN, 0, Fixed.fp(8 * 256 + 128), Fixed.fp(8 * 176 + 100));
            boolean fireWas = false;
            for (int t = 0; t < 60; t++) {
                int held = (t < 30 ? k : k & 0) | 16;
                sim.tick(MoveTraces.input(t < 2 ? k : held, fireWas));
                fireWas = ((t < 2 ? k : held) & 16) != 0;
                if (sim.sabre().isEmpty()) {
                    continue;
                }
                int x = Fixed.px(sim.player().xFp()) + field.x() - sim.room().col() * Simulation.ROOM_W_PX;
                int y = Fixed.px(sim.player().yFp()) + field.y() - sim.room().row() * Simulation.ROOM_H_PX;
                Framebuffer drawn = new Framebuffer(display.canvas().w(), display.canvas().h());
                drawn.clear(0);
                painter.paint(drawn, sim);
                String frame = Painter.frameFor(sim.player(), rules);
                Framebuffer body = new Framebuffer(display.canvas().w(), display.canvas().h());
                body.clear(0);
                sprite.blit(body, frame, x, y);

                int shape = Painter.bladeShape(sim.player(), rules, true, sim.tick());
                int intoBlade = sim.player().swingTick() - rules.sabre().windupTicks();
                int byHash = sim.player().facing().ordinal() * 2 * guards
                        + (rules.sabre().fence().thrusting(intoBlade) ? guards : 0)
                        + Blade.poseByHash(sim.tick(), rules.sabre().fence().everyTicks(), guards);
                assertThat(byHash).isEqualTo(shape);
                shapes[shape] = true;
                SpriteData.Point hand = anchors.get(frame).get("hand");
                int hx = x - sprite.originX() + hand.x();
                int hy = y - sprite.originY() + hand.y();
                Map<Long, Integer> want = new HashMap<>();
                for (int[] px : blade.pixels(shape)) {
                    want.put(key(hx + px[0], hy + px[1]), px[2]);
                    assertThat(p.vdpColour(drawn.get(hx + px[0], hy + px[1])))
                            .as("%s shape %d at %d,%d", frame, shape, px[0], px[1]).isEqualTo(px[2]);
                }
                for (int j = 0; j < display.canvas().h(); j++) {
                    for (int i = 0; i < display.canvas().w(); i++) {
                        if (drawn.get(i, j) != body.get(i, j)) {
                            assertThat(want).as("%s shape %d: %d,%d is the blade's", frame, shape, i - hx, j - hy)
                                    .containsKey(key(i, j));
                        }
                    }
                }
                seen++;
            }
        }
        assertThat(seen).isGreaterThan(300);
        int facings = 0;
        for (int f = 0; f < 8; f++) {
            boolean any = false;
            for (int s = 0; s < 2 * guards; s++) {
                any |= shapes[f * 2 * guards + s];
            }
            facings += any ? 1 : 0;
        }
        assertThat(facings).isEqualTo(8);
    }

    private static long key(int x, int y) {
        return (long) x << 32 | (y & 0xFFFFFFFFL);
    }
}
