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
    void opaqueArtNeverUsesColourZeroAndShowsTheSameColour() {
        byte[] reg = palette.registers();
        assertThat(palette.vdpColour(-1)).isEqualTo(VdpPalette.TRANSPARENT);
        assertThat(palette.paletteIndex(VdpPalette.TRANSPARENT)).isEqualTo(-1);
        for (int i = 0; i < Palette.SIZE; i++) {
            int c = palette.vdpColour(i);
            assertThat(c).as("%d", i).isNotEqualTo(VdpPalette.TRANSPARENT);
            assertThat(reg[2 * c]).as("%d", i).isEqualTo(reg[2 * i]);
            assertThat(reg[2 * c + 1]).as("%d", i).isEqualTo(reg[2 * i + 1]);
        }
    }

    @Test
    void colourZerosTwinIsTheSameColourOnTheV9938() {
        byte[] reg = palette.registers();
        int t = palette.twinOfZero();
        assertThat(reg[2 * t]).isEqualTo(reg[0]);
        assertThat(reg[2 * t + 1]).isEqualTo(reg[1]);
    }
}
