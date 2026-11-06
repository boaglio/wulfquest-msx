package wulfmsx.convert;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import wulf.Content;
import wulf.data.SpriteData;

/**
 * Art into the V9938's layered sprites (AGENTS.md §5, §10 M5).
 *
 * <p>In sprite mode 2 every 16x16 sprite has a colour per line, and a sprite
 * with the CC bit set laid over the one before it shows, where both have a
 * pixel, the OR of their colour numbers. So a 16x16 block of art is two
 * sprites, a base and an overlay, and a line of it can have three colours:
 * the base's {@code a}, the overlay's {@code b}, and {@code a | b}. Art is cut
 * into such blocks ({@link #cut}); the palette's numbers are ordered so that
 * as many lines as can be are made exactly ({@link #order}). A line with more
 * colours than that, or three that no OR makes, has its other pixels shown in
 * the nearest colour it can make: a {@link Change}, which the converter
 * reports, every one.
 *
 * <p>A cut frame is its blocks, row by row, as sprite-mode-2 data: first each
 * block's base and overlay patterns (32 bytes each, the left 8 columns' 16
 * rows, then the right 8's), then each block's base and overlay colours (16
 * bytes each, a line a byte; the overlay's with CC, {@link #CC}). The early
 * clock bit is the ROM's to set.
 */
public final class SpriteCut {

    public static final int BLOCK = 16;
    public static final int PATTERN_BYTES = 32;
    public static final int COLOUR_BYTES = 16;
    /** One block, both layers: patterns and colours. */
    public static final int BLOCK_BYTES = 2 * (PATTERN_BYTES + COLOUR_BYTES);
    /** The colour byte's CC bit: OR with the sprite before. */
    public static final int CC = 0x40;

    static final String LIST = "/wulfmsx/convert/sprites.json";
    private static final int RESTARTS = 24;
    private static final long SEED = 0x5EED;

    /** A pixel shown in another colour than the art's: palette index {@code from}, shown as number {@code to}. */
    public record Change(String sprite, String frame, int x, int y, int from, int to) {
    }

    /** A cut frame: its bytes, and the pixels it shows otherwise than the art. */
    public record Frame(byte[] bytes, List<Change> changes) {
    }

    /** The list in sprites.json. */
    public record Sprites(List<String> rom, List<String> later, Map<String, Integer> accepted) {

        List<String> all(Content c) {
            List<String> out = new ArrayList<>();
            out.add(c.player().sprite());
            out.addAll(rom);
            out.addAll(later);
            return out;
        }
    }

    private SpriteCut() {
    }

    public static Sprites sprites(Content c) {
        JsonNode n;
        try (InputStream in = SpriteCut.class.getResourceAsStream(LIST)) {
            n = JsonMapper.builder().build().readTree(in);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        List<String> rom = names(n.get("rom"));
        List<String> later = names(n.get("later"));
        Map<String, Integer> accepted = new LinkedHashMap<>();
        for (Map.Entry<String, JsonNode> e : n.get("accepted").properties()) {
            accepted.put(e.getKey(), e.getValue().asInt());
        }
        Sprites s = new Sprites(rom, later, accepted);
        for (String name : s.all(c)) {
            if (!c.sprites().all().containsKey(name)) {
                throw new ConvertException("convert/.../sprites.json names sprite '" + name
                        + "', which data/art/sprites/ has not got");
            }
        }
        return s;
    }

    private static List<String> names(JsonNode list) {
        List<String> out = new ArrayList<>();
        for (JsonNode s : list) {
            out.add(s.asString());
        }
        return List.copyOf(out);
    }

    /**
     * Every block line of every frame of every sprite the game shows as
     * hardware sprites, as how many pixels of each distinct colour it has
     * (by {@link VdpPalette#group}), with how often each such line occurs.
     */
    static Map<List<Integer>, Integer> lines(Content c, VdpPalette plain) {
        Map<List<Integer>, Integer> lines = new HashMap<>();
        int groups = plain.groups();
        for (String name : sprites(c).all(c)) {
            SpriteData sprite = c.sprites().get(name);
            int w = sprite.size().w();
            int h = sprite.size().h();
            for (byte[] px : sprite.decode("data/art/sprites/" + name + ".sprite.json").values()) {
                for (int y = 0; y < h; y++) {
                    for (int x0 = 0; x0 < w; x0 += BLOCK) {
                        int[] count = new int[groups];
                        boolean any = false;
                        for (int x = x0; x < Math.min(w, x0 + BLOCK); x++) {
                            int i = px[y * w + x];
                            if (i >= 0) {
                                count[plain.group(i)]++;
                                any = true;
                            }
                        }
                        if (any) {
                            List<Integer> key = new ArrayList<>();
                            for (int g = 0; g < groups; g++) {
                                if (count[g] > 0) {
                                    key.add(g);
                                    key.add(count[g]);
                                }
                            }
                            lines.merge(List.copyOf(key), 1, Integer::sum);
                        }
                    }
                }
            }
        }
        return lines;
    }

    /**
     * The colour numbers, by distinct colour (VdpPalette's groups), that leave
     * the fewest pixels changed in {@code lines}: a hill climb over swaps from
     * seeded random starts, the same every run.
     */
    static int[] order(VdpPalette plain, Map<List<Integer>, Integer> lines) {
        int groups = plain.groups();
        // Lines in a fixed order, as groups and counts, and which lines each group is in.
        List<List<Integer>> keys = new ArrayList<>(lines.keySet());
        keys.sort((a, b) -> Arrays.compare(a.stream().mapToInt(Integer::intValue).toArray(),
                b.stream().mapToInt(Integer::intValue).toArray()));
        int n = keys.size();
        int[][] g = new int[n][];
        int[][] k = new int[n][];
        int[] mult = new int[n];
        List<List<Integer>> having = new ArrayList<>();
        for (int i = 0; i < VdpPalette.CODES; i++) {
            having.add(new ArrayList<>());
        }
        for (int i = 0; i < n; i++) {
            List<Integer> key = keys.get(i);
            g[i] = new int[key.size() / 2];
            k[i] = new int[key.size() / 2];
            for (int j = 0; j < g[i].length; j++) {
                g[i][j] = key.get(2 * j);
                k[i][j] = key.get(2 * j + 1);
                having.get(g[i][j]).add(i);
            }
            mult[i] = lines.get(key);
        }
        int[][] in = new int[VdpPalette.CODES][];
        for (int i = 0; i < VdpPalette.CODES; i++) {
            in[i] = having.get(i).stream().mapToInt(Integer::intValue).toArray();
        }

        // slot[c - 1] is the group with number c, or -1: a permutation of the 15 numbers.
        Random rng = new Random(SEED);
        int[] best = null;
        long bestCost = Long.MAX_VALUE;
        int[] cost = new int[n];
        for (int r = 0; r < RESTARTS; r++) {
            int[] slot = new int[VdpPalette.CODES];
            for (int i = 0; i < slot.length; i++) {
                slot[i] = i < groups ? i : -1;
            }
            for (int i = slot.length - 1; i > 0; i--) {
                int j = rng.nextInt(i + 1);
                int t = slot[i];
                slot[i] = slot[j];
                slot[j] = t;
            }
            int[] code = codes(slot, groups);
            long total = 0;
            for (int i = 0; i < n; i++) {
                cost[i] = lineCost(g[i], k[i], code);
                total += (long) cost[i] * mult[i];
            }
            boolean better = true;
            while (better) {
                better = false;
                for (int a = 0; a < slot.length; a++) {
                    for (int b = a + 1; b < slot.length; b++) {
                        if (slot[a] < 0 && slot[b] < 0) {
                            continue;
                        }
                        swap(slot, a, b, code);
                        long delta = 0;
                        int[] touched = touched(in, slot[a], slot[b]);
                        int[] now = new int[touched.length];
                        for (int t = 0; t < touched.length; t++) {
                            int i = touched[t];
                            now[t] = lineCost(g[i], k[i], code);
                            delta += (long) (now[t] - cost[i]) * mult[i];
                        }
                        if (delta < 0) {
                            for (int t = 0; t < touched.length; t++) {
                                cost[touched[t]] = now[t];
                            }
                            total += delta;
                            better = true;
                        } else {
                            swap(slot, a, b, code);
                        }
                    }
                }
            }
            if (total < bestCost) {
                bestCost = total;
                best = codes(slot, groups);
            }
        }
        return best;
    }

    private static int[] codes(int[] slot, int groups) {
        int[] code = new int[groups];
        for (int i = 0; i < slot.length; i++) {
            if (slot[i] >= 0) {
                code[slot[i]] = i + 1;
            }
        }
        return code;
    }

    private static void swap(int[] slot, int a, int b, int[] code) {
        int t = slot[a];
        slot[a] = slot[b];
        slot[b] = t;
        if (slot[a] >= 0) {
            code[slot[a]] = a + 1;
        }
        if (slot[b] >= 0) {
            code[slot[b]] = b + 1;
        }
    }

    /** The lines either group is in, each once. */
    private static int[] touched(int[][] in, int x, int y) {
        if (x < 0) {
            return in[y];
        }
        if (y < 0) {
            return in[x];
        }
        int[] out = Arrays.copyOf(in[x], in[x].length + in[y].length);
        int m = in[x].length;
        for (int i : in[y]) {
            if (Arrays.binarySearch(in[x], i) < 0) {
                out[m++] = i;
            }
        }
        return Arrays.copyOf(out, m);
    }

    /** The pixels a line must change at best: those no pair of its colours makes. */
    static int lineCost(int[] groups, int[] counts, int[] code) {
        int best = Integer.MAX_VALUE;
        for (int i = 0; i < groups.length; i++) {
            for (int j = i; j < groups.length; j++) {
                int a = code[groups[i]];
                int b = code[groups[j]];
                int c = 0;
                for (int t = 0; t < groups.length; t++) {
                    int x = code[groups[t]];
                    if (x != a && x != b && x != (a | b)) {
                        c += counts[t];
                    }
                }
                best = Math.min(best, c);
            }
        }
        return best;
    }

    /** One frame of art (palette indices, -1 transparent) in blocks of two layers. */
    static Frame cut(VdpPalette p, String sprite, String frame, byte[] px, int w, int h) {
        int cols = (w + BLOCK - 1) / BLOCK;
        int rows = (h + BLOCK - 1) / BLOCK;
        int blocks = cols * rows;
        byte[] out = new byte[blocks * BLOCK_BYTES];
        int colours = blocks * 2 * PATTERN_BYTES;
        List<Change> changes = new ArrayList<>();
        for (int by = 0; by < rows; by++) {
            for (int bx = 0; bx < cols; bx++) {
                int b = by * cols + bx;
                int base = b * 2 * PATTERN_BYTES;
                int over = base + PATTERN_BYTES;
                for (int ly = 0; ly < BLOCK; ly++) {
                    int y = by * BLOCK + ly;
                    int[] line = new int[BLOCK];
                    Arrays.fill(line, -1);
                    for (int lx = 0; lx < BLOCK; lx++) {
                        int x = bx * BLOCK + lx;
                        if (x < w && y < h) {
                            line[lx] = px[y * w + x];
                        }
                    }
                    int[] ab = layers(p, line);
                    int a = ab[0];
                    int c = ab[1];
                    out[colours + b * 2 * COLOUR_BYTES + ly] = (byte) a;
                    out[colours + b * 2 * COLOUR_BYTES + COLOUR_BYTES + ly] = (byte) (c | CC);
                    for (int lx = 0; lx < BLOCK; lx++) {
                        if (line[lx] < 0) {
                            continue;
                        }
                        int want = p.vdpColour(line[lx]);
                        int shown = nearest(p, want, a, c);
                        if (shown != want) {
                            changes.add(new Change(sprite, frame, bx * BLOCK + lx, y, line[lx], shown));
                        }
                        int bit = 0x80 >> (lx % 8);
                        int at = (lx / 8) * 16 + ly;
                        if (shown == a || shown == (a | c) && c != 0) {
                            out[base + at] |= (byte) bit;
                        }
                        if (shown != a && (shown == c || shown == (a | c))) {
                            out[over + at] |= (byte) bit;
                        }
                    }
                }
            }
        }
        return new Frame(out, List.copyOf(changes));
    }

    /**
     * A line's base and overlay colours: of the pairs of its own colours, the
     * one that changes fewest pixels, then the one that changes them least.
     */
    static int[] layers(VdpPalette p, int[] line) {
        int[] present = Arrays.stream(line).filter(i -> i >= 0).map(p::vdpColour).distinct().sorted().toArray();
        if (present.length == 0) {
            return new int[] {0, 0};
        }
        if (present.length == 1) {
            return new int[] {present[0], 0};
        }
        int[] best = null;
        long bestScore = Long.MAX_VALUE;
        for (int i = 0; i < present.length; i++) {
            for (int j = i + 1; j < present.length; j++) {
                int a = present[i];
                int b = present[j];
                long changed = 0;
                long far = 0;
                for (int px : line) {
                    if (px < 0) {
                        continue;
                    }
                    int want = p.vdpColour(px);
                    int shown = nearest(p, want, a, b);
                    if (shown != want) {
                        changed++;
                        far += p.distance(want, shown);
                    }
                }
                long score = changed << 32 | far;
                if (score < bestScore) {
                    bestScore = score;
                    best = new int[] {a, b};
                }
            }
        }
        return best;
    }

    /** What a pixel wanting {@code want} shows with base {@code a} and overlay {@code b}: it, or the nearest made. */
    static int nearest(VdpPalette p, int want, int a, int b) {
        int[] made = b == 0 ? new int[] {a} : new int[] {a, b, a | b};
        for (int m : made) {
            if (m == want) {
                return m;
            }
        }
        int best = made[0];
        for (int m : made) {
            if (p.distance(want, m) < p.distance(want, best)) {
                best = m;
            }
        }
        return best;
    }

    /**
     * Back from a cut frame to colour numbers, as the V9938 shows it, w x h:
     * a pixel is the base's colour, ORed with the overlay's where both are
     * set, or the overlay's alone. For the tests.
     */
    static int[] shown(byte[] bytes, int w, int h) {
        int cols = (w + BLOCK - 1) / BLOCK;
        int rows = (h + BLOCK - 1) / BLOCK;
        int blocks = cols * rows;
        int colours = blocks * 2 * PATTERN_BYTES;
        int[] out = new int[w * h];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int b = (y / BLOCK) * cols + x / BLOCK;
                int ly = y % BLOCK;
                int lx = x % BLOCK;
                int at = (lx / 8) * 16 + ly;
                int bit = 0x80 >> (lx % 8);
                boolean on0 = (bytes[b * 2 * PATTERN_BYTES + at] & bit) != 0;
                boolean on1 = (bytes[b * 2 * PATTERN_BYTES + PATTERN_BYTES + at] & bit) != 0;
                int a = bytes[colours + b * 2 * COLOUR_BYTES + ly] & 0x0F;
                int c = bytes[colours + b * 2 * COLOUR_BYTES + COLOUR_BYTES + ly] & 0x0F;
                out[y * w + x] = on0 ? (on1 ? a | c : a) : on1 ? c : VdpPalette.TRANSPARENT;
            }
        }
        return out;
    }
}
