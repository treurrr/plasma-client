import java.lang.reflect.Proxy;
import net.minecraft.SharedConstants;
import net.minecraft.client.color.block.BlockColors;
import net.minecraft.client.renderer.BiomeColors;
import net.minecraft.core.BlockPos;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.BlockAndTintGetter;
import net.minecraft.world.level.ColorResolver;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.RenderShape;
import net.minecraft.world.level.block.state.BlockState;

// Asks the game, state by state, what a block's terrain particles need beyond
// its model, and prints one line per state id:
//   id terrain invisible colour
// terrain: 1 when the block spawns terrain particles at all. invisible: 1 when
// its render shape is INVISIBLE. colour: what BlockColors gives tint index 0 in
// a level: "-" for no colour, a fixed ARGB in hex, or the biome map it reads
// ("grass", "foliage", "dry_foliage", "water"), with "@below" when the map is
// read one block down.
public class ParticleExport {
    static final BlockPos ORIGIN = new BlockPos(0, 64, 0);
    static String asked;
    static int askedY;

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        BlockColors colours = BlockColors.createDefault();
        BlockAndTintGetter level = (BlockAndTintGetter) Proxy.newProxyInstance(
            ParticleExport.class.getClassLoader(), new Class<?>[]{BlockAndTintGetter.class},
            (proxy, method, arguments) -> {
                if (method.getName().equals("getBlockTint")) {
                    ColorResolver resolver = (ColorResolver) arguments[1];
                    askedY = ((BlockPos) arguments[0]).getY();
                    if (resolver == BiomeColors.GRASS_COLOR_RESOLVER) asked = "grass";
                    else if (resolver == BiomeColors.FOLIAGE_COLOR_RESOLVER) asked = "foliage";
                    else if (resolver == BiomeColors.DRY_FOLIAGE_COLOR_RESOLVER) asked = "dry_foliage";
                    else if (resolver == BiomeColors.WATER_COLOR_RESOLVER) asked = "water";
                    else asked = "unknown";
                    return 0;
                }
                if (method.getName().equals("hashCode")) return 0;
                throw new UnsupportedOperationException(method.getName());
            });
        StringBuilder out = new StringBuilder();
        for (int id = 0; id < Block.BLOCK_STATE_REGISTRY.size(); id++) {
            BlockState state = Block.BLOCK_STATE_REGISTRY.byId(id);
            if (state == null) continue;
            asked = null;
            int colour = colours.getColor(state, level, ORIGIN, 0);
            String tint;
            if (asked != null) {
                tint = askedY == ORIGIN.getY() ? asked : asked + "@below";
            } else if (colour == -1) {
                tint = "-";
            } else {
                tint = String.format("%08X", colour);
            }
            out.append(id).append(' ')
                .append(state.shouldSpawnTerrainParticles() ? 1 : 0).append(' ')
                .append(state.getRenderShape() == RenderShape.INVISIBLE ? 1 : 0).append(' ')
                .append(tint).append('\n');
        }
        System.out.print(out);
    }
}
