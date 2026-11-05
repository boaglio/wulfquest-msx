package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Map;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.SpriteData;
import wulf.sim.Simulation;
import wulf.world.WorldGrid;

/** M3: Vale's sprite frames read back as his art, in the layout player.h gives the C. */
class PlayerSpriteTest {

    static Content content;
    static PlayerSprite vale;

    @BeforeAll
    static void load() {
        content = Content.load(ConvertTest.DATA);
        vale = new PlayerSprite(content.player(), content.sprites().get(content.player().sprite()),
                content.palette(), new VdpPalette(content.palette()), new WorldGrid(content.rooms()),
                content.map().startRoom());
    }

    @Test
    void everyFrameReadsBackAsItsArtLessTheOutline() {
        SpriteData sprite = content.sprites().get(content.player().sprite());
        Map<String, byte[]> art = sprite.decode("player");
        int outline = content.palette().indexOf("black");
        byte[] blob = vale.blob();
        assertThat(blob).hasSize(vale.ids().size() * PlayerSprite.FRAME_BYTES);
        for (int f = 0; f < vale.ids().size(); f++) {
            byte[] px = art.get(vale.ids().get(f));
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 16; x++) {
                    int b = blob[f * PlayerSprite.FRAME_BYTES + (y / 16) * 32 + (x / 8) * 16 + y % 16];
                    boolean set = (b & 0x80 >> (x % 8)) != 0;
                    boolean painted = y < 24 && px[y * 16 + x] >= 0 && px[y * 16 + x] != outline;
                    assertThat(set).as("%s at %d,%d", vale.ids().get(f), x, y).isEqualTo(painted);
                }
            }
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
