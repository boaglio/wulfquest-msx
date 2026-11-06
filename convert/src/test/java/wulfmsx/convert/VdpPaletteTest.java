package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.Palette;

/** M1: the palette on the V9938, and art that never uses the transparent colour 0. */
class VdpPaletteTest {

    final VdpPalette palette = new VdpPalette(Content.load(ConvertTest.DATA).palette());

    @Test
    void threeBitsIsTheNearestLevel() {
        for (int v = 0; v <= 255; v++) {
            int best = 0;
            for (int l = 1; l <= 7; l++) {
                if (Math.abs(l * 255 - v * 7) < Math.abs(best * 255 - v * 7)) {
                    best = l;
                }
            }
            assertThat(VdpPalette.threeBits(v)).as("%d", v).isEqualTo(best);
        }
    }

    @Test
    void opaqueArtNeverUsesColourZeroAndShowsItsOwnColour() {
        VdpPalette game = VdpPalette.forGame(Content.load(ConvertTest.DATA));
        for (VdpPalette p : java.util.List.of(palette, game)) {
            byte[] reg = p.registers();
            byte[] plain = palette.registers();
            assertThat(p.vdpColour(-1)).isEqualTo(VdpPalette.TRANSPARENT);
            assertThat(p.paletteIndex(VdpPalette.TRANSPARENT)).isEqualTo(-1);
            for (int i = 0; i < Palette.SIZE; i++) {
                int c = p.vdpColour(i);
                assertThat(c).as("%d", i).isBetween(1, VdpPalette.CODES);
                // The colour shown is the palette colour's own (in palette order, colour i is group i's number).
                int own = palette.vdpColour(i);
                assertThat(reg[2 * c]).as("%d", i).isEqualTo(plain[2 * own]);
                assertThat(reg[2 * c + 1]).as("%d", i).isEqualTo(plain[2 * own + 1]);
                assertThat(p.paletteIndex(c)).as("%d", i).isEqualTo(p.paletteIndex(p.vdpColour(p.paletteIndex(c))));
            }
            // Number 0, the border's and the cleared panel's, is black.
            assertThat(reg[0]).isZero();
            assertThat(reg[1]).isZero();
        }
    }

    @Test
    void blackAndBrightBlackAreOneColourWithOneNumber() {
        Palette pal = Content.load(ConvertTest.DATA).palette();
        assertThat(palette.vdpColour(pal.indexOf("brightBlack"))).isEqualTo(palette.vdpColour(pal.indexOf("black")));
        assertThat(palette.groups()).isEqualTo(VdpPalette.CODES);
    }
}
