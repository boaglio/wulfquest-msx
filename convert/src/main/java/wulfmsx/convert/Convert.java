package wulfmsx.convert;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import wulf.Content;
import wulf.data.DataException;

/**
 * The converter (AGENTS.md §4): wulfquest/data/ in, build/gen/ out.
 *
 * <pre>
 *   Convert DATA_DIR OUT_DIR
 * </pre>
 *
 * Loads the data with the Java game's own {@link Content#load}, so a data
 * error stops the MSX build with the Java game's message, then writes:
 * palette.h, constants.h, rooms.h and rooms.bin, scenery.h and
 * scenery_N.bin, and report.txt (the palette's drift and the ROM budget).
 */
public final class Convert {

    private Convert() {
    }

    public static void main(String[] args) {
        if (args.length != 2) {
            System.err.println("usage: Convert DATA_DIR OUT_DIR");
            System.exit(2);
        }
        try {
            String report = run(Path.of(args[0]), Path.of(args[1]));
            System.out.print(report);
        } catch (DataException | ConvertException e) {
            System.err.println("convert: " + e.getMessage());
            System.exit(1);
        }
    }

    /** Converts everything and returns the report it also writes. */
    public static String run(Path dataDir, Path out) {
        Content c = Content.load(dataDir);
        VdpPalette palette = new VdpPalette(c.palette());
        SceneryAtlas atlas = new SceneryAtlas(c.scenery(), c.sprites().all(), palette);
        RoomTables rooms = new RoomTables(c.rooms(), atlas);
        Constants constants = new Constants(dataDir);

        Gen.write(out.resolve("palette.h"), palette.header());
        Gen.write(out.resolve("constants.h"), constants.header());
        Gen.write(out.resolve("scenery.h"), atlas.header());
        Gen.write(out.resolve("rooms.h"), rooms.header());

        List<Budget.Blob> blobs = new ArrayList<>();
        List<byte[]> chunks = atlas.chunks();
        for (int i = 0; i < chunks.size(); i++) {
            String name = "scenery_" + i + ".bin";
            Gen.write(out.resolve(name), chunks.get(i));
            blobs.add(new Budget.Blob(name, chunks.get(i).length));
        }
        Gen.write(out.resolve("rooms.bin"), rooms.blob());
        blobs.add(new Budget.Blob("rooms.bin", rooms.blob().length));
        Budget budget = new Budget(blobs);

        StringBuilder r = new StringBuilder();
        r.append("Palette on the V9938 (colour 0 is written as ").append(palette.twinOfZero()).append("):\n");
        for (String line : palette.report()) {
            r.append("  ").append(line).append('\n');
        }
        r.append(String.format("Scenery: %d objects in %d lines of video RAM pages 2-3 (of %d)%n",
                atlas.slots().size(), atlas.lines(), SceneryAtlas.MAX_LINES));
        r.append(String.format("Constants: %d%n", constants.entries().size()));
        r.append(budget.report());
        Gen.write(out.resolve("report.txt"), r.toString());
        return r.toString();
    }
}
