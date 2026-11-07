package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import wulf.Content;
import wulf.data.SpriteData;

/**
 * The ROM's sprites besides Vale (AGENTS.md §10, M5 and M6): every frame of
 * each sprite sprites.json lists for the ROM, its mirrored ones included, cut
 * into 16x16 blocks of two layered sprites ({@link SpriteCut}); and for the
 * sprites it lists as silhouettes, each frame again as one colour, with
 * {@code _ink} after its id, for the ROM to draw tinted.
 *
 * <p>{@code sprites_N.bin}: the frames one after another, in chunks of a ROM segment, each its blocks' base
 * and overlay patterns, then their colour tables ({@link SpriteCut#BLOCK_BYTES}
 * a block). {@code sprite_bank.h}: each frame's number,
 * {@code SPRITE_<NAME>_<FRAME>}, each sprite's origin and size in blocks, and
 * (with SPRITE_TABLES defined) where each frame starts and its blocks.
 */
public final class RomSprites {

    /** The suffix of a silhouette's frame id. */
    public static final String INK = "_ink";
    /** The colour a silhouette's lines have in the blob: the ROM sets the tint. */
    static final int SILHOUETTE_COLOUR = 1;

    /** A frame in the bank: its sprite and id, its number, and where it starts in the blob. */
    public record Entry(String sprite, String frame, int index, int w, int h, int originX, int originY, int offset,
                        int cols, int rows) {
    }

    private final List<Entry> entries = new ArrayList<>();
    private final byte[] blob;
    private final List<SpriteCut.Change> changes = new ArrayList<>();
    /** Each frame's chunk, and where each chunk starts in the bank. */
    private final List<Integer> chunk = new ArrayList<>();
    private final List<Integer> chunkStart = new ArrayList<>();
    private final Map<String, List<SpriteCut.Change>> later = new java.util.LinkedHashMap<>();

    public RomSprites(Content c, VdpPalette vdp) {
        SpriteCut.Sprites list = SpriteCut.sprites(c);
        List<byte[]> frames = new ArrayList<>();
        int offset = 0;
        for (String name : list.rom()) {
            SpriteData s = c.sprites().get(name);
            String source = "data/art/sprites/" + name + ".sprite.json";
            int w = s.size().w();
            int h = s.size().h();
            int cols = (w + SpriteCut.BLOCK - 1) / SpriteCut.BLOCK;
            int rows = (h + SpriteCut.BLOCK - 1) / SpriteCut.BLOCK;
            if (cols > 2 || rows > 2) {
                throw new ConvertException(source + ": " + w + "x" + h + "; the ROM's sprites are at most 2x2 blocks");
            }
            Map<String, byte[]> decoded = s.decode(source);
            for (Map.Entry<String, byte[]> f : decoded.entrySet()) {
                SpriteCut.Frame cut = SpriteCut.cut(vdp, name, f.getKey(), f.getValue(), w, h);
                entries.add(new Entry(name, f.getKey(), frames.size(), w, h, s.origin().x(), s.origin().y(), offset,
                        cols, rows));
                frames.add(cut.bytes());
                offset += cut.bytes().length;
                changes.addAll(cut.changes());
            }
            if (list.silhouettes().contains(name)) {
                for (Map.Entry<String, byte[]> f : decoded.entrySet()) {
                    byte[] ink = SpriteCut.silhouette(f.getValue(), w, h, SILHOUETTE_COLOUR);
                    entries.add(new Entry(name, f.getKey() + INK, frames.size(), w, h, s.origin().x(),
                            s.origin().y(), offset, cols, rows));
                    frames.add(ink);
                    offset += ink.length;
                }
            }
        }
        this.blob = new byte[offset];
        for (int i = 0; i < frames.size(); i++) {
            System.arraycopy(frames.get(i), 0, blob, entries.get(i).offset(), frames.get(i).length);
        }
        // Chunks of a ROM segment at most, a frame wholly in one.
        int start = 0;
        for (int i = 0; i < frames.size(); i++) {
            int end = entries.get(i).offset() + frames.get(i).length;
            if (end - start > SceneryAtlas.CHUNK_BYTES) {
                chunkStart.add(start);
                start = entries.get(i).offset();
            }
            chunk.add(chunkStart.size());
        }
        chunkStart.add(start);
        // The big ones are cut too, for the report: they go into the ROM later.
        for (String name : list.later()) {
            SpriteData s = c.sprites().get(name);
            List<SpriteCut.Change> ch = new ArrayList<>();
            for (Map.Entry<String, byte[]> f : s.decode("data/art/sprites/" + name + ".sprite.json").entrySet()) {
                ch.addAll(SpriteCut.cut(vdp, name, f.getKey(), f.getValue(), s.size().w(), s.size().h()).changes());
            }
            later.put(name, ch);
        }
    }

    public List<Entry> entries() {
        return List.copyOf(entries);
    }

    /** A frame's number in the bank. */
    public int index(String sprite, String frame) {
        for (Entry e : entries) {
            if (e.sprite().equals(sprite) && e.frame().equals(frame)) {
                return e.index();
            }
        }
        throw new ConvertException("data/art/sprites/" + sprite + ".sprite.json: no frame '" + frame
                + "', which the ROM shows");
    }

    public byte[] blob() {
        return blob.clone();
    }

    /** The bank in chunks of a ROM segment at most: sprites_N.bin. */
    public List<byte[]> chunks() {
        List<byte[]> out = new ArrayList<>();
        for (int c = 0; c < chunkStart.size(); c++) {
            int end = c + 1 < chunkStart.size() ? chunkStart.get(c + 1) : blob.length;
            out.add(java.util.Arrays.copyOfRange(blob, chunkStart.get(c), end));
        }
        return out;
    }

    /** The pixels the bank's frames show in another colour than the art's. */
    public List<SpriteCut.Change> changes() {
        return List.copyOf(changes);
    }

    /** The same for the sprites cut for the report only, by sprite. */
    public Map<String, List<SpriteCut.Change>> later() {
        return later;
    }

    /** Where each frame starts in its chunk, the chunk, its blocks (columns << 4 | rows), and its origin. */
    void tables(RamTables t) {
        int n = entries.size();
        int[] off = new int[n];
        int[] blocks = new int[n];
        int[] ox = new int[n];
        int[] oy = new int[n];
        for (int i = 0; i < n; i++) {
            Entry e = entries.get(i);
            off[i] = e.offset() - chunkStart.get(chunk.get(i));
            blocks[i] = e.cols() << 4 | e.rows();
            ox[i] = e.originX();
            oy[i] = e.originY();
        }
        int[] ch = chunk.stream().mapToInt(Integer::intValue).toArray();
        t.u16("g_SpriteOffset", off, "The sprite bank's frames: where each starts in its chunk, sprites_N.bin,"
                + " the chunk, its blocks (columns << 4 | rows), its origin (its feet, from its top-left).");
        t.u8("g_SpriteChunk", ch, null);
        t.u8("g_SpriteBlocks", blocks, null);
        t.u8("g_SpriteOriginX", ox, null);
        t.u8("g_SpriteOriginY", oy, null);
    }

    public String header() {
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/art/sprites/"));
        sb.append("#pragma once\n\n");
        sb.append("// sprites_N.bin: each frame's blocks, row by row: their base and overlay 16x16\n");
        sb.append("// patterns, then their colour tables, the overlays' with CC (convert/.../SpriteCut.java).\n");
        sb.append("// A silhouette (_INK) has all its pixels in the base, its lines in colour ")
                .append(SILHOUETTE_COLOUR).append(".\n");
        sb.append("#define SPRITE_FRAMES\t\t").append(entries.size()).append('\n');
        sb.append("#define SPRITE_BLOCK_BYTES\t").append(SpriteCut.BLOCK_BYTES).append('\n');
        String last = null;
        for (Entry e : entries) {
            if (!e.sprite().equals(last)) {
                last = e.sprite();
                String n = Gen.cName(e.sprite());
                sb.append("\n// ").append(e.sprite()).append(", ").append(e.w()).append('x').append(e.h())
                        .append(": its origin from the top-left, its blocks, then its frames.\n");
                sb.append("#define SPRITE_").append(n).append("_ORIGIN_X\t").append(e.originX()).append('\n');
                sb.append("#define SPRITE_").append(n).append("_ORIGIN_Y\t").append(e.originY()).append('\n');
                sb.append("#define SPRITE_").append(n).append("_COLS\t").append(e.cols()).append('\n');
                sb.append("#define SPRITE_").append(n).append("_ROWS\t").append(e.rows()).append('\n');
            }
            sb.append("#define SPRITE_").append(Gen.cName(e.sprite())).append('_').append(Gen.cName(e.frame()))
                    .append('\t').append(e.index()).append('\n');
        }
        return sb.toString();
    }
}
