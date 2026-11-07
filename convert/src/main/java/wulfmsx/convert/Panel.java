package wulfmsx.convert;

import java.io.ByteArrayOutputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import wulf.Content;
import wulf.data.FontData;
import wulf.data.LandmarksData;
import wulf.data.SpriteData;
import wulf.render.PanelPainter;
import wulf.sim.Direction8;
import wulf.ui.GameSession;

/**
 * The panel under the playfield as tables for the ROM (AGENTS.md §10, M9; the
 * Java game's PanelPainter and GameSession.message): the fonts, the strings
 * it shows, the amulet's quarters at half size and a life's head, drawn in
 * SCREEN 5's bytes, and its colours.
 *
 * <p>{@code font.bin}: the big font's glyphs for ASCII 32-95, 8 bytes each
 * (a byte a line, bit 7 the left pixel), then the small font's for 32-126, 6
 * bytes each (bits 7-4). {@code panel.bin}: the images, 32 bytes each (8x8,
 * 4 bytes a line, two pixels a byte); then the strings, each a length byte
 * and its characters. {@code panel_data.h} says where each is.
 */
public final class Panel {

    static final int BIG_FIRST = 32;
    static final int BIG_GLYPHS = 64;
    static final int SMALL_FIRST = 32;
    static final int SMALL_GLYPHS = 95;

    private final StringBuilder h = new StringBuilder();
    private final byte[] font;
    private final byte[] panel;

    public Panel(Content c, VdpPalette vdp) {
        FontData.Set big = c.font().sets().get("big");
        FontData.Set small = c.font().sets().get("small");
        if (big.size().w() != 8 || big.size().h() != 8 || small.size().w() != 4 || small.size().h() > 8) {
            throw new ConvertException("data/art/font/font.json: the panel's fonts must be 8x8 and 4 wide");
        }
        ByteArrayOutputStream f = new ByteArrayOutputStream();
        glyphs(f, big, BIG_FIRST, BIG_GLYPHS, 8);
        glyphs(f, small, SMALL_FIRST, SMALL_GLYPHS, small.size().h());
        font = f.toByteArray();

        h.append(Gen.banner("data/art/font/font.json, config/shell.json, PanelPainter, GameSession"));
        h.append("#pragma once\n\n");
        h.append("// font.bin: the big font (8x8), then the small (4 wide).\n");
        def("FONT_BIG_FIRST", BIG_FIRST);
        def("FONT_BIG_GLYPHS", BIG_GLYPHS);
        def("FONT_SMALL_FIRST", SMALL_FIRST);
        def("FONT_SMALL_GLYPHS", SMALL_GLYPHS);
        def("FONT_SMALL_AT", BIG_GLYPHS * 8);
        def("FONT_SMALL_H", small.size().h());

        // The colours (PanelPainter's, GamePainter's border).
        int white = vdp.vdpColour(c.palette().indexOf("white"));
        int bright = vdp.vdpColour(c.palette().indexOf("brightWhite"));
        int black = vdp.vdpColour(c.palette().indexOf("black"));
        h.append("\n// Colours: PanelPainter's inks and ground, GamePainter's border flashes.\n");
        def("PANEL_INK", white);
        def("PANEL_BRIGHT", bright);
        def("PANEL_DIM", white);
        def("PANEL_GROUND", black);
        def("PANEL_EASY_INK", vdp.vdpColour(c.palette().indexOf(c.shell().difficulty().easyColour())));
        def("BORDER_IDLE", vdp.vdpColour(c.palette().indexOf(c.display().border().idleColour())));
        def("BORDER_FLASHES", c.display().border().flashOnEvent() ? 1 : 0);
        def("BORDER_ALARM", vdp.vdpColour(c.palette().indexOf("brightRed")));
        def("BORDER_PICKUP", bright);
        // Simulation.borderFlash's alarm, literals in its code: the first ticks of a fall, flashing.
        def("BORDER_ALARM_TICKS", 16);
        def("BORDER_ALARM_PERIOD", 4);
        def("BORDER_ALARM_ON", 2);
        def("SCORE_DIGITS", privateInt(PanelPainter.class, "SCORE_DIGITS"));
        // Literals inside PanelPainter's methods, as it has them: the flower's bar, the heads shown.
        def("EFFECT_BAR_W", 160);
        def("LIVES_SHOWN", 5);
        h.append("// The flower's bar in its colour (GamePainter.effectColours): bright, by orchid.\n");
        h.append("#define ORCHID_INKS\t");
        for (int i = 0; i < c.orchids().orchids().size(); i++) {
            int ink = vdp.vdpColour(c.palette().indexOf(
                    wulf.data.OrchidValidator.bright(c.orchids().orchids().get(i).colour())));
            h.append(i == 0 ? "" : ", ").append(ink);
        }
        h.append('\n');

        // Images: the amulet's quarters, held and not (Sprite.blitHalf), and a life's head.
        ByteArrayOutputStream img = new ByteArrayOutputStream();
        LandmarksData marks = c.landmarks();
        SpriteData amulet = c.sprites().get(marks.amulet().sprite());
        Map<String, byte[]> frames = amulet.decode(marks.amulet().sprite());
        List<String> bySlot = wulf.render.QuestPainter.framesBySlot(marks);
        for (int dim = 0; dim < 2; dim++) {
            for (int slot = 0; slot < bySlot.size(); slot++) {
                byte[] px = frames.get(bySlot.get(slot));
                int w = amulet.size().w();
                int[] out = new int[64];
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        byte v = px[(2 * y) * w + 2 * x];
                        out[y * 8 + x] = v < 0 ? black : vdp.vdpColour(dim == 1 ? c.palette().indexOf("white") : v);
                    }
                }
                image(img, out);
            }
        }
        // A head: 4x5 of bright ink, two dots of ground in its second line (PanelPainter.drawLives).
        int[] head = new int[64];
        java.util.Arrays.fill(head, black);
        for (int y = 0; y < 5; y++) {
            for (int x = 0; x < 4; x++) {
                head[y * 8 + x] = (y == 1 && (x == 1 || x == 2)) ? black : bright;
            }
        }
        image(img, head);
        h.append("\n// panel.bin: 8x8 images, 32 bytes each: the quarters by slot, held then dim, then a head.\n");
        def("IMAGE_QUARTER", 0);
        def("IMAGE_QUARTER_DIM", bySlot.size());
        def("IMAGE_HEAD", 2 * bySlot.size());
        def("IMAGE_BYTES", 32);
        def("PANEL_STRINGS_AT", img.size());

        // Strings: PanelPainter's labels, GameSession's messages.
        List<String> strings = new ArrayList<>();
        strings.add("SCORE ");
        strings.add("HI ");
        strings.add("LIVES");
        strings.add("X");
        strings.add("PAUSED");
        strings.add(c.shell().difficulty().panelLabel());
        strings.add(c.shell().gameOver().text());   // until M10's screens: on the panel's line
        strings.add(c.shell().tally().heading());
        List<String> names = new ArrayList<>(List.of("STR_SCORE", "STR_HI", "STR_LIVES", "STR_TIMES", "STR_PAUSED",
                "STR_EASY", "STR_GAME_OVER", "STR_WON"));
        for (int toExit = 0; toExit < 2; toExit++) {
            for (Direction8 d : Direction8.values()) {
                strings.add(hint(d, toExit == 1));
                names.add("STR_HINT_" + (toExit == 1 ? "EXIT_" : "") + d.name());
            }
        }
        h.append("\n// The strings in panel.bin, after the images: each a length and its characters.\n");
        ByteArrayOutputStream str = new ByteArrayOutputStream();
        int at = img.size();
        for (int i = 0; i < strings.size(); i++) {
            byte[] b = strings.get(i).getBytes(StandardCharsets.US_ASCII);
            def(names.get(i), at);
            str.write(b.length);
            str.writeBytes(b);
            at += 1 + b.length;
        }
        h.append("// The hints by Direction8's order, to a quarter, then to the arch (GameSession.hint).\n");
        h.append("#define STR_HINTS\t").append(String.join(", ", names.subList(8, names.size()))).append('\n');
        img.writeBytes(str.toByteArray());
        panel = img.toByteArray();
    }

    /** GameSession.hint, as the Java game words it, for a heading and whether it is to the arch. */
    private static String hint(Direction8 d, boolean toExit) {
        try {
            Constructor<wulf.sim.Quest> k = wulf.sim.Quest.class.getDeclaredConstructor(wulf.sim.Creature.class,
                    wulf.sim.Creature.class);
            k.setAccessible(true);
            wulf.sim.Quest q = k.newInstance(null, null);
            Field dir = wulf.sim.Quest.class.getDeclaredField("hintDirection");
            dir.setAccessible(true);
            dir.set(q, d);
            Field exit = wulf.sim.Quest.class.getDeclaredField("hintToExit");
            exit.setAccessible(true);
            exit.setBoolean(q, toExit);
            return GameSession.hint(q);
        } catch (ReflectiveOperationException e) {
            throw new ConvertException("wulfquest's Quest or GameSession.hint changed: " + e);
        }
    }

    private static void glyphs(ByteArrayOutputStream out, FontData.Set set, int first, int count, int lines) {
        for (int i = 0; i < count; i++) {
            String ch = String.valueOf((char) (first + i));
            List<String> rows = set.glyphs().get(ch);
            for (int y = 0; y < lines; y++) {
                int b = 0;
                if (rows != null && y < rows.size()) {
                    String row = rows.get(y);
                    for (int x = 0; x < row.length() && x < 8; x++) {
                        Integer v = set.legend().get(String.valueOf(row.charAt(x)));
                        if (v != null && v >= 0) {
                            b |= 0x80 >> x;
                        }
                    }
                }
                out.write(b);
            }
        }
    }

    private static void image(ByteArrayOutputStream out, int[] px) {
        for (int i = 0; i < 64; i += 2) {
            out.write(px[i] << 4 | px[i + 1]);
        }
    }

    private static int privateInt(Class<?> c, String name) {
        try {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            return f.getInt(null);
        } catch (ReflectiveOperationException e) {
            throw new ConvertException(c.getName() + "." + name + ": the Java game does not have it");
        }
    }

    private void def(String name, long v) {
        h.append("#define ").append(name).append('\t').append(v).append('\n');
    }

    public String header() {
        return h.toString();
    }

    public byte[] font() {
        return font.clone();
    }

    public byte[] panel() {
        return panel.clone();
    }
}
