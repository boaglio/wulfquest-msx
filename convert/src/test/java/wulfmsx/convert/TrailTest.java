package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Random;
import java.util.Set;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.LandmarksData;
import wulf.data.PlayerData;
import wulf.world.RoomAddress;
import wulf.world.Wayfinder;
import wulf.world.WorldGrid;

/**
 * M12: the looking glass's trail as the ROM finds it (Trail) against the Java game's
 * (Wayfinder over the whole jungle, WayPainter's targets), from three places in every room,
 * for every set and every state of the quarters taken.
 */
class TrailTest {

    static Content c;
    static WorldGrid grid;
    static Trail trail;
    static Wayfinder finder;
    static PlayerData.Box feet;

    @BeforeAll
    static void load() {
        c = Content.load(ConvertTest.DATA);
        grid = new WorldGrid(c.rooms());
        feet = c.player().collisionBox();
        trail = new Trail(grid, c.landmarks(), feet);
        finder = new Wayfinder(grid);
    }

    /** WayPainter.cellAt. */
    static int cellAt(RoomAddress room, int x, int y) {
        return finder.nearest(null, Math.floorDiv(room.col() * 256 + x + feet.x(), 8),
                Math.floorDiv(room.row() * 176 + y + feet.y(), 8), Trail.NEAR);
    }

    /** WayPainter.targets. */
    static int[] javaTargets(LandmarksData placed, int done) {
        List<Integer> t = new ArrayList<>();
        for (int i = 0; i < placed.lairs().size(); i++) {
            if ((done & 1 << i) == 0) {
                LandmarksData.Lair l = placed.lairs().get(i);
                t.add(cellAt(LandmarksData.room(l.room()), l.pedestal().x(), l.pedestal().y()));
            }
        }
        if (t.isEmpty()) {
            LandmarksData.Rect z = placed.exit().zone();
            t.add(cellAt(placed.exitRoom(), z.x() + z.w() / 2, z.y() + z.h()));
        }
        return t.stream().mapToInt(Integer::intValue).toArray();
    }

    @Test
    void theTargetsAreTheJavaGamesWithinTheirRooms() {
        LandmarksData marks = c.landmarks();
        int lairs = marks.lairs().size();
        for (int set = 0; set < marks.amuletSets().sets().size(); set++) {
            LandmarksData placed = marks.withAmuletSet(set);
            for (int i = 0; i < lairs; i++) {
                RoomAddress room = LandmarksData.room(placed.lairs().get(i).room());
                int[] mine = trail.targets(set, ((1 << lairs) - 1) & ~(1 << i), room.index()).get(0);
                int java = javaTargets(placed, ((1 << lairs) - 1) & ~(1 << i))[0];
                assertThat(new int[] {room.col() * 32 + mine[0], room.row() * 22 + mine[1]})
                        .containsExactly(Wayfinder.gx(java), Wayfinder.gy(java));
            }
            int[] mine = trail.targets(set, (1 << lairs) - 1, placed.exitRoom().index()).get(0);
            int java = javaTargets(placed, (1 << lairs) - 1)[0];
            assertThat(new int[] {placed.exitRoom().col() * 32 + mine[0], placed.exitRoom().row() * 22 + mine[1]})
                    .containsExactly(Wayfinder.gx(java), Wayfinder.gy(java));
        }
    }

    /**
     * From three places in every room, every set, every state of the quarters taken: the
     * trail ends where the Java game's does, all but a few in a hundred (a quarter as near by
     * another way), shares most of its pixels, and fits the ROM's buffer.
     */
    @Test
    void theTrailIsCloseToTheJavaGames() {
        LandmarksData marks = c.landmarks();
        int lairs = marks.lairs().size();
        int total = 0;
        int ends = 0;
        int empty = 0;
        int longest = 0;
        long shared = 0;
        long javaPx = 0;
        Random r = new Random(1);
        for (int set = 0; set < marks.amuletSets().sets().size(); set++) {
            LandmarksData placed = marks.withAmuletSet(set);
            for (int m = 0; m < 1 << lairs; m++) {
                int[] dist = finder.distances(javaTargets(placed, m));
                for (int room = 0; room < Trail.ROOMS; room++) {
                    for (int k = 0; k < 3; k++) {
                        int gx = (room % 16) * 32 + r.nextInt(32);
                        int gy = (room / 16) * 22 + r.nextInt(22);
                        if (!grid.blockPassable(gx, gy)) {
                            continue;
                        }
                        int from = finder.nearest(dist, gx, gy, Trail.NEAR);
                        List<int[]> jc = new ArrayList<>();
                        for (int cell : finder.path(dist, from)) {
                            jc.add(new int[] {Wayfinder.gx(cell), Wayfinder.gy(cell)});
                        }
                        List<Integer> java = Trail.pixels(jc);
                        if (java.isEmpty()) {
                            continue;
                        }
                        List<Integer> mine = Trail.pixels(trail.route(set, m, room, gx * 8 - feet.x() + 4,
                                gy * 8 - feet.y() + 4, 1 << 16));
                        total++;
                        empty += mine.isEmpty() ? 1 : 0;
                        longest = Math.max(longest, mine.size());
                        Set<Integer> js = new HashSet<>(java);
                        shared += mine.stream().filter(js::contains).count();
                        javaPx += java.size();
                        ends += !mine.isEmpty() && mine.get(mine.size() - 1).equals(java.get(java.size() - 1)) ? 1 : 0;
                    }
                }
            }
        }
        assertThat(empty).as("trails not found of %d", total).isZero();
        assertThat(ends).as("trails ending where the Java game's do, of %d", total).isGreaterThan(total * 99 / 100);
        assertThat(shared).as("pixels shared with the Java game's, of %d", javaPx).isGreaterThan(javaPx * 9 / 10);
        assertThat(longest).as("the longest trail, in pixels").isLessThanOrEqualTo(Trail.MAX_PX);
    }
}
