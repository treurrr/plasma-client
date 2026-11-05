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
import net.minecraft.core.Direction;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;
import net.minecraft.world.level.EmptyBlockGetter;
import net.minecraft.world.phys.shapes.CollisionContext;
import net.minecraft.world.phys.shapes.VoxelShape;

// Where the game's own aim lands: BlockGetter.clip through one block at a
// known place, for clip_probe. One line per ray: whether it hit, the face it
// came in by and the hit point, bit for bit.
public class ClipOracle {
    static final String[] BLOCKS = {
        "minecraft:snow", "minecraft:oak_slab", "minecraft:oak_stairs", "minecraft:oak_fence",
        "minecraft:cobblestone_wall", "minecraft:white_carpet", "minecraft:glass_pane",
        "minecraft:white_bed", "minecraft:hopper", "minecraft:cauldron", "minecraft:stone",
        "minecraft:oak_trapdoor", "minecraft:scaffolding",
    };
    // Snow layers 1, 4 and 8; a bottom, a top and a double slab; stairs facing
    // four ways; a fence with and without arms; and whatever else the block has.
    static final int[] STEPS = {0, 1, 2, 3, 4, 5, 6, 7, 11, 17, 31};
    // A player-sized box walked into the block from every side.
    static final double[][] WALKS = {
        {-0.7, 0.0, 0.5}, {1.7, 0.0, 0.5}, {0.5, 0.45, 0.5}, {0.5, -1.8, 0.5},
        {0.5, 0.0, -0.7}, {0.5, 0.0, 1.7}, {0.2, 0.4, -0.31}, {1.31, 0.5, 0.9},
    };
    static final double[] WALK_DISTANCES = {0.5, -0.5, 0.08, -0.08, 1.2, -1.2};
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

    // The drawn lines of an outline, each axis run merged as far as it goes,
    // so two cuttings of the same shape read the same.
    static String edges(VoxelShape shape) {
        List<double[]> segments = new ArrayList<>();
        shape.forAllEdges(($$0, $$1, $$2, $$3, $$4, $$5) -> segments.add(new double[]{$$0, $$1, $$2, $$3, $$4, $$5}));
        TreeSet<String> out = new TreeSet<>();
        for (int axis = 0; axis < 3; axis++) {
            List<double[]> along = new ArrayList<>();
            for (double[] s : segments) {
                boolean moves = s[axis] != s[axis + 3];
                for (int other = 0; other < 3; other++) {
                    if (other != axis && s[other] != s[other + 3]) moves = false;
                }
                if (moves) along.add(s);
            }
            TreeSet<String> lines = new TreeSet<>();
            for (double[] s : along) {
                int a = (axis + 1) % 3;
                int b = (axis + 2) % 3;
                lines.add(Double.doubleToLongBits(s[a]) + "," + Double.doubleToLongBits(s[b]));
            }
            for (String line : lines) {
                List<double[]> spans = new ArrayList<>();
                for (double[] s : along) {
                    int a = (axis + 1) % 3;
                    int b = (axis + 2) % 3;
                    if (!line.equals(Double.doubleToLongBits(s[a]) + "," + Double.doubleToLongBits(s[b]))) continue;
                    double from = Math.min(s[axis], s[axis + 3]);
                    double to = Math.max(s[axis], s[axis + 3]);
                    spans.add(new double[]{from, to});
                }
                spans.sort((p, q) -> Double.compare(p[0], q[0]));
                double from = Double.NaN;
                double to = Double.NaN;
                for (double[] span : spans) {
                    if (Double.isNaN(from)) {
                        from = span[0];
                        to = span[1];
                    } else if (span[0] <= to) {
                        to = Math.max(to, span[1]);
                    } else {
                        out.add(axis + ":" + line + ":" + Double.doubleToLongBits(from) + "-" + Double.doubleToLongBits(to));
                        from = span[0];
                        to = span[1];
                    }
                }
                if (!Double.isNaN(from)) {
                    out.add(axis + ":" + line + ":" + Double.doubleToLongBits(from) + "-" + Double.doubleToLongBits(to));
                }
            }
        }
        return out.isEmpty() ? "none" : String.join(";", out);
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
                BlockState state = states.get(step);
                System.out.println("edges " + name + "/" + step + " "
                    + edges(state.getShape(EmptyBlockGetter.INSTANCE, BlockPos.ZERO)));
                StringBuilder collide = new StringBuilder();
                VoxelShape solid = state.getCollisionShape(EmptyBlockGetter.INSTANCE, pos).move(pos);
                for (double[] o : WALKS) {
                    double x = pos.getX() + o[0];
                    double y = pos.getY() + o[1];
                    double z = pos.getZ() + o[2];
                    AABB box = new AABB(x - 0.3F, y, z - 0.3F, x + 0.3F, y + 1.8F, z + 0.3F);
                    for (Direction.Axis axis : Direction.Axis.values()) {
                        for (double dist : WALK_DISTANCES) {
                            double r = solid.isEmpty() ? dist : solid.collide(axis, box, dist);
                            if (collide.length() > 0) collide.append(':');
                            collide.append(Double.doubleToLongBits(r));
                        }
                    }
                }
                System.out.println("collide " + name + "/" + step + " " + collide);
                BlockGetter level = new OneBlock(pos, state);
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
