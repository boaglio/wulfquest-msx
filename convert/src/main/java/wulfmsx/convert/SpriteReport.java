package wulfmsx.convert;

import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import javax.imageio.ImageIO;
import wulf.Content;
import wulf.data.SpriteData;

/**
 * Every pixel the sprites show in another colour than the art's (AGENTS.md
 * §10, M5), sprite by sprite, against what sprites.json accepts for each.
 */
final class SpriteReport {

    private static final int SCALE = 4;

    private final Content c;
    private final VdpPalette p;
    private final SpriteCut.Sprites list;
    private final Map<String, List<SpriteCut.Change>> bySprite = new LinkedHashMap<>();
    private final List<String> bladeDropped;

    SpriteReport(Content c, VdpPalette p, PlayerSprite vale, RomSprites bank, Blade blade) {
        this.c = c;
        this.p = p;
        this.list = SpriteCut.sprites(c);
        for (String name : list.all(c)) {
            bySprite.put(name, new ArrayList<>());
        }
        for (SpriteCut.Change ch : vale.changes()) {
            bySprite.get(ch.sprite()).add(ch);
        }
        for (SpriteCut.Change ch : bank.changes()) {
            bySprite.get(ch.sprite()).add(ch);
        }
        bank.later().forEach((name, ch) -> bySprite.get(name).addAll(ch));
        for (SpriteCut.Change ch : vale.inkedChanges()) {
            bySprite.computeIfAbsent(ch.sprite(), k -> new ArrayList<>()).add(ch);
        }
        this.bladeDropped = blade.dropped();
    }

    /** sprites.txt: every changed pixel. */
    String listing() {
        StringBuilder sb = new StringBuilder();
        sb.append("Pixels the MSX2's sprites show in another colour than the art's (convert/.../SpriteCut.java).\n");
        sb.append("sprite frame x,y: the art's colour -> the colour shown (its number)\n\n");
        bySprite.forEach((name, changes) -> {
            sb.append(name).append(": ").append(changes.size()).append(" pixels").append(accepted(name)).append('\n');
            for (SpriteCut.Change ch : changes) {
                sb.append(String.format("  %s %d,%d: %s -> %s (%d)%n", ch.frame(), ch.x(), ch.y(), p.name(ch.from()),
                        p.name(p.paletteIndex(ch.to())), ch.to()));
            }
        });
        sb.append("\nThe blade: pixels past a sprite's 16x16, left out: ").append(bladeDropped.size()).append('\n');
        for (String d : bladeDropped) {
            sb.append("  ").append(d).append('\n');
        }
        return sb.toString();
    }

    private String accepted(String name) {
        Integer a = list.accepted().get(name);
        return a == null ? " (none accepted yet)" : " (accepted " + a + ")";
    }

    /** For report.txt: the counts, sprite by sprite. */
    String summary() {
        StringBuilder sb = new StringBuilder("Sprite pixels in another colour (build/gen/sprites.txt, sprites.png):");
        int total = 0;
        for (Map.Entry<String, List<SpriteCut.Change>> e : bySprite.entrySet()) {
            sb.append(' ').append(e.getKey()).append(' ').append(e.getValue().size()).append(';');
            total += e.getValue().size();
        }
        sb.append(" ").append(total).append(" in all; the blade leaves out ").append(bladeDropped.size()).append('\n');
        return sb.toString();
    }

    /** Fails on a sprite changing more pixels than sprites.json accepts. */
    void check() {
        List<String> over = new ArrayList<>();
        bySprite.forEach((name, changes) -> {
            Integer a = list.accepted().get(name);
            if (a == null || changes.size() > a) {
                over.add(name + " " + changes.size() + (a == null ? " (none accepted)" : " (accepted " + a + ")"));
            }
        });
        if (!over.isEmpty()) {
            throw new ConvertException("sprites show more pixels in another colour than accepted: "
                    + String.join(", ", over) + "; see build/gen/sprites.txt and sprites.png, and accept them"
                    + " in convert/src/main/resources" + SpriteCut.LIST + " if they look right");
        }
    }

    /** sprites.png: each frame with a change, the art and the MSX2's, side by side, the changes marked below. */
    byte[] picture() {
        List<int[]> tiles = new ArrayList<>();   // {w, h, art..., msx...} as colour numbers
        for (Map.Entry<String, List<SpriteCut.Change>> e : bySprite.entrySet()) {
            SpriteData s = c.sprites().get(e.getKey());
            Map<String, byte[]> frames = s.decode(e.getKey());
            Map<String, List<SpriteCut.Change>> byFrame = new LinkedHashMap<>();
            for (SpriteCut.Change ch : e.getValue()) {
                byFrame.computeIfAbsent(ch.frame(), k -> new ArrayList<>()).add(ch);
            }
            byFrame.forEach((frame, changes) -> {
                int w = s.size().w();
                int h = s.size().h();
                int[] t = new int[2 + 2 * w * h];
                t[0] = w;
                t[1] = h;
                byte[] px = frames.get(frame);
                for (int i = 0; i < w * h; i++) {
                    t[2 + i] = p.vdpColour(px[i]);
                    t[2 + w * h + i] = t[2 + i];
                }
                for (SpriteCut.Change ch : changes) {
                    t[2 + w * h + ch.y() * w + ch.x()] = ch.to();
                }
                tiles.add(t);
            });
        }
        int cell = 2 * 32 + 4;
        int perRow = 12;
        int rows = Math.max(1, (tiles.size() + perRow - 1) / perRow);
        BufferedImage img = new BufferedImage(perRow * cell * SCALE, rows * 34 * SCALE, BufferedImage.TYPE_INT_RGB);
        int grey = 0x404040;
        for (int y = 0; y < img.getHeight(); y++) {
            for (int x = 0; x < img.getWidth(); x++) {
                img.setRGB(x, y, grey);
            }
        }
        for (int n = 0; n < tiles.size(); n++) {
            int[] t = tiles.get(n);
            int w = t[0];
            int h = t[1];
            int ox = (n % perRow) * cell;
            int oy = (n / perRow) * 34;
            for (int half = 0; half < 2; half++) {
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        int v = t[2 + half * w * h + y * w + x];
                        int rgb = v == VdpPalette.TRANSPARENT ? grey : eight(p.rgb3(v));
                        boolean changed = half == 1 && v != t[2 + y * w + x];
                        for (int j = 0; j < SCALE; j++) {
                            for (int i = 0; i < SCALE; i++) {
                                boolean edge = changed && (i == 0 || j == 0);
                                img.setRGB((ox + half * (w + 2) + x) * SCALE + i, (oy + y) * SCALE + j,
                                        edge ? 0xFF00FF : rgb);
                            }
                        }
                    }
                }
            }
        }
        ImageIO.setUseCache(false);   // in memory: no cache file in the system's temporary folder
        try (ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            ImageIO.write(img, "png", out);
            return out.toByteArray();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private static int eight(int rgb3) {
        return VdpPalette.eightBits(rgb3 >> 16) << 16 | VdpPalette.eightBits(rgb3 >> 8 & 0xFF) << 8
                | VdpPalette.eightBits(rgb3 & 0xFF);
    }
}
