import com.google.gson.JsonElement;
import com.google.gson.JsonParser;
import com.mojang.serialization.JsonOps;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.SharedConstants;
import net.minecraft.core.Holder;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.attribute.EnvironmentAttribute;
import net.minecraft.world.attribute.EnvironmentAttributeMap;
import net.minecraft.world.attribute.EnvironmentAttributeSystem;
import net.minecraft.world.attribute.EnvironmentAttributes;
import net.minecraft.world.attribute.WeatherAttributes;
import net.minecraft.world.timeline.Timeline;

// The game's own evaluation of the overworld's sky and light at fixed ticks
// and weathers: the overworld's attributes, the plains biome's, the day and
// moon timelines, then rain and thunder, read from the same datapack files
// timeline_probe reads.
public class TimelineOracle {
    static JsonElement read(Path root, String file) throws Exception {
        return JsonParser.parseString(Files.readString(root.resolve(file)));
    }

    // Only the visual attributes: the sound ones name sound events, which need
    // a registry this headless program does not load.
    static EnvironmentAttributeMap attributes(JsonElement body) {
        com.google.gson.JsonObject visual = new com.google.gson.JsonObject();
        body.getAsJsonObject().getAsJsonObject("attributes").entrySet().forEach(entry -> {
            if (entry.getKey().startsWith("minecraft:visual/")) visual.add(entry.getKey(), entry.getValue());
        });
        return EnvironmentAttributeMap.CODEC.parse(JsonOps.INSTANCE, visual).getOrThrow();
    }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        String home = System.getenv("XDG_DATA_HOME");
        Path data = home != null ? Path.of(home) : Path.of(System.getProperty("user.home"), ".local", "share");
        Path root = data.resolve("plasma-client/data/datapack-774");
        EnvironmentAttributeMap overworld = attributes(read(root, "dimension_type/overworld.json"));
        EnvironmentAttributeMap plains = attributes(read(root, "worldgen/biome/plains.json"));
        Timeline day = Timeline.DIRECT_CODEC.parse(JsonOps.INSTANCE, read(root, "timeline/day.json")).getOrThrow();
        Timeline moon = Timeline.DIRECT_CODEC.parse(JsonOps.INSTANCE, read(root, "timeline/moon.json")).getOrThrow();

        long[] ticks = {0, 71, 133, 500, 627, 1000, 6000, 11000, 11867, 12000, 12300, 12500, 13000, 13300,
            13670, 15000, 18000, 21000, 22330, 23000, 23400, 23757, 23999, 30000, 100000, 192000 - 1};
        float[][] weathers = {{0.0F, 0.0F}, {0.4F, 0.0F}, {1.0F, 0.0F}, {1.0F, 0.6F}};
        for (float[] weather : weathers) {
            for (long tick : ticks) {
                long[] clock = {tick};
                EnvironmentAttributeSystem.Builder builder = EnvironmentAttributeSystem.builder();
                builder.addConstantLayer(overworld);
                builder.addConstantLayer(plains);
                builder.addTimelineLayer(Holder.direct(day), () -> clock[0]);
                builder.addTimelineLayer(Holder.direct(moon), () -> clock[0]);
                WeatherAttributes.addBuiltinLayers(builder, new WeatherAttributes.WeatherAccess() {
                    public float rainLevel() { return weather[0]; }
                    public float thunderLevel() { return weather[1]; }
                });
                EnvironmentAttributeSystem system = builder.build();
                String input = tick + "/" + weather[0] + "/" + weather[1];
                colour(system, "sky", EnvironmentAttributes.SKY_COLOR, input);
                colour(system, "fog", EnvironmentAttributes.FOG_COLOR, input);
                colour(system, "cloud", EnvironmentAttributes.CLOUD_COLOR, input);
                colour(system, "sunrise", EnvironmentAttributes.SUNRISE_SUNSET_COLOR, input);
                colour(system, "sky_light", EnvironmentAttributes.SKY_LIGHT_COLOR, input);
                number(system, "sun", EnvironmentAttributes.SUN_ANGLE, input);
                number(system, "moon", EnvironmentAttributes.MOON_ANGLE, input);
                number(system, "stars", EnvironmentAttributes.STAR_ANGLE, input);
                number(system, "star_brightness", EnvironmentAttributes.STAR_BRIGHTNESS, input);
                number(system, "sky_factor", EnvironmentAttributes.SKY_LIGHT_FACTOR, input);
                number(system, "sky_level", EnvironmentAttributes.SKY_LIGHT_LEVEL, input);
                System.out.println("phase " + input + " " + system.getDimensionValue(EnvironmentAttributes.MOON_PHASE).getSerializedName());
            }
        }
    }

    static void colour(EnvironmentAttributeSystem system, String what, EnvironmentAttribute<Integer> attribute, String input) {
        System.out.println(what + " " + input + " " + Integer.toUnsignedString(system.getDimensionValue(attribute)));
    }

    static void number(EnvironmentAttributeSystem system, String what, EnvironmentAttribute<Float> attribute, String input) {
        System.out.println(what + " " + input + " " + Float.floatToIntBits(system.getDimensionValue(attribute)));
    }
}
