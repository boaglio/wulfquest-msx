package wulfmsx.convert;

import java.lang.reflect.Field;
import java.util.List;
import wulf.Content;
import wulf.data.CreatureData;
import wulf.data.GameConfig;
import wulf.data.GuardianData;
import wulf.data.LandmarksData;
import wulf.data.WulfData;
import wulf.engine.Fixed;
import wulf.engine.SinTable;
import wulf.sim.QuestRules;
import wulf.sim.Simulation;
import wulf.sim.WulfRules;
import wulf.world.CollisionMask;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * The quest and the original's Wulf as tables for the ROM (AGENTS.md §10, M7;
 * the Java game's §13.8, §14): the eight amulet sets as each lair's room and
 * pedestal, the guardians, the Keeper of the Arch and the way out, and the
 * Wulf prowling its row. Read the way the Java game reads them:
 * {@code Content.questRules}, {@code wulfRules}.
 *
 * <p>{@code quest_data.h}: the numbers as defines; the tables go into
 * {@code tables.h} ({@link RamTables}).
 */
public final class Quest {

    private final StringBuilder h = new StringBuilder();
    private final RamTables tables;
    private String note;
    /** Set k's lair i at k * lairs + i: its room's index, its pedestal, its guardian's circle (for the tests). */
    final int[] lairRoom;
    final int[] lairPedX;
    final int[] lairPedY;
    final int[] lairOrbit;

    public Quest(Content c, RomSprites bank, RamTables tables) {
        this.tables = tables;
        QuestRules q = c.questRules();
        WulfRules w = c.wulfRules();
        LandmarksData marks = q.landmarks();
        LandmarksData.AmuletSets sets = marks.amuletSets();
        List<LandmarksData.Lair> lairs = marks.lairs();
        GuardianData.Orbit orbit = q.orbit();
        String where = "data/world/landmarks.json";

        if (w.roaming()) {
            throw new ConvertException("config/game.json: roamingWulf is on; the ROM has the original's Wulf (AGENTS.md §1)");
        }
        if (!GameConfig.Level.ORIGINAL.equals(q.levelRules()) || q.isEasy()) {
            throw new ConvertException("the quest's rules are not the game itself's, Hard (M7 has only those)");
        }
        if (lairs.size() != wulf.sim.Quest.SLOTS) {
            throw new ConvertException(where + ": " + lairs.size() + " lairs; the ROM has one for each of the "
                    + wulf.sim.Quest.SLOTS + " quarters");
        }

        h.append(Gen.banner("data/world/landmarks.json, entities/guardians.json and wulf.json"));
        h.append("#pragma once\n\n");
        h.append("// The amulet (Quest.SLOTS quarters) and where the game hides it (§14.2).\n");
        def("QUEST_SLOTS", wulf.sim.Quest.SLOTS);
        def("LAIRS", lairs.size());
        def("AMULET_SETS", sets.sets().size());
        long stream = privateLong(Simulation.class, "AMULET_STREAM");
        h.append("// Simulation.AMULET_STREAM: the set is Rng(Rng.hash(seed, it)).nextInt(AMULET_SETS).\n");
        h.append("#define AMULET_STREAM_BYTES\t");
        for (int i = 0; i < 8; i++) {
            h.append(i == 0 ? "" : ", ").append(String.format("0x%02X", stream >>> (8 * i) & 0xFF));
        }
        h.append('\n');
        LandmarksData.Amulet amulet = marks.amulet();
        def("AMULET_BOX_X", amulet.collisionBox().x());
        def("AMULET_BOX_Y", amulet.collisionBox().y());
        def("AMULET_BOX_W", amulet.collisionBox().w());
        def("AMULET_BOX_H", amulet.collisionBox().h());
        def("AMULET_PICKUP_FLASH_TICKS", amulet.pickupFlashTicks());
        def("AMULET_FLY_TICKS", amulet.flyToPanelTicks());

        h.append("\n// The way out (§14.4, §14.7) and its Keeper.\n");
        LandmarksData.Exit exit = marks.exit();
        def("EXIT_ROOM", marks.exitRoom().index());
        def("EXIT_ZONE_X", exit.zone().x());
        def("EXIT_ZONE_Y", exit.zone().y());
        def("EXIT_ZONE_W", exit.zone().w());
        def("EXIT_ZONE_H", exit.zone().h());
        def("EXIT_REQUIRES_PIECES", exit.requiresPieces());
        def("ESCAPE_WALK_TICKS", exit.escapeWalkTicks());
        def("KEEPER_X", exit.keeper().x());
        def("KEEPER_Y", exit.keeper().y());
        CreatureData.Box kb = q.keeperSpecies().collisionBox();
        def("KEEPER_BOX_X", kb.x());
        def("KEEPER_BOX_Y", kb.y());
        def("KEEPER_BOX_W", kb.w());
        def("KEEPER_BOX_H", kb.h());
        def("KEEPER_STEP_ASIDE_FP", q.stepAsideFp());
        def("KEEPER_ASIDE_MIN_PX", q.asideMinPx());
        def("KEEPER_ASIDE_MAX_PX", q.asideMaxPx());
        def("KEEPER_NUDGE_ZONE_PX", q.nudgeZonePx());
        def("KEEPER_NUDGE_PX", q.nudgePx());
        String keeper = q.keeperSpecies().sprite();
        for (String f : List.of("stand", "step0", "step1", "aside")) {
            def("KEEPER_FRAME_" + f.toUpperCase(), bank.index(keeper, f));
        }

        h.append("\n// The guardians' orbit (GUARD_ORBIT, §12.5, §14.3).\n");
        def("ORBIT_TICKS_PER_REV", orbit.ticksPerRev());
        def("ORBIT_LUNGE_PX", orbit.lungePx());
        def("ORBIT_LUNGE_TICKS", orbit.lungeTicks());
        def("ORBIT_REPEL_PX", orbit.repelPx());
        def("ORBIT_STUN_TICKS", orbit.stunTicks());
        def("ORBIT_HURT_FLASH_TICKS", orbit.hurtFlashTicks());
        // parryGuardian: the push, and a diagonal one (the player's diagonal scale).
        def("ORBIT_PUSH_FP", Fixed.fp(orbit.repelPx()));
        def("ORBIT_PUSH_DIAG_FP", Fixed.mul(Fixed.fp(orbit.repelPx()), c.player().speed().diagonalScaleFp()));

        h.append("\n// The original's Wulf (§13.8): one, prowling its row.\n");
        WulfData.Patrol p = w.data().patrol();
        CreatureData.Box wb = w.species().collisionBox();
        def("WULF_BOX_X", wb.x());
        def("WULF_BOX_Y", wb.y());
        def("WULF_BOX_W", wb.w());
        def("WULF_BOX_H", wb.h());
        def("WULF_ROOM", w.patrolRoom().index());
        def("WULF_X", p.xPx());
        def("WULF_Y", p.yPx());
        def("WULF_HEADING", p.heading());
        def("WULF_SPEED_FP", p.speedFp());
        def("WULF_RISEN_SPEED_FP", p.risenSpeedFp());
        def("WULF_SIT_RELEASE_TICKS", p.sitReleaseTicks());
        def("WULF_RISE_TICKS", p.riseTicks());
        String ws = w.species().sprite();
        String[] wf = {"walk0", "walk1", "walk0_l", "walk1_l", "howl", "howl_l"};
        for (int i = 0; i < wf.length; i++) {
            def("WULF_FRAME_" + wf[i].toUpperCase(), bank.index(ws, wf[i]));
        }
        if (wb.w() > 41 || wb.h() > 41) {
            throw new ConvertException("data/entities/wulf.json: its box is too big for the ROM's map test");
        }
        // Its step (M13) takes it as free where it stands, as a step ends free or
        // stays put: so where it is placed must be.
        WorldGrid grid = new WorldGrid(c.rooms());
        int cell = 256 / CollisionMask.COLS;
        int left = w.patrolRoom().col() * 256 + p.xPx() + wb.x();
        int top = w.patrolRoom().row() * CollisionMask.ROWS * cell + p.yPx() + wb.y();
        for (int gy = Math.floorDiv(top, cell); gy <= Math.floorDiv(top + wb.h() - 1, cell); gy++) {
            for (int gx = Math.floorDiv(left, cell); gx <= Math.floorDiv(left + wb.w() - 1, cell); gx++) {
                if (grid.isSolid(Math.floorMod(gx, WorldGrid.COLS), Math.floorMod(gy, WorldGrid.ROWS))) {
                    throw new ConvertException("data/entities/wulf.json: its patrol starts in scenery; "
                            + "the ROM's Wulf steps as one free where it stands");
                }
            }
        }

        // Each set's lairs, in landmarks.lairs() order: room, pedestal, the guardian's circle.
        int n = sets.sets().size();
        int[] room = new int[n * lairs.size()];
        int[] px = new int[room.length];
        int[] py = new int[room.length];
        int[] radius = new int[room.length];
        lairRoom = room;
        lairPedX = px;
        lairPedY = py;
        lairOrbit = radius;
        for (int k = 0; k < n; k++) {
            LandmarksData placed = marks.withAmuletSet(k);
            QuestRules fitted = q.withLandmarks(placed);
            for (int i = 0; i < lairs.size(); i++) {
                LandmarksData.Lair lair = placed.lairs().get(i);
                int at = k * lairs.size() + i;
                room[at] = LandmarksData.room(lair.room()).index();
                px[at] = lair.pedestal().x();
                py[at] = lair.pedestal().y();
                radius[at] = fitted.guardianSpecies().get(i).behaviour().param("orbitRadiusPx");
                if (radius[at] > 127) {
                    throw new ConvertException(where + ": a guardian's circle of " + radius[at]
                            + " px; quest.c's MulR takes 127 at most");
                }
            }
        }
        note = "Amulet set k's lair i (landmarks.lairs() order) at k * LAIRS + i: its room, its pedestal "
                + "(room-local feet), its guardian's circle.";
        tables.u8("g_LairRoom", room, note);
        tables.u8("g_LairPedX", px, null);
        tables.u8("g_LairPedY", py, null);
        tables.u8("g_LairOrbit", radius, null);

        // Each lair's piece and guardian.
        int[] mask = new int[lairs.size()];
        int[] gbx = new int[lairs.size()];
        int[] gby = new int[lairs.size()];
        int[] gbw = new int[lairs.size()];
        int[] gbh = new int[lairs.size()];
        int[] gsx = new int[lairs.size()];
        int[] gsy = new int[lairs.size()];
        int[] gdx = new int[lairs.size()];
        int[] gdy = new int[lairs.size()];
        int diagonal = c.originalCreatures().diagonalScaleFp();
        int[] gf = new int[lairs.size() * 4];
        for (int i = 0; i < lairs.size(); i++) {
            LandmarksData.Lair lair = lairs.get(i);
            CreatureData.Species g = q.guardianSpecies().get(i);
            mask[i] = lair.slotMask();
            gbx[i] = g.collisionBox().x();
            gby[i] = g.collisionBox().y();
            gbw[i] = g.collisionBox().w();
            gbh[i] = g.collisionBox().h();
            gsx[i] = g.speed().xFp();
            gsy[i] = g.speed().yFp();
            gdx[i] = Fixed.mul(gsx[i], diagonal);   // SimContext.step on a diagonal
            gdy[i] = Fixed.mul(gsy[i], diagonal);
            if (g.ignoresScenery()) {
                throw new ConvertException("data/entities/guardians.json: '" + g.id() + "' ignores the scenery; "
                        + "the ROM's guardians walk in it");
            }
            String[] f = {"walk0", "walk1", "walk0_l", "walk1_l"};
            for (int j = 0; j < f.length; j++) {
                gf[i * 4 + j] = bank.index(g.sprite(), f[j]);
            }
        }
        note = "Each lair's quarter in the game itself: the panel slots it fills (a later job's are game_data.h's).";
        tables.u8("g_LairMask", mask, note);
        note = "Each lair's guardian: its box, its speed (straight, diagonal), its frames (walk0, walk1, walk0_l, walk1_l).";
        tables.i8("g_GuardBoxX", gbx, note);
        tables.i8("g_GuardBoxY", gby, null);
        tables.u8("g_GuardBoxW", gbw, null);
        tables.u8("g_GuardBoxH", gbh, null);
        tables.i16("g_GuardSpeedX", gsx, null);
        tables.i16("g_GuardSpeedY", gsy, null);
        tables.i16("g_GuardDiagX", gdx, null);
        tables.i16("g_GuardDiagY", gdy, null);
        tables.u8("g_GuardFrame", gf, null);

        // SinTable, a quarter of it: sin(i) for i in 0..64; the rest by symmetry.
        int[] sin = new int[65];
        for (int i = 0; i <= 64; i++) {
            sin[i] = SinTable.sin(i);
        }
        for (int i = 0; i < 256; i++) {
            int a = i & 63;
            int quarter = i >> 6 & 3;
            int v = switch (quarter) {
                case 0 -> sin[a];
                case 1 -> sin[64 - a];
                case 2 -> -sin[a];
                default -> -sin[64 - a];
            };
            if (v != SinTable.sin(i)) {
                throw new ConvertException("SinTable is not symmetric at " + i + "; quest.c folds it");
            }
        }
        note = "SinTable.sin(i), i in 0..64, 8.8; quest.c folds the rest.";
        tables.i16("g_Sin", sin, note);
    }

    private void def(String name, long v) {
        h.append("#define ").append(name).append('\t').append(v < 0 ? "(" + v + ")" : String.valueOf(v)).append('\n');
    }

    private static long privateLong(Class<?> c, String name) {
        try {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            return f.getLong(null);
        } catch (ReflectiveOperationException e) {
            throw new ConvertException(c.getName() + "." + name + ": the Java game does not have it");
        }
    }

    public String header() {
        return h.toString();
    }
}
