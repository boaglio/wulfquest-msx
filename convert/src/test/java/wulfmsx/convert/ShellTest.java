package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.ShellConfig;
import wulf.world.CollisionMask;
import wulf.world.RoomAddress;

/**
 * M10: the shell's tables as the converter writes them for the ROM: every string the
 * screens show is shell.json's (or input.json's, or the Java screens' own words), the
 * hi-score table a new machine has is default_highscores.json's, and each room's
 * miniature on the map is its mask as the Java MapScreen samples it.
 */
class ShellTest {

    static Content content;
    static Shell shell;
    static Map<String, Integer> defs = new HashMap<>();
    static byte[] bin;

    @BeforeAll
    static void load() {
        content = Content.load(ConvertTest.DATA);
        shell = new Shell(content, VdpPalette.forGame(content), 840, ConvertTest.DATA);
        Matcher m = Pattern.compile("#define (\\w+)\\t(-?\\d+)\\n").matcher(shell.header());
        while (m.find()) {
            defs.put(m.group(1), Integer.parseInt(m.group(2)));
        }
        bin = shell.blob();
    }

    static String str(String name) {
        int at = defs.get(name);
        return new String(bin, at + 1, bin[at] & 0xFF, StandardCharsets.US_ASCII);
    }

    @Test
    void theStringsAreTheData() {
        ShellConfig s = content.shell();
        assertThat(str("STR_TAGLINE")).isEqualTo(s.title().tagline());
        assertThat(str("STR_FOOTER")).isEqualTo(s.title().footer());
        assertThat(str("STR_MODE_EASY")).isEqualTo(s.difficulty().easy());
        assertThat(str("STR_PAUSE_HEADING")).isEqualTo(s.pause().heading());
        assertThat(str("STR_GAME_OVER")).isEqualTo(s.gameOver().text());
        assertThat(str("STR_PROGRESS_A") + "{percent}" + str("STR_PROGRESS_B")).isEqualTo(s.gameOver().progress());
        assertThat(str("STR_HISCORES")).isEqualTo(s.hiScores().heading());
        for (int v = 0; v < s.amuletReveal().verses().size(); v++) {
            for (int l = 0; l < s.amuletReveal().verses().get(v).size(); l++) {
                assertThat(str("STR_VERSE_" + v + "_" + l)).isEqualTo(s.amuletReveal().verses().get(v).get(l));
            }
        }
        assertThat(str("STR_KEYS_1")).isEqualTo("Q A O P");
    }

    @Test
    void theHiScoresAreTheDefaultTable() {
        var rows = content.defaultScores().ranked(content.shell().hiScores().size()).entries();
        int at = defs.get("HISCORE_DEFAULTS");
        for (int i = 0; i < rows.size(); i++) {
            int r = at + i * Shell.ROW_BYTES;
            assertThat(new String(bin, r, 3, StandardCharsets.US_ASCII)).isEqualTo(rows.get(i).name());
            long score = (bin[r + 3] & 0xFF) | (bin[r + 4] & 0xFF) << 8 | (bin[r + 5] & 0xFF) << 16
                    | (long) (bin[r + 6] & 0xFF) << 24;
            assertThat(score).isEqualTo(rows.get(i).score());
        }
    }

    @Test
    void eachRoomsMiniatureIsItsMaskAsTheMapSamplesIt() {
        int at = defs.get("MAP_MINIS");
        for (int room = 0; room < RoomAddress.GRID_W * RoomAddress.GRID_H; room++) {
            CollisionMask mask = content.rooms().room(new RoomAddress(room % RoomAddress.GRID_W,
                    room / RoomAddress.GRID_W)).mask();
            for (int v = 0; v < Shell.MINI_H; v++) {
                int bits = (bin[at + room * Shell.MINI_BYTES + 2 * v] & 0xFF) << 8
                        | bin[at + room * Shell.MINI_BYTES + 2 * v + 1] & 0xFF;
                for (int u = 0; u < Shell.MINI_W; u++) {
                    boolean open = (bits & (0x8000 >> u)) != 0;
                    assertThat(open).as("room %d, %d,%d", room, u, v)
                            .isEqualTo(!mask.isSolid(u * CollisionMask.COLS / 16, v * CollisionMask.ROWS / 11));
                }
            }
        }
    }

    @Test
    void theDemoIsTheJavaGamesRecordingToTheEnd() {
        assertThat(shell.demoCheck()).startsWith("demo ticks " + defs.get("DEMO_TICKS") + " ");
        assertThat(shell.demoMap()).hasSize(content.display().playfield().w() * content.display().playfield().h());
    }
}
