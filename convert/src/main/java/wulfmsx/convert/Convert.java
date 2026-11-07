package wulfmsx.convert;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import wulf.Content;
import wulf.data.DataException;
import wulf.world.WorldGrid;

/**
 * The converter (AGENTS.md §4): wulfquest/data/ in, build/gen/ out.
 *
 * <pre>
 *   Convert DATA_DIR OUT_DIR
 * </pre>
 *
 * Loads the data with the Java game's own {@link Content#load}, so a data
 * error stops the MSX build with the Java game's message, then writes:
 * palette.h, constants.h, rooms.h and rooms.bin, player.h and
 * player_frames.bin, sprite_bank.h and sprites.bin, blade.h and blade.bin,
 * jungle_data.h (the swarm and the big beasts), tables.h and tables.bin (the tables the
 * ROM copies into RAM), map_0.bin and map_1.bin (the map's solid cells, a bitmap), sprites.txt and sprites.png (every pixel a sprite shows otherwise), scenery.h and
 * scenery_N.bin, rom.h and rawfiles.json (where each blob goes in the ROM),
 * and report.txt (the palette's drift and the ROM budget).
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
        VdpPalette palette = VdpPalette.forGame(c);
        SceneryAtlas atlas = new SceneryAtlas(c.scenery(), c.sprites().all(), palette);
        // The ground is the Java game's: GamePainter fills the playfield with "black".
        RoomTables rooms = new RoomTables(c.rooms(), atlas, palette.vdpColour(c.palette().indexOf("black")));
        Constants constants = new Constants(dataDir);
        PlayerSprite vale = new PlayerSprite(c.player(), c.sprites().get(c.player().sprite()), palette,
                new WorldGrid(c.rooms()), c.map().startRoom());
        RomSprites bank = new RomSprites(c, palette);
        Blade blade = new Blade(c.player(), c.palette(), palette);
        RamTables tables = new RamTables();
        Jungle jungle = new Jungle(c, palette, bank, tables);
        bank.tables(tables);
        blade.tables(tables);
        vale.tables(tables);
        SpriteReport sprites = new SpriteReport(c, palette, vale, bank, blade);

        Gen.write(out.resolve("palette.h"), palette.header());
        Gen.write(out.resolve("constants.h"), constants.header());
        Gen.write(out.resolve("scenery.h"), atlas.header());
        Gen.write(out.resolve("rooms.h"), rooms.header());
        Gen.write(out.resolve("player.h"), vale.header());
        Gen.write(out.resolve("sprite_bank.h"), bank.header());
        Gen.write(out.resolve("blade.h"), blade.header());
        Gen.write(out.resolve("jungle_data.h"), jungle.header());
        Gen.write(out.resolve("tables.h"), tables.header());
        Gen.write(out.resolve("sprites.txt"), sprites.listing());
        Gen.write(out.resolve("sprites.png"), sprites.picture());

        List<Budget.Blob> blobs = new ArrayList<>();
        List<byte[]> chunks = atlas.chunks();
        for (int i = 0; i < chunks.size(); i++) {
            String name = "scenery_" + i + ".bin";
            Gen.write(out.resolve(name), chunks.get(i));
            blobs.add(new Budget.Blob(name, chunks.get(i).length));
        }
        Gen.write(out.resolve("rooms.bin"), rooms.blob());
        blobs.add(new Budget.Blob("rooms.bin", rooms.blob().length));
        Gen.write(out.resolve("player_frames.bin"), vale.blob());
        blobs.add(new Budget.Blob("player_frames.bin", vale.blob().length));
        Gen.write(out.resolve("sprites.bin"), bank.blob());
        blobs.add(new Budget.Blob("sprites.bin", bank.blob().length));
        Gen.write(out.resolve("blade.bin"), blade.blob());
        blobs.add(new Budget.Blob("blade.bin", blade.blob().length));
        Gen.write(out.resolve("tables.bin"), tables.blob());
        blobs.add(new Budget.Blob("tables.bin", tables.blob().length));
        MapBits map = new MapBits(new WorldGrid(c.rooms()));
        Gen.write(out.resolve("map_0.bin"), map.first());
        blobs.add(new Budget.Blob("map_0.bin", map.first().length));
        Gen.write(out.resolve("map_1.bin"), map.rest());
        blobs.add(new Budget.Blob("map_1.bin", map.rest().length));
        Budget budget = new Budget(blobs);
        Gen.write(out.resolve("rom.h"), budget.header());
        Gen.write(out.resolve("rawfiles.json"), budget.rawFilesJson());

        StringBuilder r = new StringBuilder();
        r.append("Palette on the V9938 (colour numbers ordered for the sprites' OR; 0 is transparent):\n");
        for (String line : palette.report()) {
            r.append("  ").append(line).append('\n');
        }
        r.append(String.format("Scenery: %d objects in %d lines of video RAM pages 2-3 (of %d)%n",
                atlas.slots().size(), atlas.lines(), SceneryAtlas.MAX_LINES));
        r.append(String.format("Vale: %d frames, %d bytes, 4 sprites each; starts at %d,%d%n",
                vale.ids().size(), vale.blob().length, vale.startXFp() >> 8, vale.startYFp() >> 8));
        r.append(String.format("Sprite bank: %d frames, %d bytes; blade: %d shapes, %d bytes%n",
                bank.entries().size(), bank.blob().length, blade.shapes(), blade.blob().length));
        r.append(sprites.summary());
        r.append(String.format("Constants: %d%n", constants.entries().size()));
        r.append(budget.report());
        Gen.write(out.resolve("report.txt"), r.toString());
        sprites.check();
        return r.toString();
    }
}
