package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Map;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.SpriteData;
import wulf.render.Framebuffer;
import wulf.render.Sprite;

/** M8: Ink inks a frame exactly as the Java game's Sprite.blitInked draws it. */
class InkTest {

    @Test
    void inksAsTheJavaGameDoes() {
        Content c = Content.load(ConvertTest.DATA);
        int keep = c.palette().indexOf("black");
        for (String name : new String[] {c.player().sprite(), "creature_tribesman"}) {
            SpriteData data = c.sprites().get(name);
            Sprite sprite = new Sprite(data, name);
            int w = data.size().w();
            int h = data.size().h();
            for (String colour : new String[] {"brightYellow", "brightRed", "brightCyan", "brightMagenta"}) {
                int bright = c.palette().indexOf(colour);
                for (Map.Entry<String, byte[]> f : data.decode(name).entrySet()) {
                    byte[] mine = Ink.ink(f.getValue(), w, bright - 8, bright, keep);
                    Framebuffer fb = new Framebuffer(w, h);
                    fb.clear(99);   // no palette index: where nothing is drawn
                    sprite.blitInked(fb, f.getKey(), data.origin().x(), data.origin().y(), bright - 8, bright, keep);
                    for (int i = 0; i < mine.length; i++) {
                        int want = mine[i] < 0 ? 99 : mine[i];
                        assertThat(fb.get(i % w, i / w)).as(name + " " + f.getKey() + " " + colour + " at " + i)
                                .isEqualTo(want);
                    }
                }
            }
        }
    }
}
