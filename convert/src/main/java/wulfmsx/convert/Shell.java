package wulfmsx.convert;

import java.io.ByteArrayOutputStream;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import wulf.Content;
import wulf.data.FontData;
import wulf.data.HighScores;
import wulf.data.LandmarksData;
import wulf.data.ShellConfig;
import wulf.data.SpriteData;
import wulf.sim.Simulation;
import wulf.world.CollisionMask;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * The shell around the game (AGENTS.md §10, M10; the Java game's {@code Shell}, its screens and
 * {@code shell.json}): the title, the menus, the amulet's reveal, game over, the map screen and
 * the hi-scores, as tables for the ROM.
 *
 * <p>{@code shell.bin}: the strings (each a length byte and its characters), the hi-score table
 * a new machine starts with ({@link #ROW_BYTES} a row), the map screen's miniature of every room
 * ({@link #MINI_BYTES} a room: 11 rows of 16 pixels, a bit each, bit 7 of the first byte the left,
 * set where the ground is open), the wordmark's words' widths, and the attract demo's input (a
 * tick count and its keys a run, as {@code demo.txt} has it: recorded on the MSX).
 * {@code shell_img.bin}: the images the screens copy from video RAM below the scenery atlas
 * (pages 2-3, from {@link #vramLine()}): each a header (x, the line from the atlas's first as a
 * 16-bit number, width, height) and its bytes, two pixels a byte,
 * colour 0 where the art is transparent. {@code shell_data.h} says where each is.
 *
 * <p>The words of the wordmark are stencils for the title's colour cycle: 0 where a letter is,
 * black elsewhere, so a band filled with a colour and the stencil laid over it by OR leaves the
 * letters in that colour on black (black is colour 15: the converter checks).
 */
public final class Shell {

    /** A hi-score row: its name (3 letters), score (32 bits), quarters, ticks (32 bits), Easy. */
    static final int ROW_BYTES = 13;
    /** A room on the map screen: 16x11 pixels, two bytes a row. */
    static final int MINI_W = 16;
    static final int MINI_H = 11;
    static final int MINI_BYTES = MINI_H * 2;
    /** The screen: SCREEN 5's width; the fonts' widest line must fit. */
    static final int SCREEN_W = 256;

    private final StringBuilder h = new StringBuilder();
    private final ByteArrayOutputStream bin = new ByteArrayOutputStream();
    private final ByteArrayOutputStream img = new ByteArrayOutputStream();
    private final ByteArrayOutputStream jobImg = new ByteArrayOutputStream();
    private final VdpPalette vdp;
    private final Content c;
    private final int vramLine;
    private int imgLine;
    private int images;
    private final String demoCheck;

    public Shell(Content c, VdpPalette vdp, int vramLine) {
        this.c = c;
        this.vdp = vdp;
        this.vramLine = vramLine;
        ShellConfig s = c.shell();
        FontData.Set big = c.font().sets().get("big");
        FontData.Set small = c.font().sets().get("small");

        h.append(Gen.banner("config/shell.json, input.json, default_highscores.json, the Java game's shell"));
        h.append("#pragma once\n\n");
        h.append("// Ticks (Shell, GameSession, shell.json).\n");
        def("SHELL_HOLD_TICKS", s.screenHoldTicks());
        def("ATTRACT_IDLE_TICKS", s.attract().idleTicks());
        def("GAME_OVER_HOLD_TICKS", s.gameOver().holdTicks());
        def("GAME_OVER_CONTINUE_TICKS", s.gameOver().continueTicks());
        def("GAME_OVER_COUNTDOWN_SCALE", s.gameOver().countdownScale());
        def("REVEAL_SKIP_TICKS", s.amuletReveal().skipAfterTicks());
        def("REVEAL_SCALE", s.amuletReveal().scale());
        def("TALLY_HOLD_TICKS", s.tally().holdTicks());
        def("HISCORE_SIZE", s.hiScores().size());
        def("HISCORE_NAME", s.hiScores().nameLength());
        def("HISCORE_ALPHABET_N", s.hiScores().alphabet().length());
        def("HISCORE_BLINK_TICKS", s.hiScores().cursorBlinkTicks());
        def("HISCORE_REPEAT_DELAY", s.hiScores().repeatDelayTicks());
        def("HISCORE_REPEAT_EVERY", s.hiScores().repeatEveryTicks());
        def("HISCORE_ROW_BYTES", ROW_BYTES);
        def("MAP_BLINK_TICKS", s.map().blinkTicks());
        def("MAP_ICON_TICKS", s.map().quarterIcon().frameTicks());
        def("MAP_ICON_SHINY", s.map().quarterIcon().shiny().size());
        def("MAP_ARCH_TICKS", s.map().exitIcon().frameTicks());
        def("MAP_ARCH_OPEN", s.map().exitIcon().open().size());
        // The looking glass's trail (MapScreen.paintTrail): a dot every dotEveryPx pixels, marching.
        def("MAP_TRAIL_DOT_EVERY", s.map().trail().dotEveryPx());
        def("MAP_TRAIL_MARCH_TICKS", s.map().trail().marchTicks());
        def("CYCLE_TICKS", s.title().cycle().ticksPerStep());
        def("CYCLE_STEPS", s.title().cycle().colours().size());
        h.append("#define CYCLE_COLOURS\t").append(colours(s.title().cycle().colours())).append('\n');
        // GameOverScreen.percentDone: rooms visited and quarters held, of every room and quarter.
        def("PERCENT_OF", c.map().roomCount() + c.landmarks().lairs().size());

        h.append("\n// Layout: the Java screens' own numbers (literals in their code), and the fonts'.\n");
        def("WORDMARK_SCALE", privateInt(wulf.ui.TitleScreen.class, "WORDMARK_SCALE"));
        def("WORDMARK_Y", privateInt(wulf.ui.TitleScreen.class, "WORDMARK_Y"));
        def("WORDMARK_GAP", privateInt(wulf.ui.TitleScreen.class, "WORDMARK_GAP"));
        def("TITLE_MENU_Y", privateInt(wulf.ui.TitleScreen.class, "MENU_Y"));
        def("MENU_PAD", privateInt(cls("wulf.ui.MenuScreen"), "PAD_PX"));
        def("MARKER_GAP", privateInt(cls("wulf.ui.MenuScreen"), "MARKER_GAP_PX"));
        def("PIECES_FROM_RIGHT", privateInt(wulf.ui.HiScoreScreen.class, "PIECES_FROM_RIGHT"));
        def("NAME_SCALE", privateInt(wulf.ui.HiScoreEntryScreen.class, "NAME_SCALE"));
        def("BIG_LINE", big.lineHeight());
        def("SMALL_LINE", small.lineHeight());
        def("BIG_ADVANCE", big.advance());
        def("SMALL_ADVANCE", small.advance());

        h.append("\n// Colours: Chrome's inks, the difficulty's, the map's.\n");
        def("INK_GROUND", colour("black"));
        def("INK_HEADING", colour("brightYellow"));
        def("INK_BODY", colour("white"));
        def("INK_BRIGHT", colour("brightWhite"));
        def("INK_DIM", colour("cyan"));
        def("INK_EASY", colour(s.difficulty().easyColour()));
        def("INK_MAP_VISITED", colour(s.map().visited()));
        def("INK_MAP_REVEALED", colour(s.map().revealed()));
        def("INK_MAP_HERE", colour(s.map().here()));
        def("INK_MAP_TRAIL", colour(s.map().trail().colour()));
        if (colour("black") != 15) {
            throw new ConvertException("the title's colour cycle ORs a stencil over a band: black must be colour 15, "
                    + "it is " + colour("black"));
        }
        // Framebuffer.dim: a colour's index & 7, its plain twin: as each colour number's.
        h.append("// Framebuffer.dim (the pause menu's jungle): each colour number's plain twin's.\n");
        int[] dim = new int[16];
        for (int n = 0; n < 16; n++) {
            dim[n] = n;
        }
        for (int i = 0; i < 16; i++) {   // the palette's 16 colours, bright ones from 8
            dim[vdp.vdpColour(i)] = vdp.vdpColour(i & 7);
        }
        h.append("#define DIM_COLOURS\t");
        for (int n = 0; n < 16; n++) {
            h.append(n == 0 ? "" : ", ").append(dim[n]);
        }
        h.append('\n');
        h.append("// The same for a byte, two pixels: its plain twins'.\n#define DIM_BYTES\t");
        for (int b = 0; b < 256; b++) {
            h.append(b == 0 ? "" : ",").append(dim[b >> 4] << 4 | dim[b & 15]);
        }
        h.append('\n');

        h.append("\n// shell.bin: the strings, each a length and its characters.\n");
        // The title (TitleScreen): the wordmark (in the attract demo's band), the tagline, the menu.
        str("STR_WORDMARK", s.title().wordmark(), true);
        int taglineAt = bin.size();
        str("STR_TAGLINE", s.title().tagline(), false);
        // §14.9: the title's job line (TitleScreen: "< label >"), each job's tagline (else the
        // title's), the key that goes through them, the line's ink.
        List<ShellConfig.Job> jobList = s.newGamePlus().levels();
        int[] label = new int[jobList.size()];
        int[] tagline = new int[jobList.size()];
        for (int j = 0; j < jobList.size(); j++) {
            label[j] = bin.size();
            str("STR_JOB_LABEL_" + j, "< " + jobList.get(j).label() + " >", false);
            if (jobList.get(j).tagline() == null) {
                tagline[j] = -1;
            } else {
                tagline[j] = bin.size();
                str("STR_JOB_TAGLINE_" + j, jobList.get(j).tagline(), false);
            }
        }
        for (int j = 0; j < jobList.size(); j++) {
            if (tagline[j] < 0) {
                tagline[j] = taglineAt;
            }
        }
        h.append("#define STR_JOB_LABEL(j)\t").append(chain("j", label)).append('\n');
        h.append("#define STR_JOB_TAGLINE(j)\t").append(chain("j", tagline)).append('\n');
        String jobKey = s.newGamePlus().key();
        if (!jobKey.matches("[A-Z]")) {
            throw new ConvertException("config/shell.json: newGamePlus.key '" + jobKey + "' is not a letter");
        }
        h.append("#define JOB_KEY\tKEY_").append(jobKey).append('\n');
        def("INK_JOB", colour(s.newGamePlus().labelColour()));
        ShellConfig.Title t = s.title();
        str("STR_START", label(t.items(), "start"), true);
        str("STR_OPTIONS", label(t.items(), "options"), true);
        str("STR_FOOTER", t.footer(), false);
        str("STR_MODE_HARD", s.difficulty().hard(), false);
        str("STR_MODE_EASY", s.difficulty().easy(), false);
        str("STR_BANNER", s.attract().banner(), false);
        // The options (§7: the MSX keeps difficulty, keys, sound, load and the hi-scores).
        ShellConfig.Options o = s.options();
        str("STR_OPT_HEADING", o.heading(), true);
        for (String id : OPTIONS) {
            str("STR_OPT_" + Gen.cName(id), label(o.items(), id), true);
        }
        def("OPTION_LINES", OPTIONS.size());
        h.append("#define OPTION_STRS\t");
        for (int i = 0; i < OPTIONS.size(); i++) {
            h.append(i == 0 ? "" : ", ").append("STR_OPT_").append(Gen.cName(OPTIONS.get(i)));
        }
        h.append('\n');
        for (int i = 0; i < OPTIONS.size(); i++) {
            def("OPT_" + Gen.cName(OPTIONS.get(i)), i);
        }
        def("OPTION_LINES_IN_GAME", OPTIONS.stream().filter(id -> !ShellConfig.Options.TITLE_ONLY.contains(id)).count());
        str("STR_ON", o.on(), true);
        str("STR_OFF", o.off(), true);
        str("STR_OPT_FOOTER", o.footer(), false);
        // The keys: the input profiles' walking keys (§7: cursors or Q A O P), the active one first.
        List<String> profiles = new ArrayList<>(c.input().profiles().keySet());
        profiles.remove(c.input().active());
        profiles.add(0, c.input().active());
        if (profiles.size() != 2) {
            throw new ConvertException("data/config/input.json: " + profiles.size() + " profiles; the MSX offers two");
        }
        for (int p = 0; p < 2; p++) {
            var pr = c.input().profiles().get(profiles.get(p));
            str("STR_KEYS_" + p, String.join(" ", pr.up().get(0), pr.down().get(0), pr.left().get(0),
                    pr.right().get(0)), true);
        }
        // The pause menu, and the save and load pages (M11 fills their slots).
        ShellConfig.Pause pause = s.pause();
        str("STR_PAUSE_HEADING", pause.heading(), true);
        for (ShellConfig.Entry e : pause.items()) {
            str("STR_PAUSE_" + Gen.cName(e.id()), e.label(), true);
        }
        def("PAUSE_LINES", pause.items().size());
        h.append("#define PAUSE_STRS\t");
        for (int i = 0; i < pause.items().size(); i++) {
            h.append(i == 0 ? "" : ", ").append("STR_PAUSE_").append(Gen.cName(pause.items().get(i).id()));
        }
        h.append('\n');
        h.append("#define PAUSE_IDS\t");
        for (int i = 0; i < pause.items().size(); i++) {
            h.append(i == 0 ? "" : ", ").append("PAUSE_").append(Gen.cName(pause.items().get(i).id()));
        }
        h.append('\n');
        for (int i = 0; i < ShellConfig.Pause.IDS.size(); i++) {
            def("PAUSE_" + Gen.cName(ShellConfig.Pause.IDS.get(i)), i);
        }
        str("STR_PAUSE_FOOTER", pause.footer(), false);
        ShellConfig.Saves sv = s.saves();
        def("SAVE_SLOTS", sv.slots());
        str("STR_SAVE_HEADING", sv.saveHeading(), true);
        str("STR_LOAD_HEADING", sv.loadHeading(), true);
        str("STR_SAVE_FOOTER", sv.footer(), false);
        str("STR_NEW_GAME_HEADING", sv.newGameHeading(), true);
        str("STR_NEW_GAME", t.newGameLabel(), true);
        str("STR_CONTINUE_LINE", label(t.items(), "continue"), true);
        str("STR_MODE_NAME_HARD", sv.modeNames().hard(), true);
        str("STR_MODE_NAME_EASY", sv.modeNames().easy(), true);
        // Each job's name (§14.9), and STR_JOB(j) to pick one.
        int jobs = c.game().newGamePlus().levels().size();
        StringBuilder pick = new StringBuilder();
        for (int j = 0; j < jobs; j++) {
            str("STR_JOB_" + j, sv.jobNames().get(j), true);
            pick.append(j + 1 < jobs ? "(j) == " + j + " ? " : "").append("STR_JOB_").append(j)
                    .append(j + 1 < jobs ? " : " : "");
        }
        h.append("#define STR_JOB(j)\t(").append(pick).append(")\n");
        def("AUTOSAVE_TICKS", sv.autosaveEveryTicks());
        def("NOTE_TICKS", sv.noteTicks());
        h.append("// Templates: each {field} a byte under 32 (TF_*), which shell.c fills in.\n");
        for (int i = 0; i < FIELDS.size(); i++) {
            def("TF_" + Gen.cName(FIELDS.get(i).replaceAll("[{}]", "")), i + 1);
        }
        tmpl("STR_T_SLOT", sv.slot());
        tmpl("STR_T_DETAIL", sv.detail());
        tmpl("STR_T_EMPTY", sv.empty());
        tmpl("STR_T_SAVED", sv.saved());
        tmpl("STR_T_LOADED", sv.loaded());
        tmpl("STR_T_EMPTY_SLOT", sv.emptySlot());
        tmpl("STR_T_REFUSED", sv.refused());
        tmpl("STR_T_REPLACE", sv.replace());
        tmpl("STR_T_AUTOSAVED", sv.autosaved());
        tmpl("STR_T_CONTINUE_DETAIL", t.continueDetail());
        // Why a slot will not load: SaveSlots.load's words (literals in its code); and the MSX's own
        // when it has nothing to save on (AGENTS.md §10, M11: saving greyed out, and saying why).
        str("STR_WHY_DAMAGED", "SAVE IS DAMAGED", false);
        str("STR_WHY_OTHER_VERSION", "SAVE IS FROM ANOTHER VERSION", false);
        str("STR_WHY_NO_MEDIUM", "NO DISK DRIVE TO SAVE ON", false);
        // Game over (GameOverScreen), the reveal (AmuletRevealScreen), the tally's heading.
        ShellConfig.GameOver go = s.gameOver();
        str("STR_GAME_OVER", go.text(), true);
        template("STR_PROGRESS", go.progress(), "{percent}", true);
        str("STR_CONTINUE", go.continuePrompt(), true);
        template("STR_CONTINUES", go.continuesLeft(), "{n}", true);
        str("STR_TALLY", s.tally().heading(), true);
        str("STR_TOTAL", s.tally().total(), true);
        str("STR_PRESS_FIRE", s.tally().prompt(), false);
        str("STR_REVEAL_PROMPT", s.amuletReveal().prompt(), true);
        List<List<String>> verses = s.amuletReveal().verses();
        List<Integer> verseAt = new ArrayList<>();
        def("VERSES", verses.size());
        def("VERSE_LINES", verses.get(0).size());
        for (int v = 0; v < verses.size(); v++) {
            if (verses.get(v).size() != verses.get(0).size()) {
                throw new ConvertException("config/shell.json: the reveal's verses must have as many lines each");
            }
            for (int l = 0; l < verses.get(v).size(); l++) {
                verseAt.add(bin.size());
                str("STR_VERSE_" + v + "_" + l, verses.get(v).get(l), true);
            }
        }
        // The hi-scores (HiScoreScreen, HiScoreEntryScreen: its line's words are literals in its code).
        ShellConfig.HiScores hs = s.hiScores();
        str("STR_HISCORES", hs.heading(), true);
        str("STR_NEW_BEST", hs.entryHeading(), true);
        str("STR_ENTRY_PROMPT", hs.entryPrompt(), false);
        str("STR_ENTRY_SCORE", "SCORE ", false);
        str("STR_ENTRY_PLACE", "   PLACE ", false);
        str("STR_EASY_MARK", s.difficulty().hiScoreMark(), false);
        str("STR_OF_QUARTERS", "/" + c.landmarks().lairs().size(), false);
        str("STR_ALPHABET", hs.alphabet(), false);
        // The map (MapScreen): what the panel's line says while it is open.
        str("STR_MAP", s.map().messageFor(c.input().active()), false);
        // §14.9: each job's verses (Shell.revealFor: the game's own for the first), their lines'
        // strings, then a table of each job's: its lines' offsets, verse by verse, two bytes each.
        int[][] lines = new int[jobs][];
        int[] count = new int[jobs];
        for (int j = 0; j < jobs; j++) {
            List<List<String>> own = j == 0 || s.newGamePlus().levels().get(j).verses() == null ? null
                    : s.newGamePlus().levels().get(j).verses();
            if (own == null) {
                lines[j] = null;
                count[j] = verses.size();
                continue;
            }
            count[j] = own.size();
            lines[j] = new int[own.size() * verses.get(0).size()];
            for (int v = 0; v < own.size(); v++) {
                if (own.get(v).size() != verses.get(0).size()) {
                    throw new ConvertException("config/shell.json: job " + j + "'s verses must have "
                            + verses.get(0).size() + " lines each, as the reveal's");
                }
                for (int l = 0; l < own.get(v).size(); l++) {
                    lines[j][v * verses.get(0).size() + l] = bin.size();
                    str("STR_JOB_VERSE_" + j + "_" + v + "_" + l, own.get(v).get(l), true);
                }
            }
        }
        h.append("// Job j's verses (VERSE_TABLE(j)): VERSE_LINES offsets of shell.bin's strings a verse; JOB_VERSES(j) of them.\n");
        int[] table = new int[jobs];
        for (int j = 0; j < jobs; j++) {
            table[j] = bin.size();
            if (lines[j] == null) {
                for (int v = 0; v < verses.size(); v++) {
                    for (int l = 0; l < verses.get(v).size(); l++) {
                        int at = verseAt.get(v * verses.get(0).size() + l);
                        bin.write(at & 0xFF);
                        bin.write(at >> 8);
                    }
                }
            } else {
                for (int at : lines[j]) {
                    bin.write(at & 0xFF);
                    bin.write(at >> 8);
                }
            }
        }
        h.append("#define VERSE_TABLE(j)\t").append(chain("j", table)).append('\n');
        h.append("#define JOB_VERSES(j)\t").append(chain("j", count)).append('\n');

        // The hi-score table a new machine has (default_highscores.json), ranked.
        h.append("\n// The hi-score table a new machine has: HISCORE_SIZE rows of HISCORE_ROW_BYTES.\n");
        def("HISCORE_DEFAULTS", bin.size());
        List<HighScores.Entry> rows = c.defaultScores().ranked(hs.size()).entries();
        for (int i = 0; i < hs.size(); i++) {
            HighScores.Entry e = i < rows.size() ? rows.get(i) : null;
            String name = e == null ? " ".repeat(hs.nameLength()) : e.name();
            if (name.length() != hs.nameLength()) {
                throw new ConvertException("default_highscores.json: " + name + " is not " + hs.nameLength() + " letters");
            }
            bin.writeBytes(name.getBytes(StandardCharsets.US_ASCII));
            u32(e == null ? 0 : e.score());
            bin.write(e == null ? 0 : e.pieces());
            u32(e == null ? 0 : e.ticks());
            bin.write(e != null && e.easy() ? 1 : 0);
        }
        if (hs.nameLength() != 3) {
            throw new ConvertException("config/shell.json: hi-score names of " + hs.nameLength() + "; a row holds 3");
        }

        // The map screen's rooms (MapScreen.paint): pixel (u, v) of a room is lit where its mask's
        // cell (u * COLS / 16, v * ROWS / 11) is open.
        h.append("\n// The map screen: each room's miniature, MINI_BYTES a room (MapScreen).\n");
        int fieldW = c.display().playfield().w();
        int fieldH = c.display().playfield().h();
        if (fieldW / RoomAddress.GRID_W != MINI_W || fieldH / RoomAddress.GRID_H != MINI_H) {
            throw new ConvertException("config/display.json: the map's rooms are " + fieldW / RoomAddress.GRID_W + "x"
                    + fieldH / RoomAddress.GRID_H + "; the ROM draws them 16x11");
        }
        def("MAP_MINIS", bin.size());
        def("MINI_W", MINI_W);
        def("MINI_H", MINI_H);
        def("MINI_BYTES", MINI_BYTES);
        for (int room = 0; room < RoomAddress.GRID_W * RoomAddress.GRID_H; room++) {
            CollisionMask mask = c.rooms().room(new RoomAddress(room % RoomAddress.GRID_W, room / RoomAddress.GRID_W))
                    .mask();
            for (int v = 0; v < MINI_H; v++) {
                int bits = 0;
                for (int u = 0; u < MINI_W; u++) {
                    if (!mask.isSolid(u * CollisionMask.COLS / MINI_W, v * CollisionMask.ROWS / MINI_H)) {
                        bits |= 0x8000 >> u;
                    }
                }
                bin.write(bits >> 8);
                bin.write(bits & 0xFF);
            }
        }
        // The map's marks: the arch, and which lairs each eye shows (by the lairs' order).
        LandmarksData marks = c.landmarks();
        def("MAP_ARCH_ROOM", marks.exitRoom().index());
        h.append("// Each eye: the lairs it shows on the map, a bit a lair (landmarks.lairs()'s order).\n");
        h.append("#define EYE_SHOWS\t");
        List<LandmarksData.Eye> eyes = marks.items().eyes();
        for (int i = 0; i < eyes.size(); i++) {
            int m = 0;
            for (int l = 0; l < marks.lairs().size(); l++) {
                if (eyes.get(i).reveals().contains(marks.lairs().get(l).id())) {
                    m |= 1 << l;
                }
            }
            h.append(i == 0 ? "" : ", ").append(m);
        }
        h.append('\n');

        // The images, in video RAM below the scenery.
        h.append("\n// shell_img.bin's images, in video RAM from SHELL_VRAM_LINE: where each is.\n");
        def("SHELL_VRAM_LINE", vramLine);
        int rowTop = 0;
        // The amulet's quarters at the reveal's scale, by slot, in a row.
        SpriteData amulet = c.sprites().get(marks.amulet().sprite());
        Map<String, byte[]> amuletPx = amulet.decode(marks.amulet().sprite());
        List<String> bySlot = wulf.render.QuestPainter.framesBySlot(marks);
        int scale = s.amuletReveal().scale();
        int qw = amulet.size().w() * scale;
        int qh = amulet.size().h() * scale;
        def("IMG_QUARTER_W", qw);
        def("IMG_QUARTER_H", qh);
        def("IMG_QUARTER_X", 0);
        def("IMG_QUARTER_Y", vramLine + rowTop);
        for (int slot = 0; slot < bySlot.size(); slot++) {
            image(slot * qw, rowTop, scaled(amuletPx.get(bySlot.get(slot)), amulet.size().w(), amulet.size().h(), scale),
                    qw, qh);
        }
        // §14.9: each later job's own quarters, the same images in the same place, which the ROM
        // loads over them when a game of that job reveals its amulet (shell_jobs.bin, from job 1;
        // job 0's are shell_img.bin's own).
        wulf.render.AmuletFrames jobFrames = wulf.render.AmuletFrames.of(marks, s);
        int jobCount = c.game().newGamePlus().levels().size();
        for (int j = 1; j < jobCount; j++) {
            List<String> own = jobFrames.forLevel(j);
            for (int slot = 0; slot < own.size(); slot++) {
                image(jobImg, slot * qw, rowTop,
                        scaled(amuletPx.get(own.get(slot)), amulet.size().w(), amulet.size().h(), scale), qw, qh);
            }
            if (j == 1) {
                def("SHELL_JOB_IMG_BYTES", jobImg.size());
            }
        }
        def("SHELL_JOB_IMAGES", bySlot.size());
        // The marker after them (the menu's wolf).
        SpriteData marker = c.sprites().get(t.marker().sprite());
        int mx = bySlot.size() * qw;
        def("IMG_MARKER_X", mx);
        def("IMG_MARKER_Y", vramLine + rowTop);
        def("IMG_MARKER_W", marker.size().w());
        def("IMG_MARKER_H", marker.size().h());
        image(mx, rowTop, marker.decode(t.marker().sprite()).get(t.marker().frame()), marker.size().w(),
                marker.size().h());
        rowTop += Math.max(qh, marker.size().h());
        // The map's icons, in a row: the quarter's shiny frames and its dull one, then the arch's.
        SpriteData diamond = c.sprites().get(s.map().quarterIcon().sprite());
        SpriteData arch = c.sprites().get(s.map().exitIcon().sprite());
        List<String> dFrames = new ArrayList<>(s.map().quarterIcon().shiny());
        dFrames.add(s.map().quarterIcon().taken());
        List<String> aFrames = new ArrayList<>();
        aFrames.add(s.map().exitIcon().frame());
        aFrames.addAll(s.map().exitIcon().open());
        int step = Math.max(even(diamond.size().w()), even(arch.size().w()));
        int x = 0;
        def("IMG_ICON_STEP", step);
        def("IMG_ICON_Y", vramLine + rowTop);
        def("IMG_DIAMOND_X", x);
        def("IMG_DIAMOND_W", diamond.size().w());
        def("IMG_DIAMOND_H", diamond.size().h());
        def("IMG_DIAMOND_OX", diamond.origin().x());
        def("IMG_DIAMOND_OY", diamond.origin().y());
        Map<String, byte[]> dPx = diamond.decode(s.map().quarterIcon().sprite());
        for (String f : dFrames) {
            image(x, rowTop, dPx.get(f), diamond.size().w(), diamond.size().h());
            x += step;
        }
        def("IMG_ARCH_X", x);
        def("IMG_ARCH_W", arch.size().w());
        def("IMG_ARCH_H", arch.size().h());
        def("IMG_ARCH_OX", arch.origin().x());
        def("IMG_ARCH_OY", arch.origin().y());
        Map<String, byte[]> aPx = arch.decode(s.map().exitIcon().sprite());
        for (String f : aFrames) {
            image(x, rowTop, aPx.get(f), arch.size().w(), arch.size().h());
            x += step;
        }
        rowTop += Math.max(diamond.size().h(), arch.size().h());
        // The wordmark's words, as stencils at the title's scale, one under the other.
        int ws = privateInt(wulf.ui.TitleScreen.class, "WORDMARK_SCALE");
        String[] words = t.wordmark().split(" ");
        def("WORDMARK_WORDS", words.length);
        def("IMG_WORD_Y", vramLine + rowTop);
        def("IMG_WORD_H", 8 * ws);
        h.append("// The words' widths: shell.bin at WORD_WIDTHS, a byte each.\n");
        def("WORD_WIDTHS", bin.size());
        for (String word : words) {
            int w = word.length() * big.advance() * ws;
            if (w > SCREEN_W - 2) {
                throw new ConvertException("config/shell.json: the wordmark's word " + word + " is " + w + " wide");
            }
            bin.write(w);
            byte[] px = new byte[w * 8 * ws];
            int black = c.palette().indexOf("black");
            java.util.Arrays.fill(px, (byte) black);
            for (int i = 0; i < word.length(); i++) {
                List<String> glyph = big.glyphs().get(String.valueOf(word.charAt(i)));
                for (int gy = 0; gy < 8 && glyph != null; gy++) {
                    for (int gx = 0; gx < 8; gx++) {
                        Integer v = big.legend().get(String.valueOf(glyph.get(gy).charAt(gx)));
                        if (v != null && v >= 0) {
                            for (int dy = 0; dy < ws; dy++) {
                                for (int dx = 0; dx < ws; dx++) {
                                    px[(gy * ws + dy) * w + (i * big.advance() + gx) * ws + dx] = -1;
                                }
                            }
                        }
                    }
                }
            }
            image(0, rowTop, px, w, 8 * ws);
            rowTop += 8 * ws;
        }
        def("SHELL_IMAGES", images);
        // Below them, room for a word to be coloured in before it goes on screen (the title's cycle).
        def("SHELL_SCRATCH_Y", vramLine + rowTop);
        rowTop += 8 * ws;
        imgLine = rowTop;
        if (vramLine + rowTop > 1024) {
            throw new ConvertException("the shell's images need " + rowTop + " lines of video RAM under the scenery; "
                    + (1024 - vramLine) + " are free");
        }

        // The attract demo (§17.2), recorded on the MSX (AGENTS.md §7; tools/record-demo.sh): the
        // keys the ROM read each tick of a new game, Hard, from its start and seed, as runs.
        h.append("\n// The attract demo, recorded on the MSX (demo.txt): its runs in shell.bin at DEMO_RUNS,\n");
        h.append("// DEMO_RUN_COUNT of them, each a tick count and the keys (DEMO_UP ...).\n");
        long seed = -1;
        int startX = 0;
        int startY = 0;
        List<int[]> recorded = new ArrayList<>();
        try (var in = Shell.class.getResourceAsStream(DEMO)) {
            if (in == null) {
                throw new ConvertException(DEMO + ": no recorded demo (tools/record-demo.sh records one)");
            }
            for (String line : new String(in.readAllBytes(), StandardCharsets.US_ASCII).split("\n")) {
                String[] w = line.trim().split(" ");
                if (line.startsWith("#") || line.isBlank()) {
                    continue;
                }
                if (w[0].equals("seed")) {
                    seed = Long.parseLong(w[1]);
                } else if (w[0].equals("start")) {
                    startX = Integer.parseInt(w[1]);
                    startY = Integer.parseInt(w[2]);
                } else {
                    int n = Integer.parseInt(w[0]);
                    int k = Integer.parseInt(w[1]);
                    if (n < 1 || n > 255 || (k & ~31) != 0) {
                        throw new ConvertException(DEMO + ": a run of " + n + " ticks of keys " + k);
                    }
                    recorded.add(new int[] {n, k});
                }
            }
        } catch (java.io.IOException e) {
            throw new ConvertException(DEMO + ": " + e.getMessage());
        }
        if (seed < 0 || recorded.isEmpty()) {
            throw new ConvertException(DEMO + ": no seed, or no runs");
        }
        def("DEMO_RUNS", bin.size());
        int ticks = 0;
        for (int[] run : recorded) {
            bin.write(run[0]);
            bin.write(run[1]);
            ticks += run[0];
        }
        def("DEMO_RUN_COUNT", recorded.size());
        def("DEMO_TICKS", ticks);
        def("DEMO_UP", 1);
        def("DEMO_DOWN", 2);
        def("DEMO_LEFT", 4);
        def("DEMO_RIGHT", 8);
        def("DEMO_FIRE", 16);
        h.append("#define DEMO_START_X_FP\t").append(startX).append("L\n");
        h.append("#define DEMO_START_Y_FP\t").append(startY).append("L\n");
        h.append("#define DEMO_SEED_BYTES\t");
        for (int i = 0; i < 8; i++) {
            h.append(i == 0 ? "" : ", ").append(String.format("0x%02X", (seed >> (8 * i)) & 0xFF));
        }
        h.append('\n');
        // The Java game, given the same keys: where the demo must end (test/shell.sh checks the ROM's).
        WorldGrid world = new WorldGrid(c.rooms());
        MoveTraces.Scenario sc = new MoveTraces.Scenario("demo", c.map().startRoom(), false, startX, startY, List.of(), "-",
                seed, MoveTraces.Scenario.GAME, false, false);
        Simulation sim = MoveTraces.simulation(c, world, sc);
        boolean fireWas = false;
        for (int[] run : recorded) {
            for (int i = 0; i < run[0]; i++) {
                sim.tick(MoveTraces.input(run[1], fireWas));
                fireWas = (run[1] & 16) != 0;
                if (sim.player().mode() == wulf.sim.Player.Mode.GAME_OVER) {
                    throw new ConvertException(DEMO + ": the demo's game is over; record one that lasts");
                }
            }
        }
        int demoTicks = ticks;
        // And the map screen (MapScreen) for that game, opened as the demo ends, at its first tick.
        demoMap = mapOf(sim);
        // And the looking glass's trail over it, had he one (M12): the ROM's search (Trail), and
        // how much of the Java game's own way it shares, for test/trail.sh.
        demoTrail = trailOf(sim, world);
        // What test/shell.sh checks the ROM's demo against: where it ends.
        demoCheck = String.format("demo ticks %d room %d x %d y %d score %d lives %d%n", demoTicks,
                sim.room().index(), sim.player().xFp(), sim.player().yFp(), sim.score(), sim.player().lives());

        if (bin.size() > 16384 || img.size() > 16384) {
            throw new ConvertException("shell.bin (" + bin.size() + ") or shell_img.bin (" + img.size()
                    + ") is more than a segment");
        }
    }

    private final byte[] demoMap;
    private final String demoTrail;

    private String trailOf(Simulation sim, WorldGrid world) {
        wulf.data.PlayerData.Box feet = c.player().collisionBox();
        Trail trail = new Trail(world, c.landmarks(), feet);
        LandmarksData marks = sim.landmarks();
        int set = marks.amuletSet();
        int done = 0;
        for (int i = 0; i < marks.lairs().size(); i++) {
            done |= sim.quest().taken(i) ? 1 << i : 0;
        }
        int x = wulf.engine.Fixed.px(sim.player().xFp());
        int y = wulf.engine.Fixed.px(sim.player().yFp());
        List<Integer> mine = Trail.pixels(trail.route(set, done, sim.room().index(), x, y, 1 << 16));
        // The Java game's (WayPainter.route: Wayfinder over the whole jungle).
        wulf.world.Wayfinder finder = new wulf.world.Wayfinder(world);
        List<Integer> targets = new ArrayList<>();
        for (int i = 0; i < marks.lairs().size(); i++) {
            if (!sim.quest().taken(i)) {
                LandmarksData.Lair l = marks.lairs().get(i);
                wulf.world.RoomAddress r = LandmarksData.room(l.room());
                targets.add(finder.nearest(null, Math.floorDiv(r.col() * 256 + l.pedestal().x() + feet.x(), 8),
                        Math.floorDiv(r.row() * 176 + l.pedestal().y() + feet.y(), 8), Trail.NEAR));
            }
        }
        int[] dist = finder.distances(targets.stream().mapToInt(Integer::intValue).toArray());
        int from = finder.nearest(dist, Math.floorDiv(x + feet.x(), 8), Math.floorDiv(y + feet.y(), 8), Trail.NEAR);
        List<int[]> jc = new ArrayList<>();
        for (int cell : finder.path(dist, from)) {
            jc.add(new int[] {wulf.world.Wayfinder.gx(cell), wulf.world.Wayfinder.gy(cell)});
        }
        List<Integer> java = Trail.pixels(jc);
        java.util.Set<Integer> js = new java.util.HashSet<>(java);
        StringBuilder sb = new StringBuilder();
        sb.append(String.format("trail set %d done %d whole %d pixels %d java %d shared %d%n", set, done,
                trail.whole ? 1 : 0, mine.size(), java.size(), mine.stream().filter(js::contains).count()));
        for (int px : mine) {
            sb.append(px % 256).append(' ').append(px / 256).append('\n');
        }
        return sb.toString();
    }

    /** The trail over the demo's map, as the ROM finds it: for test/trail.sh. */
    public String demoTrail() {
        return demoTrail;
    }

    /** The Java game's map screen over {@code sim}: the playfield, a byte a pixel, in colour numbers. */
    private byte[] mapOf(Simulation sim) {
        try {
            wulf.render.SpriteBank sprites = new wulf.render.SpriteBank(c.sprites());
            ShellConfig.MapScreen m = c.shell().map();
            Class<?> k = Class.forName("wulf.ui.MapScreen");
            java.lang.reflect.Constructor<?> make = k.getDeclaredConstructor(wulf.data.DisplayConfig.class,
                    wulf.world.RoomBaker.class, LandmarksData.class, wulf.data.Palette.class,
                    ShellConfig.MapScreen.class, wulf.render.Sprite.class, wulf.render.Sprite.class,
                    wulf.render.WayPainter.class);
            make.setAccessible(true);
            Object screen = make.newInstance(c.display(), c.rooms(), c.landmarks(), c.palette(), m,
                    sprites.get(m.quarterIcon().sprite()), sprites.get(m.exitIcon().sprite()), null);
            java.lang.reflect.Method paint = k.getDeclaredMethod("paint", wulf.render.Framebuffer.class,
                    Simulation.class, long.class);
            paint.setAccessible(true);
            var d = c.display();
            wulf.render.Framebuffer fb = new wulf.render.Framebuffer(d.canvas().w(), d.canvas().h());
            paint.invoke(screen, fb, sim, 0L);
            var f = d.playfield();
            byte[] out = new byte[f.w() * f.h()];
            for (int y = 0; y < f.h(); y++) {
                for (int x = 0; x < f.w(); x++) {
                    out[y * f.w() + x] = (byte) vdp.vdpColour(fb.get(f.x() + x, f.y() + y));
                }
            }
            return out;
        } catch (ReflectiveOperationException e) {
            throw new ConvertException("wulfquest's MapScreen changed: " + e);
        }
    }

    /** The demo's map, as the Java game draws it: for test/shell.sh. */
    public byte[] demoMap() {
        return demoMap.clone();
    }

    /** The attract demo, as tools/record-demo.sh recorded it on the MSX. */
    static final String DEMO = "demo.txt";

    /** The options the MSX keeps (AGENTS.md §7), in the Java page's order. */
    static final List<String> OPTIONS = List.of("difficulty", "keys", "sound", "load", "hiScores", "back");

    /** The video RAM lines the images take. */
    public int lines() {
        return imgLine;
    }

    public int imageCount() {
        return images;
    }

    public int vramLine() {
        return vramLine;
    }

    /** {@code values[arg]} as C: a chain of choices, or the one value they all are. */
    static String chain(String arg, int[] values) {
        boolean same = java.util.Arrays.stream(values).allMatch(v -> v == values[0]);
        if (same) {
            return "(" + values[0] + ")";
        }
        StringBuilder sb = new StringBuilder("(");
        for (int i = 0; i < values.length; i++) {
            sb.append(i + 1 < values.length ? "(" + arg + ") == " + i + " ? " : "").append(values[i])
                    .append(i + 1 < values.length ? " : " : "");
        }
        return sb.append(')').toString();
    }

    private static int even(int w) {
        return (w + 1) & ~1;
    }

    private static byte[] scaled(byte[] px, int w, int hgt, int scale) {
        byte[] out = new byte[w * scale * hgt * scale];
        for (int y = 0; y < hgt * scale; y++) {
            for (int x = 0; x < w * scale; x++) {
                out[y * w * scale + x] = px[(y / scale) * w + x / scale];
            }
        }
        return out;
    }

    /** An image of palette indices (-1 transparent) at x, the atlas's line y: its header and bytes. */
    private void image(int x, int y, byte[] px, int w, int hgt) {
        image(img, x, y, px, w, hgt);
        images++;
    }

    private void image(ByteArrayOutputStream img, int x, int y, byte[] px, int w, int hgt) {
        if ((x & 1) != 0) {
            throw new ConvertException("an image at odd x " + x);
        }
        int bw = even(w);
        img.write(x);
        img.write(y & 0xFF);
        img.write(y >> 8);
        img.write(bw);
        img.write(hgt);
        for (int yy = 0; yy < hgt; yy++) {
            for (int xx = 0; xx < bw; xx += 2) {
                int a = xx < w ? px[yy * w + xx] : -1;
                int b = xx + 1 < w ? px[yy * w + xx + 1] : -1;
                img.write((a < 0 ? 0 : vdp.vdpColour(a)) << 4 | (b < 0 ? 0 : vdp.vdpColour(b)));
            }
        }
    }

    private int colour(String name) {
        return vdp.vdpColour(c.palette().indexOf(name));
    }

    private String colours(List<String> names) {
        StringBuilder sb = new StringBuilder();
        for (String n : names) {
            sb.append(sb.length() == 0 ? "" : ", ").append(colour(n));
        }
        return sb.toString();
    }

    private static String label(List<ShellConfig.Entry> items, String id) {
        for (ShellConfig.Entry e : items) {
            if (e.id().equals(id)) {
                return e.label();
            }
        }
        throw new ConvertException("config/shell.json: no line '" + id + "', which the MSX shows");
    }

    /** A string, checked to fit a line in its font (the big, or the small). */
    private void str(String name, String text, boolean big) {
        fits(text, big);
        byte[] b = text.getBytes(StandardCharsets.US_ASCII);
        def(name, bin.size());
        bin.write(b.length);
        bin.writeBytes(b);
    }

    /** Whether the text fits a line in its font, and the font has its characters. */
    private void fits(String text, boolean big) {
        int advance = c.font().sets().get(big ? "big" : "small").advance();
        if (text.length() * advance > SCREEN_W) {
            throw new ConvertException("config/shell.json: \"" + text + "\" is " + text.length() * advance
                    + " pixels in the " + (big ? "big" : "small") + " font; the screen is " + SCREEN_W);
        }
        for (char ch : text.toCharArray()) {
            if (ch < 32 || ch > (big ? 95 : 126)) {
                throw new ConvertException("config/shell.json: \"" + text + "\" has '" + ch + "', which the "
                        + (big ? "big" : "small") + " font has not");
            }
        }
    }

    /** The fields the saves' templates fill in (shell.json's saves, title.continueDetail). */
    static final List<String> FIELDS = List.of("{n}", "{mode}", "{job}", "{quarters}", "{score}", "{date}", "{room}",
            "{why}", "{line}");

    /** A template: its {@code {field}}s as bytes 1.. (FIELDS' order), the rest as it is. */
    private void tmpl(String name, String text) {
        String plain = text;
        String coded = text;
        for (int i = 0; i < FIELDS.size(); i++) {
            plain = plain.replace(FIELDS.get(i), "");
            coded = coded.replace(FIELDS.get(i), String.valueOf((char) (i + 1)));
        }
        if (plain.contains("{")) {
            throw new ConvertException("config/shell.json: \"" + text + "\" has a field the MSX does not fill in");
        }
        fits(plain, false);   // its words are the font's
        byte[] b = coded.getBytes(StandardCharsets.US_ASCII);
        def(name, bin.size());
        bin.write(b.length);
        bin.writeBytes(b);
    }

    /** A template with one {@code {x}}: the words before it and after it, as _A and _B. */
    private void template(String name, String text, String hole, boolean big) {
        int at = text.indexOf(hole);
        if (at < 0) {
            throw new ConvertException("config/shell.json: \"" + text + "\" has no " + hole);
        }
        str(name + "_A", text.substring(0, at), big);
        str(name + "_B", text.substring(at + hole.length()), big);
    }

    private void u32(long v) {
        for (int i = 0; i < 4; i++) {
            bin.write((int) (v >> (8 * i)) & 0xFF);
        }
    }

    private static Class<?> cls(String name) {
        try {
            return Class.forName(name);
        } catch (ClassNotFoundException e) {
            throw new ConvertException(name + ": the Java game does not have it");
        }
    }

    private static int privateInt(Class<?> k, String name) {
        try {
            Field f = k.getDeclaredField(name);
            f.setAccessible(true);
            return f.getInt(null);
        } catch (ReflectiveOperationException e) {
            throw new ConvertException(k.getName() + "." + name + ": the Java game does not have it");
        }
    }

    private void def(String name, long v) {
        h.append("#define ").append(name).append('\t').append(v).append('\n');
    }

    public String header() {
        return h.toString();
    }

    public byte[] blob() {
        return bin.toByteArray();
    }

    public byte[] images() {
        return img.toByteArray();
    }

    /** shell_jobs.bin: each later job's quarters at the reveal's scale, SHELL_JOB_IMG_BYTES a job from job 1. */
    public byte[] jobImages() {
        return jobImg.toByteArray();
    }

    /** The demo's end, as the Java game plays it: for test/shell.sh. */
    public String demoCheck() {
        return demoCheck;
    }
}
