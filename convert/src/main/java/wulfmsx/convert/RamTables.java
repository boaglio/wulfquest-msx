package wulfmsx.convert;

import java.io.ByteArrayOutputStream;

/**
 * Tables the ROM copies into RAM once, at start (AGENTS.md §10, M6): the
 * program's segments are full, and a table in a blob costs no program space.
 * {@code tables.bin} holds them one after another; {@code tables.h} names
 * each as a pointer into {@code g_Tables}, so the C reads them as arrays.
 */
final class RamTables {

    private final ByteArrayOutputStream blob = new ByteArrayOutputStream();
    private final StringBuilder names = new StringBuilder();

    /** Bytes, 0..255. */
    void u8(String name, int[] values, String comment) {
        add(name, "u8", values, 1, 0, 255, comment);
    }

    /** Bytes, -128..127. */
    void i8(String name, int[] values, String comment) {
        add(name, "i8", values, 1, -128, 127, comment);
    }

    /** Words, 0..65535, little-endian. */
    void u16(String name, int[] values, String comment) {
        add(name, "u16", values, 2, 0, 65535, comment);
    }

    /** Words, -32768..32767, little-endian. */
    void i16(String name, int[] values, String comment) {
        add(name, "i16", values, 2, -32768, 32767, comment);
    }

    private void add(String name, String type, int[] values, int size, int min, int max, String comment) {
        if (comment != null) {
            names.append("// ").append(comment).append('\n');
        }
        names.append("#define ").append(name).append("\t((const ").append(type).append("*)(g_Tables + ")
                .append(blob.size()).append("))\t// [").append(values.length).append("]\n");
        // Its address, for assembly (M13).
        names.append("#define ").append(name).append("_AT\t(RAM_TABLES + ").append(blob.size()).append(")\n");
        for (int v : values) {
            if (v < min || v > max) {
                throw new ConvertException(name + ": " + v + " does not fit a " + type);
            }
            blob.write(v & 0xFF);
            if (size == 2) {
                blob.write(v >> 8 & 0xFF);
            }
        }
    }

    byte[] blob() {
        return blob.toByteArray();
    }

    String header() {
        return Gen.banner("data/") + "#pragma once\n\n"
                + "// Tables copied from tables.bin into RAM at start (data.c); each a pointer into it.\n"
                + "#define TABLES_BYTES\t" + blob.size() + "\n"
                + "// At its fixed place in RAM (src/ram.h, M11: above where a disk ROM's work area may begin).\n"
                + "#include \"ram.h\"\n"
                + "extern __at(RAM_TABLES) unsigned char g_Tables[TABLES_BYTES];\n\n" + names;
    }
}
