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
 * player_frames.bin, sprite_bank.h and sprites_N.bin, blade.h and blade.bin,
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
        Quest quest = new Quest(c, bank, tables);
        Game game = new Game(c, palette, bank, tables);
        Panel panel = new Panel(c, palette);
        Sound sound = new Sound(c);
        Shell shell = new Shell(c, palette, SceneryAtlas.VRAM_FIRST_LINE + atlas.lines());
        bank.tables(tables);
        blade.tables(tables);
        vale.tables(tables);
        // Vale inked in each flower's colour that he wears (M8): untouchable orchids'.
        List<byte[]> inked = new ArrayList<>();
        int[] inkedChunk = new int[c.orchids().orchids().size()];
        for (int o = 0; o < inkedChunk.length; o++) {
            wulf.data.OrchidData.Orchid orchid = c.orchids().orchids().get(o);
            inkedChunk[o] = 255;
            if (orchid.untouchable()) {
                int bright = c.palette().indexOf(wulf.data.OrchidValidator.bright(orchid.colour()));
                inkedChunk[o] = inked.size();
                inked.add(vale.inked(bright - 8, bright, c.palette().indexOf("black"),
                        c.player().sprite() + "_" + orchid.colour(), vale.inkedChanges));
            }
        }
        tables.u8("g_ValeInked", inkedChunk, "Each orchid: Vale's inked frames' chunk, player_inked_N.bin (255: none).");
        SpriteReport sprites = new SpriteReport(c, palette, vale, bank, blade);

        Gen.write(out.resolve("palette.h"), palette.header());
        Gen.write(out.resolve("constants.h"), constants.header());
        Gen.write(out.resolve("scenery.h"), atlas.header());
        Gen.write(out.resolve("rooms.h"), rooms.header());
        Gen.write(out.resolve("player.h"), vale.header());
        Gen.write(out.resolve("sprite_bank.h"), bank.header());
        Gen.write(out.resolve("blade.h"), blade.header());
        Gen.write(out.resolve("jungle_data.h"), jungle.header());
        Gen.write(out.resolve("quest_data.h"), quest.header());
        Gen.write(out.resolve("game_data.h"), game.header());
        Gen.write(out.resolve("panel_data.h"), panel.header());
        Gen.write(out.resolve("sound_data.h"), sound.header());
        Gen.write(out.resolve("shell_data.h"), shell.header());
        Gen.write(out.resolve("demo_check.txt"), shell.demoCheck());
        Gen.write(out.resolve("demo_map.bin"), shell.demoMap());
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
        for (int i = 0; i < inked.size(); i++) {
            Gen.write(out.resolve("player_inked_" + i + ".bin"), inked.get(i));
            blobs.add(new Budget.Blob("player_inked_" + i + ".bin", inked.get(i).length));
        }
        List<byte[]> spriteChunks = bank.chunks();
        for (int i = 0; i < spriteChunks.size(); i++) {
            Gen.write(out.resolve("sprites_" + i + ".bin"), spriteChunks.get(i));
            blobs.add(new Budget.Blob("sprites_" + i + ".bin", spriteChunks.get(i).length));
        }
        Gen.write(out.resolve("blade.bin"), blade.blob());
        blobs.add(new Budget.Blob("blade.bin", blade.blob().length));
        List<byte[]> ways = game.ways();
        for (int i = 0; i < ways.size(); i++) {
            Gen.write(out.resolve("ways_" + i + ".bin"), ways.get(i));
            blobs.add(new Budget.Blob("ways_" + i + ".bin", ways.get(i).length));
        }
        for (int i = 0; i < Sound.RATES.length; i++) {
            String name = "sound" + Sound.RATES[i] + ".bin";
            Gen.write(out.resolve(name), sound.blobs().get(i));
            blobs.add(new Budget.Blob(name, sound.blobs().get(i).length));
        }
        Gen.write(out.resolve("shell.bin"), shell.blob());
        blobs.add(new Budget.Blob("shell.bin", shell.blob().length));
        Gen.write(out.resolve("shell_img.bin"), shell.images());
        blobs.add(new Budget.Blob("shell_img.bin", shell.images().length));
        Gen.write(out.resolve("font.bin"), panel.font());
        blobs.add(new Budget.Blob("font.bin", panel.font().length));
        Gen.write(out.resolve("panel.bin"), panel.panel());
        blobs.add(new Budget.Blob("panel.bin", panel.panel().length));
        Gen.write(out.resolve("frames.bin"), bank.frameInfo());
        blobs.add(new Budget.Blob("frames.bin", bank.frameInfo().length));
        Gen.write(out.resolve("rooms_more.bin"), game.rooms());
        blobs.add(new Budget.Blob("rooms_more.bin", game.rooms().length));
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
        r.append(String.format("Shell: %d images in %d lines of video RAM from line %d; shell.bin %d bytes%n",
                shell.imageCount(), shell.lines(), shell.vramLine(), shell.blob().length));
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
