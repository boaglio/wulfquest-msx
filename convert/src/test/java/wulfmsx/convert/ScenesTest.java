package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Collections;
import java.util.List;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import wulf.Content;
import wulf.data.ShellConfig;

/**
 * M12: the scenes' words as the converter gives them to the ROM. The credits roll carries
 * shell.json's credits lines, the §2.5 attribution among them, verbatim and in order, then the
 * MSX2's own credit of MSXgl; a line too wide for the screen is broken only at a word.
 */
class ScenesTest {

    static Content content;
    static Scenes scenes;

    @BeforeAll
    static void load() {
        content = Content.load(ConvertTest.DATA);
        VdpPalette palette = VdpPalette.forGame(content);
        SceneryAtlas atlas = new SceneryAtlas(content.scenery(), content.sprites().all(), palette);
        scenes = new Scenes(content, palette, atlas, SceneryAtlas.VRAM_FIRST_LINE + atlas.lines());
    }

    @Test
    void theRollCarriesTheCreditsVerbatimThenMsxgl() {
        List<String> rows = scenes.rollRows();
        List<String> credits = content.shell().credits().lines();
        int at = Collections.indexOfSubList(rows, credits);
        assertThat(at).as("shell.json's credits.lines, in order, unchanged").isGreaterThanOrEqualTo(0);
        List<String> after = rows.subList(at + credits.size(), rows.size());
        assertThat(String.join(" ", after)).contains("MSXGL").contains("BLANCHARD");
        ShellConfig.Roll roll = content.shell().credits().roll();
        assertThat(rows.get(0)).isEqualTo(roll.theEnd());
        assertThat(rows.subList(rows.size() - roll.after().size(), rows.size())).isEqualTo(roll.after());
    }

    @Test
    void theAttributionIsAmongTheCredits() throws java.io.IOException {
        // §2.5 of the Java game's AGENTS.md (its quoted lines), as shell.json carries it: in capitals,
        // across its rows.
        String agents = java.nio.file.Files.readString(ConvertTest.DATA.getParent().resolve("AGENTS.md"));
        String section = agents.substring(agents.indexOf("### 2.5"), agents.indexOf("### 2.6"));
        StringBuilder quote = new StringBuilder();
        for (String line : section.split("\n")) {
            if (line.startsWith("> ")) {
                quote.append(line.substring(2)).append(' ');
            }
        }
        String attribution = quote.toString().trim().replaceAll(" +", " ").toUpperCase(java.util.Locale.ROOT);
        assertThat(attribution).startsWith("WULF QUEST IS AN INDEPENDENT");
        String credits = String.join(" ", content.shell().credits().lines()).replaceAll(" +", " ");
        assertThat(credits).contains(attribution);
    }

    @Test
    void aLineIsBrokenOnlyAtAWord() {
        for (ShellConfig.Line line : content.shell().ending().lines()) {
            for (String row : line.text()) {
                List<String> wrapped = Scenes.wrap(row);
                assertThat(String.join(" ", wrapped)).isEqualTo(row);
                for (String w : wrapped) {
                    assertThat(w.length()).isLessThanOrEqualTo(Scenes.BIG_CHARS);
                }
            }
        }
    }
}
