package wulfmsx.convert;

import java.io.IOException;
import java.io.InputStream;
import java.io.UncheckedIOException;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;
import tools.jackson.databind.json.JsonMapper;

/**
 * The game's numbers for the C, as constants.h (AGENTS.md §4, §9 rule 2).
 *
 * <p>The list lives in {@code constants.json} next to this class: a C name and
 * where the number comes from, either {@code "<data file>#<JSON pointer>"}
 * into wulfquest/data/ or {@code "java:<class>.<field>"} for one of the Java
 * game's own static constants (the fixed-point shift). Nothing here holds a
 * number. Content.load has already checked the data by the time this reads
 * it; this only fails on a name it cannot find, or a value that is not a
 * whole number or a true/false.
 */
public final class Constants {

    /** One constant: its C name, where it came from, and its value. */
    public record Entry(String name, String from, long value) {
    }

    private static final ObjectMapper JSON = JsonMapper.builder().build();
    private static final String LIST = "constants.json";

    private final List<Entry> entries;

    public Constants(Path dataDir) {
        this(dataDir, shippedList());
    }

    /** With another list than the shipped one: for the tests. */
    Constants(Path dataDir, JsonNode list) {
        List<Entry> out = new ArrayList<>();
        Set<String> names = new HashSet<>();
        for (JsonNode c : list.get("constants")) {
            String name = c.get("name").asString();
            String from = c.get("from").asString();
            if (!name.matches("[A-Z][A-Z0-9_]*") || !names.add(name)) {
                throw new ConvertException("convert/" + LIST + ": '" + name + "' is not a new C constant name");
            }
            out.add(new Entry(name, from, resolve(dataDir, name, from)));
        }
        this.entries = List.copyOf(out);
    }

    private static JsonNode shippedList() {
        try (InputStream in = Constants.class.getResourceAsStream(LIST)) {
            return JSON.readTree(in);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private static long resolve(Path dataDir, String name, String from) {
        if (from.startsWith("java:")) {
            return javaConstant(name, from.substring("java:".length()));
        }
        int hash = from.indexOf('#');
        if (hash < 0) {
            throw new ConvertException("convert/" + LIST + ": " + name + " comes from '" + from
                    + "', which is not <data file>#<JSON pointer>");
        }
        Path file = dataDir.resolve(from.substring(0, hash) + ".json");
        JsonNode node;
        try {
            node = JSON.readTree(Files.readString(file)).at(from.substring(hash + 1));
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        if (node.isBoolean()) {
            return node.booleanValue() ? 1 : 0;
        }
        if (!node.isIntegralNumber()) {
            throw new ConvertException("convert/" + LIST + ": " + name + " comes from '" + from
                    + "', which is " + (node.isMissingNode() ? "not in the data" : "not a whole number"));
        }
        return node.longValue();
    }

    private static long javaConstant(String name, String ref) {
        int dot = ref.lastIndexOf('.');
        try {
            Field f = Class.forName(ref.substring(0, dot)).getField(ref.substring(dot + 1));
            if (!Modifier.isStatic(f.getModifiers()) || !Modifier.isFinal(f.getModifiers())
                    || (f.getType() != int.class && f.getType() != long.class)) {
                throw new ConvertException("convert/" + LIST + ": " + name + " comes from " + ref
                        + ", which is not a static final whole number");
            }
            return f.getLong(null);
        } catch (ReflectiveOperationException | IndexOutOfBoundsException e) {
            throw new ConvertException("convert/" + LIST + ": " + name + " comes from " + ref
                    + ", which the Java game does not have");
        }
    }

    public List<Entry> entries() {
        return entries;
    }

    public long value(String name) {
        return entries.stream().filter(e -> e.name().equals(name)).findFirst().orElseThrow().value();
    }

    public String header() {
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/ (the list is convert/src/main/resources/wulfmsx/convert/constants.json)"));
        sb.append("#pragma once\n\n");
        for (Entry e : entries) {
            sb.append("#define ").append(e.name()).append('\t')
                    .append(e.value() < 0 ? "(" + e.value() + ")" : String.valueOf(e.value()))
                    .append("\t// ").append(e.from()).append('\n');
        }
        return sb.toString();
    }
}
