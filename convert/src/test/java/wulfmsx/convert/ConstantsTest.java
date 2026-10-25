package wulfmsx.convert;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import wulf.Content;

/** AGENTS.md §4: every constant comes from the data, and follows it. */
class ConstantsTest {

    @Test
    void everyConstantHasASourceThatResolves() {
        Constants c = new Constants(ConvertTest.DATA);
        assertThat(c.entries()).isNotEmpty();
        for (Constants.Entry e : c.entries()) {
            assertThat(e.from()).as(e.name()).matches("java:.+|[a-z_/]+#/.+");
        }
        assertThat(c.value("TICK_HZ")).isEqualTo(Content.load(ConvertTest.DATA).game().tickHz());
        assertThat(c.value("FIXED_SHIFT")).isEqualTo(wulf.engine.Fixed.SHIFT);
    }

    @Test
    void changingASpeedInTheDataChangesTheConstant(@TempDir Path tmp) throws IOException {
        Path data = ConvertTest.copyData(tmp);
        Path player = data.resolve("entities/player.json");
        Files.writeString(player, Files.readString(player).replace("\"xFp\": 768", "\"xFp\": 640"));
        Content.load(data);
        assertThat(new Constants(ConvertTest.DATA).value("PLAYER_SPEED_X_FP")).isEqualTo(768);
        assertThat(new Constants(data).value("PLAYER_SPEED_X_FP")).isEqualTo(640);
        assertThat(new Constants(data).header()).contains("#define PLAYER_SPEED_X_FP\t640\t");
    }

    @Test
    void negativeValuesAreParenthesised() {
        assertThat(new Constants(ConvertTest.DATA).header()).contains("#define PLAYER_BOX_X\t(-5)\t");
    }

    @Test
    void aSourceNotInTheDataFails() {
        assertThatThrownBy(() -> new Constants(ConvertTest.DATA, list("NOPE", "entities/player#/no/such")))
                .isInstanceOf(ConvertException.class).hasMessageContaining("not in the data");
        assertThatThrownBy(() -> new Constants(ConvertTest.DATA, list("NOPE", "entities/player#/displayName")))
                .isInstanceOf(ConvertException.class).hasMessageContaining("not a whole number");
        assertThatThrownBy(() -> new Constants(ConvertTest.DATA, list("NOPE", "java:wulf.engine.Fixed.NOPE")))
                .isInstanceOf(ConvertException.class).hasMessageContaining("does not have");
        assertThatThrownBy(() -> new Constants(ConvertTest.DATA, list("lower", "config/game#/tickHz")))
                .isInstanceOf(ConvertException.class).hasMessageContaining("not a new C constant name");
    }

    private static JsonNode list(String name, String from) {
        return JsonMapper.builder().build().readTree(
                "{\"constants\": [{\"name\": \"" + name + "\", \"from\": \"" + from + "\"}]}");
    }
}
