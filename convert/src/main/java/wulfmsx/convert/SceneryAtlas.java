package wulfmsx.convert;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import wulf.data.SpriteData;
import wulf.world.SceneryCatalog;

/**
 * The 41 scenery objects packed into one picture for video RAM pages 2-3
 * (AGENTS.md §4, §5), where the blitter copies them from to draw a room.
 *
 * <p>SCREEN 5 lays a page out as 256 pixels a line, two to a byte, the left
 * one in the high nibble; pages 2 and 3 are lines 512-1023 of video RAM. The
 * objects are packed in shelves, tallest first. Every object is a whole
 * number of 8-pixel cells, so every copy starts on a byte.
 */
public final class SceneryAtlas {

    public static final int WIDTH = 256;
    public static final int BYTES_PER_LINE = WIDTH / 2;
    /** Video RAM pages 2 and 3: 512 lines from line 512. */
    public static final int VRAM_FIRST_LINE = 512;
    public static final int MAX_LINES = 512;
    /** One ASCII16 ROM segment. */
    public static final int CHUNK_BYTES = 16 * 1024;
    public static final int CHUNK_LINES = CHUNK_BYTES / BYTES_PER_LINE;

    /** One object's place in the atlas, in pixels from its top-left. */
    public record Slot(String id, int x, int y, int w, int h) {
    }

    private final List<Slot> slots;
    private final byte[] pixels;
    private final int lines;

    public SceneryAtlas(SceneryCatalog catalog, Map<String, SpriteData> sprites, VdpPalette palette) {
        List<String> ids = new ArrayList<>(catalog.ids());
        List<String> order = new ArrayList<>(ids);
        order.sort(Comparator.comparingInt((String id) -> -catalog.piece(id).h())
                .thenComparingInt(id -> -catalog.piece(id).w())
                .thenComparing(Comparator.naturalOrder()));

        Map<String, Slot> placed = new java.util.HashMap<>();
        int x = 0;
        int y = 0;
        int shelf = 0;
        for (String id : order) {
            int w = catalog.piece(id).w() * SceneryCatalog.CELL;
            int h = catalog.piece(id).h() * SceneryCatalog.CELL;
            if (x + w > WIDTH) {
                y += shelf;
                x = 0;
                shelf = 0;
            }
            placed.put(id, new Slot(id, x, y, w, h));
            x += w;
            shelf = Math.max(shelf, h);
        }
        this.lines = y + shelf;
        if (lines > MAX_LINES) {
            throw new ConvertException("data/world/scenery.json: the scenery needs " + lines
                    + " lines of video RAM and pages 2-3 have " + MAX_LINES);
        }

        this.pixels = new byte[WIDTH * lines];
        java.util.Arrays.fill(pixels, (byte) VdpPalette.TRANSPARENT);
        List<Slot> inIdOrder = new ArrayList<>();
        for (String id : ids) {
            Slot s = placed.get(id);
            inIdOrder.add(s);
            byte[] art = art(catalog.piece(id), sprites);
            for (int py = 0; py < s.h(); py++) {
                for (int px = 0; px < s.w(); px++) {
                    pixels[(s.y() + py) * WIDTH + s.x() + px] = (byte) palette.vdpColour(art[py * s.w() + px]);
                }
            }
        }
        this.slots = List.copyOf(inIdOrder);
    }

    /** The object's art as palette indices, -1 transparent: the frame the Java game draws. */
    static byte[] art(SceneryCatalog.Piece piece, Map<String, SpriteData> sprites) {
        SpriteData sprite = sprites.get(piece.sprite());
        return sprite.decode("data/art/sprites/" + piece.sprite() + ".sprite.json").get(sprite.frames().get(0).id());
    }

    /** Every object, in id order; an object's number in the room tables is its place here. */
    public List<Slot> slots() {
        return slots;
    }

    public int index(String id) {
        for (int i = 0; i < slots.size(); i++) {
            if (slots.get(i).id().equals(id)) {
                return i;
            }
        }
        throw new IllegalArgumentException("no scenery object " + id);
    }

    public int lines() {
        return lines;
    }

    /** The colour number at an atlas pixel. */
    public int colourAt(int x, int y) {
        return pixels[y * WIDTH + x];
    }

    /** The atlas as video RAM bytes, in ROM segments of {@link #CHUNK_LINES} lines; the last one shorter. */
    public List<byte[]> chunks() {
        List<byte[]> out = new ArrayList<>();
        for (int first = 0; first < lines; first += CHUNK_LINES) {
            int n = Math.min(CHUNK_LINES, lines - first);
            byte[] chunk = new byte[n * BYTES_PER_LINE];
            for (int i = 0; i < chunk.length; i++) {
                int p = first * WIDTH + 2 * i;
                chunk[i] = (byte) (pixels[p] << 4 | pixels[p + 1]);
            }
            out.add(chunk);
        }
        return out;
    }

    public String header() {
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/world/scenery.json and data/art/sprites/"));
        sb.append("#pragma once\n\n");
        sb.append("// The scenery atlas: video RAM from line SCENERY_VRAM_LINE, 256 pixels a\n");
        sb.append("// line, two a byte; in the ROM as SCENERY_CHUNKS files of up to\n");
        sb.append("// SCENERY_CHUNK_LINES lines (scenery_0.bin ...).\n");
        sb.append("#define SCENERY_COUNT\t\t").append(slots.size()).append('\n');
        sb.append("#define SCENERY_VRAM_LINE\t").append(VRAM_FIRST_LINE).append('\n');
        sb.append("#define SCENERY_LINES\t\t").append(lines).append('\n');
        sb.append("#define SCENERY_CHUNK_LINES\t").append(CHUNK_LINES).append('\n');
        sb.append("#define SCENERY_CHUNKS\t\t").append(chunks().size()).append("\n\n");
        for (int i = 0; i < slots.size(); i++) {
            sb.append("#define SCENERY_").append(Gen.cName(slots.get(i).id())).append('\t').append(i).append('\n');
        }
        sb.append("\n// Each object's place in the atlas, in pixels; y from SCENERY_VRAM_LINE.\n");
        table(sb, "unsigned char", "g_SceneryX", Slot::x);
        table(sb, "unsigned short", "g_SceneryY", Slot::y);
        table(sb, "unsigned char", "g_SceneryW", Slot::w);
        table(sb, "unsigned char", "g_SceneryH", Slot::h);
        return sb.toString();
    }

    private void table(StringBuilder sb, String type, String name, java.util.function.ToIntFunction<Slot> f) {
        sb.append("static const ").append(type).append(' ').append(name).append("[SCENERY_COUNT] = {");
        for (int i = 0; i < slots.size(); i++) {
            sb.append(i % 12 == 0 ? "\n\t" : " ").append(f.applyAsInt(slots.get(i))).append(',');
        }
        sb.append("\n};\n");
    }
}
