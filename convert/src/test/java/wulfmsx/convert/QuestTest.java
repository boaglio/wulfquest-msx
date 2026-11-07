package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.LandmarksData;
import wulf.engine.Fixed;
import wulf.sim.Simulation;
import wulf.world.RoomAddress;
import wulf.world.WorldGrid;

/**
 * M7: each of the eight amulet sets, as the converter writes it for the ROM, puts the
 * quarters where the Java game puts them: every lair's room and pedestal, and the
 * circle its guardian walks there.
 */
class QuestTest {

    static Content content;
    static Quest quest;

    @BeforeAll
    static void load() {
        content = Content.load(ConvertTest.DATA);
        quest = new Quest(content, new RomSprites(content, VdpPalette.forGame(content)), new RamTables());
    }

    @Test
    void everySetHidesTheQuartersWhereTheJavaGameDoes() {
        WorldGrid grid = new WorldGrid(content.rooms());
        int sets = content.questRules().landmarks().amuletSets().sets().size();
        int lairs = content.questRules().landmarks().lairs().size();
        assertThat(quest.lairRoom).hasSize(sets * lairs);
        for (int k = 0; k < sets; k++) {
            long seed = MoveTraces.seedForSet(content, "QuestTest", "set:" + k);
            Simulation sim = Simulation.at(content.player(), grid, 0, Fixed.fp(8), Fixed.fp(8),
                    MoveTraces.quest(content), seed);
            assertThat(sim.landmarks().amuletSet()).isEqualTo(k);
            List<LandmarksData.Lair> placed = sim.landmarks().lairs();
            for (int i = 0; i < lairs; i++) {
                LandmarksData.Lair lair = placed.get(i);
                RoomAddress room = LandmarksData.room(lair.room());
                int at = k * lairs + i;
                String what = "set " + k + ", " + lair.id();
                assertThat(quest.lairRoom[at]).as(what + "'s room").isEqualTo(room.index());
                assertThat(quest.lairPedX[at]).as(what + "'s pedestal x").isEqualTo(lair.pedestal().x());
                assertThat(quest.lairPedY[at]).as(what + "'s pedestal y").isEqualTo(lair.pedestal().y());
                // In the lair at tick 0 the guardian is seated on its ring at sin(64) = 1: its circle to the right.
                int x = room.col() * Simulation.ROOM_W_PX + lair.pedestal().x();
                int y = room.row() * Simulation.ROOM_H_PX + lair.pedestal().y();
                Simulation in = Simulation.at(content.player(), grid, 0, Fixed.fp(x), Fixed.fp(y),
                        MoveTraces.quest(content), seed);
                assertThat(in.quest().inLair()).as(what).isTrue();
                assertThat(in.quest().guardian().xFp() - Fixed.fp(x)).as(what + "'s circle")
                        .isEqualTo(Fixed.fp(quest.lairOrbit[at]));
                assertThat(in.quest().guardian().yFp()).as(what).isEqualTo(Fixed.fp(y));
            }
        }
    }
}
