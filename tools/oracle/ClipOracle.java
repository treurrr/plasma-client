import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.level.material.Fluids;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.CollisionContext;

// Where the game's own aim lands: BlockGetter.clip through one block at a
// known place, for clip_probe. One line per ray: whether it hit, the face it
// came in by and the hit point, bit for bit.
public class ClipOracle {
    static final String[] BLOCKS = {
        "minecraft:snow", "minecraft:oak_slab", "minecraft:oak_stairs", "minecraft:oak_fence",
        "minecraft:cobblestone_wall", "minecraft:white_carpet", "minecraft:glass_pane",
        "minecraft:white_bed", "minecraft:hopper", "minecraft:cauldron", "minecraft:stone",
    };
    // Snow layers 1, 4 and 8; a bottom, a top and a double slab; stairs facing
    // four ways; a fence with and without arms; and whatever else the block has.
    static final int[] STEPS = {0, 1, 3, 5, 7, 11, 17, 31};
    static final double[][] RAYS = {
        {0.5, 2.0, 0.5, 0.5, -0.5, 0.5},
        {0.5, -0.5, 0.5, 0.5, 2.0, 0.5},
        {-1.0, 0.0625, 0.5, 2.0, 0.0625, 0.5},
        {-1.0, 0.3, 0.5, 2.0, 0.3, 0.5},
        {-1.0, 0.6, 0.5, 2.0, 0.6, 0.5},
        {-1.0, 0.95, 0.5, 2.0, 0.95, 0.5},
        {0.5, 1.5, -1.0, 0.5, 0.05, 2.0},
        {-0.5, 1.5, -0.5, 1.5, -0.5, 1.5},
        {0.2, 0.05, 0.2, 0.9, 0.9, 0.9},
        {0.5, 0.5, 0.5, 0.5, 3.0, 0.5},
        {0.0, 0.125, -1.0, 0.0, 0.125, 2.0},
        {1.0, 0.5, 1.0, -1.0, 0.5, -1.0},
        {0.5, 3.0, 0.5, 0.5, 2.5, 0.5},
        {-1.0, 1.01, 0.5, 2.0, 0.99, 0.5},
    };

    record OneBlock(BlockPos where, BlockState state) implements BlockGetter {
        @Override
        public BlockEntity getBlockEntity(BlockPos $$0) {
            return null;
        }

        @Override
        public BlockState getBlockState(BlockPos $$0) {
            return $$0.equals(this.where) ? this.state : Blocks.AIR.defaultBlockState();
        }

        @Override
        public FluidState getFluidState(BlockPos $$0) {
            return Fluids.EMPTY.defaultFluidState();
        }

        @Override
        public int getHeight() {
            return 384;
        }

        @Override
        public int getMinY() {
            return -64;
        }
    }

    public static void main(String[] args) {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        BlockPos pos = new BlockPos(3, 64, -7);
        for (String name : BLOCKS) {
            Block block = BuiltInRegistries.BLOCK.getValue(Identifier.parse(name));
            var states = block.getStateDefinition().getPossibleStates();
            for (int step : STEPS) {
                if (step >= states.size()) continue;
                BlockGetter level = new OneBlock(pos, states.get(step));
                for (int r = 0; r < RAYS.length; r++) {
                    double[] ray = RAYS[r];
                    Vec3 from = new Vec3(pos.getX() + ray[0], pos.getY() + ray[1], pos.getZ() + ray[2]);
                    Vec3 to = new Vec3(pos.getX() + ray[3], pos.getY() + ray[4], pos.getZ() + ray[5]);
                    BlockHitResult hit = level.clip(
                        new ClipContext(from, to, ClipContext.Block.OUTLINE, ClipContext.Fluid.NONE,
                                        CollisionContext.empty()));
                    String value;
                    if (hit.getType() == HitResult.Type.MISS) {
                        value = "miss";
                    } else {
                        Vec3 at = hit.getLocation();
                        value = "hit:" + hit.getDirection().ordinal() + ":" + (hit.isInside() ? 1 : 0)
                            + ":" + Double.doubleToLongBits(at.x) + ":" + Double.doubleToLongBits(at.y)
                            + ":" + Double.doubleToLongBits(at.z) + ":" + hit.getBlockPos().getX()
                            + "," + hit.getBlockPos().getY() + "," + hit.getBlockPos().getZ();
                    }
                    System.out.println("clip " + name + "/" + step + "," + r + " " + value);
                }
            }
        }
    }
}
