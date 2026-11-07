import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.block.ModelBlockRenderer;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.level.BlockAndTintGetter;
import net.minecraft.world.level.ColorResolver;
import net.minecraft.world.level.LightLayer;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.lighting.LevelLightEngine;
import net.minecraft.world.level.material.FluidState;
import org.joml.Vector3f;
import org.joml.Vector3fc;

// Ambient occlusion and smooth lighting for one quad of one block, on the
// neighbourhoods in ao_cases.txt, straight out of the game's own renderer.
// Its storage classes are private, so they are reached by reflection.
public final class AoOracle {
    private static final BlockPos CENTRE = new BlockPos(8, 8, 8);

    static final class Hand implements BlockAndTintGetter {
        final Map<Long, BlockState> blocks = new HashMap<>();
        final Map<Long, int[]> lights = new HashMap<>();
        BlockState air;

        @Override
        public BlockState getBlockState(BlockPos at) {
            BlockState found = this.blocks.get(at.asLong());
            return found == null ? this.air : found;
        }

        @Override
        public FluidState getFluidState(BlockPos at) {
            return this.getBlockState(at).getFluidState();
        }

        @Override
        public BlockEntity getBlockEntity(BlockPos at) {
            return null;
        }

        @Override
        public int getHeight() {
            return 256;
        }

        @Override
        public int getMinY() {
            return 0;
        }

        @Override
        public float getShade(Direction face, boolean shade) {
            if (!shade) {
                return 1.0F;
            }
            return switch (face) {
                case DOWN -> 0.5F;
                case UP -> 1.0F;
                case NORTH, SOUTH -> 0.8F;
                case WEST, EAST -> 0.6F;
            };
        }

        @Override
        public LevelLightEngine getLightEngine() {
            throw new UnsupportedOperationException();
        }

        @Override
        public int getBlockTint(BlockPos at, ColorResolver resolver) {
            return -1;
        }

        @Override
        public int getBrightness(LightLayer layer, BlockPos at) {
            int[] pair = this.lights.get(at.asLong());
            if (pair != null) {
                return layer == LightLayer.BLOCK ? pair[0] : pair[1];
            }
            return layer == LightLayer.BLOCK ? 0 : (this.getBlockState(at).getLightBlock() >= 15 ? 0 : 15);
        }

        @Override
        public int getRawBrightness(BlockPos at, int amount) {
            return Math.max(this.getBrightness(LightLayer.BLOCK, at),
                            this.getBrightness(LightLayer.SKY, at) - amount);
        }
    }

    private static BlockState first(String name) {
        return BuiltInRegistries.BLOCK.getValue(Identifier.parse(name))
            .getStateDefinition().getPossibleStates().get(0);
    }

    private static int[] triple(String text) {
        String[] parts = text.split(",");
        return new int[]{Integer.parseInt(parts[0]), Integer.parseInt(parts[1]),
                         Integer.parseInt(parts[2])};
    }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();

        // The renderer's helpers are private even to its own package, so they
        // are reached the only way left: reflection on the game's own classes.
        Class<?> common = Class.forName(
            "net.minecraft.client.renderer.block.ModelBlockRenderer$CommonRenderStorage");
        Class<?> storageClass = Class.forName(
            "net.minecraft.client.renderer.block.ModelBlockRenderer$AmbientOcclusionRenderStorage");
        java.lang.reflect.Constructor<?> newStorage = storageClass.getDeclaredConstructor();
        newStorage.setAccessible(true);
        Method calculate = storageClass.getDeclaredMethod(
            "calculate", BlockAndTintGetter.class, BlockState.class, BlockPos.class,
            Direction.class, boolean.class);
        calculate.setAccessible(true);
        java.lang.reflect.Field brightnessField = common.getDeclaredField("brightness");
        brightnessField.setAccessible(true);
        java.lang.reflect.Field lightmapField = common.getDeclaredField("lightmap");
        lightmapField.setAccessible(true);
        Method calculateShape = ModelBlockRenderer.class.getDeclaredMethod(
            "calculateShape", BlockAndTintGetter.class, BlockState.class, BlockPos.class,
            net.minecraft.client.renderer.block.model.BakedQuad.class, common);
        calculateShape.setAccessible(true);

        Path cases = Path.of(System.getProperty("oracle.dir", "."), "ao_cases.txt");
        StringBuilder out = new StringBuilder();
        for (String line : Files.readAllLines(cases)) {
            if (line.isBlank() || line.startsWith("#")) {
                continue;
            }
            String[] groups = line.split("\\|", -1);
            String[] head = groups[0].trim().split("\\s+");
            String name = head[0];
            BlockState centre = first(head[1]);
            Direction face = Direction.valueOf(head[2].toUpperCase(java.util.Locale.ROOT));
            int[] from = triple(head[3]);
            int[] to = triple(head[4]);

            Hand level = new Hand();
            level.air = first("minecraft:air");
            level.blocks.put(CENTRE.asLong(), centre);
            if (groups.length > 1 && !groups[1].isBlank()) {
                for (String entry : groups[1].trim().split(";")) {
                    String[] halves = entry.split("=");
                    int[] at = triple(halves[0]);
                    level.blocks.put(CENTRE.offset(at[0], at[1], at[2]).asLong(), first(halves[1]));
                }
            }
            if (groups.length > 2 && !groups[2].isBlank()) {
                for (String entry : groups[2].trim().split(";")) {
                    String[] halves = entry.split("=");
                    int[] at = triple(halves[0]);
                    String[] pair = halves[1].split("/");
                    level.lights.put(CENTRE.offset(at[0], at[1], at[2]).asLong(),
                                     new int[]{Integer.parseInt(pair[0]), Integer.parseInt(pair[1])});
                }
            }

            Vector3fc[] positions = cornerPositions(face, from, to);
            net.minecraft.client.renderer.block.model.BakedQuad quad =
                new net.minecraft.client.renderer.block.model.BakedQuad(
                    positions[0], positions[1], positions[2], positions[3], 0L, 0L, 0L, 0L,
                    -1, face, null, true, 0);

            float[] brightness = new float[4];
            int[] lightmap = new int[4];
            if (centre.getLightEmission() == 0) {
                Object storage = newStorage.newInstance();
                calculateShape.invoke(null, level, centre, CENTRE, quad, storage);
                calculate.invoke(storage, level, centre, CENTRE, face, true);
                System.arraycopy((float[]) brightnessField.get(storage), 0, brightness, 0, 4);
                System.arraycopy((int[]) lightmapField.get(storage), 0, lightmap, 0, 4);
            } else {
                // `ModelBlockRenderer.renderModelFaceFlat`: a block that gives
                // off light is lit flat, from the cell its face looks into.
                int flat = LevelRenderer.getLightColor(LevelRenderer.BrightnessGetter.DEFAULT,
                                                       level, centre, CENTRE.relative(face));
                float shade = level.getShade(face, true);
                for (int i = 0; i < 4; i++) {
                    brightness[i] = shade;
                    lightmap[i] = flat;
                }
            }

            for (int i = 0; i < 4; i++) {
                Vector3fc at = quad.position(i);
                out.append("ao ").append(name).append('.')
                   .append(Math.round(at.x() * 16.0F)).append('_')
                   .append(Math.round(at.y() * 16.0F)).append('_')
                   .append(Math.round(at.z() * 16.0F)).append(' ')
                   .append(Math.round(Math.clamp(brightness[i], 0.0F, 1.0F) * 255.0F)).append(':')
                   .append(lightmap[i] & 0xFF).append(':')
                   .append(lightmap[i] >> 16 & 0xFF).append('\n');
            }
        }
        System.out.print(out);
    }

    // The game's own vertex order for a face: vertex remap.vertK sits at the
    // corner that AdjacencyInfo blends for corner K.
    private static Vector3fc[] cornerPositions(Direction face, int[] from, int[] to)
            throws Exception {
        Class<?> adjacency = Class.forName(
            "net.minecraft.client.renderer.block.ModelBlockRenderer$AdjacencyInfo");
        Method fromFacing = adjacency.getDeclaredMethod("fromFacing", Direction.class);
        fromFacing.setAccessible(true);
        Object info = fromFacing.invoke(null, face);
        java.lang.reflect.Field cornersField = adjacency.getDeclaredField("corners");
        cornersField.setAccessible(true);
        Direction[] corners = (Direction[]) cornersField.get(info);

        Class<?> remapClass = Class.forName(
            "net.minecraft.client.renderer.block.ModelBlockRenderer$AmbientVertexRemap");
        Method remapFrom = remapClass.getDeclaredMethod("fromFacing", Direction.class);
        remapFrom.setAccessible(true);
        Object remap = remapFrom.invoke(null, face);
        int[] vertex = new int[4];
        for (int k = 0; k < 4; k++) {
            java.lang.reflect.Field field = remapClass.getDeclaredField("vert" + k);
            field.setAccessible(true);
            vertex[k] = field.getInt(remap);
        }
        Direction[] first = {corners[3], corners[2], corners[2], corners[3]};
        Direction[] second = {corners[0], corners[0], corners[1], corners[1]};
        Vector3fc[] out = new Vector3fc[4];
        for (int k = 0; k < 4; k++) {
            int[] side = new int[3];
            place(side, face);
            place(side, first[k]);
            place(side, second[k]);
            float[] at = new float[3];
            for (int axis = 0; axis < 3; axis++) {
                at[axis] = (side[axis] == 1 ? to[axis] : from[axis]) / 16.0F;
            }
            out[vertex[k]] = new Vector3f(at[0], at[1], at[2]);
        }
        return out;
    }

    private static void place(int[] side, Direction towards) {
        side[towards.getAxis().ordinal()] =
            towards.getAxisDirection() == Direction.AxisDirection.POSITIVE ? 1 : 0;
    }
}
