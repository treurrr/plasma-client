import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.renderer.WeatherEffectRenderer;
import net.minecraft.util.Mth;
import net.minecraft.world.level.levelgen.LegacyRandomSource;

// Drives the game's own WeatherEffectRenderer: its column table, the two
// column makers behind extractRenderState, and the quad renderInstances lays
// down. The makers are private and take only numbers, so they are called by
// reflection rather than rebuilt here; the level lookups they would need are
// fed in as the heights and light values the C++ probe uses too.
public class WeatherOracle {
    static final int[][] COLUMNS = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {7, -3}, {-12, 9}, {131, -47},
        {-1000, 1000}, {46341, -46341}, {1234567, 7654321}, {-2147483, 2147483},
    };
    static final int[] TICKS = {0, 1, 7, 100, 1023, 131071, 131072, 524287, 1000000};
    static final float[] PARTIALS = {0.0F, 0.25F, 0.5F, 0.99F};
    static final int[] LIGHTS = {0, 15728880, 983040, 240, 15 << 4 | 7 << 20, 3 << 4 | 11 << 20};

    static int bits(float f) {
        return Float.floatToIntBits(f);
    }

    // The seed extractRenderState gives a column, which is the one piece of it
    // that is pure arithmetic on the column's own position.
    static int columnSeed(int x, int z) {
        return x * x * 3121 + x * 45238971 ^ z * z * 418711 + z * 13761;
    }

    public static void main(String[] args) throws Exception {
        WeatherEffectRenderer renderer = new WeatherEffectRenderer();

        Field sizeX = WeatherEffectRenderer.class.getDeclaredField("columnSizeX");
        Field sizeZ = WeatherEffectRenderer.class.getDeclaredField("columnSizeZ");
        sizeX.setAccessible(true);
        sizeZ.setAccessible(true);
        float[] tableX = (float[]) sizeX.get(renderer);
        float[] tableZ = (float[]) sizeZ.get(renderer);
        for (int i = 0; i < 1024; i++) {
            System.out.println("tablex " + i + " " + bits(tableX[i]));
            System.out.println("tablez " + i + " " + bits(tableZ[i]));
        }

        Class<?> instance = Class.forName("net.minecraft.client.renderer.WeatherEffectRenderer$ColumnInstance");
        Method rain = WeatherEffectRenderer.class.getDeclaredMethod(
            "createRainColumnInstance", net.minecraft.util.RandomSource.class,
            int.class, int.class, int.class, int.class, int.class, int.class, float.class);
        Method snow = WeatherEffectRenderer.class.getDeclaredMethod(
            "createSnowColumnInstance", net.minecraft.util.RandomSource.class,
            int.class, int.class, int.class, int.class, int.class, int.class, float.class);
        rain.setAccessible(true);
        snow.setAccessible(true);
        Method uOffset = instance.getDeclaredMethod("uOffset");
        Method vOffset = instance.getDeclaredMethod("vOffset");
        Method lightCoords = instance.getDeclaredMethod("lightCoords");
        uOffset.setAccessible(true);
        vOffset.setAccessible(true);
        lightCoords.setAccessible(true);

        int n = 0;
        for (int[] column : COLUMNS) {
            int x = column[0];
            int z = column[1];
            int seed = columnSeed(x, z);
            System.out.println("seed " + x + " " + z + " " + seed);
            for (int ticks : TICKS) {
                float partial = PARTIALS[n % PARTIALS.length];
                int light = LIGHTS[n % LIGHTS.length];
                int bottom = -3 + (n % 7) * 11;
                int top = bottom + 1 + (n % 23);
                n++;
                String key = x + " " + z + " " + ticks + " " + partial;

                LegacyRandomSource r = new LegacyRandomSource(seed);
                Object a = rain.invoke(renderer, r, ticks, x, bottom, top, z, light, partial);
                System.out.println("rainu " + key + " " + bits((Float) uOffset.invoke(a)));
                System.out.println("rainv " + key + " " + bits((Float) vOffset.invoke(a)));
                System.out.println("rainl " + key + " " + lightCoords.invoke(a));

                LegacyRandomSource s = new LegacyRandomSource(seed);
                Object b = snow.invoke(renderer, s, ticks, x, bottom, top, z, light, partial);
                System.out.println("snowu " + key + " " + bits((Float) uOffset.invoke(b)));
                System.out.println("snowv " + key + " " + bits((Float) vOffset.invoke(b)));
                System.out.println("snowl " + key + " " + lightCoords.invoke(b));

            }
        }

        // Columns close enough to the camera for the quad pass, which indexes
        // a 32 by 32 table around the camera's own column.
        List<Object> made = new ArrayList<>();
        int m = 0;
        for (int x = -7; x <= 7; x += 3) {
            for (int z = -7; z <= 7; z += 3) {
                int ticks = TICKS[m % TICKS.length];
                float partial = PARTIALS[m % PARTIALS.length];
                int light = LIGHTS[m % LIGHTS.length];
                int bottom = 54 + (m % 7);
                int top = bottom + 1 + (m % 23);
                int seed = columnSeed(x, z);
                m++;
                made.add(rain.invoke(renderer, new LegacyRandomSource(seed), ticks, x, bottom, top, z, light, partial));
                made.add(snow.invoke(renderer, new LegacyRandomSource(seed), ticks, x, bottom, top, z, light, partial));
            }
        }

        // renderInstances, read off a vertex consumer that writes down what it
        // is given instead of filling a buffer.
        Method render = WeatherEffectRenderer.class.getDeclaredMethod(
            "renderInstances", com.mojang.blaze3d.vertex.VertexConsumer.class, List.class,
            net.minecraft.world.phys.Vec3.class, float.class, int.class, float.class);
        render.setAccessible(true);
        double[][] cameras = {
            {0.5, 70.0, 0.5}, {-0.5, 64.3, 0.5}, {3.25, 100.75, -3.5}, {7.5, 61.0, -7.5},
        };
        float[] alphas = {1.0F, 0.8F};
        int[] radii = {5, 10, 15};
        float[] intensities = {1.0F, 0.35F};
        int run = 0;
        for (double[] camera : cameras) {
            for (float alpha : alphas) {
                int radius = radii[run % radii.length];
                float intensity = intensities[run % intensities.length];
                run++;
                Recorder recorder = new Recorder();
                render.invoke(renderer, recorder, made,
                    new net.minecraft.world.phys.Vec3(camera[0], camera[1], camera[2]),
                    alpha, radius, intensity);
                recorder.finish();
                String[] names = {"qx", "qy", "qz", "qu", "qv", "qc", "ql"};
                for (int i = 0; i < recorder.lines.size(); i++) {
                    String[] parts = recorder.lines.get(i).split(" ");
                    for (int f = 0; f < parts.length; f++) {
                        System.out.println(names[f] + " " + run + " " + i + " " + parts[f]);
                    }
                }
            }
        }
    }

    // A VertexConsumer that keeps what it was told, in the order it was told.
    static class Recorder implements com.mojang.blaze3d.vertex.VertexConsumer {
        final List<String> lines = new ArrayList<>();
        private StringBuilder current;

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer addVertex(float x, float y, float z) {
            if (current != null) {
                lines.add(current.toString());
            }
            current = new StringBuilder();
            current.append(bits(x)).append(' ').append(bits(y)).append(' ').append(bits(z));
            return this;
        }

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer setColor(int r, int g, int b, int a) {
            current.append(' ').append(a << 24 | r << 16 | g << 8 | b);
            return this;
        }

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer setColor(int argb) {
            current.append(' ').append(argb);
            return this;
        }

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer setUv(float u, float v) {
            current.append(' ').append(bits(u)).append(' ').append(bits(v));
            return this;
        }

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer setUv1(int u, int v) {
            return this;
        }

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer setUv2(int u, int v) {
            current.append(' ').append(u | v << 16);
            return this;
        }

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer setNormal(float x, float y, float z) {
            return this;
        }

        @Override
        public com.mojang.blaze3d.vertex.VertexConsumer setLineWidth(float width) {
            return this;
        }

        String finish() {
            if (current != null) {
                lines.add(current.toString());
                current = null;
            }
            return "";
        }
    }
}
