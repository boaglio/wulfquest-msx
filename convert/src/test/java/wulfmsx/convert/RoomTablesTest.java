package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.Placement;
import wulf.world.CollisionMask;
import wulf.world.Room;
import wulf.world.RoomAddress;

/** M1: the MSX collides with the same walls and draws the same scenery as the Java game. */
class RoomTablesTest {

    static Content content;
    static SceneryAtlas atlas;
    static RoomTables tables;

    @BeforeAll
    static void load() {
        content = Content.load(ConvertTest.DATA);
        atlas = new SceneryAtlas(content.scenery(), content.sprites().all(), new VdpPalette(content.palette()));
        tables = new RoomTables(content.rooms(), atlas, 8);
    }

    @Test
    void everyRoomsMaskIsRoomBakers() {
        int solid = 0;
        for (int row = 0; row < RoomAddress.GRID_H; row++) {
            for (int col = 0; col < RoomAddress.GRID_W; col++) {
                RoomAddress a = new RoomAddress(col, row);
                CollisionMask mask = content.rooms().room(a).mask();
                for (int cy = 0; cy < CollisionMask.ROWS; cy++) {
                    for (int cx = 0; cx < CollisionMask.COLS; cx++) {
                        assertThat(tables.solid(a, cx, cy)).as("room %s cell %d,%d", a, cx, cy)
                                .isEqualTo(mask.isSolid(cx, cy));
                        solid += mask.isSolid(cx, cy) ? 1 : 0;
                    }
                }
            }
        }
        assertThat(solid).isPositive();
    }

    @Test
    void everyRoomsSceneryIsTheJavaGamesInItsDrawingOrder() {
        for (int row = 0; row < RoomAddress.GRID_H; row++) {
            for (int col = 0; col < RoomAddress.GRID_W; col++) {
                RoomAddress a = new RoomAddress(col, row);
                Room room = content.rooms().room(a);
                List<int[]> got = tables.placements(a);
                assertThat(got).as("room %s", a).hasSameSizeAs(room.placements());
                for (int i = 0; i < got.size(); i++) {
                    Placement p = room.placements().get(i);
                    assertThat(atlas.slots().get(got.get(i)[0]).id()).isEqualTo(p.graphic());
                    assertThat(got.get(i)[1]).isEqualTo(p.x());
                    assertThat(got.get(i)[2]).isEqualTo(p.y());
                }
            }
        }
    }
}
