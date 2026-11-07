package wulfmsx.convert;

import wulf.world.WorldGrid;

/**
 * The whole map's solid cells as one bitmap (AGENTS.md §10, M6), for the big
 * beasts, who walk the whole map (§12.9), and for where Vale gets up: a row of
 * cells a run of {@link #ROW_BYTES} bytes, the leftmost cell in bit 7. A
 * beast's box is tested a few times a tick; from the rooms' masks each cell
 * took a template lookup and a multiplication, from this a byte and a bit.
 *
 * <p>{@code map_0.bin} holds the rows that fit one 16 KB segment,
 * {@code map_1.bin} the rest.
 */
final class MapBits {

    static final int ROW_BYTES = WorldGrid.COLS / 8;
    static final int ROWS_0 = SceneryAtlas.CHUNK_BYTES / ROW_BYTES;

    private final byte[] first;
    private final byte[] rest;

    MapBits(WorldGrid grid) {
        if (WorldGrid.COLS % 8 != 0 || ROW_BYTES != 64) {
            throw new ConvertException("the map is " + WorldGrid.COLS + " cells wide; the ROM's bitmap takes 512");
        }
        byte[] all = new byte[WorldGrid.ROWS * ROW_BYTES];
        for (int gy = 0; gy < WorldGrid.ROWS; gy++) {
            for (int gx = 0; gx < WorldGrid.COLS; gx++) {
                if (grid.isSolid(gx, gy)) {
                    all[gy * ROW_BYTES + gx / 8] |= (byte) (0x80 >> (gx % 8));
                }
            }
        }
        int split = Math.min(WorldGrid.ROWS, ROWS_0) * ROW_BYTES;
        this.first = java.util.Arrays.copyOfRange(all, 0, split);
        this.rest = java.util.Arrays.copyOfRange(all, split, all.length);
    }

    byte[] first() {
        return first.clone();
    }

    byte[] rest() {
        return rest.clone();
    }
}
