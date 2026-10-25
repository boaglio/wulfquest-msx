package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import wulf.data.Palette;

/**
 * The game's 16 colours on the V9938 (AGENTS.md §4): each the nearest of its
 * 512, three bits a channel, and the colour numbers the art is written with.
 *
 * <p>Colour 0 is transparent on the V9938 wherever it matters to us: in the
 * blitter's transparent copies and in sprites. So art never uses it. Our
 * opaque palette index 0 is written as another index that comes out the same
 * on the V9938 (black's twin, bright black), and transparent becomes 0.
 */
public final class VdpPalette {

    /** The V9938's transparent colour number. */
    public static final int TRANSPARENT = 0;

    private static final int LEVELS = 7;
    private static final int FULL = 255;

    private final Palette palette;
    private final int[] red = new int[Palette.SIZE];
    private final int[] green = new int[Palette.SIZE];
    private final int[] blue = new int[Palette.SIZE];
    private final int twinOfZero;

    public VdpPalette(Palette palette) {
        this.palette = palette;
        int[] argb = palette.toArgb();
        for (int i = 0; i < Palette.SIZE; i++) {
            red[i] = threeBits(argb[i] >> 16 & 0xFF);
            green[i] = threeBits(argb[i] >> 8 & 0xFF);
            blue[i] = threeBits(argb[i] & 0xFF);
        }
        int twin = -1;
        for (int i = 1; i < Palette.SIZE && twin < 0; i++) {
            if (red[i] == red[0] && green[i] == green[0] && blue[i] == blue[0]) {
                twin = i;
            }
        }
        if (twin < 0) {
            throw new ConvertException("data/art/palette.json: colour 0 (" + name(0) + ") has no twin among"
                    + " colours 1-15 that is the same on the V9938, so art cannot use it: colour 0 is the"
                    + " V9938's transparent one");
        }
        this.twinOfZero = twin;
    }

    /** 0..255 to the nearest of the V9938's 0..7. */
    static int threeBits(int v) {
        return (v * LEVELS + FULL / 2) / FULL;
    }

    /** 0..7 back to 0..255, for the report. */
    static int eightBits(int v) {
        return (v * FULL + LEVELS / 2) / LEVELS;
    }

    /** The colour number a pixel of palette index {@code index} (-1 transparent) is written as. */
    public int vdpColour(int index) {
        if (index < 0) {
            return TRANSPARENT;
        }
        return index == 0 ? twinOfZero : index;
    }

    /**
     * Back from a colour number to a palette index, -1 for transparent. The
     * twin comes back as 0: the two are the same colour on screen.
     */
    public int paletteIndex(int vdpColour) {
        if (vdpColour == TRANSPARENT) {
            return -1;
        }
        return vdpColour == twinOfZero ? 0 : vdpColour;
    }

    public int twinOfZero() {
        return twinOfZero;
    }

    /** VDP_SetPalette's two bytes for each colour: [0|red:3|0|blue:3], [0:5|green:3]. */
    public byte[] registers() {
        byte[] out = new byte[Palette.SIZE * 2];
        for (int i = 0; i < Palette.SIZE; i++) {
            out[2 * i] = (byte) (red[i] << 4 | blue[i]);
            out[2 * i + 1] = (byte) green[i];
        }
        return out;
    }

    public String header() {
        byte[] reg = registers();
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/art/palette.json"));
        sb.append("#pragma once\n\n");
        sb.append("// Art never uses colour 0, the V9938's transparent one; palette colour 0\n");
        sb.append("// is written as its twin.\n");
        sb.append("#define COLOUR_TWIN_OF_0\t").append(twinOfZero).append('\n');
        sb.append("\nstatic const unsigned char g_Palette[16 * 2] = {\n");
        for (int i = 0; i < Palette.SIZE; i++) {
            sb.append(String.format("\t0x%02X, 0x%02X,\t// %2d %s %s%n", reg[2 * i], reg[2 * i + 1], i, name(i), rgb(i)));
        }
        sb.append("};\n");
        return sb.toString();
    }

    /** How far each colour moved on its way to the V9938, worst channel first. */
    public List<String> report() {
        int[] argb = palette.toArgb();
        List<String> lines = new ArrayList<>();
        for (int i = 0; i < Palette.SIZE; i++) {
            int dr = Math.abs((argb[i] >> 16 & 0xFF) - eightBits(red[i]));
            int dg = Math.abs((argb[i] >> 8 & 0xFF) - eightBits(green[i]));
            int db = Math.abs((argb[i] & 0xFF) - eightBits(blue[i]));
            lines.add(String.format("%2d %-14s %s -> #%02X%02X%02X  (%d,%d,%d of 7)  moved up to %d of 255",
                    i, name(i), rgb(i), eightBits(red[i]), eightBits(green[i]), eightBits(blue[i]),
                    red[i], green[i], blue[i], Math.max(dr, Math.max(dg, db))));
        }
        return lines;
    }

    private String name(int i) {
        return palette.entries().stream().filter(e -> e.i() == i).findFirst().orElseThrow().name();
    }

    private String rgb(int i) {
        return palette.entries().stream().filter(e -> e.i() == i).findFirst().orElseThrow().rgb();
    }
}
