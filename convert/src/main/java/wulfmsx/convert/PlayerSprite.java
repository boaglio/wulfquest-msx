package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import wulf.data.AnimationValidator;
import wulf.data.Palette;
import wulf.data.PlayerData;
import wulf.data.SpriteData;
import wulf.engine.Fixed;
import wulf.sim.Simulation;
import wulf.world.CollisionWorld;
import wulf.world.RoomAddress;

/**
 * Ranger Vale as hardware sprites, for M3 (AGENTS.md §10): one colour, every
 * frame the Java game's {@code PlayerPainter} can show him in while he walks
 * and fights, and where he starts.
 *
 * <p>Each frame is 16x24, so it is two 16x16 sprites, one above the other: 64
 * bytes, two sprite patterns in the V9938's order (the left 8 columns' 16 rows,
 * then the right 8's), the lower sprite's last 8 rows empty. A pixel is set
 * where the art has a colour other than its black outline: the Java game's own
 * one-colour look (§17.9, {@code inked}), in bright white. Colour comes in M5.
 *
 * <p>The frames go in four views, {@link #VIEWS}, of {@link #PER_VIEW} each:
 * the four walking frames, the three sabre poses, then each pose on each of the
 * four walking legs ({@code side_swing0_walk2}).
 */
public final class PlayerSprite {

    /** The views in frame order: side facing right, side facing left (mirrored), up, down. */
    public static final List<String> VIEWS = List.of("side", "side" + AnimationValidator.MIRROR_SUFFIX, "up", "down");
    public static final int GAIT = 4;
    public static final int POSES = 3;
    public static final int PER_VIEW = GAIT + POSES + POSES * GAIT;
    public static final int FRAME_BYTES = 64;
    private static final int W = 16;
    private static final int H = 24;

    private final List<String> ids = new ArrayList<>();
    private final byte[] blob;
    private final int ink;
    private final int originX;
    private final int originY;
    private final int startXFp;
    private final int startYFp;

    public PlayerSprite(PlayerData rules, SpriteData sprite, Palette palette, VdpPalette vdp,
                        CollisionWorld world, RoomAddress start) {
        String source = "data/art/sprites/" + rules.sprite() + ".sprite.json";
        if (sprite.size().w() != W || sprite.size().h() != H) {
            throw new ConvertException(source + ": Vale is " + sprite.size().w() + "x" + sprite.size().h()
                    + "; the MSX2 draws him as two 16x16 sprites, so he must be " + W + "x" + H);
        }
        Map<String, byte[]> frames = sprite.decode(source);
        int outline = palette.indexOf("black");
        this.ink = vdp.vdpColour(palette.indexOf("brightWhite"));
        this.originX = sprite.origin().x();
        this.originY = sprite.origin().y();

        int ticks = -1;
        for (String view : List.of("side", "up", "down")) {
            PlayerData.Animation walk = rules.animation("walk_" + view);
            PlayerData.Animation swing = rules.animation("swing_" + view);
            if (walk.frames().size() != GAIT || swing.frames().size() != POSES || !swing.sabrePhase()) {
                throw new ConvertException("data/entities/player.json: walk_" + view + " must have " + GAIT
                        + " frames and swing_" + view + " " + POSES + " sabre-phase poses, as the MSX2's frame table does");
            }
            if (ticks >= 0 && walk.ticksPerFrame() != ticks) {
                throw new ConvertException("data/entities/player.json: the walks step at different rates;"
                        + " the MSX2 has one PLAYER_WALK_TICKS_PER_FRAME");
            }
            ticks = walk.ticksPerFrame();
        }

        for (String v : VIEWS) {
            String view = v.replace(AnimationValidator.MIRROR_SUFFIX, "");
            String mirror = v.endsWith(AnimationValidator.MIRROR_SUFFIX) ? AnimationValidator.MIRROR_SUFFIX : "";
            List<String> walk = rules.animation("walk_" + view).frames();
            List<String> swing = rules.animation("swing_" + view).frames();
            for (String f : walk) {
                ids.add(f + mirror);
            }
            for (String f : swing) {
                ids.add(f + mirror);
            }
            for (String f : swing) {
                for (int g = 0; g < GAIT; g++) {
                    ids.add(f + AnimationValidator.GAIT_INFIX + g + mirror);
                }
            }
        }
        this.blob = new byte[ids.size() * FRAME_BYTES];
        for (int i = 0; i < ids.size(); i++) {
            byte[] px = frames.get(ids.get(i));
            if (px == null) {
                throw new ConvertException(source + ": no frame '" + ids.get(i) + "', which the MSX2 shows");
            }
            pack(px, outline, blob, i * FRAME_BYTES);
        }

        // Where a new game puts him: the Java game's own spot nearest the start room's centre.
        int[] at = startOf(rules, world, start);
        this.startXFp = at[0];
        this.startYFp = at[1];
    }

    static int[] startOf(PlayerData rules, CollisionWorld world, RoomAddress start) {
        Simulation sim = Simulation.startingIn(rules, world, 0, start);
        return new int[] {sim.player().xFp(), sim.player().yFp()};
    }

    /** One frame as two 16x16 sprite patterns, at {@code out[at]}. */
    static void pack(byte[] px, int outline, byte[] out, int at) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int c = px[y * W + x];
                if (c < 0 || c == outline) {
                    continue;
                }
                int sprite = y / 16;
                int column = x / 8;
                out[at + sprite * 32 + column * 16 + y % 16] |= (byte) (0x80 >> (x % 8));
            }
        }
    }

    public List<String> ids() {
        return List.copyOf(ids);
    }

    public byte[] blob() {
        return blob.clone();
    }

    public int startXFp() {
        return startXFp;
    }

    public int startYFp() {
        return startYFp;
    }

    public String header() {
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/entities/player.json and data/art/sprites/player.sprite.json"));
        sb.append("#pragma once\n\n");
        sb.append("// player_frames.bin: PLAYER_FRAME_BYTES a frame, two 16x16 sprite patterns, the upper\n");
        sb.append("// sprite first. Frame = view * PLAYER_FRAMES_PER_VIEW + one of: walk gait g (0..3);\n");
        sb.append("// PLAYER_FRAME_POSE + pose p (0..2); PLAYER_FRAME_POSE_GAIT + p * PLAYER_GAIT + g.\n");
        sb.append("// See convert/.../PlayerSprite.java.\n");
        sb.append("#define PLAYER_FRAMES\t\t").append(ids.size()).append('\n');
        sb.append("#define PLAYER_FRAME_BYTES\t").append(FRAME_BYTES).append('\n');
        sb.append("#define PLAYER_FRAMES_PER_VIEW\t").append(PER_VIEW).append('\n');
        sb.append("#define PLAYER_GAIT\t\t").append(GAIT).append('\n');
        sb.append("#define PLAYER_FRAME_POSE\t").append(GAIT).append('\n');
        sb.append("#define PLAYER_FRAME_POSE_GAIT\t").append(GAIT + POSES).append('\n');
        for (int v = 0; v < VIEWS.size(); v++) {
            sb.append("#define PLAYER_VIEW_").append(Gen.cName(VIEWS.get(v))).append('\t').append(v).append('\n');
        }
        sb.append("\n// The sprite's origin, his feet, from its top-left; his one colour (M3).\n");
        sb.append("#define PLAYER_ORIGIN_X\t\t").append(originX).append('\n');
        sb.append("#define PLAYER_ORIGIN_Y\t\t").append(originY).append('\n');
        sb.append("#define PLAYER_INK\t\t").append(ink).append('\n');
        sb.append("\n// Where a new game starts him, in world 8.8 fixed point (the Java game's SpawnFinder).\n");
        sb.append("#define PLAYER_START_X_FP\t").append(startXFp).append("L\t// ")
                .append(Fixed.px(startXFp)).append(" px\n");
        sb.append("#define PLAYER_START_Y_FP\t").append(startYFp).append("L\t// ")
                .append(Fixed.px(startYFp)).append(" px\n");
        return sb.toString();
    }
}
