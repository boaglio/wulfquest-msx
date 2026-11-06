package wulfmsx.convert;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import wulf.Content;
import wulf.data.Palette;

/**
 * The game's 16 colours on the V9938 (AGENTS.md §4, §5): each the nearest of
 * its 512, three bits a channel, and the colour number (1-15) the art is
 * written with.
 *
 * <p>Colour number 0 is transparent on the V9938 wherever it matters to us:
 * in the blitter's transparent copies and in sprites. So art never uses it;
 * transparent pixels are written as 0, and the 15 colours the palette has on
 * the V9938 (black and bright black are one) take numbers 1-15. Register 0
 * holds black too, for the border and the panel's clear.
 *
 * <p>Which colour gets which number is chosen for the sprites (M5): two
 * sprites laid over each other with the CC bit show the OR of their numbers
 * where they overlap, so the numbers are ordered so that the colour pairs
 * sprite lines need are made that way ({@link SpriteCut#order}). The scenery
 * does not care: it is written with whatever numbers the order gives.
 */
public final class VdpPalette {

    /** The V9938's transparent colour number. */
    public static final int TRANSPARENT = 0;
    /** Colour numbers art can use: 1-15. */
    public static final int CODES = 15;

    private static final int LEVELS = 7;
    private static final int FULL = 255;

    private final Palette palette;
    private final int[] red = new int[Palette.SIZE];
    private final int[] green = new int[Palette.SIZE];
    private final int[] blue = new int[Palette.SIZE];
    /** Palette index -> its group: the distinct colours on the V9938, in palette order. */
    private final int[] group = new int[Palette.SIZE];
    private final int groups;
    /** Palette index -> colour number, 1-15. */
    private final int[] code = new int[Palette.SIZE];

    /** The game's palette, its colour numbers ordered for the sprites the game shows. */
    public static VdpPalette forGame(Content c) {
        VdpPalette plain = new VdpPalette(c.palette());
        return new VdpPalette(c.palette(), SpriteCut.order(plain, SpriteCut.lines(c, plain)));
    }

    /** In palette order: group g (the g-th distinct colour) is number g + 1. */
    public VdpPalette(Palette palette) {
        this(palette, null);
    }

    /**
     * @param byGroup colour number (1-15) for each distinct colour, in palette
     *                order of first appearance; null for 1, 2, 3...
     */
    public VdpPalette(Palette palette, int[] byGroup) {
        this.palette = palette;
        int[] argb = palette.toArgb();
        int n = 0;
        for (int i = 0; i < Palette.SIZE; i++) {
            red[i] = threeBits(argb[i] >> 16 & 0xFF);
            green[i] = threeBits(argb[i] >> 8 & 0xFF);
            blue[i] = threeBits(argb[i] & 0xFF);
            group[i] = -1;
            for (int j = 0; j < i && group[i] < 0; j++) {
                if (red[j] == red[i] && green[j] == green[i] && blue[j] == blue[i]) {
                    group[i] = group[j];
                }
            }
            if (group[i] < 0) {
                group[i] = n++;
            }
        }
        if (n > CODES) {
            throw new ConvertException("data/art/palette.json: " + n + " different colours on the V9938; with"
                    + " colour 0 transparent it shows " + CODES);
        }
        this.groups = n;
        int[] order = byGroup;
        if (order == null) {
            order = new int[n];
            for (int g = 0; g < n; g++) {
                order[g] = g + 1;
            }
        }
        if (order.length != n) {
            throw new IllegalArgumentException(order.length + " numbers for " + n + " colours");
        }
        boolean[] used = new boolean[CODES + 1];
        for (int g = 0; g < n; g++) {
            if (order[g] < 1 || order[g] > CODES || used[order[g]]) {
                throw new IllegalArgumentException("colour numbers " + Arrays.toString(order));
            }
            used[order[g]] = true;
        }
        for (int i = 0; i < Palette.SIZE; i++) {
            code[i] = order[group[i]];
        }
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
        return index < 0 ? TRANSPARENT : code[index];
    }

    /** Back from a colour number to a palette index (the first with it), -1 for transparent or unused. */
    public int paletteIndex(int vdpColour) {
        for (int i = 0; i < Palette.SIZE && vdpColour != TRANSPARENT; i++) {
            if (code[i] == vdpColour) {
                return i;
            }
        }
        return -1;
    }

    /** The distinct colours on the V9938. */
    public int groups() {
        return groups;
    }

    /** Palette index -> its distinct colour, 0..groups()-1. */
    public int group(int index) {
        return group[index];
    }

    /** A colour number's colour on the V9938 as 0xRRGGBB at 3 bits a channel (black for 0 and unused ones). */
    public int rgb3(int vdpColour) {
        int i = vdpColour == TRANSPARENT ? 0 : paletteIndex(vdpColour);
        if (i < 0) {
            i = 0;
        }
        return red[i] << 16 | green[i] << 8 | blue[i];
    }

    /** How far apart two colour numbers look: squared distance in V9938 levels. */
    public int distance(int a, int b) {
        int x = rgb3(a);
        int y = rgb3(b);
        int dr = (x >> 16) - (y >> 16);
        int dg = (x >> 8 & 0xFF) - (y >> 8 & 0xFF);
        int db = (x & 0xFF) - (y & 0xFF);
        return dr * dr + dg * dg + db * db;
    }

    /**
     * VDP_SetPalette's two bytes for each colour number: [0|red:3|0|blue:3],
     * [0:5|green:3]. Number 0 is palette colour 0 (black), as are unused ones.
     */
    public byte[] registers() {
        byte[] out = new byte[Palette.SIZE * 2];
        for (int c = 0; c < Palette.SIZE; c++) {
            int v = rgb3(c);
            out[2 * c] = (byte) ((v >> 16) << 4 | (v & 0xFF));
            out[2 * c + 1] = (byte) (v >> 8 & 0xFF);
        }
        return out;
    }

    public String header() {
        byte[] reg = registers();
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/art/palette.json"));
        sb.append("#pragma once\n\n");
        sb.append("// The V9938's 16 colour numbers. Art never uses 0, the transparent one;\n");
        sb.append("// the order is chosen for the sprites' OR (convert/.../SpriteCut.java).\n");
        sb.append("// Each palette colour's number:\n");
        for (int i = 0; i < Palette.SIZE; i++) {
            sb.append("#define COLOUR_").append(Gen.cName(name(i))).append('\t').append(code[i]).append('\n');
        }
        sb.append("\nstatic const unsigned char g_Palette[16 * 2] = {\n");
        for (int c = 0; c < Palette.SIZE; c++) {
            int i = c == TRANSPARENT ? 0 : paletteIndex(c);
            sb.append(String.format("\t0x%02X, 0x%02X,\t// %2d %s%n", reg[2 * c], reg[2 * c + 1], c,
                    c == TRANSPARENT ? "(transparent; black)" : i < 0 ? "(unused; black)" : namesOf(c)));
        }
        sb.append("};\n");
        return sb.toString();
    }

    /** How far each colour moved on its way to the V9938, and its number. */
    public List<String> report() {
        int[] argb = palette.toArgb();
        List<String> lines = new ArrayList<>();
        for (int i = 0; i < Palette.SIZE; i++) {
            int dr = Math.abs((argb[i] >> 16 & 0xFF) - eightBits(red[i]));
            int dg = Math.abs((argb[i] >> 8 & 0xFF) - eightBits(green[i]));
            int db = Math.abs((argb[i] & 0xFF) - eightBits(blue[i]));
            lines.add(String.format("%2d %-14s %s -> #%02X%02X%02X  (%d,%d,%d of 7)  moved up to %3d of 255;  number %2d",
                    i, name(i), rgb(i), eightBits(red[i]), eightBits(green[i]), eightBits(blue[i]),
                    red[i], green[i], blue[i], Math.max(dr, Math.max(dg, db)), code[i]));
        }
        return lines;
    }

    String name(int i) {
        return palette.entries().stream().filter(e -> e.i() == i).findFirst().orElseThrow().name();
    }

    private String namesOf(int c) {
        List<String> names = new ArrayList<>();
        for (int i = 0; i < Palette.SIZE; i++) {
            if (code[i] == c) {
                names.add(name(i));
            }
        }
        return String.join(", ", names);
    }

    private String rgb(int i) {
        return palette.entries().stream().filter(e -> e.i() == i).findFirst().orElseThrow().rgb();
    }
}
