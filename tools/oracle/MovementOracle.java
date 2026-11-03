import java.lang.reflect.Method;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.EmptyBlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.VoxelShape;

// What the game's own movement pieces return for fixed inputs, for
// movement_probe: the mouse curve into Entity.turn, the push of the keys
// (getInputVector), and a moving box against block shapes (VoxelShape.collide).
public class MovementOracle {
    static final String[] BLOCKS = {
        "minecraft:stone", "minecraft:oak_slab", "minecraft:oak_stairs", "minecraft:oak_fence",
        "minecraft:cobblestone_wall", "minecraft:oak_door", "minecraft:oak_trapdoor",
        "minecraft:snow", "minecraft:white_carpet", "minecraft:soul_sand", "minecraft:honey_block",
        "minecraft:chest", "minecraft:hopper", "minecraft:cauldron", "minecraft:anvil",
        "minecraft:bamboo", "minecraft:ladder", "minecraft:glass_pane", "minecraft:lectern",
        "minecraft:scaffolding",
    };
    static final int[] STEPS = {0, 1, 5, 11, 17, 29};
    static final double[] DISTANCES = {0.5, -0.5, 0.08, -0.08, 1.2, -1.2, 3.9, -3.9};
    static final double[][] OFFSETS = {
        {-0.7, 0.0, 0.5}, {1.7, 0.0, 0.5}, {0.5, 1.0, 0.5}, {0.5, -1.8, 0.5},
        {0.5, 0.0, -0.7}, {0.5, 0.0, 1.7}, {0.2, 0.4, -0.31}, {1.31, 0.5, 0.9},
    };

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();

        // The mouse: sensitivity, a delta in pixels, the yaw and pitch after turning.
        double[] sensitivities = {0.0, 0.25, 0.5, 0.75, 1.0, 0.123};
        double[] pixels = {1.0, -1.0, 3.0, 17.5, -250.0, 0.25};
        sun.misc.Unsafe unsafe = unsafe();
        for (double s : sensitivities) {
            for (double p : pixels) {
                Entity e = (Entity) unsafe.allocateInstance(net.minecraft.world.entity.decoration.ArmorStand.class);
                double d = s * 0.6F + 0.2F;
                double scale = d * d * d * 8.0;
                e.turn(p * scale, p * 0.5 * scale);
                System.out.println("turn " + s + "," + p + " " + Float.floatToIntBits(e.getYRot()) + ":"
                    + Float.floatToIntBits(e.getXRot()));
            }
        }

        // The push of the keys.
        Method input = Entity.class.getDeclaredMethod("getInputVector", Vec3.class, float.class, float.class);
        input.setAccessible(true);
        float[] speeds = {0.1F, 0.13F, 0.02F, 0.025999999F, 0.1F * 0.21600002F / (0.98F * 0.98F * 0.98F)};
        float[] yaws = {0.0F, 45.0F, -90.0F, 123.456F, 3600.25F, -0.001F};
        double[][] inputs = {{0.98, 0.0, 0.0}, {0.0, 0.0, 0.98}, {0.70710677, 0.0, 0.70710677}, {-0.294, 0.0, 0.294}, {1.0, 0.0, 1.0}};
        for (float speed : speeds) {
            for (float yaw : yaws) {
                for (double[] in : inputs) {
                    Vec3 v = (Vec3) input.invoke(null, new Vec3(in[0], in[1], in[2]), speed, yaw);
                    System.out.println("input " + speed + "," + yaw + " " + Double.doubleToLongBits(v.x) + ":"
                        + Double.doubleToLongBits(v.y) + ":" + Double.doubleToLongBits(v.z));
                }
            }
        }

        // A player-sized box moving into one block at (1000, 64, -2000).
        BlockPos pos = new BlockPos(1000, 64, -2000);
        for (String name : BLOCKS) {
            Block block = BuiltInRegistries.BLOCK.getValue(Identifier.parse(name));
            var states = block.getStateDefinition().getPossibleStates();
            for (int step : STEPS) {
                if (step >= states.size()) continue;
                BlockState state = states.get(step);
                VoxelShape shape = state.getCollisionShape(EmptyBlockGetter.INSTANCE, pos).move(pos);
                for (double[] o : OFFSETS) {
                    double x = pos.getX() + o[0];
                    double y = pos.getY() + o[1];
                    double z = pos.getZ() + o[2];
                    AABB box = new AABB(x - 0.3F, y, z - 0.3F, x + 0.3F, y + 1.8F, z + 0.3F);
                    for (Direction.Axis axis : Direction.Axis.values()) {
                        for (double dist : DISTANCES) {
                            double r = shape.isEmpty() ? dist : shape.collide(axis, box, dist);
                            System.out.println("collide " + name + "/" + step + "," + axis + "," + dist + " "
                                + Double.doubleToLongBits(r));
                        }
                    }
                }
            }
        }
    }

    static sun.misc.Unsafe unsafe() throws Exception {
        java.lang.reflect.Field f = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
        f.setAccessible(true);
        return (sun.misc.Unsafe) f.get(null);
    }
}
