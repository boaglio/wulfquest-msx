package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import java.util.TreeMap;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.audio.BeeperSynth;
import wulf.data.MusicData;
import wulf.data.SfxData;

/**
 * M9: every effect and tune plays on the PSG at the pitch and for the length the Java game's
 * own synth gives it: each frame's tone against the synth's samples in that frame (its pitch
 * from the square's edges), and the whole against the synth's length, to a frame.
 */
class SoundTest {

    static final Content C = Content.load(ConvertTest.DATA);
    static final BeeperSynth SYNTH = new BeeperSynth(44_100);

    @Test
    void everyEffectHasTheJavaGamesPitchAndLength() {
        for (var e : new TreeMap<>(C.sfx().sfx()).entrySet()) {
            short[] samples = SYNTH.render(e.getValue());
            for (int rate : Sound.RATES) {
                List<int[]> frames = Sound.effect(e.getValue(), rate);
                int perFrame = 44_100 / rate;
                assertThat(Math.abs(frames.size() - samples.length / (double) perFrame)).as(e.getKey() + " length")
                        .isLessThanOrEqualTo(1);
                for (int f = 0; f < frames.size(); f++) {
                    int[] fr = frames.get(f);
                    if (fr[1] != 0) {
                        continue;   // noise: its shift rate, not a pitch
                    }
                    // The frame's middle half: the table takes the pitch at its middle.
                    int mid = f * perFrame + perFrame / 2;
                    int lo = Math.max(0, mid - perFrame / 4);
                    int hi = Math.min(samples.length, mid + perFrame / 4);
                    if (stepEdgeIn(e.getValue(), lo, hi)) {
                        continue;   // two steps' pitches in the window: neither can be read
                    }
                    double want = pitch(samples, lo, hi);
                    if (want <= 0) {
                        continue;
                    }
                    double got = Sound.PSG_HZ / (16.0 * fr[0]);
                    assertThat(Math.abs(got / want - 1)).as(e.getKey() + " frame " + f + " at " + rate + " Hz: "
                            + got + " Hz, the synth " + want).isLessThan(0.12);
                }
            }
        }
    }

    @Test
    void everyTuneHasTheJavaGamesNotesAndLength() {
        for (var t : new TreeMap<>(C.music().tunes()).entrySet()) {
            int ms = 0;
            for (MusicData.Note n : t.getValue().notes()) {
                ms += n.ms();
            }
            for (int rate : Sound.RATES) {
                List<int[]> entries = Sound.tune(t.getValue(), rate);
                int frames = entries.stream().mapToInt(e -> e[3]).sum();
                assertThat(Math.abs(frames - ms * rate / 1000.0)).as(t.getKey() + " length").isLessThanOrEqualTo(1);
                for (int[] e : entries) {
                    if (e[2] == 0) {
                        continue;
                    }
                    double got = Sound.PSG_HZ / (16.0 * e[0]);
                    boolean near = t.getValue().notes().stream().filter(n -> !n.rest())
                            .anyMatch(n -> Math.abs(got / BeeperSynth.hzOf(n) - 1) < 0.03);
                    assertThat(near).as(t.getKey() + ": " + got + " Hz is one of its notes").isTrue();
                }
            }
        }
    }

    /** Whether a step of the effect ends within samples lo..hi. */
    static boolean stepEdgeIn(SfxData.Effect e, int lo, int hi) {
        int at = 0;
        for (SfxData.Step st : e.steps()) {
            at += SYNTH.samplesFor(st.ms());
            if (at > lo && at < hi) {
                return true;
            }
        }
        return false;
    }

    /** A square's pitch in a window: its edges over the window's time; 0 where it is silent. */
    static double pitch(short[] s, int from, int to) {
        int edges = 0;
        int last = 0;
        int first = -1;
        int lastEdge = -1;
        for (int i = from; i < to; i++) {
            int sign = Integer.signum(s[i]);
            if (sign != 0 && last != 0 && sign != last) {
                edges++;
                if (first < 0) {
                    first = i;
                }
                lastEdge = i;
            }
            if (sign != 0) {
                last = sign;
            }
        }
        if (edges < 2) {
            return 0;
        }
        return (edges - 1) / 2.0 * 44_100 / (lastEdge - first);
    }
}
