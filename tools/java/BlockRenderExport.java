import java.lang.reflect.Method;
import net.minecraft.SharedConstants;
import net.minecraft.client.renderer.ItemBlockRenderTypes;
import net.minecraft.core.BlockPos;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.EmptyBlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.LeavesBlock;
import net.minecraft.world.level.block.state.BlockBehaviour;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FlowingFluid;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.level.material.Fluids;
import net.minecraft.world.phys.Vec3;

// Asks the game, state by state, what the chunk mesher needs to light and
// place a block the way the game does, and prints one line per state id:
//   id layer flags emission light_block shade_bits offset max_h_bits max_v_bits
//      fluid amount
// flags: 1 collision shape is a full block, 2 view blocking, 4 solid render,
// 8 emissive rendering, 16 leaves, 32 can occlude, 64 falling fluid,
// 128 fluid source, 256 half transparent (glass, ice). layer: 0 solid, 1 cutout, 2 translucent, 3 tripwire.
// offset: 0 none, 1 xz, 2 xyz. fluid: 0 none, 1 water, 2 lava.
public class BlockRenderExport {
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        ItemBlockRenderTypes.setCutoutLeaves(true);
        Method horizontal = BlockBehaviour.class.getDeclaredMethod("getMaxHorizontalOffset");
        Method vertical = BlockBehaviour.class.getDeclaredMethod("getMaxVerticalOffset");
        horizontal.setAccessible(true);
        vertical.setAccessible(true);
        EmptyBlockGetter level = EmptyBlockGetter.INSTANCE;
        BlockPos origin = BlockPos.ZERO;
        StringBuilder out = new StringBuilder();
        for (int id = 0; id < Block.BLOCK_STATE_REGISTRY.size(); id++) {
            BlockState state = Block.BLOCK_STATE_REGISTRY.byId(id);
            if (state == null) continue;
            int layer = ItemBlockRenderTypes.getChunkRenderType(state).ordinal();
            FluidState fluid = state.getFluidState();
            if (!fluid.isEmpty()) {
                int fluidLayer = ItemBlockRenderTypes.getRenderLayer(fluid).ordinal();
                if (state.getBlock() instanceof net.minecraft.world.level.block.LiquidBlock) layer = fluidLayer;
            }
            int flags = 0;
            if (state.isCollisionShapeFullBlock(level, origin)) flags |= 1;
            if (state.isViewBlocking(level, origin)) flags |= 2;
            if (state.isSolidRender()) flags |= 4;
            if (state.emissiveRendering(level, origin)) flags |= 8;
            if (state.getBlock() instanceof LeavesBlock) flags |= 16;
            if (state.canOcclude()) flags |= 32;
            if (state.getBlock() instanceof net.minecraft.world.level.block.HalfTransparentBlock) flags |= 256;
            int fluidType = 0;
            if (fluid.getType().isSame(Fluids.WATER)) fluidType = 1;
            if (fluid.getType().isSame(Fluids.LAVA)) fluidType = 2;
            if (fluidType != 0 && !fluid.isSource() && fluid.getValue(FlowingFluid.FALLING)) flags |= 64;
            if (fluidType != 0 && fluid.isSource()) flags |= 128;
            int offset = 0;
            if (state.hasOffsetFunction()) {
                offset = 1;
                for (int x = 0; x < 64 && offset == 1; x++) {
                    Vec3 shift = state.getOffset(new BlockPos(x, 0, x * 7));
                    if (shift.y != 0.0) offset = 2;
                }
            }
            float maxH = (Float) horizontal.invoke(state.getBlock());
            float maxV = (Float) vertical.invoke(state.getBlock());
            out.append(id).append(' ').append(layer).append(' ').append(flags).append(' ')
                .append(state.getLightEmission()).append(' ').append(state.getLightBlock()).append(' ')
                .append(Float.floatToIntBits(state.getShadeBrightness(level, origin))).append(' ')
                .append(offset).append(' ').append(Float.floatToIntBits(maxH)).append(' ')
                .append(Float.floatToIntBits(maxV)).append(' ').append(fluidType).append(' ')
                .append(fluid.getAmount()).append('\n');
        }
        System.out.print(out);
    }
}
