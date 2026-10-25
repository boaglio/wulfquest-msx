package wulfmsx.convert;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import wulf.data.OriginalMapRepository;
import wulf.data.Placement;
import wulf.world.CollisionMask;
import wulf.world.RoomAddress;
import wulf.world.RoomBaker;

/**
 * The 256 rooms as one blob, rooms.bin (AGENTS.md §4). Rooms that look the
 * same share a template, as in the original, so the blob holds each template
 * once:
 *
 * <pre>
 *   ROOMS_TEMPLATE_OF   one byte a room, row by row: its template, 0..ROOM_TEMPLATES-1
 *   ROOMS_PLACEMENTS    two bytes a template, little-endian: where its placement list starts in the blob
 *                       then each list: a count, and three bytes a placement (object, x cell, y cell),
 *                       in drawing order
 *   ROOMS_MASKS         ROOM_MASK_BYTES a template: its collision mask from RoomBaker, a row at a time,
 *                       eight cells a byte, the leftmost in bit 7; a set bit is solid
 * </pre>
 */
public final class RoomTables {

    public static final int MASK_ROW_BYTES = CollisionMask.COLS / 8;
    public static final int MASK_BYTES = MASK_ROW_BYTES * CollisionMask.ROWS;

    private final List<Integer> templateIds;
    private final byte[] blob;
    private final int placementsAt;
    private final int masksAt;
    private final int maxPlacements;

    public RoomTables(RoomBaker rooms, SceneryAtlas atlas) {
        OriginalMapRepository map = rooms.map();
        this.templateIds = new ArrayList<>(map.templateIds());
        int rooms256 = map.roomCount();

        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (int i = 0; i < rooms256; i++) {
            out.write(templateIds.indexOf(map.roomType(i % map.gridW(), i / map.gridW())));
        }

        this.placementsAt = out.size();
        int listAt = placementsAt + 2 * templateIds.size();
        ByteArrayOutputStream lists = new ByteArrayOutputStream();
        int most = 0;
        for (int type : templateIds) {
            int at = listAt + lists.size();
            out.write(at & 0xFF);
            out.write(at >> 8);
            List<Placement> ps = map.placements(type);
            most = Math.max(most, ps.size());
            lists.write(ps.size());
            for (Placement p : ps) {
                lists.write(atlas.index(p.graphic()));
                lists.write(p.x());
                lists.write(p.y());
            }
        }
        out.writeBytes(lists.toByteArray());
        this.maxPlacements = most;

        this.masksAt = out.size();
        for (int type : templateIds) {
            out.writeBytes(pack(rooms.mask(type)));
        }
        this.blob = out.toByteArray();
        if (blob.length > SceneryAtlas.CHUNK_BYTES) {
            throw new ConvertException("data/world/original_map.json: the room tables are " + blob.length
                    + " bytes, more than one " + SceneryAtlas.CHUNK_BYTES + "-byte ROM segment");
        }
    }

    static byte[] pack(CollisionMask mask) {
        byte[] out = new byte[MASK_BYTES];
        for (int cy = 0; cy < CollisionMask.ROWS; cy++) {
            for (int cx = 0; cx < CollisionMask.COLS; cx++) {
                if (mask.isSolid(cx, cy)) {
                    out[cy * MASK_ROW_BYTES + cx / 8] |= (byte) (0x80 >> (cx % 8));
                }
            }
        }
        return out;
    }

    public byte[] blob() {
        return blob.clone();
    }

    /** Whether a cell of a room is solid, read back from the blob as the Z80 will read it. */
    public boolean solid(RoomAddress room, int cx, int cy) {
        int template = blob[room.index()] & 0xFF;
        int b = blob[masksAt + template * MASK_BYTES + cy * MASK_ROW_BYTES + cx / 8] & 0xFF;
        return (b & 0x80 >> (cx % 8)) != 0;
    }

    /** A room's placements, read back from the blob: {object, x, y} each. */
    public List<int[]> placements(RoomAddress room) {
        int template = blob[room.index()] & 0xFF;
        int at = (blob[placementsAt + 2 * template] & 0xFF) | (blob[placementsAt + 2 * template + 1] & 0xFF) << 8;
        List<int[]> out = new ArrayList<>();
        int n = blob[at] & 0xFF;
        for (int i = 0; i < n; i++) {
            int p = at + 1 + 3 * i;
            out.add(new int[] {blob[p] & 0xFF, blob[p + 1] & 0xFF, blob[p + 2] & 0xFF});
        }
        return out;
    }

    public String header() {
        return Gen.banner("data/world/original_map.json and data/world/scenery.json")
                + "#pragma once\n\n"
                + "// rooms.bin: see convert/.../RoomTables.java for the layout.\n"
                + "#define ROOM_TEMPLATES\t\t" + templateIds.size() + '\n'
                + "#define ROOM_MAX_PLACEMENTS\t" + maxPlacements + '\n'
                + "#define ROOM_MASK_ROW_BYTES\t" + MASK_ROW_BYTES + '\n'
                + "#define ROOM_MASK_BYTES\t\t" + MASK_BYTES + '\n'
                + "#define ROOMS_TEMPLATE_OF\t0\n"
                + "#define ROOMS_PLACEMENTS\t" + placementsAt + '\n'
                + "#define ROOMS_MASKS\t\t" + masksAt + '\n'
                + "#define ROOMS_BYTES\t\t" + blob.length + '\n';
    }
}
