package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import wulf.Content;
import wulf.data.SpriteData;

/**
 * The ROM's sprites besides Vale (AGENTS.md §10, M5): every frame of each
 * sprite sprites.json lists for the ROM, its mirrored ones included, cut into
 * two layered 16x16 sprites ({@link SpriteCut}). They all fit one block.
 *
 * <p>{@code sprites.bin}: {@link SpriteCut#BLOCK_BYTES} a frame: the base and
 * overlay patterns, then their colour tables. {@code sprite_bank.h}: each frame's
 * number, {@code SPRITE_<NAME>_<FRAME>}, and each sprite's origin.
 */
public final class RomSprites {

    /** A frame in the bank: its sprite and id, and its number. */
    public record Entry(String sprite, String frame, int index, int w, int h, int originX, int originY) {
    }

    private final List<Entry> entries = new ArrayList<>();
    private final byte[] blob;
    private final List<SpriteCut.Change> changes = new ArrayList<>();
    private final Map<String, List<SpriteCut.Change>> later = new java.util.LinkedHashMap<>();

    public RomSprites(Content c, VdpPalette vdp) {
        SpriteCut.Sprites list = SpriteCut.sprites(c);
        List<byte[]> frames = new ArrayList<>();
        for (String name : list.rom()) {
            SpriteData s = c.sprites().get(name);
            String source = "data/art/sprites/" + name + ".sprite.json";
            if (s.size().w() > SpriteCut.BLOCK || s.size().h() > SpriteCut.BLOCK) {
                throw new ConvertException(source + ": " + s.size().w() + "x" + s.size().h() + "; the ROM's sprite"
                        + " bank holds sprites of one 16x16 block");
            }
            for (Map.Entry<String, byte[]> f : s.decode(source).entrySet()) {
                SpriteCut.Frame cut = SpriteCut.cut(vdp, name, f.getKey(), f.getValue(), s.size().w(), s.size().h());
                entries.add(new Entry(name, f.getKey(), frames.size(), s.size().w(), s.size().h(),
                        s.origin().x(), s.origin().y()));
                frames.add(cut.bytes());
                changes.addAll(cut.changes());
            }
        }
        this.blob = new byte[frames.size() * SpriteCut.BLOCK_BYTES];
        for (int i = 0; i < frames.size(); i++) {
            System.arraycopy(frames.get(i), 0, blob, i * SpriteCut.BLOCK_BYTES, SpriteCut.BLOCK_BYTES);
        }
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

    public byte[] blob() {
        return blob.clone();
    }

    /** The pixels the bank's frames show in another colour than the art's. */
    public List<SpriteCut.Change> changes() {
        return List.copyOf(changes);
    }

    /** The same for the sprites cut for the report only, by sprite. */
    public Map<String, List<SpriteCut.Change>> later() {
        return later;
    }

    public String header() {
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/art/sprites/"));
        sb.append("#pragma once\n\n");
        sb.append("// sprites.bin: SPRITE_FRAME_BYTES a frame: base and overlay 16x16 patterns, then\n");
        sb.append("// their colour tables, the overlay's with CC (convert/.../SpriteCut.java).\n");
        sb.append("#define SPRITE_FRAMES\t\t").append(entries.size()).append('\n');
        sb.append("#define SPRITE_FRAME_BYTES\t").append(SpriteCut.BLOCK_BYTES).append('\n');
        String last = null;
        for (Entry e : entries) {
            if (!e.sprite().equals(last)) {
                last = e.sprite();
                String n = Gen.cName(e.sprite());
                sb.append("\n// ").append(e.sprite()).append(", ").append(e.w()).append('x').append(e.h())
                        .append(": its origin from the top-left, then its frames.\n");
                sb.append("#define SPRITE_").append(n).append("_ORIGIN_X\t").append(e.originX()).append('\n');
                sb.append("#define SPRITE_").append(n).append("_ORIGIN_Y\t").append(e.originY()).append('\n');
            }
            sb.append("#define SPRITE_").append(Gen.cName(e.sprite())).append('_').append(Gen.cName(e.frame()))
                    .append('\t').append(e.index()).append('\n');
        }
        return sb.toString();
    }
}
