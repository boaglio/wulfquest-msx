package wulfmsx.convert;

import java.util.List;
import wulf.Content;
import wulf.data.CreatureData;
import wulf.engine.Fixed;
import wulf.sim.Beast;
import wulf.sim.BeastRules;
import wulf.sim.Simulation;
import wulf.sim.SwarmRules;
import wulf.world.RoomAddress;

/**
 * The original's jungle as tables for the ROM (AGENTS.md §10, M6; the Java
 * game's §12.8, §12.9): the swarm's roster and how it comes, the big beasts'
 * kinds, where a new game sets them, and each room's spot. Read the way the
 * Java game reads it: {@code Content.originalCreatures}, {@code swarmRules},
 * {@code beastRules}.
 *
 * <p>{@code jungle_data.h}: the numbers as defines; the tables (with JUNGLE_TABLES
 * defined) as arrays.
 */
public final class Jungle {

    /** The small creatures' behaviours the ROM has, by their data names; the order is the ROM's. */
    static final List<String> BEHAVIOURS = List.of("DART", "HOME_IN");
    static final List<String> DART = List.of("speedMinFp", "speedStepFp", "speedSteps", "runTicks", "pauseMinTicks",
            "pauseSpanTicks");
    static final List<String> HOME_IN = List.of("accelFp", "maxFp");
    /** The beasts' parameters the ROM reads, each kind's that it has, 0 for the others. */
    static final List<String> BEAST_PARAMS = List.of("paceFp", "shovedFp", "walkSlowFp", "walkFastFp", "chargeFp",
            "wakeTicks");

    private final StringBuilder h = new StringBuilder();
    private final RamTables tables;

    public Jungle(Content c, VdpPalette vdp, RomSprites bank, RamTables tables) {
        this.tables = tables;
        CreatureData roster = c.originalCreatures();
        SwarmRules swarm = c.swarmRules();
        BeastRules beasts = c.beastRules();
        List<CreatureData.Species> small = roster.creatures();

        h.append(Gen.banner("data/entities/jungle.json and creatures.json"));
        h.append("#pragma once\n\n");
        h.append("// The swarm's roster (Content.originalCreatures), in its order.\n");
        h.append("#define CREATURES\t\t").append(small.size()).append('\n');
        for (int i = 0; i < BEHAVIOURS.size(); i++) {
            h.append("#define BEHAVIOUR_").append(BEHAVIOURS.get(i)).append('\t').append(i).append('\n');
        }
        h.append("#define SWARM_PICKS\t\t").append(swarm.pick().length).append('\n');
        h.append("#define SWARM_TINTS\t\t").append(swarm.tints().length).append('\n');
        h.append("\n// The big beasts' kinds (BeastRules.kinds), their placings, the sleepers.\n");
        h.append("#define BEAST_KINDS\t\t").append(beasts.kinds().size()).append('\n');
        h.append("#define BEASTS\t\t\t").append(beasts.placings().size()).append('\n');
        h.append("#define SLEEPER_KINDS\t\t").append(beasts.sleepers().length).append('\n');
        for (Beast.Kind k : Beast.Kind.values()) {
            h.append("#define BEAST_").append(k.name()).append('\t').append(k.ordinal()).append('\n');
        }
        h.append("\n// The tables are in tables.h.\n");

        // The roster.
        int[] bx = new int[small.size()];
        int[] by = new int[small.size()];
        int[] bw = new int[small.size()];
        int[] bh = new int[small.size()];
        int[] hp = new int[small.size()];
        int[] kind = new int[small.size()];
        int[] flier = new int[small.size()];
        int[][] params = new int[DART.size()][small.size()];
        int[] frames = new int[small.size() * 4];
        for (int i = 0; i < small.size(); i++) {
            CreatureData.Species s = small.get(i);
            String where = "data/entities/jungle.json: creature '" + s.id() + "'";
            bx[i] = s.collisionBox().x();
            by[i] = s.collisionBox().y();
            bw[i] = s.collisionBox().w();
            bh[i] = s.collisionBox().h();
            hp[i] = s.hp();
            flier[i] = s.ignoresScenery() ? 1 : 0;
            kind[i] = BEHAVIOURS.indexOf(s.behaviour().kind());
            if (kind[i] < 0) {
                throw new ConvertException(where + " behaves as " + s.behaviour().kind() + "; the ROM has "
                        + BEHAVIOURS);
            }
            List<String> names = kind[i] == 0 ? DART : HOME_IN;
            for (int p = 0; p < names.size(); p++) {
                params[p][i] = s.behaviour().param(names.get(p));
            }
            String[] f = {"walk0", "walk1", "walk0_l", "walk1_l"};
            for (int k = 0; k < 4; k++) {
                frames[i * 4 + k] = bank.index(s.sprite(), f[k]);
            }
            if (bw[i] > 255 || bh[i] > 255) {
                throw new ConvertException(where + ": its box is too big for the ROM");
            }
        }
        array("signed char", "g_CreatureBoxX", bx);
        array("signed char", "g_CreatureBoxY", by);
        array("unsigned char", "g_CreatureBoxW", bw);
        array("unsigned char", "g_CreatureBoxH", bh);
        array("unsigned char", "g_CreatureHp", hp);
        array("unsigned char", "g_CreatureBehaviour", kind);
        array("unsigned char", "g_CreatureFlier", flier);
        note = "DART: speedMinFp, speedStepFp, speedSteps, runTicks, pauseMinTicks, pauseSpanTicks; HOME_IN: accelFp, maxFp.";
        for (int p = 0; p < params.length; p++) {
            array("short", "g_CreatureP" + p, params[p]);
        }
        note = "Bank frames: walk0, walk1, walk0_l, walk1_l (sprite_bank.h).";
        array("unsigned char", "g_CreatureFrame", frames);

        // The swarm.
        array("unsigned char", "g_SwarmPick", swarm.pick());
        int[] tints = new int[swarm.tints().length];
        for (int i = 0; i < tints.length; i++) {
            tints[i] = vdp.vdpColour(swarm.tints()[i]);
        }
        note = "The tints, as colour numbers.";
        array("unsigned char", "g_SwarmTint", tints);
        String puff = "puff";
        int[] puffFrames = {bank.index(puff, "f0"), bank.index(puff, "f1"), bank.index(puff, "f0" + RomSprites.INK),
            bank.index(puff, "f1" + RomSprites.INK)};
        note = "The puff: f0, f1, and the same as silhouettes, for a forming one in its tint.";
        array("unsigned char", "g_PuffFrame", puffFrames);

        // The beasts' kinds.
        int n = beasts.kinds().size();
        int[] kk = new int[n];
        int[] kbx = new int[n];
        int[] kby = new int[n];
        int[] kbw = new int[n];
        int[] kbh = new int[n];
        int[] halfW = new int[n];
        int[][] kp = new int[BEAST_PARAMS.size()][n];
        int[] kf = new int[n * 6];
        for (int k = 0; k < n; k++) {
            CreatureData.Species s = beasts.kinds().get(k);
            kk[k] = beasts.kindOf().get(k).ordinal();
            kbx[k] = s.collisionBox().x();
            kby[k] = s.collisionBox().y();
            kbw[k] = s.collisionBox().w();
            kbh[k] = s.collisionBox().h();
            halfW[k] = s.size().w() / 2;
            for (int p = 0; p < BEAST_PARAMS.size(); p++) {
                Integer v = s.behaviour().params().get(BEAST_PARAMS.get(p));
                kp[p][k] = v == null ? 0 : v;
                // move.c's Beast_Step takes a step into one new strip of cells at most.
                if (BEAST_PARAMS.get(p).endsWith("Fp") && Math.abs(kp[p][k]) >= Simulation.CELL_PX * Fixed.ONE) {
                    throw new ConvertException("data/entities/jungle.json: beast '" + s.id() + "''s "
                            + BEAST_PARAMS.get(p) + " is a cell or more a tick; the ROM's beasts step less");
                }
            }
            // data.c's Data_BoxTest takes a box over 6 cells at most each way; move.c its edges in a byte.
            if (kbw[k] > 41 || kbh[k] > 41 || kbx[k] + kbw[k] - 1 > 127 || kby[k] + kbh[k] - 1 > 127
                    || kbx[k] < -128 || kby[k] < -128) {
                throw new ConvertException("data/entities/jungle.json: beast '" + s.id() + "''s box is too big for the ROM");
            }
            String[] f = {"walk0", "walk1", "walk0_l", "walk1_l", "sleep", "sleep_l"};
            for (int i = 0; i < f.length; i++) {
                boolean has = c.sprites().get(s.sprite()).decode(s.sprite()).containsKey(f[i]);
                kf[k * 6 + i] = has ? bank.index(s.sprite(), f[i]) : bank.index(s.sprite(), f[i % 2 == 0 ? 0 : 2]);
            }
        }
        array("unsigned char", "g_BeastKind", kk);
        array("signed char", "g_BeastBoxX", kbx);
        array("signed char", "g_BeastBoxY", kby);
        array("unsigned char", "g_BeastBoxW", kbw);
        array("unsigned char", "g_BeastBoxH", kbh);
        note = "Half the sprite's width: a spot's x is the sprite's left (§12.9).";
        array("unsigned char", "g_BeastHalfW", halfW);
        note = String.join(", ", BEAST_PARAMS) + ".";
        for (int p = 0; p < BEAST_PARAMS.size(); p++) {
            array("short", "g_BeastP" + p, kp[p]);
        }
        note = "Bank frames: walk0, walk1, walk0_l, walk1_l, sleep, sleep_l (a kind without sleep: walk0).";
        array("unsigned char", "g_BeastFrame", kf);
        array("unsigned char", "g_Sleepers", beasts.sleepers());

        // The placings.
        List<BeastRules.Placing> pl = beasts.placings();
        int[] pk = new int[pl.size()];
        int[] pr = new int[pl.size()];
        int[] px = new int[pl.size()];
        int[] py = new int[pl.size()];
        int[] ph = new int[pl.size()];
        int[] pa = new int[pl.size()];
        for (int i = 0; i < pl.size(); i++) {
            BeastRules.Placing p = pl.get(i);
            pk[i] = p.kind();
            pr[i] = p.room().index();
            px[i] = p.xPx();
            py[i] = p.yPx();
            ph[i] = p.heading();
            pa[i] = p.asleep() ? 1 : 0;
        }
        note = "Where a new game sets them: kind, room, world feet, heading, asleep.";
        array("unsigned char", "g_PlaceKind", pk);
        array("unsigned char", "g_PlaceRoom", pr);
        array("unsigned short", "g_PlaceX", px);
        array("unsigned short", "g_PlaceY", py);
        array("signed char", "g_PlaceHeading", ph);
        array("unsigned char", "g_PlaceAsleep", pa);

        // The spots.
        int rooms = RoomAddress.GRID_W * RoomAddress.GRID_H;
        int[] sx = new int[rooms];
        int[] sy = new int[rooms];
        for (int i = 0; i < rooms; i++) {
            sx[i] = beasts.spotXPx()[i];
            sy[i] = beasts.spotYPx()[i];
            if (sx[i] < 0 || sx[i] > 255 || sy[i] < 0 || sy[i] > 255) {
                throw new ConvertException("data/entities/jungle.json: room " + i + "'s spot " + sx[i] + "," + sy[i]
                        + " does not fit a byte");
            }
        }
        note = "Each room's spot (§12.9): x the sprite's left, y its feet, room-local.";
        array("unsigned char", "g_SpotX", sx);
        array("unsigned char", "g_SpotY", sy);
    }

    private String note;

    private void array(String type, String name, int[] values) {
        switch (type) {
            case "unsigned char" -> tables.u8(name, values, note);
            case "signed char" -> tables.i8(name, values, note);
            case "unsigned short" -> tables.u16(name, values, note);
            default -> tables.i16(name, values, note);
        }
        note = null;
    }

    public String header() {
        return h.toString();
    }
}
