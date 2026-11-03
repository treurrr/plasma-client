import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.EmptyBlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.PowderSnowBlock;
import net.minecraft.world.level.block.ScaffoldingBlock;
import net.minecraft.world.level.block.state.BlockBehaviour;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.Property;
import net.minecraft.world.level.material.FlowingFluid;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.level.material.Fluids;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.VoxelShape;

// Asks the game itself for every block state's shapes and movement numbers
// and writes them as text, so the C++ never guesses a shape: tools/oracle/bake_shapes.py.

public final class BlockOracle {
    private static final Map<String, Integer> shapeIds = new HashMap<>();
    private static final List<String> shapes = new ArrayList<>();

    private static String number(double value) {
        if (value == Math.rint(value) && Math.abs(value) < 1e9) return Long.toString((long) value);
        return Double.toString(value);
    }

    private static String coords(VoxelShape shape, Direction.Axis axis) {
        StringBuilder out = new StringBuilder();
        for (double value : shape.getCoords(axis)) {
            if (out.length() > 0) out.append(',');
            out.append(number(value));
        }
        return out.toString();
    }

    // A collision shape the way the game collides with it: the cell edges on
    // each axis and which cells are full, x fastest, then y, then z.
    private static int voxels(VoxelShape shape) {
        if (shape.isEmpty()) return intern("-");
        List<AABB> boxes = shape.toAabbs();
        double[] xs = shape.getCoords(Direction.Axis.X).toDoubleArray();
        double[] ys = shape.getCoords(Direction.Axis.Y).toDoubleArray();
        double[] zs = shape.getCoords(Direction.Axis.Z).toDoubleArray();
        StringBuilder cells = new StringBuilder();
        for (int z = 0; z + 1 < zs.length; ++z) {
            for (int y = 0; y + 1 < ys.length; ++y) {
                for (int x = 0; x + 1 < xs.length; ++x) {
                    double cx = (xs[x] + xs[x + 1]) * 0.5;
                    double cy = (ys[y] + ys[y + 1]) * 0.5;
                    double cz = (zs[z] + zs[z + 1]) * 0.5;
                    boolean full = false;
                    for (AABB box : boxes) {
                        if (box.contains(cx, cy, cz)) {
                            full = true;
                            break;
                        }
                    }
                    cells.append(full ? '1' : '0');
                }
            }
        }
        return intern("v " + coords(shape, Direction.Axis.X) + " " + coords(shape, Direction.Axis.Y) + " "
            + coords(shape, Direction.Axis.Z) + " " + cells);
    }

    private static int boxes(VoxelShape shape) {
        if (shape.isEmpty()) return intern("-");
        StringBuilder out = new StringBuilder("b");
        for (AABB box : shape.toAabbs()) {
            out.append(' ').append(number(box.minX)).append(',').append(number(box.minY)).append(',')
                .append(number(box.minZ)).append(',').append(number(box.maxX)).append(',')
                .append(number(box.maxY)).append(',').append(number(box.maxZ));
        }
        return intern(out.toString());
    }

    private static VoxelShape field(Class<?> owner, String name) throws Exception {
        java.lang.reflect.Field field = owner.getDeclaredField(name);
        field.setAccessible(true);
        return (VoxelShape) field.get(null);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    private static String nameOf(Property property, Object value) {
        return property.getName((Comparable) value);
    }

    private static int intern(String key) {
        return shapeIds.computeIfAbsent(key, k -> {
            shapes.add(k);
            return shapes.size() - 1;
        });
    }

    private static String fluid(BlockState state) {
        FluidState fluid = state.getFluidState();
        if (fluid.isEmpty()) return "-";
        String kind = fluid.getType().isSame(Fluids.WATER) ? "w" : "l";
        boolean falling = fluid.hasProperty(FlowingFluid.FALLING) && fluid.getValue(FlowingFluid.FALLING);
        return kind + fluid.getAmount() + (falling ? "f" : "") + (fluid.isSource() ? "s" : "");
    }

    // Runs of equal values written once with a count: most of a redstone
    // wire's 1296 states share one shape.
    private static String runs(List<String> values) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < values.size();) {
            int j = i;
            while (j < values.size() && values.get(j).equals(values.get(i))) ++j;
            if (out.length() > 0) out.append(',');
            out.append(values.get(i));
            if (j - i > 1) out.append('*').append(j - i);
            i = j;
        }
        return out.toString();
    }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        Method horizontal = BlockBehaviour.class.getDeclaredMethod("getMaxHorizontalOffset");
        Method vertical = BlockBehaviour.class.getDeclaredMethod("getMaxVerticalOffset");
        horizontal.setAccessible(true);
        vertical.setAccessible(true);

        EmptyBlockGetter level = EmptyBlockGetter.INSTANCE;
        BlockPos origin = BlockPos.ZERO;
        TreeMap<String, String> rows = new TreeMap<>();
        for (Block block : BuiltInRegistries.BLOCK) {
            String name = BuiltInRegistries.BLOCK.getKey(block).toString();
            List<BlockState> states = block.getStateDefinition().getPossibleStates();
            int first = Block.getId(states.get(0));
            List<String> collision = new ArrayList<>();
            List<String> outline = new ArrayList<>();
            List<String> interaction = new ArrayList<>();
            List<String> flags = new ArrayList<>();
            List<String> fluids = new ArrayList<>();
            List<String> hardness = new ArrayList<>();
            for (int i = 0; i < states.size(); ++i) {
                BlockState state = states.get(i);
                if (Block.getId(state) != first + i) throw new IllegalStateException(name + ": ids not contiguous");
                Vec3 offset = state.getOffset(origin);
                collision.add(Integer.toString(
                    voxels(state.getCollisionShape(level, origin).move(-offset.x, -offset.y, -offset.z))));
                outline.add(Integer.toString(
                    boxes(state.getShape(level, origin).move(-offset.x, -offset.y, -offset.z))));
                interaction.add(Integer.toString(boxes(state.getInteractionShape(level, origin))));
                int bits = (state.isAir() ? 1 : 0) | (state.hasLargeCollisionShape() ? 2 : 0)
                    | (state.isSuffocating(level, origin) ? 4 : 0) | (state.canBeReplaced() ? 8 : 0)
                    | (state.requiresCorrectToolForDrops() ? 16 : 0)
                    | (state.isCollisionShapeFullBlock(level, origin) ? 32 : 0)
                    | (state.blocksMotion() ? 64 : 0);
                for (Direction direction : Direction.values()) {
                    if (state.isFaceSturdy(level, origin, direction)) bits |= 256 << direction.get3DDataValue();
                }
                flags.add(Integer.toString(bits));
                fluids.add(fluid(state));
                hardness.add(number(state.getDestroySpeed(level, origin)));
            }
            String offset = "-";
            if (states.get(0).hasOffsetFunction()) {
                boolean xyz = false;
                for (int x = 0; x < 64 && !xyz; ++x) {
                    xyz = states.get(0).getOffset(new BlockPos(x, 0, x * 7)).y != 0.0;
                }
                offset = (xyz ? "xyz:" : "xz:") + number((float) horizontal.invoke(block)) + ":"
                    + number((float) vertical.invoke(block));
            }
            StringBuilder properties = new StringBuilder();
            for (Property<?> property : block.getStateDefinition().getProperties()) {
                if (properties.length() > 0) properties.append(';');
                properties.append(property.getName()).append('=');
                boolean firstValue = true;
                for (Object value : property.getPossibleValues()) {
                    if (!firstValue) properties.append('|');
                    firstValue = false;
                    properties.append(nameOf(property, value));
                }
            }
            if (properties.length() == 0) properties.append('-');
            rows.put(name, String.join("\t", name, Integer.toString(BuiltInRegistries.BLOCK.getId(block)),
                Integer.toString(first), Integer.toString(states.size()),
                properties.toString(),
                number(block.getFriction()), number(block.getSpeedFactor()), number(block.getJumpFactor()),
                offset, runs(hardness), runs(flags), runs(fluids), runs(collision), runs(outline),
                runs(interaction)));
        }

        // Shapes the game picks by who is asking rather than by state.
        TreeMap<String, Integer> named = new TreeMap<>();
        named.put("scaffolding_unstable_bottom", voxels(field(ScaffoldingBlock.class, "SHAPE_UNSTABLE_BOTTOM")));
        named.put("powder_snow_falling", voxels(field(PowderSnowBlock.class, "FALLING_COLLISION_SHAPE")));

        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(Path.of(args[0])))) {
            out.println("# Written by tools/oracle/bake_shapes.py from the game itself; do not edit.");
            out.println("# shape <id> <- | v xs ys zs cells | b boxes>");
            out.println("# <block> <registry id> <first state> <states> <properties, first slowest> <friction> <speed> <jump> <offset> <hardness>"
                + " <flags> <fluid> <collision> <outline> <interaction>");
            for (int i = 0; i < shapes.size(); ++i) out.println("shape\t" + i + "\t" + shapes.get(i));
            for (var entry : named.entrySet()) out.println("named\t" + entry.getKey() + "\t" + entry.getValue());
            for (String row : rows.values()) out.println(row);
        }
        System.out.println(String.format(Locale.ROOT, "%d blocks, %d shapes", rows.size(), shapes.size()));
    }
}
