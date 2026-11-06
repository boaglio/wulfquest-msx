package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Arrays;
import java.util.Map;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.SpriteData;
import wulf.sim.Simulation;
import wulf.world.WorldGrid;

/** M3, M5: Vale's sprite frames read back as his art, in the layout player.h gives the C. */
class PlayerSpriteTest {

    static Content content;
    static PlayerSprite vale;

    @BeforeAll
    static void load() {
        content = Content.load(ConvertTest.DATA);
        vale = new PlayerSprite(content.player(), content.sprites().get(content.player().sprite()),
                VdpPalette.forGame(content), new WorldGrid(content.rooms()),
                content.map().startRoom());
    }

    @Test
    void everyFrameReadsBackAsItsArtButForTheChangesReported() {
        SpriteData sprite = content.sprites().get(content.player().sprite());
        Map<String, byte[]> art = sprite.decode("player");
        VdpPalette p = VdpPalette.forGame(content);
        byte[] blob = vale.blob();
        assertThat(blob).hasSize(vale.ids().size() * PlayerSprite.FRAME_BYTES);
        for (int f = 0; f < vale.ids().size(); f++) {
            String id = vale.ids().get(f);
            byte[] px = art.get(id);
            int[] shown = SpriteCut.shown(Arrays.copyOfRange(blob, f * PlayerSprite.FRAME_BYTES,
                    (f + 1) * PlayerSprite.FRAME_BYTES), 16, 32);
            int[] want = new int[16 * 32];
            for (int i = 0; i < 16 * 24; i++) {
                want[i] = p.vdpColour(px[i]);
            }
            for (SpriteCut.Change ch : vale.changes()) {
                if (ch.frame().equals(id)) {
                    assertThat(want[ch.y() * 16 + ch.x()]).isEqualTo(p.vdpColour(ch.from()));
                    want[ch.y() * 16 + ch.x()] = ch.to();
                }
            }
            assertThat(shown).as(id).isEqualTo(want);
        }
    }

    @Test
    void theFramesAreInTheOrderPlayerHSays() {
        // view * PER_VIEW + walk g; + GAIT + pose p; + GAIT + POSES + p * GAIT + g
        assertThat(vale.ids().get(0)).isEqualTo("side_walk0");
        assertThat(vale.ids().get(PlayerSprite.PER_VIEW + 3)).isEqualTo("side_walk3_l");
        assertThat(vale.ids().get(2 * PlayerSprite.PER_VIEW + PlayerSprite.GAIT + 1)).isEqualTo("up_swing1");
        assertThat(vale.ids().get(3 * PlayerSprite.PER_VIEW + PlayerSprite.GAIT + PlayerSprite.POSES
                + 2 * PlayerSprite.GAIT + 3)).isEqualTo("down_swing2_walk3");
        assertThat(vale.ids()).doesNotHaveDuplicates().hasSize(PlayerSprite.VIEWS.size() * PlayerSprite.PER_VIEW);
    }

    @Test
    void heStartsWhereTheJavaGameStartsHim() {
        Simulation sim = Simulation.startingIn(content.player(), new WorldGrid(content.rooms()), 0,
                content.map().startRoom());
        assertThat(vale.startXFp()).isEqualTo(sim.player().xFp());
        assertThat(vale.startYFp()).isEqualTo(sim.player().yFp());
    }
}
