package wulfmsx.convert;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import wulf.data.AnimationValidator;
import wulf.data.PlayerData;
import wulf.data.SpriteData;
import wulf.engine.Fixed;
import wulf.sim.Simulation;
import wulf.world.CollisionWorld;
import wulf.world.RoomAddress;

/**
 * Ranger Vale as hardware sprites (AGENTS.md §10, M3 and M5): every frame the
 * Java game's {@code PlayerPainter} can show him in while he walks and fights,
 * in his colours, and where he starts.
 *
 * <p>Each frame is 16x24, so it is two 16x16 blocks, one above the other, each
 * two layered sprites ({@link SpriteCut}): {@link #FRAME_BYTES} bytes, the four
 * patterns (upper base, upper overlay, lower base, lower overlay), then their
 * four colour tables. The lower blocks' last 8 lines are empty.
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
    public static final int FRAME_BYTES = 2 * SpriteCut.BLOCK_BYTES;
    private static final int W = 16;
    private static final int H = 24;

    private final List<String> ids = new ArrayList<>();
    private final byte[] blob;
    private final List<SpriteCut.Change> changes = new ArrayList<>();
    private final int[] hand;
    private final int originX;
    private final int originY;
    private final int startXFp;
    private final int startYFp;

    public PlayerSprite(PlayerData rules, SpriteData sprite, VdpPalette vdp,
                        CollisionWorld world, RoomAddress start) {
        String source = "data/art/sprites/" + rules.sprite() + ".sprite.json";
        if (sprite.size().w() != W || sprite.size().h() != H) {
            throw new ConvertException(source + ": Vale is " + sprite.size().w() + "x" + sprite.size().h()
                    + "; the MSX2 draws him as two 16x16 sprites, so he must be " + W + "x" + H);
        }
        Map<String, byte[]> frames = sprite.decode(source);
        Map<String, Map<String, SpriteData.Point>> anchors = sprite.anchors();
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

        ids.addAll(frameIds(rules));
        this.blob = new byte[ids.size() * FRAME_BYTES];
        this.hand = new int[ids.size() * 2];
        for (int i = 0; i < ids.size(); i++) {
            byte[] px = frames.get(ids.get(i));
            if (px == null) {
                throw new ConvertException(source + ": no frame '" + ids.get(i) + "', which the MSX2 shows");
            }
            SpriteCut.Frame f = SpriteCut.cut(vdp, rules.sprite(), ids.get(i), px, W, H);
            System.arraycopy(f.bytes(), 0, blob, i * FRAME_BYTES, FRAME_BYTES);
            changes.addAll(f.changes());
            // The blade comes out of the hand (§11.6); the walking frames have none: -1.
            SpriteData.Point h = anchors.getOrDefault(ids.get(i), Map.of()).get(AnimationValidator.HAND);
            hand[2 * i] = h == null ? -1 : h.x();
            hand[2 * i + 1] = h == null ? -1 : h.y();
        }

        // Where a new game puts him: the Java game's own spot nearest the start room's centre.
        int[] at = startOf(rules, world, start);
        this.startXFp = at[0];
        this.startYFp = at[1];
    }

    /** The frames in the ROM's order: views, then walk, poses, poses on legs. */
    static List<String> frameIds(PlayerData rules) {
        List<String> ids = new ArrayList<>();
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
        return ids;
    }

    static int[] startOf(PlayerData rules, CollisionWorld world, RoomAddress start) {
        Simulation sim = Simulation.startingIn(rules, world, 0, start);
        return new int[] {sim.player().xFp(), sim.player().yFp()};
    }

    public List<String> ids() {
        return List.copyOf(ids);
    }

    public byte[] blob() {
        return blob.clone();
    }

    /** The pixels his frames show in another colour than the art's. */
    public List<SpriteCut.Change> changes() {
        return List.copyOf(changes);
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
        sb.append("// player_frames.bin: PLAYER_FRAME_BYTES a frame: four 16x16 sprite patterns (upper base,\n");
        sb.append("// upper overlay, lower base, lower overlay), then their four colour tables, the overlays'\n");
        sb.append("// with CC (convert/.../SpriteCut.java).\n");
        sb.append("// Frame = view * PLAYER_FRAMES_PER_VIEW + one of: walk gait g (0..3);\n");
        sb.append("// PLAYER_FRAME_POSE + pose p (0..2); PLAYER_FRAME_POSE_GAIT + p * PLAYER_GAIT + g.\n");
        sb.append("// See convert/.../PlayerSprite.java.\n");
        sb.append("#define PLAYER_FRAMES\t\t").append(ids.size()).append('\n');
        sb.append("#define PLAYER_FRAME_BYTES\t").append(FRAME_BYTES).append('\n');
        sb.append("#define PLAYER_SPRITES\t\t4\n");
        sb.append("#define PLAYER_PATTERN_BYTES\t").append(4 * SpriteCut.PATTERN_BYTES).append('\n');
        sb.append("#define PLAYER_FRAMES_PER_VIEW\t").append(PER_VIEW).append('\n');
        sb.append("#define PLAYER_GAIT\t\t").append(GAIT).append('\n');
        sb.append("#define PLAYER_FRAME_POSE\t").append(GAIT).append('\n');
        sb.append("#define PLAYER_FRAME_POSE_GAIT\t").append(GAIT + POSES).append('\n');
        for (int v = 0; v < VIEWS.size(); v++) {
            sb.append("#define PLAYER_VIEW_").append(Gen.cName(VIEWS.get(v))).append('\t').append(v).append('\n');
        }
        sb.append("\n// The sprite's origin, his feet, from its top-left.\n");
        sb.append("#define PLAYER_ORIGIN_X\t\t").append(originX).append('\n');
        sb.append("#define PLAYER_ORIGIN_Y\t\t").append(originY).append('\n');
        sb.append("\n// Where a new game starts him, in world 8.8 fixed point (the Java game's SpawnFinder).\n");
        sb.append("#define PLAYER_START_X_FP\t").append(startXFp).append("L\t// ")
                .append(Fixed.px(startXFp)).append(" px\n");
        sb.append("#define PLAYER_START_Y_FP\t").append(startYFp).append("L\t// ")
                .append(Fixed.px(startYFp)).append(" px\n");
        return sb.toString();
    }

    /** The hand each frame holds the blade in (x, y from the sprite's top-left; -1, -1 none): player_hand.h. */
    public String handHeader() {
        StringBuilder sb = new StringBuilder();
        sb.append(Gen.banner("data/art/sprites/player.sprite.json"));
        sb.append("#pragma once\n\n");
        sb.append("// Where each of Vale's frames (player.h) holds the blade: x, y from the sprite's\n");
        sb.append("// top-left; -1 in the frames that hold none.\n");
        sb.append("static const signed char g_PlayerHand[").append(ids.size()).append(" * 2] = {\n");
        for (int i = 0; i < ids.size(); i++) {
            sb.append("\t").append(hand[2 * i]).append(", ").append(hand[2 * i + 1]).append(",\t// ")
                    .append(ids.get(i)).append('\n');
        }
        sb.append("};\n");
        return sb.toString();
    }
}
