package wulfmsx.convert;

import java.lang.reflect.Field;
import java.util.List;
import wulf.Content;
import wulf.data.CreatureData;
import wulf.data.FlameData;
import wulf.data.GameConfig;
import wulf.data.LandmarksData;
import wulf.data.OrchidData;
import wulf.sim.BeastRules;
import wulf.sim.Ecosystem;
import wulf.sim.OrchidSpots;
import wulf.sim.QuestRules;
import wulf.sim.Simulation;
import wulf.sim.SwarmRules;
import wulf.sim.Treasures;
import wulf.sim.effects.EffectKind;
import wulf.world.RoomAddress;

/**
 * What makes a run a run, as tables for the ROM (AGENTS.md §10, M8; the Java
 * game's §12.7, §14.5, §14.8, §14.10, §15, §16, §17.5, §17.8): the orchids and
 * the effects they give, the flame, the treasures, the map scroll, the eyes and
 * the looking glasses, the shrines, the score's events, continues, and Easy.
 * Read the way the Java game reads them: {@code Content.ecosystem()}, and
 * {@code ecosystem(0, true)} for Easy.
 *
 * <p>{@code game_data.h}: the numbers as defines; small tables into
 * {@code tables.h}; and {@code rooms_more.bin}, {@link #ROOM_BYTES} a room, which
 * the ROM reads on entering one: its orchid's spot, its treasures' two places,
 * whether it is a shrine's.
 */
public final class Game {

    /**
     * rooms_more.bin: orchid x, y; place 0 x, y; place 1 x, y; flags; spare; the beasts'
     * spot x, y (§12.9, BeastRules: out of RAM since M8). 0xFF: none.
     */
    public static final int ROOM_BYTES = 16;
    /** Flags in a room's last byte. */
    static final int SHRINE = 1;

    /** The effects the ROM has: the original's orchids' (others are the invented ones, §1). */
    static final List<EffectKind> EFFECTS = List.of(EffectKind.NONE, EffectKind.HASTE, EffectKind.TORPOR,
            EffectKind.REVERSAL, EffectKind.IMMUNITY, EffectKind.KNOCKDOWN, EffectKind.CURE);

    private final StringBuilder h = new StringBuilder();
    private final byte[] rooms;

    public Game(Content c, VdpPalette vdp, RomSprites bank, RamTables tables) {
        Ecosystem eco = c.ecosystem();
        Ecosystem easy = c.ecosystem(0, true);
        QuestRules q = eco.quest();
        OrchidData orchids = eco.orchids();
        h.append(Gen.banner("data/entities/orchids.json, flame.json, data/world/treasures.json, landmarks.json, "
                + "config/game.json, entities/loot.json"));
        h.append("#pragma once\n\n");

        def("GAME_ROOM_BYTES", ROOM_BYTES);
        def("GAME_ROOM_SHRINE", SHRINE);

        // The orchids (§15).
        List<OrchidData.Orchid> list = orchids.orchids();
        h.append("// The orchids (§15): their cycle, and each one's effect (EffectKind's order).\n");
        def("ORCHIDS", list.size());
        OrchidData.Cycle cy = orchids.cycle();
        def("ORCHID_SEED_TICKS", cy.seedTicks());
        def("ORCHID_SPROUT_TICKS", cy.sproutTicks());
        def("ORCHID_BUD_TICKS", cy.budTicks());
        def("ORCHID_BLOOM_TICKS", cy.bloomTicks());
        def("ORCHID_WILT_TICKS", cy.wiltTicks());
        def("ORCHID_SWAY_TICKS", cy.swayTicks());
        def("ORCHID_CYCLE_TICKS", cy.totalTicks());
        def("ORCHID_TINT_WARN_TICKS", orchids.tint().warnTicks());
        def("ORCHID_TINT_FLASH_TICKS", orchids.tint().flashTicks());
        def("ORCHID_IMMUNITY_FLASH_TICKS", orchids.immunity().flashPeriodTicks());
        for (EffectKind k : EffectKind.values()) {
            def("EFFECT_" + k.name(), k.ordinal());
        }
        // OrchidField.firstCycleStart's last hash value, a literal in its code (no field to read).
        def("ORCHID_FIRST_STREAM", 0x5EED);
        int[] kind = new int[list.size()];
        int[] weight = new int[list.size()];
        int[] ticks = new int[list.size()];
        int[] scale = new int[list.size()];
        int[] score = new int[list.size()];
        int[] untouchable = new int[list.size()];
        int total = 0;
        for (int i = 0; i < list.size(); i++) {
            OrchidData.Orchid o = list.get(i);
            EffectKind k = EffectKind.of(o.effect());
            if (!EFFECTS.contains(k)) {
                throw new ConvertException("data/entities/orchids.json: orchid '" + o.colour() + "' gives " + k
                        + "; the ROM has the original's effects " + EFFECTS);
            }
            kind[i] = k.ordinal();
            weight[i] = o.weight();
            ticks[i] = o.ticks();
            scale[i] = o.speedScaleFp();
            // vale.c's Speeds carries its levels' quotients on (M13): a positive scale.
            if (scale[i] <= 0) {
                throw new ConvertException("an orchid's speedScaleFp is " + scale[i] + "; the ROM's are positive");
            }
            score[i] = o.score();
            untouchable[i] = o.untouchable() ? 1 : 0;
            total += o.weight();
        }
        def("ORCHID_WEIGHTS", total);
        tables.u8("g_OrchidEffect", kind, "Each orchid: its effect, weight, ticks, speed scale, score, untouchable.");
        tables.u8("g_OrchidWeight", weight, null);
        tables.u16("g_OrchidTicks", ticks, null);
        tables.i16("g_OrchidScale", scale, null);
        tables.u16("g_OrchidScore", score, null);
        tables.u8("g_OrchidUntouchable", untouchable, null);

        // The flame (§12.7).
        FlameData f = eco.flame();
        h.append("\n// The flame (§12.7): FlameData, and FLAME's parameters.\n");
        def("FLAME_COMES", f.comes() ? 1 : 0);
        def("FLAME_LINGER_TICKS", f.lingerTicks());
        def("FLAME_LIFE_TICKS", f.lifeTicks());
        def("FLAME_STEER_TICKS", f.steerTicks());
        def("FLAME_HOMING_PER10K", f.homingPer10k());
        def("FLAME_INSET_PX", f.spawnInsetPx());
        def("FLAME_BOX_X", f.collisionBox().x());
        def("FLAME_BOX_Y", f.collisionBox().y());
        def("FLAME_BOX_W", f.collisionBox().w());
        def("FLAME_BOX_H", f.collisionBox().h());
        def("FLAME_SPEED_FP", f.speed().xFp());
        if (f.speed().xFp() != f.speed().yFp()) {
            throw new ConvertException("data/entities/flame.json: the ROM's flame goes as fast either way");
        }
        def("FLAME_DIAG_FP", wulf.engine.Fixed.mul(f.speed().xFp(), eco.creatures().diagonalScaleFp()));
        int[] ff = new int[4];
        String[] fn = {"walk0", "walk1", "walk0_l", "walk1_l"};
        for (int i = 0; i < 4; i++) {
            boolean has = c.sprites().get(f.sprite()).decode(f.sprite()).containsKey(fn[i]);
            ff[i] = bank.index(f.sprite(), has ? fn[i] : fn[i % 2]);
        }
        tables.u8("g_FlameFrame", ff, "The flame's frames: walk0, walk1, walk0_l, walk1_l (CreaturePainter.frameFor).");
        def("FLAME_IGNORES_SCENERY", f.asSpecies().ignoresScenery() ? 1 : 0);

        // The treasures (§16.4).
        Treasures t = q.treasures();
        h.append("\n// The treasures (§16.4): Rng(Rng.hash(seed, TREASURE_STREAM)) lays them.\n");
        long stream = privateLong(Simulation.class, "TREASURE_STREAM");
        h.append("#define TREASURE_STREAM_BYTES\t").append(bytes(stream)).append('\n');
        def("TREASURE_KINDS", t.kinds());
        def("TREASURE_FILL_PER10K", t.fillPercent() * 100);
        def("TREASURE_SCORE", t.score());
        def("TREASURE_BOX_X", t.pickupBox().x());
        def("TREASURE_BOX_Y", t.pickupBox().y());
        def("TREASURE_BOX_W", t.pickupBox().w());
        def("TREASURE_BOX_H", t.pickupBox().h());
        if (t.kinds() > 254) {
            throw new ConvertException("data/world/treasures.json: " + t.kinds() + " kinds; the ROM keeps one in a byte");
        }
        int[] life = new int[t.kinds()];
        for (int i = 0; i < life.length; i++) {
            life[i] = t.extraLife()[i] ? 1 : 0;
        }
        tables.u8("g_TreasureLife", life, "Each treasure kind: whether it is a life too.");

        // The items (§14.8, §14.10) and the shrines (§14.5).
        LandmarksData marks = q.landmarks();
        LandmarksData.Items items = marks.items();
        h.append("\n// The map scroll, the eyes, the looking glasses (§14.8, §14.10); the shrines (§14.5).\n");
        def("ITEM_BOX_X", items.pickupBox().x());
        def("ITEM_BOX_Y", items.pickupBox().y());
        def("ITEM_BOX_W", items.pickupBox().w());
        def("ITEM_BOX_H", items.pickupBox().h());
        def("MAP_ROOM", LandmarksData.room(items.map().room()).index());
        def("MAP_X", items.map().spot().x());
        def("MAP_Y", items.map().spot().y());
        def("EYES", items.eyes().size());
        List<LandmarksData.Place> glasses = items.glass().all();
        def("GLASSES", glasses.size());
        def("GLASSES_BEFORE_WHOLE", items.glass().places().size());
        def("GLASS_WAY_TICKS", items.glass().showWayTicks());
        int[] ir = new int[items.eyes().size() + glasses.size()];
        int[] ix = new int[ir.length];
        int[] iy = new int[ir.length];
        for (int i = 0; i < items.eyes().size(); i++) {
            LandmarksData.Eye e = items.eyes().get(i);
            ir[i] = LandmarksData.room(e.room()).index();
            ix[i] = e.spot().x();
            iy[i] = e.spot().y();
        }
        for (int i = 0; i < glasses.size(); i++) {
            LandmarksData.Place p = glasses.get(i);
            int at = items.eyes().size() + i;
            ir[at] = LandmarksData.room(p.room()).index();
            ix[at] = p.spot().x();
            iy[at] = p.spot().y();
        }
        tables.u8("g_ItemRoom", ir, "The eyes, then the glasses (all, then the whole amulet's): room, spot.");
        tables.u8("g_ItemX", ix, null);
        tables.u8("g_ItemY", iy, null);
        LandmarksData.Rect shrine = marks.shrines().zone();
        def("SHRINE_ZONE_X", shrine.x());
        def("SHRINE_ZONE_Y", shrine.y());
        def("SHRINE_ZONE_W", shrine.w());
        def("SHRINE_ZONE_H", shrine.h());
        def("HINT_TICKS", marks.hint().messageTicks());
        def("HINT_ONCE_PER_LIFE", marks.hint().oncePerLife() ? 1 : 0);
        def("CAVE_HINTS", q.caveHints() ? 1 : 0);

        // The score (§16).
        h.append("\n// The score's events (§16): loot.json, the swarm's.\n");
        def("SCORE_FIRST_VISIT", eco.roomFirstVisitScore());
        def("SCORE_PIECE", q.pieceScore());
        def("SCORE_ESCAPE", q.escapeBonus());
        def("SCORE_TIME_MAX", q.timeBonusMax());
        def("SCORE_TIME_DIVISOR", q.timeBonusTicksDivisor());
        def("SCORE_LIFE_LEFT", q.lifeRemainingBonus());
        def("CONTINUES_START", q.continues());
        SwarmRules s = eco.swarm();
        def("SWARM_FACING_RIGHT_BONUS", s.facingRightBonus());
        int[] appear = s.appearScores();
        tables.u16("g_SwarmAppearScore", appear, "SwarmRules.appearScores: a forming puff burst, by its tick.");
        def("SWARM_APPEAR_SCORES", appear.length);
        int[] cs = new int[eco.creatures().creatures().size() + 1];   // and the flame's: none
        for (int i = 0; i < cs.length - 1; i++) {
            cs[i] = eco.creatures().creatures().get(i).score();
        }
        tables.u16("g_CreatureScore", cs, "Each small creature's score.");

        // Easy (§17.8): what changes, as the ROM's Hard values' replacements.
        GameConfig.Easy e = c.game().easy();
        h.append("\n// Easy (§17.8).\n");
        def("EASY_LIVES", e.livesStart());
        def("EASY_SWARM_ROOM_MAX", easy.swarm().roomMax());
        def("EASY_SWARM_OPEN_PER_QUARTER", easy.swarm().openPerQuarter());
        def("EASY_FLAME", e.flame() ? 1 : 0);
        def("EASY_WAY_TICKS", e.showWayTicks());
        def("EASY_HINTS_ONCE_PER_LIFE", e.hintsOncePerLife() ? 1 : 0);
        jobs(c, bank);
        BeastRules hb = eco.beasts();
        BeastRules eb = easy.beasts();
        int n = hb.kinds().size();
        int[][] ep = new int[Jungle.BEAST_PARAMS.size()][n];
        for (int k = 0; k < n; k++) {
            for (int p = 0; p < Jungle.BEAST_PARAMS.size(); p++) {
                Integer v = eb.kinds().get(k).behaviour().params().get(Jungle.BEAST_PARAMS.get(p));
                ep[p][k] = v == null ? 0 : v;
            }
        }
        for (int p = 0; p < Jungle.BEAST_PARAMS.size(); p++) {
            tables.i16("g_EasyBeastP" + p, ep[p], p == 0 ? "Easy's beast parameters, as g_BeastP0.. (slower paces)." : null);
        }

        // Drawing (M8): OrchidPainter's frames, QuestPainter's treasures and items, the flashes.
        h.append("\n// Drawing: the orchids' frames by stage, the treasures' and items', the flashes.\n");
        String os = orchids.sprite();
        def("ORCHID_FRAME_SPROUT", bank.index(os, "sprout"));
        int[] of = new int[list.size() * 4];
        for (int i = 0; i < list.size(); i++) {
            String col = list.get(i).colour();
            of[4 * i] = bank.index(os, "bud_" + col);
            of[4 * i + 1] = bank.index(os, "bloom0_" + col);
            of[4 * i + 2] = bank.index(os, "bloom1_" + col);
            of[4 * i + 3] = bank.index(os, "wilt_" + col);
        }
        tables.u8("g_OrchidFrame", of, "Each orchid: bud, bloom0, bloom1, wilt.");
        List<wulf.data.TreasureData.Kind> tk = c.treasures().kinds();
        int[] tf = new int[tk.size()];
        int[] tinks = new int[tk.size()];
        for (int i = 0; i < tk.size(); i++) {
            tf[i] = bank.index(c.treasures().sprite(), tk.get(i).frame() + RomSprites.INK);
            tinks[i] = vdp.vdpColour(c.palette().indexOf(tk.get(i).ink()));
        }
        tables.u8("g_TreasureFrame", tf, "Each treasure kind: its silhouette, and the ink it is drawn in (blitTinted).");
        tables.u8("g_TreasureInk", tinks, null);
        def("MAP_FRAME", bank.index(items.map().sprite(), "f0"));
        def("ITEM_GLANCE_TICKS", items.glanceTicks());
        String eye = items.eyes().get(0).sprite();
        String glassSprite = items.glass().sprite();
        int[] ef = new int[6];
        for (int i = 0; i < 3; i++) {
            ef[i] = bank.index(eye, "f" + i);
            ef[3 + i] = bank.index(glassSprite, "f" + i);
        }
        tables.u8("g_GlanceFrame", ef, "An eye's f0, f1, f2, then a glass's (QuestPainter's glance: 0, 1, 0, 2).");
        def("FLASH_COLOUR", vdp.vdpColour(c.palette().indexOf("brightWhite")));
        List<CreatureData.Species> roster = eco.creatures().creatures();
        int[] cf = new int[roster.size() * 4];
        String[] walk = {"walk0", "walk1", "walk0_l", "walk1_l"};
        for (int i = 0; i < roster.size(); i++) {
            for (int k = 0; k < 4; k++) {
                cf[4 * i + k] = bank.index(roster.get(i).sprite(), walk[k] + RomSprites.INK);
            }
        }
        tables.u8("g_CreatureFlash", cf, "A hurt creature's flash: its walk frames' silhouettes (CreaturePainter.flashing).");
        int[] gfl = new int[q.guardianSpecies().size() * 4];
        for (int i = 0; i < q.guardianSpecies().size(); i++) {
            for (int k = 0; k < 4; k++) {
                gfl[4 * i + k] = bank.index(q.guardianSpecies().get(i).sprite(), walk[k] + RomSprites.INK);
            }
        }
        tables.u8("g_GuardFlash", gfl, "A stunned guardian's flash, each lair's: walk0, walk1, walk0_l, walk1_l.");
        BeastRules br = eco.beasts();
        int hintKind = -1;
        for (int k = 0; k < br.kinds().size(); k++) {
            if (br.kindOf().get(k) == wulf.sim.Beast.Kind.TRIBESMAN) {
                hintKind = k;
            }
        }
        int[] hf = new int[4];
        for (int k = 0; k < 4; k++) {
            hf[k] = bank.index(br.kinds().get(hintKind).sprite(), walk[k] + RomSprites.INKED);
        }
        tables.u8("g_HintFrame", hf, "A tribesman level with a quarter, inked yellow: walk0, walk1, walk0_l, walk1_l.");

        // The looking glass's arrow (§14.10, WayPainter): the way out of each room to the
        // nearest quarter still lying, or to the arch. The Java game finds it on the map's cells
        // from Vale's feet; the ROM has it found here, from each room's middle, with the Java
        // game's own Wayfinder, for every set and every state of the quarters taken.
        LandmarksData.Arrow arrow = items.glass().arrow();
        def("WAY_BLINK_TICKS", arrow.blinkTicks());
        def("WAY_WARN_BLINK_TICKS", arrow.warnBlinkTicks());
        def("WAY_WARN_TICKS", items.glass().warnTicks());
        def("WAY_INSET_PX", arrow.insetPx());
        String[] edges = {"n", "e", "s", "w"};
        int[] af = new int[4];
        for (int i = 0; i < 4; i++) {
            af[i] = bank.index(arrow.sprite(), edges[i]);
        }
        tables.u8("g_ArrowFrame", af, "The arrow's frames: n, e, s, w.");
        def("WAY_ARROW_HALF_H", c.sprites().get(arrow.sprite()).size().h() / 2);
        ways = ways(c, eco, q, box(c));

        // Each room: its orchid, its treasures' places, a shrine.
        OrchidSpots spots = eco.orchidSpots();
        int count = RoomAddress.GRID_W * RoomAddress.GRID_H;
        rooms = new byte[count * ROOM_BYTES];
        for (int i = 0; i < count; i++) {
            RoomAddress r = new RoomAddress(i % RoomAddress.GRID_W, i / RoomAddress.GRID_W);
            int o = i * ROOM_BYTES;
            if (spots.xPx()[i] == OrchidSpots.NO_SPOT) {
                rooms[o] = (byte) 0xFF;
                rooms[o + 1] = (byte) 0xFF;
            } else {
                rooms[o] = (byte) fits(spots.xPx()[i], "orchid x in room " + r);
                rooms[o + 1] = (byte) fits(spots.yPx()[i], "orchid y in room " + r);
            }
            for (int pl = 0; pl < Treasures.PLACES_PER_ROOM; pl++) {
                int ti = i * Treasures.PLACES_PER_ROOM + pl;
                boolean has = t.hasPlace(ti);
                rooms[o + 2 + 2 * pl] = (byte) (has ? fits(t.x()[ti], "treasure x in room " + r) : 0xFF);
                rooms[o + 3 + 2 * pl] = (byte) (has ? fits(t.y()[ti], "treasure y in room " + r) : 0xFF);
            }
            rooms[o + 7] = (byte) (marks.isShrine(r) ? SHRINE : 0);
            rooms[o + 8] = (byte) eco.beasts().spotXPx()[i];   // Jungle checks they fit a byte
            rooms[o + 9] = (byte) eco.beasts().spotYPx()[i];
        }
        if (!spots.on()) {
            throw new ConvertException("the orchids find their own anchors (inventedOrchids); the ROM has the "
                    + "original's spot a room");
        }
    }

    private final byte[] ways;

    private static wulf.data.PlayerData.Box box(Content c) {
        return c.player().collisionBox();
    }

    /**
     * ways_N.bin: for set k (sets 4N..4N+3), taken mask m, room r, at ((k % 4) * 16 + m) * 256 + r: the
     * way's edge (n e s w) << 6 and the cell along it it crosses at, or 0xFF where the way ends in the room.
     */
    private static byte[] ways(Content c, Ecosystem eco, QuestRules q, wulf.data.PlayerData.Box feet) {
        LandmarksData marks = q.landmarks();
        int sets = marks.amuletSets().sets().size();
        int lairs = marks.lairs().size();
        int rooms = RoomAddress.GRID_W * RoomAddress.GRID_H;
        wulf.world.Wayfinder finder = new wulf.world.Wayfinder(new wulf.world.WorldGrid(c.rooms()));
        int cols = wulf.world.CollisionMask.COLS;
        int rows = wulf.world.CollisionMask.ROWS;
        byte[] out = new byte[sets * (1 << lairs) * rooms];
        for (int k = 0; k < sets; k++) {
            LandmarksData placed = marks.withAmuletSet(k);
            for (int m = 0; m < 1 << lairs; m++) {
                int[] targets = new int[lairs];
                int n = 0;
                for (int i = 0; i < lairs; i++) {
                    if ((m & 1 << i) == 0) {
                        LandmarksData.Lair lair = placed.lairs().get(i);
                        targets[n++] = cellAt(finder, LandmarksData.room(lair.room()), lair.pedestal().x(),
                                lair.pedestal().y(), feet);
                    }
                }
                if (n == 0) {
                    LandmarksData.Rect zone = placed.exit().zone();
                    targets = new int[] {cellAt(finder, placed.exitRoom(), zone.x() + zone.w() / 2, zone.y() + zone.h(),
                            feet)};
                    n = 1;
                }
                int[] dist = finder.distances(java.util.Arrays.copyOf(targets, n));
                for (int r = 0; r < rooms; r++) {
                    int col = r % RoomAddress.GRID_W;
                    int row = r / RoomAddress.GRID_W;
                    int v = 0xFF;
                    int from = finder.nearest(dist, col * cols + cols / 2, row * rows + rows / 2, cols / 2);
                    if (from >= 0) {
                        int[] way = finder.path(dist, from);
                        for (int i = 1; i < way.length; i++) {
                            int gx = wulf.world.Wayfinder.gx(way[i]);
                            int gy = wulf.world.Wayfinder.gy(way[i]);
                            if (gx / cols == col && gy / rows == row) {
                                continue;
                            }
                            int lx = wulf.world.Wayfinder.gx(way[i - 1]) - col * cols;
                            int ly = wulf.world.Wayfinder.gy(way[i - 1]) - row * rows;
                            int dx = gx - wulf.world.Wayfinder.gx(way[i - 1]);
                            int dy = gy - wulf.world.Wayfinder.gy(way[i - 1]);
                            int edge = dx > 0 ? 1 : dx < 0 ? 3 : dy > 0 ? 2 : 0;
                            int along = edge == 0 || edge == 2 ? lx : ly;
                            if (along < 0 || along > 63) {
                                throw new ConvertException("the way out of room " + r + " crosses outside it");
                            }
                            v = edge << 6 | along;
                            break;
                        }
                    }
                    out[(k * (1 << lairs) + m) * rooms + r] = (byte) v;
                }
            }
        }
        return out;
    }

    /** WayPainter.cellAt: where the feet would stand at a spot, a fitting cell near it. */
    private static int cellAt(wulf.world.Wayfinder finder, RoomAddress room, int localX, int localY,
            wulf.data.PlayerData.Box feet) {
        int gx = Math.floorDiv(room.col() * Simulation.ROOM_W_PX + localX + feet.x(), Simulation.CELL_PX);
        int gy = Math.floorDiv(room.row() * Simulation.ROOM_H_PX + localY + feet.y(), Simulation.CELL_PX);
        return finder.nearest(null, gx, gy, NEAR_CELLS);
    }

    /** WayPainter.NEAR_CELLS: how far from a point it looks for somewhere the feet fit. */
    private static final int NEAR_CELLS = (int) privateLong(wulf.render.WayPainter.class, "NEAR_CELLS");

    /** ways_0.bin and ways_1.bin: four sets each, a ROM segment each. */
    public List<byte[]> ways() {
        int half = ways.length / 2;
        return List.of(java.util.Arrays.copyOfRange(ways, 0, half), java.util.Arrays.copyOfRange(ways, half, ways.length));
    }

    /**
     * §14.9, New Game+: Mr Jones's jobs, as {@code Content.ecosystem(n, easy)} makes each. Their
     * numbers as macros of the job (a chain of choices, the converter's), so the ROM names none:
     * how many pieces the amulet comes in (0: one a lair, the game itself), the slots piece p fills,
     * the jungle's head start, the Wulf's pace (Simulation.paced) in Hard and Easy, the amulet's
     * frame for each slot (AmuletFrames.forLevel), and the stream that picks the lairs kept.
     */
    private void jobs(Content c, RomSprites bank) {
        int n = c.game().newGamePlus().levels().size();
        LandmarksData marks = c.landmarks();
        int lairs = marks.lairs().size();
        int[] pieces = new int[n];
        int[] head = new int[n];
        int[][] mask = new int[lairs][n];
        int[][] speed = new int[2][n];
        int[][] risen = new int[2][n];
        int[][] frame = new int[wulf.sim.Quest.SLOTS][n];
        wulf.render.AmuletFrames frames = wulf.render.AmuletFrames.of(marks, c.shell());
        String sprite = marks.amulet().sprite();
        for (int j = 0; j < n; j++) {
            GameConfig.Level rules = c.ecosystem(j).quest().levelRules();
            int k = rules.pieces().size();
            pieces[j] = k >= lairs ? 0 : k;   // Simulation.shareOut keeps them all
            head[j] = rules.rampHeadStart();
            for (int p = 0; p < pieces[j]; p++) {
                mask[p][j] = rules.slotMask(p);
            }
            for (int e = 0; e < 2; e++) {
                Ecosystem eco = c.ecosystem(j, e == 1);
                speed[e][j] = paced(eco, eco.wulf().data().patrol().speedFp());
                risen[e][j] = paced(eco, eco.wulf().data().patrol().risenSpeedFp());
            }
            for (int slot = 0; slot < wulf.sim.Quest.SLOTS; slot++) {
                frame[slot][j] = bank.index(sprite, frames.forLevel(j).get(slot));
            }
        }
        h.append("\n// New Game+ (§14.9): the jobs, job j's numbers.\n");
        def("JOBS", n);
        h.append("#define JOB_STREAM_BYTES\t").append(bytes(privateLong(Simulation.class, "JOB_STREAM"))).append('\n');
        macro("JOB_PIECES(j)", "j", pieces);
        macro("JOB_HEAD_START(j)", "j", head);
        h.append("#define JOB_MASK(j, p)\t(");
        for (int p = 0; p < lairs; p++) {
            h.append(p + 1 < lairs ? "(p) == " + p + " ? " : "").append(chain("j", mask[p])).append(p + 1 < lairs ? " : " : "");
        }
        h.append(")\n");
        macro("JOB_WULF_SPEED_FP(j, e)", "j", speed[0], speed[1]);
        macro("JOB_WULF_RISEN_SPEED_FP(j, e)", "j", risen[0], risen[1]);
        h.append("#define JOB_FRAME(j, s)\t(");
        for (int s = 0; s < wulf.sim.Quest.SLOTS; s++) {
            h.append(s + 1 < wulf.sim.Quest.SLOTS ? "(s) == " + s + " ? " : "").append(chain("j", frame[s]))
                    .append(s + 1 < wulf.sim.Quest.SLOTS ? " : " : "");
        }
        h.append(")\n");
    }

    private void macro(String name, String arg, int[] values) {
        h.append("#define ").append(name).append('\t').append(chain(arg, values)).append('\n');
    }

    private void macro(String name, String arg, int[] hard, int[] easy) {
        h.append("#define ").append(name).append("\t((e) ? ").append(chain(arg, easy)).append(" : ")
                .append(chain(arg, hard)).append(")\n");
    }

    /** {@code values[arg]} as C: a chain of choices, or the one value they all are. */
    private static String chain(String arg, int[] values) {
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

    /** Simulation.paced: the Wulf's pace in a job, and in Easy at Easy's percent of it. */
    private static int paced(Ecosystem eco, int speedFp) {
        int percent = eco.quest().levelRules().wulfSpeedPercent();
        if (eco.quest().isEasy()) {
            percent = percent * eco.quest().easy().wulfSpeedPercent() / 100;
        }
        return percent == 100 ? speedFp : speedFp * percent / 100;
    }

    private static int fits(int v, String what) {
        if (v < 0 || v > 254) {
            throw new ConvertException(what + " is " + v + "; the ROM keeps it in a byte below 255");
        }
        return v;
    }

    private void def(String name, long v) {
        h.append("#define ").append(name).append('\t').append(v < 0 ? "(" + v + ")" : String.valueOf(v)).append('\n');
    }

    private static String bytes(long v) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 8; i++) {
            sb.append(i == 0 ? "" : ", ").append(String.format("0x%02X", v >>> (8 * i) & 0xFF));
        }
        return sb.toString();
    }

    static long privateLong(Class<?> c, String name) {
        try {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            return f.getLong(null);
        } catch (ReflectiveOperationException ex) {
            throw new ConvertException(c.getName() + "." + name + ": the Java game does not have it");
        }
    }

    public String header() {
        return h.toString();
    }

    /** rooms_more.bin. */
    public byte[] rooms() {
        return rooms.clone();
    }
}
