package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import wulf.data.Palette;
import wulf.data.PlayerData;
import wulf.sim.Direction8;

/**
 * Vale's blade as hardware sprites (AGENTS.md §10, M5; the Java game's §11.6
 * and {@code PlayerPainter.paintBlade}): for each way he faces and each of the
 * fence's poses (the guards, then the thrusts), the blade's pixels out of his
 * hand, in two one-colour sprites: the blade in bright white and the
 * crossguard in bright yellow over it.
 *
 * <p>{@code blade.bin}: {@link #SHAPE_BYTES} a shape, shape = facing (the
 * Java game's Direction8 order) * poses + pose; each the blade's 16x16
 * pattern, then the guard's. {@code blade.h}: where each sprite's top-left is
 * from the hand, and how the ROM picks the pose as the Java game does.
 */
public final class Blade {

    public static final int SHAPE_BYTES = 2 * SpriteCut.PATTERN_BYTES;
    /**
     * {@code PlayerPainter.pose}'s hash, {@code (window + 1) * 0x9E3779B97F4A7C15L},
     * {@code h ^= h >>> 29}, its low bits: they come from the multiplier's low 32
     * bits alone, so the ROM multiplies 32 bits. (Code in the Java game, not
     * data; {@code BladeTest} holds it to {@code pose}.)
     */
    public static final long HASH_LOW = 0x7F4A7C15L;
    public static final int HASH_SHIFT = 29;

    private final int poses;
    private final int guards;
    private final byte[] blob;
    private final int[][] offsets;   // shape -> {blade x, blade y, guard x, guard y}
    private final List<String> dropped = new ArrayList<>();
    private final List<List<int[]>> pixels = new ArrayList<>();
    private final int ink;
    private final int hilt;
    private final PlayerData.Sabre sabre;

    public Blade(PlayerData rules, Palette palette, VdpPalette vdp) {
        this.sabre = rules.sabre();
        PlayerData.Fence fence = sabre.fence();
        this.guards = fence.guards().size();
        if (fence.thrusts().size() != guards || Integer.bitCount(guards) != 1) {
            throw new ConvertException("data/entities/player.json: the fence has " + guards + " guards and "
                    + fence.thrusts().size() + " thrusts; the MSX2 picks one by the hash's low bits, so"
                    + " both must be the same power of two");
        }
        this.poses = 2 * guards;
        this.ink = vdp.vdpColour(palette.indexOf("brightWhite"));
        this.hilt = vdp.vdpColour(palette.indexOf("brightYellow"));
        Direction8[] facings = Direction8.values();
        this.blob = new byte[facings.length * poses * SHAPE_BYTES];
        this.offsets = new int[facings.length * poses][];
        for (Direction8 d : facings) {
            for (int p = 0; p < poses; p++) {
                List<Integer> pose = p < guards ? fence.guards().get(p) : fence.thrusts().get(p - guards);
                int s = d.ordinal() * poses + p;
                boolean[][] white = new boolean[64][64];
                boolean[][] yellow = new boolean[64][64];
                draw(d, pose, white, yellow);
                List<int[]> px = new ArrayList<>();
                for (int y = 0; y < 64; y++) {
                    for (int x = 0; x < 64; x++) {
                        if (white[y][x] || yellow[y][x]) {
                            px.add(new int[] {x - 32, y - 32, yellow[y][x] ? hilt : ink});
                        }
                    }
                }
                pixels.add(px);
                int[] w = pack(white, s * SHAPE_BYTES, d + " " + pose + " blade");
                int[] y = pack(yellow, s * SHAPE_BYTES + SpriteCut.PATTERN_BYTES, d + " " + pose + " crossguard");
                offsets[s] = new int[] {w[0], w[1], y[0], y[1]};
            }
        }
    }

    /** paintBlade with the hand at (32, 32): the blade, and the crossguard over it. */
    private void draw(Direction8 d, List<Integer> pose, boolean[][] white, boolean[][] yellow) {
        int fx = d.dx();
        int fy = d.dy();
        int vx = fx * pose.get(0) - fy * pose.get(1);
        int vy = fy * pose.get(0) + fx * pose.get(1);
        int reach = d.dx() == 0 ? sabre.bladePx() * sabre.fence().towardViewerPercent() / 100 : sabre.bladePx();
        boolean flat = Math.abs(vx) >= Math.abs(vy);
        for (int i = 1; i <= reach; i++) {
            fill(white, 32 + Math.floorDiv(vx * i, 16), 32 + Math.floorDiv(vy * i, 16), flat ? 1 : 2, flat ? 2 : 1);
        }
        int gx = 32 + Math.floorDiv(vx, 16);
        int gy = 32 + Math.floorDiv(vy, 16);
        if (flat) {
            fill(yellow, gx, gy - 2, 1, 6);
        } else {
            fill(yellow, gx - 2, gy, 6, 1);
        }
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                white[y][x] &= !yellow[y][x];
            }
        }
    }

    private static void fill(boolean[][] px, int x, int y, int w, int h) {
        for (int j = y; j < y + h; j++) {
            for (int i = x; i < x + w; i++) {
                px[j][i] = true;
            }
        }
    }

    /** A sprite's pattern from its top-left pixel; returns that top-left, from the hand. */
    private int[] pack(boolean[][] px, int at, String what) {
        int left = 64;
        int top = 64;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                if (px[y][x]) {
                    left = Math.min(left, x);
                    top = Math.min(top, y);
                }
            }
        }
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                if (!px[y][x]) {
                    continue;
                }
                int lx = x - left;
                int ly = y - top;
                if (lx >= SpriteCut.BLOCK || ly >= SpriteCut.BLOCK) {
                    dropped.add(what + ": pixel " + (x - 32) + "," + (y - 32) + " from the hand");
                    continue;
                }
                blob[at + (lx / 8) * 16 + ly] |= (byte) (0x80 >> (lx % 8));
            }
        }
        return new int[] {left - 32, top - 32};
    }

    /** A shape's pixels from the hand: {x, y, colour number}. */
    List<int[]> pixels(int shape) {
        return pixels.get(shape);
    }

    /**
     * The pose in its list (guards or thrusts) as the ROM picks it, from the
     * game tick: PlayerPainter.pose's hash in 32 bits.
     */
    static int poseByHash(long tick, int everyTicks, int perList) {
        long h = ((tick / everyTicks + 1) * HASH_LOW) & 0xFFFFFFFFL;
        return (int) ((h ^ (h >>> HASH_SHIFT)) & (perList - 1));
    }

    public byte[] blob() {
        return blob.clone();
    }

    /** Pixels past a sprite's 16x16, left out. */
    public List<String> dropped() {
        return List.copyOf(dropped);
    }

    public int shapes() {
        return offsets.length;
    }

    /** Each shape's sprites' top-left from the hand: blade x, y, crossguard x, y. */
    void tables(RamTables t) {
        int[] at = new int[offsets.length * 4];
        for (int i = 0; i < offsets.length; i++) {
            System.arraycopy(offsets[i], 0, at, 4 * i, 4);
        }
        t.i8("g_BladeAt", at, "The blade's shapes: each sprite's top-left from the hand, blade x, y, crossguard x, y.");
    }

    public String header() {
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/entities/player.json"));
        sb.append("#pragma once\n\n");
        sb.append("// Vale's blade (convert/.../Blade.java): blade.bin has BLADE_SHAPE_BYTES a shape,\n");
        sb.append("// shape = facing * BLADE_POSES + pose, the guards first, then the thrusts; each the\n");
        sb.append("// blade's 16x16 pattern, then the crossguard's.\n");
        sb.append("#define BLADE_SHAPES\t\t").append(offsets.length).append('\n');
        sb.append("#define BLADE_SHAPE_BYTES\t").append(SHAPE_BYTES).append('\n');
        sb.append("#define BLADE_POSES\t\t").append(poses).append('\n');
        sb.append("#define BLADE_GUARDS\t\t").append(guards).append('\n');
        sb.append("#define BLADE_INK\t\t").append(ink).append("\t// bright white\n");
        sb.append("#define BLADE_HILT\t\t").append(hilt).append("\t// bright yellow\n");
        sb.append("\n// PlayerPainter.pose: on guard for the first window of SABRE_FENCE_EVERY_TICKS\n");
        sb.append("// (constants.h) ticks into the blade, thrusting for the next; the pose in the list by a\n");
        sb.append("// hash of the game tick's window: h = (tick / SABRE_FENCE_EVERY_TICKS + 1) * BLADE_HASH\n");
        sb.append("// (32 bits), pose = (h ^ (h >> BLADE_HASH_SHIFT)) & (BLADE_GUARDS - 1).\n");
        sb.append(String.format("#define BLADE_HASH\t\t0x%08XUL%n", HASH_LOW));
        sb.append("#define BLADE_HASH_SHIFT\t").append(HASH_SHIFT).append('\n');
        return sb.toString();
    }
}
