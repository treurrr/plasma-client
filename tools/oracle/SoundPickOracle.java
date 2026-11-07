import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.reflect.TypeToken;
import java.io.Reader;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import net.minecraft.SharedConstants;
import net.minecraft.client.resources.sounds.Sound;
import net.minecraft.client.resources.sounds.SoundEventRegistration;
import net.minecraft.client.resources.sounds.SoundEventRegistrationSerializer;
import net.minecraft.client.sounds.SoundManager;
import net.minecraft.client.sounds.WeighedSoundEvents;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.util.RandomSource;
import net.minecraft.world.damagesource.DamageEffects;

// Loads the player's own sounds.json through the game's registration code, then
// asks the game's WeighedSoundEvents which variant each (event, seed) picks and
// what an instance played at a volume comes to. The range and the gain at a
// distance follow the sound engine's rule and OpenAL's linear clamped model,
// which the game hands to OpenAL rather than computing itself.
public class SoundPickOracle {
    static final String[] EVENTS = {
        "music.creative", "music.game", "music.menu", "music.nether.basalt_deltas",
        "block.note_block.imitate.creeper", "block.note_block.harp", "ambient.basalt_deltas.additions",
        "ambient.cave", "ambient.crimson_forest.additions", "block.stone.step", "block.wood.break",
        "block.grass.step", "block.anvil.land", "block.bell.use", "entity.zombie.ambient",
        "entity.generic.explode", "ui.button.click", "block.cave_vines.pick_berries",
        "entity.player.levelup", "weather.rain", "music_disc.cat", "entity.ghast.scream",
        "block.portal.ambient", "entity.experience_orb.pickup", "block.hanging_sign.waxed_interact_fail",
        "intentionally_empty", "no.such.event",
    };
    static final long[] SEEDS = {0L, 1L, 42L, -1L, 123456789L, 7046029254386353131L, Long.MIN_VALUE,
        -4490259591351732256L};
    static final float[] VOLUMES = {1.0F, 0.25F, 2.0F, 4.0F, 0.15F};
    static final float[] DISTANCES = {0.0F, 4.0F, 8.0F, 15.5F, 16.0F, 31.0F, 64.0F, 100.0F};

    static int bits(float f) {
        return Float.floatToIntBits(f);
    }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        Path assets = Path.of(System.getProperty("user.home"), ".var/app/com.mojang.Minecraft/.minecraft/assets");
        if (!Files.isDirectory(assets)) assets = Path.of(System.getProperty("user.home"), ".minecraft/assets");
        JsonObject objects;
        try (Reader reader = Files.newBufferedReader(assets.resolve("indexes/29.json"))) {
            objects = JsonParser.parseReader(reader).getAsJsonObject().getAsJsonObject("objects");
        }
        Map<Identifier, Resource> files = new HashMap<>();
        for (String name : objects.keySet()) {
            if (name.startsWith("minecraft/sounds/")) {
                files.put(Identifier.withDefaultNamespace(name.substring("minecraft/".length())), new Resource(null, () -> null));
            }
        }
        String hash = objects.getAsJsonObject("minecraft/sounds.json").get("hash").getAsString();
        Gson gson = new GsonBuilder()
            .registerTypeAdapter(SoundEventRegistration.class, new SoundEventRegistrationSerializer())
            .create();
        Map<String, SoundEventRegistration> registrations;
        try (Reader reader = Files.newBufferedReader(assets.resolve("objects/" + hash.substring(0, 2) + "/" + hash))) {
            registrations = gson.fromJson(reader, new TypeToken<Map<String, SoundEventRegistration>>() {}.getType());
        }

        Class<?> preparationsClass = Class.forName("net.minecraft.client.sounds.SoundManager$Preparations");
        Constructor<?> make = preparationsClass.getDeclaredConstructor();
        make.setAccessible(true);
        Object preparations = make.newInstance();
        Field cache = preparationsClass.getDeclaredField("soundCache");
        cache.setAccessible(true);
        cache.set(preparations, files);
        Method register = preparationsClass.getDeclaredMethod("handleRegistration", Identifier.class, SoundEventRegistration.class);
        register.setAccessible(true);
        for (Map.Entry<String, SoundEventRegistration> entry : registrations.entrySet()) {
            register.invoke(preparations, Identifier.withDefaultNamespace(entry.getKey()), entry.getValue());
        }
        Field registryField = preparationsClass.getDeclaredField("registry");
        registryField.setAccessible(true);
        @SuppressWarnings("unchecked")
        Map<Identifier, WeighedSoundEvents> registry = (Map<Identifier, WeighedSoundEvents>) registryField.get(preparations);

        int n = 0;
        for (String event : EVENTS) {
            for (long seed : SEEDS) {
                float volume = VOLUMES[n++ % VOLUMES.length];
                String key = event + " " + seed;
                WeighedSoundEvents weighed = registry.get(Identifier.withDefaultNamespace(event));
                System.out.println("weight " + key + " " + (weighed == null ? 0 : weighed.getWeight()));
                RandomSource random = RandomSource.create(seed);
                Sound sound = weighed == null ? SoundManager.EMPTY_SOUND : weighed.getSound(random);
                if (sound == SoundManager.EMPTY_SOUND || event.equals("intentionally_empty")) {
                    System.out.println("file " + key + " none");
                    continue;
                }
                float instanceVolume = volume * sound.getVolume().sample(random);
                float instancePitch = 1.0F * sound.getPitch().sample(random);
                float range = Math.max(instanceVolume, 1.0F) * sound.getAttenuationDistance();
                System.out.println("file " + key + " " + sound.getPath());
                System.out.println("stream " + key + " " + sound.shouldStream());
                System.out.println("volume " + key + " " + bits(instanceVolume));
                System.out.println("pitch " + key + " " + bits(instancePitch));
                System.out.println("range " + key + " " + bits(range));
                for (float d : DISTANCES) {
                    float clamped = Math.min(Math.max(d, 0.0F), range);
                    float gain = Math.min(Math.max(instanceVolume, 0.0F), 1.0F) * (1.0F - clamped / range);
                    System.out.println("gain " + key + " " + d + " " + bits(gain));
                }
            }
        }

        // Every DamageEffects by its serialized name and the sound it carries,
        // which is the whole of Player.getHurtSound once the damage type is
        // read. The name is what the damage_type registry writes.
        for (DamageEffects effects : DamageEffects.values()) {
            System.out.println("effect " + effects.getSerializedName() + " "
                + effects.sound().location().getPath());
        }
        // LivingEntity.getVoicePitch, both arms of it, against the game's own
        // random so the two draws and the float arithmetic are the same.
        for (long seed : SEEDS) {
            for (boolean baby : new boolean[] {false, true}) {
                RandomSource random = RandomSource.create(seed);
                float pitch = (random.nextFloat() - random.nextFloat()) * 0.2F + (baby ? 1.5F : 1.0F);
                System.out.println("voicepitch " + seed + " " + baby + " " + bits(pitch));
            }
        }
    }
}
