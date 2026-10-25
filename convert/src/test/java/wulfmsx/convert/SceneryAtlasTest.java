package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.world.SceneryCatalog;

/** M1: the atlas round-trips to the same pixels, and the blitter can copy from it. */
class SceneryAtlasTest {

    static Content content;
    static VdpPalette palette;
    static SceneryAtlas atlas;

    @BeforeAll
    static void load() {
        content = Content.load(ConvertTest.DATA);
        palette = new VdpPalette(content.palette());
        atlas = new SceneryAtlas(content.scenery(), content.sprites().all(), palette);
    }

    @Test
    void everyObjectReadsBackFromTheVideoRamBytesAsItsArt() {
        List<byte[]> chunks = atlas.chunks();
        assertThat(atlas.slots()).hasSize(content.scenery().ids().size());
        for (SceneryAtlas.Slot s : atlas.slots()) {
            byte[] art = SceneryAtlas.art(content.scenery().piece(s.id()), content.sprites().all());
            for (int y = 0; y < s.h(); y++) {
                for (int x = 0; x < s.w(); x++) {
                    int line = s.y() + y;
                    byte b = chunks.get(line / SceneryAtlas.CHUNK_LINES)[
                            (line % SceneryAtlas.CHUNK_LINES) * SceneryAtlas.BYTES_PER_LINE + (s.x() + x) / 2];
                    int colour = (s.x() + x) % 2 == 0 ? (b >> 4) & 15 : b & 15;
                    assertThat(colour).as("%s at %d,%d", s.id(), x, y)
                            .isEqualTo(palette.vdpColour(art[y * s.w() + x]));
                }
            }
        }
    }

    @Test
    void objectsDoNotOverlapAndStartOnAByte() {
        List<SceneryAtlas.Slot> slots = atlas.slots();
        for (SceneryAtlas.Slot a : slots) {
            assertThat(a.x() % 2).as(a.id()).isZero();
            assertThat(a.x() + a.w()).isLessThanOrEqualTo(SceneryAtlas.WIDTH);
            assertThat(a.y() + a.h()).isLessThanOrEqualTo(atlas.lines());
            assertThat(a.w()).isEqualTo(content.scenery().piece(a.id()).w() * SceneryCatalog.CELL);
            for (SceneryAtlas.Slot b : slots) {
                if (a != b) {
                    boolean apart = a.x() + a.w() <= b.x() || b.x() + b.w() <= a.x()
                            || a.y() + a.h() <= b.y() || b.y() + b.h() <= a.y();
                    assertThat(apart).as("%s and %s", a.id(), b.id()).isTrue();
                }
            }
        }
    }

    @Test
    void theChunksAreRomSegmentsAndCoverTheAtlas() {
        int bytes = 0;
        for (byte[] c : atlas.chunks()) {
            assertThat(c.length).isLessThanOrEqualTo(SceneryAtlas.CHUNK_BYTES);
            bytes += c.length;
        }
        assertThat(bytes).isEqualTo(atlas.lines() * SceneryAtlas.BYTES_PER_LINE);
    }
}
