package wulfmsx.convert;

/**
 * A frame inked as the Java game inks it ({@code Sprite.blitInked}, AGENTS.md
 * §10, M8): the outline colour kept, the bright half of the palette in one
 * colour, the plain half in its plain twin, and an outline drawn where two of
 * the drawing's colours meet. Vale in a flower's colour (§15.2), a tribesman
 * level with a quarter (§12.9). {@code InkTest} holds this to the Java game's own.
 */
final class Ink {

    private Ink() {
    }

    /** The pixels (palette indices, -1 clear), w wide, inked: plain and bright for the colours, keep kept. */
    static byte[] ink(byte[] px, int w, int plain, int bright, int keep) {
        byte[] out = new byte[px.length];
        for (int i = 0; i < px.length; i++) {
            byte v = px[i];
            if (v < 0) {
                out[i] = -1;
                continue;
            }
            int x = i % w;
            int y = i / w;
            boolean seam = v != keep && (seam(px, w, v, x - 1, y, keep) || seam(px, w, v, x, y - 1, keep));
            out[i] = (byte) (v == keep || seam ? keep : v >= 9 ? bright : plain);
        }
        return out;
    }

    private static boolean seam(byte[] px, int w, byte v, int x, int y, int keep) {
        if (x < 0 || y < 0) {
            return false;
        }
        byte n = px[y * w + x];
        return n >= 0 && n != keep && n != v;
    }
}
