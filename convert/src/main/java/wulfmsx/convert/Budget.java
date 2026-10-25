package wulfmsx.convert;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * Where the ROM's bytes go (AGENTS.md §10, M1): every blob the converter
 * writes, placed in the ROM's 16 KB segments, so we see the cartridge fill up
 * from the start.
 *
 * <p>The ROM is an ASCII16 MegaROM of {@link #ROM_SEGMENTS} segments
 * (src/project_config.js: ROMSize 256). The first {@link #CODE_SEGMENTS} are
 * the program's: MSXgl's start-up maps them at 4000h-BFFFh. Blobs go into the
 * rest, biggest first, each wholly inside one segment so the program reads it
 * by switching one bank.
 */
public final class Budget {

    public static final int ROM_SEGMENTS = 16;
    public static final int CODE_SEGMENTS = 2;

    /** A file for the ROM and how big it is. */
    public record Blob(String file, int bytes) {
    }

    /** A blob's place: its segment, and where in it. */
    public record Place(Blob blob, int segment, int offset) {
    }

    private final List<Place> places = new ArrayList<>();
    private final int[] used = new int[ROM_SEGMENTS];

    public Budget(List<Blob> blobs) {
        List<Blob> order = new ArrayList<>(blobs);
        order.sort(Comparator.comparingInt((Blob b) -> -b.bytes()).thenComparing(Blob::file));
        for (Blob b : order) {
            int seg = CODE_SEGMENTS;
            while (seg < ROM_SEGMENTS && used[seg] + b.bytes() > SceneryAtlas.CHUNK_BYTES) {
                seg++;
            }
            if (seg == ROM_SEGMENTS) {
                throw new ConvertException("the ROM is full: " + b.file() + " (" + b.bytes()
                        + " bytes) fits in none of segments " + CODE_SEGMENTS + "-" + (ROM_SEGMENTS - 1));
            }
            places.add(new Place(b, seg, used[seg]));
            used[seg] += b.bytes();
        }
        places.sort(Comparator.comparingInt(Place::segment).thenComparingInt(Place::offset));
    }

    public List<Place> places() {
        return List.copyOf(places);
    }

    public int dataBytes() {
        int n = 0;
        for (int u : used) {
            n += u;
        }
        return n;
    }

    public int freeSegments() {
        int n = 0;
        for (int s = CODE_SEGMENTS; s < ROM_SEGMENTS; s++) {
            if (used[s] == 0) {
                n++;
            }
        }
        return n;
    }

    public String report() {
        int segBytes = SceneryAtlas.CHUNK_BYTES;
        StringBuilder sb = new StringBuilder();
        sb.append(String.format("ROM: %d KB, %d segments of %d KB; segments 0-%d are the program's%n",
                ROM_SEGMENTS * segBytes / 1024, ROM_SEGMENTS, segBytes / 1024, CODE_SEGMENTS - 1));
        for (Place p : places) {
            sb.append(String.format("  segment %2d  +%05d  %6d bytes  %s%n",
                    p.segment(), p.offset(), p.blob().bytes(), p.blob().file()));
        }
        for (int s = CODE_SEGMENTS; s < ROM_SEGMENTS; s++) {
            if (used[s] > 0) {
                sb.append(String.format("  segment %2d  %d of %d bytes used (%d%%)%n",
                        s, used[s], segBytes, used[s] * 100 / segBytes));
            }
        }
        int dataSegs = ROM_SEGMENTS - CODE_SEGMENTS;
        sb.append(String.format("data: %d bytes in %d of %d data segments; %d segments (%d KB) free%n",
                dataBytes(), dataSegs - freeSegments(), dataSegs, freeSegments(), freeSegments() * segBytes / 1024));
        return sb.toString();
    }
}
