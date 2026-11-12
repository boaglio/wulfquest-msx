package wulfmsx.convert;

import java.io.ByteArrayOutputStream;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import wulf.Content;
import wulf.data.ShellConfig;
import wulf.data.SpriteData;
import wulf.world.SceneryCatalog;

/**
 * The scenes around the game (AGENTS.md §10, M12; the Java game's EndingScreen, TallyScreen,
 * CreditsRoll and IntroScreen, from {@code shell.json}): the closing scene, the tally, the
 * credits roll and the plane's intro, as tables for the ROM.
 *
 * <p>The Java game draws them on its whole canvas (320x256); the MSX2's screen is the
 * playfield's size and the panel's: everything is moved by the playfield's place on the canvas,
 * so the scenes stand where the game's rooms do, and what falls off the edges is cut. A typed
 * line wider than the screen in the big font is broken at a word.
 *
 * <p>{@code scenes_img.bin}: the figures (Vale walking, Mr Jones, the amulet's quarters at half
 * size, the plane, the intro's strip), loaded into the scenery atlas's video RAM while a scene
 * plays, after the backdrop is drawn from it: a header each (x, line from the first as 16 bits,
 * width, height) and its bytes, as {@code shell_img.bin}'s. {@code scenes.bin}: the images'
 * places and origins, the backdrops as copies already clipped, the moves worked out a tick at a
 * time (they divide, which the ROM's banked code may not), the lines and rows of text.
 */
public final class Scenes {

    static final int SCREEN_W = 256;
    static final int SCREEN_H = 212;
    static final int BIG_CHARS = SCREEN_W / 8;
    /** The video RAM lines of the scenery atlas the scenes borrow (SceneryAtlas). */
    static final int ATLAS_FIRST = SceneryAtlas.VRAM_FIRST_LINE;

    private final Content c;
    private final VdpPalette vdp;
    private final SceneryAtlas atlas;
    private final StringBuilder h = new StringBuilder();
    private final ByteArrayOutputStream bin = new ByteArrayOutputStream();
    private final ByteArrayOutputStream img = new ByteArrayOutputStream();
    private final int ox;
    private final int oy;
    private final int canvasH;
    private final Map<String, int[]> images = new LinkedHashMap<>();   // name: index, x, y, w, h, ox, oy
    private int imgX;
    private int imgY;
    private int imgShelf;
    private int imgLine0;
    private final List<String> rollRows = new ArrayList<>();

    public Scenes(Content c, VdpPalette vdp, SceneryAtlas atlas, int shellFirstLine) {
        this.c = c;
        this.vdp = vdp;
        this.atlas = atlas;
        this.ox = c.display().playfield().x();
        this.oy = c.display().playfield().y();
        this.canvasH = c.display().canvas().h();
        ShellConfig s = c.shell();
        ShellConfig.Ending e = s.ending();
        ShellConfig.Intro intro = s.attract().intro();

        h.append(Gen.banner("config/shell.json (ending, tally, credits, attract.intro), the Java game's scenes"));
        h.append("#pragma once\n\n");
        h.append("// The scenes stand where the rooms do: the Java canvas moved by the playfield's place.\n");

        // The band the figures move in, restored a rectangle at a time from its copy: above the
        // first typed line of either scene.
        int band = Math.min(e.lines().stream().mapToInt(l -> l.y() - oy).min().orElse(SCREEN_H),
                intro.textY() - oy);
        def("SCENE_BAND", band);
        def("SCENE_BACKDROP_Y", ATLAS_FIRST);
        imgLine0 = ATLAS_FIRST + band;
        def("SCENE_IMG_Y", imgLine0);

        // The figures, into the rest of the atlas's lines.
        SpriteData player = c.sprites().get(c.player().sprite());
        Map<String, byte[]> pf = player.decode(c.player().sprite());
        for (String f : e.vale().walk()) {
            image("VALE_" + Gen.cName(f), pf.get(f), player);
        }
        image("VALE_STAND", pf.get(e.vale().stand()), player);
        SpriteData jones = c.sprites().get(e.jones().sprite());
        Map<String, byte[]> jf = jones.decode(e.jones().sprite());
        for (String f : List.of(e.jones().stand(), e.jones().talk(), e.jones().hold(), e.jones().holdTalk())) {
            image("JONES_" + Gen.cName(f), jf.get(f), jones);
        }
        SpriteData amulet = c.sprites().get(c.landmarks().amulet().sprite());
        Map<String, byte[]> af = amulet.decode(c.landmarks().amulet().sprite());
        // §14.9: each job's own amulet, job after job (SCI_QUARTER(j) + slot).
        wulf.render.AmuletFrames jobFrames = wulf.render.AmuletFrames.of(c.landmarks(), s);
        int jobs = c.game().newGamePlus().levels().size();
        int[] quarter = new int[jobs];
        for (int j = 0; j < jobs; j++) {
            List<String> bySlot = jobFrames.forLevel(j);
            quarter[j] = images.size();
            for (int slot = 0; slot < bySlot.size(); slot++) {
                imageHalf("QUARTER_" + j + "_" + slot, af.get(bySlot.get(slot)), amulet.size().w(), amulet.size().h());
            }
        }
        // The ride home (§14.7): the horses of the one job that has it.
        ShellConfig.Ride ride = null;
        int rideJob = -1;
        for (int j = 0; j < jobs; j++) {
            ShellConfig.Ride rj = s.endingFor(j).ride();
            if (rj != null) {
                if (ride != null) {
                    throw new ConvertException("config/shell.json: two jobs ride home; the ROM has one ride");
                }
                ride = rj;
                rideJob = j;
            }
        }
        if (ride != null) {
            SpriteData horse = c.sprites().get(ride.sprite());
            Map<String, byte[]> hf = horse.decode(ride.sprite());
            for (List<String> set : List.of(ride.valeHorse(), ride.jonesHorse(), ride.valeRiding(), ride.jonesRiding())) {
                if (set.size() != ride.valeHorse().size()) {
                    throw new ConvertException("config/shell.json: the ride's frame lists differ in length");
                }
                for (String f : set) {
                    image("HORSE_" + Gen.cName(f), hf.get(f), horse);
                }
            }
        }
        SpriteData plane = c.sprites().get(intro.plane().sprite());
        Map<String, byte[]> planeF = plane.decode(intro.plane().sprite());
        for (String f : intro.plane().frames()) {
            image("PLANE_" + Gen.cName(f), planeF.get(f), plane);
        }
        image("PLANE_PARKED", planeF.get(intro.plane().parked()), plane);
        // Backdrop pieces that are not scenery (the intro's strip): drawn from here too.
        for (ShellConfig.Placed p : intro.backdrop()) {
            if (atlasSlot(p.sprite()) == null && !images.containsKey("BACK_" + Gen.cName(p.sprite()))) {
                SpriteData sd = c.sprites().get(p.sprite());
                image("BACK_" + Gen.cName(p.sprite()), sd.decode(p.sprite()).get(p.frame()), sd);
            }
        }
        if (imgY + imgShelf + imgLine0 > SceneryAtlas.VRAM_FIRST_LINE + SceneryAtlas.MAX_LINES
                || imgLine0 + imgY + imgShelf > shellFirstLine) {
            throw new ConvertException("the scenes' figures need video RAM to line " + (imgLine0 + imgY + imgShelf)
                    + "; the shell's images begin at " + shellFirstLine);
        }
        h.append("\n// The figures in scenes.bin's image table: SCENE_IMG_BYTES each (x, line 16 bits, w, h, ox, oy).\n");
        def("SCENE_IMG_BYTES", 7);
        def("SCENE_IMAGES", images.size());
        def("SCENE_IMG_TABLE", bin.size());
        for (Map.Entry<String, int[]> im : images.entrySet()) {
            int[] v = im.getValue();
            def("SCI_" + im.getKey(), v[0]);
            bin.write(v[1]);
            u16(v[2]);
            bin.write(v[3]);
            bin.write(v[4]);
            bin.write(v[5]);
            bin.write(v[6]);
        }

        // The ending (EndingScreen), the game's own job.
        h.append("\n// The ending (shell.json's ending): its backdrop, the walk, Mr Jones, the amulet, the lines.\n");
        backdrop("ENDING", e.backdrop());
        int gy = e.groundY() - oy;
        def("ENDING_GROUND", gy);
        def("ENDING_WALK_TICKS", e.vale().walkTicks());
        def("ENDING_WALK_EVERY", e.vale().ticksPerFrame());
        def("ENDING_WALK_FRAMES", e.vale().walk().size());
        def("ENDING_VALE_AT", e.vale().toX() - ox);
        // Vale's feet a tick at a time, walking in (EndingScreen.valeX).
        def("ENDING_WALK_X", bin.size());
        for (int t = 0; t <= e.vale().walkTicks(); t++) {
            int x = t >= e.vale().walkTicks() ? e.vale().toX()
                    : e.vale().fromX() + (e.vale().toX() - e.vale().fromX()) * t / e.vale().walkTicks();
            u16(x - ox);
        }
        def("ENDING_JONES_X", e.jones().x() - ox);
        def("ENDING_TALK_EVERY", e.jones().talkTicksPerFrame());
        ShellConfig.Amulet a = e.amulet();
        def("ENDING_HAND_AT", a.handOverAt());
        def("ENDING_HAND_TICKS", a.handOverTicks());
        def("ENDING_CARRY_DX", a.carry().dx());
        def("ENDING_CARRY_DY", a.carry().dy());
        def("ENDING_HOLD_X", e.jones().x() + a.hold().dx() - ox);
        def("ENDING_HOLD_Y", gy + a.hold().dy());
        // The amulet's way across, a tick at a time (EndingScreen.paintAmulet), from where Vale stands.
        def("ENDING_HAND_PATH", bin.size());
        int fromX = e.vale().toX() + a.carry().dx() - ox;
        int fromY = gy + a.carry().dy();
        int toX = e.jones().x() + a.hold().dx() - ox;
        int toY = gy + a.hold().dy();
        for (int k = 0; k <= a.handOverTicks(); k++) {
            u16(fromX + (toX - fromX) * k / a.handOverTicks());
            u16(fromY + (toY - fromY) * k / a.handOverTicks());
        }
        def("ENDING_HALF", amulet.size().w() / 2);
        def("ENDING_TICKS_PER_CHAR", e.ticksPerChar());
        def("ENDING_TUNE_AT", Math.max(1, e.tuneAt()));
        def("ENDING_PROMPT_AT", e.promptAt());
        def("ENDING_PROMPT_INK", colour(e.promptColour()));
        def("ENDING_PROMPT_Y", SCREEN_H - 8);
        str("STR_ENDING_PROMPT", e.prompt());
        lines("ENDING_LINES", e.lines());

        // §14.9: each job's ending (ShellConfig.endingFor: the game's, its words and beats laid over),
        // its amulet, its "the end", and Mr Jones's offer of it after the roll.
        h.append("\n// Job j's ending, roll and offer (shell.json's newGamePlus).\n");
        int[] linesAt = new int[jobs];
        int[] linesN = new int[jobs];
        int[] tuneAt = new int[jobs];
        int[] promptAt = new int[jobs];
        int[] theEnd = new int[jobs];
        int[] offerAt = new int[jobs];
        int[] offerN = new int[jobs];
        int[] offerPrompt = new int[jobs];
        for (int j = 0; j < jobs; j++) {
            ShellConfig.Ending ej = s.endingFor(j);
            if (j == 0) {
                linesAt[j] = (int) defined("ENDING_LINES");
            } else {
                linesAt[j] = bin.size();
                lines("ENDING_LINES_J" + j, ej.lines());
            }
            linesN[j] = ej.lines().size();
            tuneAt[j] = Math.max(1, ej.tuneAt());
            promptAt[j] = ej.promptAt();
            String end = s.theEndFor(j);
            if (end.length() > BIG_CHARS) {
                throw new ConvertException("config/shell.json: \"" + end + "\" is wider than the screen");
            }
            theEnd[j] = bin.size();
            str("STR_THE_END_" + j, end);
            ShellConfig.Offer offer = s.newGamePlus().levels().get(j).offer();
            if (offer == null) {
                offerAt[j] = 0;
                offerN[j] = 0;
                offerPrompt[j] = 0;
            } else {
                offerAt[j] = bin.size();
                lines("OFFER_LINES_J" + j, offer.lines());
                offerN[j] = offer.lines().size();
                offerPrompt[j] = offer.promptAt();
            }
        }
        macro("ENDING_LINES(j)", linesAt);
        macro("ENDING_LINES_N(j)", linesN);
        macro("ENDING_TUNE_AT(j)", tuneAt);
        macro("ENDING_PROMPT_AT(j)", promptAt);
        macro("STR_THE_END(j)", theEnd);
        macro("OFFER_LINES(j)", offerAt);
        macro("OFFER_LINES_N(j)", offerN);
        macro("OFFER_PROMPT_AT(j)", offerPrompt);
        macro("SCI_QUARTER(j)", quarter);
        def("ENDING_RIDE_JOB", rideJob);
        if (ride != null) {
            // EndingScreen.paintHorses, the moves a tick at a time: the horses trotting in, the gallop away.
            def("RIDE_EVERY", ride.ticksPerFrame());
            def("RIDE_FRAMES", ride.valeHorse().size());
            def("RIDE_COME_AT", ride.comeAt());
            def("RIDE_COME_TICKS", ride.comeTicks());
            def("RIDE_MOUNT_AT", ride.mountAt());
            def("RIDE_AT", ride.rideAt());
            def("RIDE_TICKS", ride.rideTicks());
            def("SCI_HORSE_VALE", images.get("HORSE_" + Gen.cName(ride.valeHorse().get(0)))[0]);
            def("SCI_HORSE_JONES", images.get("HORSE_" + Gen.cName(ride.jonesHorse().get(0)))[0]);
            def("SCI_RIDE_VALE", images.get("HORSE_" + Gen.cName(ride.valeRiding().get(0)))[0]);
            def("SCI_RIDE_JONES", images.get("HORSE_" + Gen.cName(ride.jonesRiding().get(0)))[0]);
            int valeAt = e.vale().toX();
            int jonesAt = e.jones().x();
            def("RIDE_COME_PATH", bin.size());   // per tick: Vale's horse x, Mr Jones's
            for (int k = 0; k < ride.comeTicks(); k++) {
                u16(ride.fromX() + (valeAt - ride.fromX()) * k / ride.comeTicks() - ox);
                u16(ride.fromX() + (jonesAt - ride.fromX()) * k / ride.comeTicks() - ox);
            }
            def("RIDE_SHIFT_PATH", bin.size());   // per tick of the gallop: how far they are
            for (int k = 0; k <= ride.rideTicks(); k++) {
                u16((ride.toX() - valeAt) * k / ride.rideTicks());
            }
        }

        // The intro (IntroScreen).
        h.append("\n// The intro (shell.json's attract.intro): its backdrop, the plane's way, its words.\n");
        backdrop("INTRO", intro.backdrop());
        ShellConfig.Plane p = intro.plane();
        int last = p.path().get(p.path().size() - 1).at();
        def("INTRO_LANDED", last);
        def("INTRO_FLY_EVERY", p.ticksPerFrame());
        def("INTRO_FLY_FRAMES", p.frames().size());
        def("INTRO_PLANE_PATH", bin.size());
        for (int t = 0; t <= last; t++) {
            int[] at = p.at(t);
            u16(at[0] - ox);
            u16(at[1] - oy);
        }
        def("INTRO_TEXT_AT", intro.textAt());
        def("INTRO_TEXT_Y", intro.textY() - oy);
        def("INTRO_ROW_STEP", intro.rowStepPx());
        def("INTRO_INK", colour(intro.textColour()));
        def("INTRO_TICKS_PER_CHAR", intro.ticksPerChar());
        def("INTRO_TICKS", intro.ticks());
        def("INTRO_ROWS", intro.text().size());
        def("INTRO_TEXT", bin.size());
        for (String row : intro.text()) {
            if (row.length() > BIG_CHARS) {
                throw new ConvertException("config/shell.json: the intro's \"" + row + "\" is wider than the screen");
            }
            raw(row);
        }

        // The tally (TallyScreen): its words, colours and layout.
        h.append("\n// The tally (shell.json's tally, TallyScreen).\n");
        ShellConfig.Tally t = s.tally();
        def("TALLY_TOP", privateInt(wulf.ui.TallyScreen.class, "TOP_PX"));
        def("TALLY_HALF", privateInt(wulf.ui.TallyScreen.class, "HALF_COLUMNS_PX"));
        // The prompt on the pages' footer line (Chrome.footer): TallyScreen's own place, from the
        // bottom of a canvas 44 lines taller, falls on its "new best" line here.
        def("TALLY_PROMPT_Y", SCREEN_H - 14);
        def("TALLY_LINE_EVERY", t.lineEveryTicks());
        def("TALLY_HEADING_INK", colour(t.headingColour()));
        def("TALLY_LABEL_INK", colour(t.labelColour()));
        def("TALLY_FIGURE_INK", colour(t.figureColour()));
        for (int i = 0; i < t.labels().size(); i++) {
            str("STR_TALLY_LABEL_" + i, t.labels().get(i));
        }
        def("TALLY_LABELS", t.labels().size());
        h.append("#define TALLY_LABEL_STRS\t");
        for (int i = 0; i < t.labels().size(); i++) {
            h.append(i == 0 ? "" : ", ").append("STR_TALLY_LABEL_").append(i);
        }
        h.append('\n');
        str("STR_TALLY_HEADING", t.heading());
        str("STR_TALLY_TOTAL", t.total());
        str("STR_TALLY_NEW_BEST", t.newBest());
        str("STR_TALLY_PROMPT", t.prompt());

        // The roll (CreditsRoll), with the MSX2's own credit after the game's lines.
        h.append("\n// The credits roll (shell.json's credits, CreditsRoll): its rows, a flag each, then the text.\n");
        ShellConfig.Credits cr = s.credits();
        ShellConfig.Roll roll = cr.roll();
        List<String> msx = msxCredits();
        rollRows.add(roll.theEnd());   // the game's; a later job's own in its place (STR_THE_END(j))
        rollRows.addAll(roll.before());
        rollRows.add(cr.heading());
        rollRows.add("");
        int bodyFrom = rollRows.size();
        rollRows.addAll(cr.lines());
        rollRows.addAll(msx);
        int bodyTo = rollRows.size();
        rollRows.addAll(roll.after());
        def("ROLL_ROWS", rollRows.size());
        def("ROLL_LINE", roll.lineHeightPx());
        def("ROLL_TICKS_PER_PX", roll.ticksPerPx());
        // The first row starts just under the screen, as the Java roll's under its canvas.
        def("ROLL_FROM_Y", roll.fromY() - (canvasH - SCREEN_H));
        def("ROLL_HEADING_INK", colour(roll.headingColour()));
        def("ROLL_BODY_INK", colour(roll.bodyColour()));
        def("ROLL_BAND_PAD", privateInt(cls("wulf.ui.CreditsRoll"), "BAND_PAD_PX"));
        def("ROLL_PX", roll.fromY() - (canvasH - SCREEN_H) + rollRows.size() * roll.lineHeightPx());
        str("STR_ROLL_HINT", roll.hint());
        str("STR_ROLL_PAUSED", roll.paused());
        def("ROLL_TEXT", bin.size());
        for (int i = 0; i < rollRows.size(); i++) {
            boolean body = i >= bodyFrom && i < bodyTo;
            String row = rollRows.get(i);
            int width = row.length() * (body ? 4 : 8);
            if (width > SCREEN_W) {
                throw new ConvertException("config/shell.json: the roll's \"" + row + "\" is " + width
                        + " pixels; the screen is " + SCREEN_W);
            }
            bin.write(body ? 1 : 0);
            raw(row);
        }
        if (bin.size() > 16384 || img.size() > 16384) {
            throw new ConvertException("scenes.bin or scenes_img.bin is more than a segment");
        }
    }

    /** The MSX2's own lines in the credits (AGENTS.md §2: MSXgl and its author). */
    static List<String> msxCredits() {
        try (var in = Scenes.class.getResourceAsStream("msx_credits.txt")) {
            List<String> out = new ArrayList<>();
            for (String line : new String(in.readAllBytes(), StandardCharsets.US_ASCII).split("\n", -1)) {
                if (!line.startsWith("#")) {
                    out.add(line.stripTrailing());
                }
            }
            while (!out.isEmpty() && out.get(out.size() - 1).isEmpty()) {
                out.remove(out.size() - 1);
            }
            return out;
        } catch (java.io.IOException | NullPointerException e) {
            throw new ConvertException("convert's msx_credits.txt: " + e);
        }
    }

    /** The roll's rows, as the ROM shows them (for the tests: the §2.5 text is among them). */
    public List<String> rollRows() {
        return List.copyOf(rollRows);
    }

    private SceneryAtlas.Slot atlasSlot(String sprite) {
        for (String id : c.scenery().ids()) {
            SceneryCatalog.Piece piece = c.scenery().piece(id);
            if (piece.sprite().equals(sprite)) {
                return atlas.slots().get(atlas.index(id));
            }
        }
        return null;
    }

    /**
     * A backdrop as copies (Sprite.blit at the piece's place): from the atlas (pass 0) or from the
     * figures (pass 1, after they are loaded), each clipped to the screen: sx, sline, w, h, dx, dy.
     */
    private void backdrop(String name, List<ShellConfig.Placed> placed) {
        for (int pass = 0; pass < 2; pass++) {
            ByteArrayOutputStream list = new ByteArrayOutputStream();
            int n = 0;
            for (ShellConfig.Placed pl : placed) {
                SceneryAtlas.Slot slot = atlasSlot(pl.sprite());
                int sx;
                int sy;
                int w;
                int hh;
                if (slot != null) {
                    if (pass != 0) {
                        continue;
                    }
                    sx = slot.x();
                    sy = ATLAS_FIRST + slot.y();
                    w = slot.w();
                    hh = slot.h();
                } else {
                    if (pass != 1) {
                        continue;
                    }
                    int[] im = images.get("BACK_" + Gen.cName(pl.sprite()));
                    if (im == null) {
                        throw new ConvertException("config/shell.json: backdrop piece " + pl.sprite() + " has no image");
                    }
                    sx = im[1];
                    sy = im[2];
                    w = im[3];
                    hh = im[4];
                }
                int dx = pl.x() - ox;
                int dy = pl.y() - oy;
                // Clipped to the screen.
                if (dx < 0) {
                    sx -= dx;
                    w += dx;
                    dx = 0;
                }
                if (dy < 0) {
                    sy -= dy;
                    hh += dy;
                    dy = 0;
                }
                w = Math.min(w, SCREEN_W - dx);
                hh = Math.min(hh, SCREEN_H - dy);
                if (w <= 0 || hh <= 0) {
                    continue;
                }
                list.write(sx);
                list.write(sy & 0xFF);
                list.write(sy >> 8);
                list.write(w);
                list.write(hh);
                list.write(dx);
                list.write(dy);
                n++;
            }
            def(name + "_BACK" + pass, bin.size());
            def(name + "_BACK" + pass + "_N", n);
            bin.writeBytes(list.toByteArray());
        }
    }

    /**
     * Typed lines (EndingScreen.paintLines): each at, y, ink, the speaker (1 Mr Jones), its rows;
     * a row wider than the screen in the big font broken at a word.
     */
    private void lines(String name, List<ShellConfig.Line> lines) {
        def(name + "_N", lines.size());
        def(name, bin.size());
        for (ShellConfig.Line l : lines) {
            List<String> rows = new ArrayList<>();
            for (String row : l.text()) {
                rows.addAll(wrap(row));
            }
            u16(l.at());
            bin.write(l.y() - oy);
            bin.write(colour(l.colour()));
            bin.write(ShellConfig.Line.JONES.equals(l.speaker()) ? 1 : 0);
            bin.write(rows.size());
            for (String row : rows) {
                raw(row);
            }
        }
    }

    static List<String> wrap(String row) {
        List<String> out = new ArrayList<>();
        String rest = row;
        while (rest.length() > BIG_CHARS) {
            int cut = rest.lastIndexOf(' ', BIG_CHARS);
            if (cut <= 0) {
                throw new ConvertException("config/shell.json: \"" + row + "\" has a word wider than the screen");
            }
            out.add(rest.substring(0, cut));
            rest = rest.substring(cut + 1);
        }
        out.add(rest);
        return out;
    }

    /** An image of the figures: placed on a shelf, its pixels to scenes_img.bin. */
    private void image(String name, byte[] px, SpriteData sd) {
        place(name, px, sd.size().w(), sd.size().h(), sd.origin().x(), sd.origin().y());
    }

    /** Sprite.blitHalf: every other pixel of every other row. */
    private void imageHalf(String name, byte[] px, int w, int hgt) {
        byte[] half = new byte[(w / 2) * (hgt / 2)];
        for (int y = 0; y < hgt / 2; y++) {
            for (int x = 0; x < w / 2; x++) {
                half[y * (w / 2) + x] = px[(2 * y) * w + 2 * x];
            }
        }
        place(name, half, w / 2, hgt / 2, 0, 0);
    }

    private void place(String name, byte[] px, int w, int hgt, int originX, int originY) {
        int bw = (w + 1) & ~1;
        if (imgX + bw > SCREEN_W) {
            imgY += imgShelf;
            imgX = 0;
            imgShelf = 0;
        }
        int line = imgLine0 + imgY;
        images.put(name, new int[] {images.size(), imgX, line, w, hgt, originX, originY});
        img.write(imgX);
        img.write((line - ATLAS_FIRST) & 0xFF);
        img.write((line - ATLAS_FIRST) >> 8);
        img.write(bw);
        img.write(hgt);
        for (int y = 0; y < hgt; y++) {
            for (int x = 0; x < bw; x += 2) {
                int a = x < w ? px[y * w + x] : -1;
                int b = x + 1 < w ? px[y * w + x + 1] : -1;
                img.write((a < 0 ? 0 : vdp.vdpColour(a)) << 4 | (b < 0 ? 0 : vdp.vdpColour(b)));
            }
        }
        imgX += bw;
        imgShelf = Math.max(imgShelf, hgt);
    }

    /** How many images are in scenes_img.bin. */
    public int imageCount() {
        return images.size();
    }

    private int colour(String name) {
        return vdp.vdpColour(c.palette().indexOf(name));
    }

    private void str(String name, String text) {
        def(name, bin.size());
        raw(text);
    }

    private void raw(String text) {
        for (char ch : text.toCharArray()) {
            if (ch < 32 || ch > 126) {
                throw new ConvertException("config/shell.json: \"" + text + "\" has '" + ch + "', which no font has");
            }
        }
        byte[] b = text.getBytes(StandardCharsets.US_ASCII);
        bin.write(b.length);
        bin.writeBytes(b);
    }

    private void u16(int v) {
        bin.write(v & 0xFF);
        bin.write((v >> 8) & 0xFF);
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

    private final Map<String, Long> defined = new java.util.HashMap<>();

    private long defined(String name) {
        Long v = defined.get(name);
        if (v == null) {
            throw new ConvertException("Scenes: " + name + " is not defined yet");
        }
        return v;
    }

    private void macro(String name, int[] values) {
        h.append("#define ").append(name).append('\t').append(Shell.chain("j", values)).append('\n');
    }

    private void def(String name, long v) {
        defined.put(name, v);
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
}
