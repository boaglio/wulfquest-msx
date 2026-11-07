package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Arrays;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.SpriteData;

/** M5: art into layered sprites, and the palette ordered for them. */
class SpriteCutTest {

    static final Content C = Content.load(ConvertTest.DATA);
    static final VdpPalette GAME = VdpPalette.forGame(C);

    @Test
    void everyBankFrameReadsBackAsItsArtButForTheChangesReported() {
        RomSprites bank = new RomSprites(C, GAME);
        byte[] blob = bank.blob();
        for (RomSprites.Entry e : bank.entries()) {
            boolean ink = e.frame().endsWith(RomSprites.INK);
            boolean inked = e.frame().endsWith(RomSprites.INKED);
            String id = ink ? e.frame().substring(0, e.frame().length() - RomSprites.INK.length())
                    : inked ? e.frame().substring(0, e.frame().length() - RomSprites.INKED.length()) : e.frame();
            byte[] px = C.sprites().get(e.sprite()).decode(e.sprite()).get(id);
            if (inked) {
                // M8: inked as Sprite.blitInked inks it (Ink, InkTest).
                int bright = C.palette().indexOf(SpriteCut.sprites(C).inked().get(e.sprite()));
                px = Ink.ink(px, e.w(), bright - 8, bright, C.palette().indexOf("black"));
            }
            int[] shown = SpriteCut.shown(Arrays.copyOfRange(blob, e.offset(),
                    e.offset() + e.cols() * e.rows() * SpriteCut.BLOCK_BYTES), e.w(), e.h());
            int[] want = new int[e.w() * e.h()];
            for (int i = 0; i < want.length; i++) {
                want[i] = px[i] < 0 ? 0 : ink ? RomSprites.SILHOUETTE_COLOUR : GAME.vdpColour(px[i]);
            }
            if (ink) {
                assertThat(shown).as("%s %s", e.sprite(), e.frame()).isEqualTo(want);
                continue;
            }
            for (SpriteCut.Change ch : bank.changes()) {
                if (ch.sprite().equals(e.sprite()) && ch.frame().equals(e.frame())) {
                    want[ch.y() * e.w() + ch.x()] = ch.to();
                }
            }
            assertThat(shown).as("%s %s", e.sprite(), e.frame()).isEqualTo(want);
        }
    }

    @Test
    void theOrderBeatsPaletteOrderAndIsTheSameEveryRun() {
        VdpPalette plain = new VdpPalette(C.palette());
        Map<List<Integer>, Integer> lines = SpriteCut.lines(C, plain);
        int[] order = SpriteCut.order(plain, lines);
        assertThat(SpriteCut.order(plain, lines)).isEqualTo(order);
        int[] identity = new int[plain.groups()];
        for (int g = 0; g < identity.length; g++) {
            identity[g] = g + 1;
        }
        // 904 against 2 397 pixels to change with the data of 2026-11.
        assertThat(cost(lines, order)).isLessThan(cost(lines, identity) / 2);
    }

    @Test
    void aLineOfThreeColoursTheOrMakesIsExact() {
        // Black, the outline, takes every bit: black over anything is the overlay's colour ORed in.
        int black = GAME.vdpColour(C.palette().indexOf("black"));
        assertThat(black).isEqualTo(VdpPalette.CODES);
        int a = 1;
        int b = VdpPalette.CODES ^ a;
        int ia = GAME.paletteIndex(a);
        int ib = GAME.paletteIndex(b);
        byte[] px = new byte[16];
        Arrays.fill(px, (byte) -1);
        px[0] = (byte) ia;
        px[1] = (byte) ib;
        px[2] = (byte) C.palette().indexOf("black");
        SpriteCut.Frame f = SpriteCut.cut(GAME, "test", "f", px, 16, 1);
        assertThat(f.changes()).isEmpty();
        assertThat(SpriteCut.shown(f.bytes(), 16, 1)).startsWith(a, b, black, 0);
        assertThat(f.bytes()[SpriteCut.BLOCK_BYTES - SpriteCut.COLOUR_BYTES] & SpriteCut.CC).isEqualTo(SpriteCut.CC);
    }

    @Test
    void aLineOfFourColoursChangesOnePixelToTheNearestMade() {
        SpriteData s = C.sprites().get(C.player().sprite());
        byte[] px = new byte[16];
        Arrays.fill(px, (byte) -1);
        int[] idx = {C.palette().indexOf("black"), C.palette().indexOf("brightWhite"),
            C.palette().indexOf("red"), C.palette().indexOf("brightGreen")};
        for (int i = 0; i < 4; i++) {
            px[i] = (byte) idx[i];
        }
        SpriteCut.Frame f = SpriteCut.cut(GAME, s.name(), "f", px, 16, 1);
        assertThat(f.changes()).isNotEmpty();
        int[] shown = SpriteCut.shown(f.bytes(), 16, 1);
        int kept = 0;
        for (int i = 0; i < 4; i++) {
            kept += shown[i] == GAME.vdpColour(idx[i]) ? 1 : 0;
        }
        assertThat(kept).isEqualTo(4 - f.changes().size()).isGreaterThanOrEqualTo(2);
    }

    private static long cost(Map<List<Integer>, Integer> lines, int[] code) {
        long total = 0;
        for (Map.Entry<List<Integer>, Integer> e : lines.entrySet()) {
            List<Integer> k = e.getKey();
            int[] g = new int[k.size() / 2];
            int[] n = new int[k.size() / 2];
            for (int i = 0; i < g.length; i++) {
                g[i] = k.get(2 * i);
                n[i] = k.get(2 * i + 1);
            }
            total += (long) SpriteCut.lineCost(g, n, code) * e.getValue();
        }
        return total;
    }
}
