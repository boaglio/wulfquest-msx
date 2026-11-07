package wulfmsx.convert;

import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;
import wulf.Content;
import wulf.audio.BeeperSynth;
import wulf.data.MusicData;
import wulf.data.SfxData;

/**
 * The game's sound on the PSG (AGENTS.md §10, M9): the effects and the tunes
 * of {@code data/audio/}, as the PSG plays them, a frame of the interrupt at a
 * time (50 or 60 a second).
 *
 * <p>Each sound is a list of entries, 4 bytes each: the tone period's low
 * byte (a noise step: the noise period), its high nibble with bit 7 set for
 * noise, the volume (0-15), and for how many frames it holds. An effect's
 * steps go a frame at a time, following their slide and vibrato as the Java
 * game's {@link BeeperSynth} does; a tune's notes are an entry each, their
 * lengths rounded so the tune never drifts (a note ends at the frame its time
 * says). {@code sound50.bin} and {@code sound60.bin}: an index (offset, count,
 * 4 bytes a sound), then the entries. {@code sound_data.h}: each sound's
 * number, priority and loop, by name.
 */
public final class Sound {

    /** The MSX's PSG clock: 3 579 545 / 2. */
    static final double PSG_HZ = 1_789_772.5;
    static final int[] RATES = {50, 60};

    private final StringBuilder h = new StringBuilder();
    private final List<String> names = new ArrayList<>();
    private final List<byte[]> blobs = new ArrayList<>();
    /** By rate, by sound: its entries (period, noise, volume, frames). */
    final List<List<List<int[]>>> entries = new ArrayList<>();

    public Sound(Content c) {
        TreeMap<String, SfxData.Effect> sfx = new TreeMap<>(c.sfx().sfx());
        TreeMap<String, MusicData.Tune> tunes = new TreeMap<>(c.music().tunes());
        h.append(Gen.banner("data/audio/sfx.json and music.json"));
        h.append("#pragma once\n\n");
        h.append("// The sounds, effects then tunes, numbered as in sound50.bin and sound60.bin.\n");
        int n = 0;
        StringBuilder pri = new StringBuilder();
        StringBuilder loop = new StringBuilder();
        for (var e : sfx.entrySet()) {
            def("SFX_" + e.getKey().toUpperCase(), n++);
            names.add(e.getKey());
            pri.append(pri.length() == 0 ? "" : ", ").append(e.getValue().priority());
            loop.append(loop.length() == 0 ? "" : ", ").append(e.getValue().loop() ? 1 : 0);
        }
        def("SFX_COUNT", sfx.size());
        for (var t : tunes.entrySet()) {
            def("TUNE_" + t.getKey().toUpperCase(), n++);
            names.add("tune:" + t.getKey());
            loop.append(", ").append(t.getValue().loop() ? 1 : 0);
        }
        def("SOUNDS", n);
        def("SOUND_VOICES", c.sfx().channels());
        h.append("// Each effect's priority (Mixer: a busy voice is taken by a more important one), each\n");
        h.append("// sound's loop.\n");
        h.append("#define SFX_PRIORITIES\t").append(pri).append('\n');
        h.append("#define SOUND_LOOPS\t").append(loop).append('\n');
        h.append("// The footstep's cadence (Simulation.move): every so many ticks walked, faster in haste.\n");
        def("FOOTSTEP_EVERY_TICKS", c.sfx().cadence().footstepEveryTicks());
        def("FOOTSTEP_HASTE_STEP", c.sfx().cadence().hasteFootstepStep());
        def("SOUND_ENTRY_BYTES", 4);
        if (c.sfx().channels() != 2) {
            throw new ConvertException("data/audio/sfx.json: " + c.sfx().channels() + " channels; the PSG has three, "
                    + "the ROM gives effects two and the tune the third");
        }

        for (int rate : RATES) {
            List<List<int[]>> byRate = new ArrayList<>();
            for (SfxData.Effect e : sfx.values()) {
                byRate.add(effect(e, rate));
            }
            for (MusicData.Tune t : tunes.values()) {
                byRate.add(tune(t, rate));
            }
            entries.add(byRate);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            int at = 4 * byRate.size();
            for (List<int[]> s : byRate) {
                out.write(at & 0xFF);
                out.write(at >> 8);
                out.write(s.size() & 0xFF);
                out.write(s.size() >> 8);
                at += 4 * s.size();
            }
            for (List<int[]> s : byRate) {
                for (int[] e : s) {
                    out.write(e[0] & 0xFF);
                    out.write(e[0] >> 8 & 0x0F | (e[1] != 0 ? 0x80 : 0));
                    out.write(e[2]);
                    out.write(e[3]);
                }
            }
            if (out.size() > 16384) {
                throw new ConvertException("the sounds at " + rate + " Hz take " + out.size() + " bytes, more than a segment");
            }
            blobs.add(out.toByteArray());
        }
    }

    /** An effect, a frame an entry: at each frame's middle, the step's pitch, shape and volume. */
    static List<int[]> effect(SfxData.Effect e, int rate) {
        List<int[]> out = new ArrayList<>();
        double frameMs = 1000.0 / rate;
        int total = 0;
        for (SfxData.Step s : e.steps()) {
            total += s.ms();
        }
        int frames = Math.max(1, (int) Math.round(total / frameMs));
        for (int f = 0; f < frames; f++) {
            double t = Math.min((f + 0.5) * frameMs, total - 0.001);   // a last frame past the end: its end
            double start = 0;
            SfxData.Step step = e.steps().get(e.steps().size() - 1);
            for (SfxData.Step s : e.steps()) {
                if (t < start + s.ms()) {
                    step = s;
                    break;
                }
                start += s.ms();
            }
            double into = Math.min(1, Math.max(0, (t - start) / step.ms()));
            double hz = step.hz();
            if (step.slides()) {
                hz = step.hz() + (step.slideToHz() - step.hz()) * into;
            }
            if (step.vibrates()) {
                double bend = Math.sin(2 * Math.PI * step.vibratoHz() * (t - start) / 1000.0);
                hz *= 1 + bend * step.vibratoDepthPerMille() / 1000.0;
            }
            boolean noise = SfxData.Step.NOISE.equals(step.shape());
            out.add(new int[] {noise ? noisePeriod(hz) : tonePeriod(hz), noise ? 1 : 0, volume(step.volPerMille()), 1});
        }
        return out;
    }

    /** A tune, a note an entry: its period at the synth's volume, a rest silent. */
    static List<int[]> tune(MusicData.Tune t, int rate) {
        List<int[]> out = new ArrayList<>();
        double frameMs = 1000.0 / rate;
        long ms = 0;
        for (MusicData.Note n : t.notes()) {
            int from = (int) Math.round(ms / frameMs);
            ms += n.ms();
            int frames = (int) Math.round(ms / frameMs) - from;
            if (frames <= 0) {
                continue;
            }
            int period = n.rest() ? 0 : tonePeriod(BeeperSynth.hzOf(n));
            int vol = n.rest() ? 0 : volume(TUNE_VOL_PER_MILLE);
            while (frames > 0) {
                int k = Math.min(255, frames);
                out.add(new int[] {period, 0, vol, k});
                frames -= k;
            }
        }
        return out;
    }

    /** BeeperSynth.render(Tune): the square's volume, a literal in its code. */
    static final int TUNE_VOL_PER_MILLE = 420;

    static int tonePeriod(double hz) {
        return (int) Math.max(1, Math.min(4095, Math.round(PSG_HZ / (16 * Math.max(1, hz)))));
    }

    /** The noise's shift rate (BeeperSynth's LFSR clock) as the PSG's noise period, 1-31. */
    static int noisePeriod(double hz) {
        return (int) Math.max(1, Math.min(31, Math.round(PSG_HZ / (16 * Math.max(1, hz)))));
    }

    /** A loudness in thousandths as the PSG's volume: 15 full, 3 dB a step down. */
    static int volume(int perMille) {
        if (perMille <= 0) {
            return 0;
        }
        double db = 20 * Math.log10(perMille / 1000.0);
        return (int) Math.max(0, Math.min(15, Math.round(15 + db / 3)));
    }

    private void def(String name, long v) {
        h.append("#define ").append(name).append('\t').append(v).append('\n');
    }

    public String header() {
        return h.toString();
    }

    public List<String> names() {
        return names;
    }

    /** sound50.bin, sound60.bin. */
    public List<byte[]> blobs() {
        return blobs;
    }
}
