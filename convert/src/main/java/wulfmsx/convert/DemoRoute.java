package wulfmsx.convert;

import java.nio.file.Path;
import java.util.List;
import wulf.Content;
import wulf.world.WorldGrid;

/**
 * The keys tools/record-demo.sh plays into the MSX to record the attract demo there (AGENTS.md
 * §7, M10): a new game of the game as it ships, Hard, from the start, seeded so; QuestRoute's way
 * to the quarters, a swing of the sabre every so many ticks, for as many ticks as the demo lasts.
 *
 * <p>Writes {@code seed S}, {@code start X_FP Y_FP}, then a tick's keys a line (the bits the ROM's
 * input has: up 1, down 2, left 4, right 8, fire 16).
 *
 * <p>Arguments: the data folder, the file to write, the seed, the ticks, the swing's period.
 */
public final class DemoRoute {

    private DemoRoute() {
    }

    public static void main(String[] args) {
        Content c = Content.load(Path.of(args[0]));
        // A seed, or a range ("1-40"): the first seed whose route lasts the whole demo.
        String[] range = args[2].split("-");
        long seed = Long.parseLong(range[0]);
        long last = range.length > 1 ? Long.parseLong(range[1]) : seed;
        int ticks = Integer.parseInt(args[3]);
        int swing = Integer.parseInt(args[4]);
        WorldGrid grid = new WorldGrid(c.rooms());
        int[] at = PlayerSprite.startOf(c.player(), grid, c.map().startRoom());
        List<Integer> keys;
        while (true) {
            MoveTraces.Scenario s = new MoveTraces.Scenario("demo", c.map().startRoom(), false, at[0], at[1],
                    List.of(), "-", seed, MoveTraces.Scenario.GAME, false, false);
            keys = QuestRoute.keys(c, grid, s, ticks, swing);
            if (keys.size() >= ticks || seed >= last) {
                break;
            }
            System.out.println("demo route: seed " + seed + " is over after " + keys.size() + " ticks");
            seed++;
        }
        if (keys.size() < ticks) {
            throw new ConvertException("no seed in " + args[2] + " keeps the demo's game going for " + ticks + " ticks");
        }
        StringBuilder sb = new StringBuilder();
        sb.append("seed ").append(seed).append('\n');
        sb.append("start ").append(at[0]).append(' ').append(at[1]).append('\n');
        for (int k : keys) {
            sb.append(k).append('\n');
        }
        Gen.write(Path.of(args[1]), sb.toString());
        System.out.println("demo route: " + keys.size() + " ticks from the start, seed " + seed);
    }
}
