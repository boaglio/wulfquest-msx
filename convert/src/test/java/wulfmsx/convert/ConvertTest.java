package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import wulf.data.DataException;

/** The whole converter, from the data to build/gen/ (AGENTS.md §10, M1). */
class ConvertTest {

    static final Path DATA = Path.of("wulfquest/data");

    @Test
    void writesEveryFileTheCBuildReads(@TempDir Path out) {
        String report = Convert.run(DATA, out);
        for (String f : List.of("palette.h", "constants.h", "scenery.h", "rooms.h", "rooms.bin",
                "scenery_0.bin", "report.txt")) {
            assertThat(out.resolve(f)).as(f).isNotEmptyFile();
        }
        assertThat(report).contains("ROM: 256 KB").contains("free");
    }

    @Test
    void theDataFitsInTheRomWithRoomToSpare(@TempDir Path out) {
        Convert.run(DATA, out);
        // M1: "fits in a 256 KB ROM with room to spare": at least half the data segments free.
        String report = read(out.resolve("report.txt"));
        int free = Integer.parseInt(report.replaceAll("(?s).*; (\\d+) segments \\(.*", "$1"));
        assertThat(free).isGreaterThanOrEqualTo((Budget.ROM_SEGMENTS - Budget.CODE_SEGMENTS) / 2);
    }

    @Test
    void aDataErrorStopsTheConversionWithTheJavaGamesMessage(@TempDir Path tmp) throws IOException {
        Path data = copyData(tmp);
        Path player = data.resolve("entities/player.json");
        Files.writeString(player, Files.readString(player).replace("\"xFp\": 768", "\"xFp\": \"fast\""));
        assertThatThrownBy(() -> Convert.run(data, tmp.resolve("gen")))
                .isInstanceOf(DataException.class)
                .hasMessageContaining("player");
    }

    /** A scratch copy of wulfquest/data/, to change without touching the submodule. */
    static Path copyData(Path tmp) throws IOException {
        Path to = tmp.resolve("data");
        try (Stream<Path> all = Files.walk(DATA)) {
            for (Path p : (Iterable<Path>) all::iterator) {
                Path q = to.resolve(DATA.relativize(p).toString());
                if (Files.isDirectory(p)) {
                    Files.createDirectories(q);
                } else {
                    Files.copy(p, q);
                }
            }
        }
        return to;
    }

    private static String read(Path p) {
        try {
            return Files.readString(p);
        } catch (IOException e) {
            throw new java.io.UncheckedIOException(e);
        }
    }
}
