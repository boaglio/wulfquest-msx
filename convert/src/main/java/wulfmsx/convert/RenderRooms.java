package wulfmsx.convert;

import java.nio.file.Path;
import wulf.Content;
import wulf.data.DisplayConfig;
import wulf.render.Framebuffer;
import wulf.render.RoomPainter;
import wulf.render.SpriteBank;
import wulf.world.RoomAddress;

/**
 * The test oracle for M2 (AGENTS.md §10): every room as the Java game draws
 * it, for test/rooms.sh to compare with what the MSX2 shows.
 *
 * <pre>
 *   RenderRooms DATA_DIR OUT_FILE
 * </pre>
 *
 * Writes the 256 playfields, room by room in index order (row by row), each
 * as SCREEN 5 video RAM bytes in the MSX2's colour numbers: 128 bytes a line,
 * two pixels a byte, the left one in the high nibble.
 */
public final class RenderRooms {

    private RenderRooms() {
    }

    public static void main(String[] args) {
        if (args.length != 2) {
            System.err.println("usage: RenderRooms DATA_DIR OUT_FILE");
            System.exit(2);
        }
        Gen.write(Path.of(args[1]), render(Content.load(Path.of(args[0]))));
    }

    static byte[] render(Content c) {
        DisplayConfig.Playfield field = c.display().playfield();
        VdpPalette palette = new VdpPalette(c.palette());
        // As GamePainter draws it: the playfield filled with "black", then the scenery.
        RoomPainter painter = new RoomPainter(c.display(), new SpriteBank(c.sprites()), c.scenery(),
                c.palette().indexOf("black"));
        Framebuffer fb = new Framebuffer(c.display().canvas().w(), c.display().canvas().h());
        int roomBytes = field.w() * field.h() / 2;
        int rooms = c.map().roomCount();
        byte[] out = new byte[rooms * roomBytes];
        for (int i = 0; i < rooms; i++) {
            painter.paint(fb, c.rooms().room(new RoomAddress(i % c.map().gridW(), i / c.map().gridW())));
            for (int y = 0; y < field.h(); y++) {
                for (int x = 0; x < field.w(); x += 2) {
                    int left = palette.vdpColour(fb.get(field.x() + x, field.y() + y));
                    int right = palette.vdpColour(fb.get(field.x() + x + 1, field.y() + y));
                    out[i * roomBytes + y * field.w() / 2 + x / 2] = (byte) (left << 4 | right);
                }
            }
        }
        return out;
    }
}
