import java.lang.reflect.Field;
import java.lang.reflect.Proxy;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.SharedConstants;
import net.minecraft.client.Minecraft;
import net.minecraft.client.color.block.BlockColors;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleEngine;
import net.minecraft.client.particle.ParticleResources;
import net.minecraft.client.particle.SingleQuadParticle;
import net.minecraft.client.particle.SpellParticle;
import net.minecraft.client.renderer.block.BlockModelShaper;
import net.minecraft.client.renderer.block.BlockRenderDispatcher;
import net.minecraft.client.renderer.block.model.BlockStateModel;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.particles.ColorParticleOption;
import net.minecraft.core.particles.DustColorTransitionOptions;
import net.minecraft.core.particles.DustParticleOptions;
import net.minecraft.core.particles.ParticleOptions;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.core.particles.SpellParticleOption;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.util.RandomSource;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.AABB;

// The game's own terrain particles for particle_probe: how many a broken block
// throws and where, the bit a dig knocks off each face, and each particle's
// first 20 ticks against a floor and a wall. Every particle's generator is
// seeded from the clock, so the oracle reads the generator after the particle
// is made, winds it back over the draws and prints that seed for the probe to
// use; the level's own generator is seeded here.
public class ParticleOracle {
    static final java.io.PrintStream OUT = System.out;
    static final sun.misc.Unsafe U = unsafe();
    static final Map<BlockPos, BlockState> BLOCKS = new HashMap<>();
    static final long MULTIPLIER = 0x5DEECE66DL;
    static final long INVERSE = 0xDFE05BCB1365L;
    static final long MASK = (1L << 48) - 1;

    static final List<String> REQUESTS = new ArrayList<>();

    public static class FakeLevel extends ClientLevel {
        FakeLevel() {
            super(null, null, null, null, 0, 0, null, false, 0L, 0);
        }

        // What a particle asks the level for as it ticks - a drop's fall, an
        // emitter's explosions - recorded instead of made, so the oracle can
        // show the request without a camera behind it.
        @Override
        public void addParticle(ParticleOptions options, double x, double y, double z,
                                double xd, double yd, double zd) {
            REQUESTS.add(BuiltInRegistries.PARTICLE_TYPE.getKey(options.getType()).getPath() + ":"
                + d(x) + ":" + d(y) + ":" + d(z) + ":" + d(xd) + ":" + d(yd) + ":" + d(zd));
        }

        @Override
        public BlockState getBlockState(BlockPos pos) {
            return BLOCKS.getOrDefault(pos, Blocks.AIR.defaultBlockState());
        }

        @Override
        public BlockGetter getChunkForCollisions(int x, int z) {
            return GETTER;
        }

        @Override
        public void playLocalSound(double x, double y, double z, net.minecraft.sounds.SoundEvent event,
                                   net.minecraft.sounds.SoundSource source, float volume, float pitch,
                                   boolean distance) {
        }

        @Override
        public FluidState getFluidState(BlockPos pos) {
            return getBlockState(pos).getFluidState();
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

    static final BlockGetter GETTER = new BlockGetter() {
        public BlockEntity getBlockEntity(BlockPos pos) { return null; }
        public BlockState getBlockState(BlockPos pos) {
            return BLOCKS.getOrDefault(pos, Blocks.AIR.defaultBlockState());
        }
        public FluidState getFluidState(BlockPos pos) { return getBlockState(pos).getFluidState(); }
        public int getHeight() { return 384; }
        public int getMinY() { return -64; }
    };

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();

        TextureAtlasSprite sprite = (TextureAtlasSprite) U.allocateInstance(TextureAtlasSprite.class);
        set(sprite, "atlasLocation", TextureAtlas.LOCATION_BLOCKS);
        set(sprite, "u0", 0.0F);
        set(sprite, "u1", 1.0F);
        set(sprite, "v0", 0.0F);
        set(sprite, "v1", 1.0F);
        BlockStateModel model = (BlockStateModel) Proxy.newProxyInstance(ParticleOracle.class.getClassLoader(),
            new Class<?>[]{BlockStateModel.class}, (p, m, a) -> m.getName().equals("particleIcon") ? sprite : null);
        BlockModelShaper shaper = (BlockModelShaper) U.allocateInstance(BlockModelShaper.class);
        shaper.replaceCache(new HashMap<BlockState, BlockStateModel>() {
            @Override
            public BlockStateModel get(Object key) { return model; }
        });
        BlockRenderDispatcher dispatcher = (BlockRenderDispatcher) U.allocateInstance(BlockRenderDispatcher.class);
        set(dispatcher, "blockModelShaper", shaper);
        ParticleEngine engine = (ParticleEngine) U.allocateInstance(ParticleEngine.class);
        Queue<Particle> added = new ArrayDeque<>();
        set(engine, "particlesToAdd", added);
        Minecraft minecraft = (Minecraft) U.allocateInstance(Minecraft.class);
        set(minecraft, "blockRenderer", dispatcher);
        set(minecraft, "blockColors", BlockColors.createDefault());
        set(minecraft, "particleEngine", engine);
        Field instance = Minecraft.class.getDeclaredField("instance");
        U.putObject(U.staticFieldBase(instance), U.staticFieldOffset(instance), minecraft);
        FakeLevel level = (FakeLevel) U.allocateInstance(FakeLevel.class);
        set(level, "minecraft", minecraft);

        BlockPos at = new BlockPos(100, 64, 200);
        for (int x = 96; x <= 104; x++) {
            for (int z = 196; z <= 204; z++) BLOCKS.put(new BlockPos(x, 63, z), Blocks.STONE.defaultBlockState());
        }
        for (int y = 64; y <= 66; y++) {
            for (int z = 196; z <= 204; z++) BLOCKS.put(new BlockPos(102, y, z), Blocks.STONE.defaultBlockState());
        }

        String[] names = {"minecraft:stone", "minecraft:oak_slab", "minecraft:oak_fence", "minecraft:torch",
            "minecraft:oak_stairs", "minecraft:spruce_leaves", "minecraft:birch_leaves", "minecraft:grass_block",
            "minecraft:barrier", "minecraft:anvil", "minecraft:cobweb"};
        for (String name : names) {
            BlockState state = BuiltInRegistries.BLOCK.getValue(Identifier.parse(name)).defaultBlockState();
            int id = Block.getId(state);
            added.clear();
            level.addDestroyBlockEffect(at, state);
            List<Particle> made = new ArrayList<>(added);
            OUT.println("destroy " + id + " " + made.size());
            report(name, made, 9);
            for (int tick = 0; tick < 20; tick++) {
                for (int i = 0; i < made.size(); i++) {
                    Particle p = made.get(i);
                    p.tick();
                    OUT.println("tick " + name + "#" + i + "@" + tick + " " + motion(p));
                }
            }
        }

        makeEveryType(level, added);

        String[] dug = {"minecraft:stone", "minecraft:oak_slab", "minecraft:oak_fence", "minecraft:torch"};
        for (String name : dug) {
            BlockState state = BuiltInRegistries.BLOCK.getValue(Identifier.parse(name)).defaultBlockState();
            BLOCKS.put(at, state);
            for (Direction face : Direction.values()) {
                long seed = 1234L + face.get3DDataValue() * 7919L + Block.getId(state);
                set(level, "random", RandomSource.create(seed));
                added.clear();
                level.addBreakingBlockEffect(at, face);
                List<Particle> made = new ArrayList<>(added);
                OUT.println("crack " + Block.getId(state) + "," + face.get3DDataValue() + "," + seed + " " + made.size());
                report(name + "/" + face, made, 9);
            }
            BLOCKS.remove(at);
        }
    }

    // One particle of every type the client makes, each with its own engine
    // generator seed, then twenty ticks of it against the same floor and wall.
    // DRAWS says how often a type's constructors take from the particle's own
    // generator, which is what the seed has to be wound back over.
    static final Object[][] TYPES = {
        {"smoke", 9}, {"large_smoke", 9}, {"white_smoke", 9}, {"ash", 9}, {"white_ash", 9},
        {"dust_plume", 10}, {"campfire_cosy_smoke", 4}, {"campfire_signal_smoke", 4},
        {"flame", 14}, {"small_flame", 14}, {"soul_fire_flame", 14}, {"copper_fire_flame", 14},
        {"poof", 9}, {"explosion", 9}, {"explosion_emitter", 6},
        {"crit", 9}, {"enchanted_hit", 9}, {"damage_indicator", 9},
        {"effect", 8}, {"instant_effect", 8}, {"entity_effect", 8}, {"witch", 8},
        {"infested", 8}, {"raid_omen", 8}, {"trial_omen", 8},
        {"splash", 9}, {"rain", 9}, {"bubble", 7}, {"bubble_column_up", 7}, {"bubble_pop", 2},
        {"note", 7}, {"heart", 7}, {"angry_villager", 7}, {"totem_of_undying", 7},
        {"portal", 5}, {"reverse_portal", 6}, {"dust", 13}, {"dust_color_transition", 16},
        {"dripping_water", 2}, {"falling_water", 3}, {"dripping_lava", 2}, {"falling_lava", 3},
        {"landing_lava", 3}, {"dripping_honey", 2}, {"falling_honey", 3}, {"landing_honey", 3},
        {"falling_nectar", 2}, {"dripping_obsidian_tear", 2}, {"falling_obsidian_tear", 3},
        {"landing_obsidian_tear", 3}, {"dripping_dripstone_water", 2},
        {"falling_dripstone_water", 3}, {"dripping_dripstone_lava", 2},
        {"falling_dripstone_lava", 3},
    };

    static void makeEveryType(ClientLevel level, Queue<Particle> added) throws Exception {
        ParticleResources resources = new ParticleResources();
        @SuppressWarnings("unchecked")
        Map<Identifier, Object> sets = (Map<Identifier, Object>) get(resources, "spriteSets");
        for (Map.Entry<Identifier, Object> entry : sets.entrySet()) {
            int count = spriteCount(entry.getKey());
            List<TextureAtlasSprite> sprites = new ArrayList<>();
            for (int i = 0; i < count; i++) sprites.add(indexSprite(i));
            java.lang.reflect.Method rebind = entry.getValue().getClass().getMethod("rebind", List.class);
            rebind.setAccessible(true);
            rebind.invoke(entry.getValue(), sprites);
        }
        // SpellParticle's static generator is replaced below, so the class
        // has to be initialised first or its own initialiser overwrites it.
        Class.forName(SpellParticle.class.getName(), true, SpellParticle.class.getClassLoader());
        ParticleEngine engine = (ParticleEngine) U.allocateInstance(ParticleEngine.class);
        set(engine, "resourceManager", resources);
        set(engine, "particlesToAdd", added);
        set(engine, "level", level);

        for (Object[] row : TYPES) {
            String name = (String) row[0];
            int draws = (Integer) row[1];
            long engineSeed = 7717L + name.hashCode();
            set(engine, "random", RandomSource.create(engineSeed));
            // SpellParticle's drift comes from a static generator of its own,
            // seeded from the clock; the probe draws it from the engine's, so
            // both are given the same seed here.
            Field spellRandom = SpellParticle.class.getDeclaredField("RANDOM");
            U.putObject(U.staticFieldBase(spellRandom), U.staticFieldOffset(spellRandom),
                RandomSource.create(engineSeed));
            added.clear();
            ParticleOptions options = optionsFor(name);
            engine.createParticle(options, 100.5, 65.3, 200.5, 0.2, 0.3, -0.1);
            List<Particle> made = new ArrayList<>(added);
            OUT.println("make " + name + "," + engineSeed + " " + made.size());
            for (int i = 0; i < made.size(); i++) {
                Particle p = made.get(i);
                OUT.println("seed make/" + name + "#" + i + " " + seedOf(p, draws));
                OUT.println("born make/" + name + "#" + i + " " + born(p));
            }
            for (int tick = 0; tick < 20; tick++) {
                for (int i = 0; i < made.size(); i++) {
                    Particle p = made.get(i);
                    REQUESTS.clear();
                    p.tick();
                    OUT.println("tick make/" + name + "#" + i + "@" + tick + " " + motion(p));
                    OUT.println("emit make/" + name + "#" + i + "@" + tick + " "
                        + (REQUESTS.isEmpty() ? "-" : String.join("|", REQUESTS)));
                }
            }
        }
    }

    // The sprite set's size, read from the particle description the client jar
    // itself ships, so the sprite a type picks is the one the game would.
    static int spriteCount(Identifier id) throws Exception {
        String path = "/assets/" + id.getNamespace() + "/particles/" + id.getPath() + ".json";
        try (java.io.InputStream in = ParticleOracle.class.getResourceAsStream(path)) {
            if (in == null) return 1;
            String text = new String(in.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8);
            int count = 0;
            for (int at = text.indexOf('"'); at >= 0; ) {
                int end = text.indexOf('"', at + 1);
                if (end < 0) break;
                if (!text.startsWith("\"textures\"", at)) count++;
                at = text.indexOf('"', end + 1);
            }
            return Math.max(count, 1);
        }
    }

    // A sprite that says which of the set it is, so both sides can compare the
    // pick without sharing an atlas.
    static TextureAtlasSprite indexSprite(int index) throws Exception {
        TextureAtlasSprite sprite = (TextureAtlasSprite) U.allocateInstance(TextureAtlasSprite.class);
        set(sprite, "atlasLocation", TextureAtlas.LOCATION_PARTICLES);
        set(sprite, "u0", (float) index);
        set(sprite, "u1", (float) index);
        set(sprite, "v0", 0.0F);
        set(sprite, "v1", 1.0F);
        return sprite;
    }

    static ParticleOptions optionsFor(String name) throws Exception {
        switch (name) {
            case "dust":
                return new DustParticleOptions(0xFF4C1A99, 1.5F);
            case "dust_color_transition":
                return new DustColorTransitionOptions(0xFF4C1A99, 0xFF19B2CC, 1.5F);
            case "entity_effect":
                return ColorParticleOption.create(ParticleTypes.ENTITY_EFFECT, 0x80CC3366);
            case "effect":
                return SpellParticleOption.create(ParticleTypes.EFFECT, 0xFF2266CC, 1.3F);
            case "instant_effect":
                return SpellParticleOption.create(ParticleTypes.INSTANT_EFFECT, 0xFFCC2266, 0.7F);
            default:
                return (ParticleOptions) BuiltInRegistries.PARTICLE_TYPE
                    .getValue(Identifier.withDefaultNamespace(name));
        }
    }

    // What a particle looks like the moment it is made, sprite pick and all.
    static String born(Particle p) throws Exception {
        String sprite = "-";
        try {
            TextureAtlasSprite s = (TextureAtlasSprite) get(p, "sprite");
            if (s != null) sprite = Integer.toString((int) (float) (Float) get(s, "u0"));
        } catch (NoSuchFieldException ignored) {
        }
        String size = "-";
        String colour = "-";
        try {
            size = f(((SingleQuadParticle) p).getQuadSize(0.0F));
            colour = f(get(p, "rCol")) + ":" + f(get(p, "gCol")) + ":" + f(get(p, "bCol")) + ":"
                + f(get(p, "alpha"));
        } catch (ClassCastException ignored) {
        }
        AABB bb = p.getBoundingBox();
        return d(get(p, "x")) + ":" + d(get(p, "y")) + ":" + d(get(p, "z")) + ":" + d(get(p, "xd")) + ":"
            + d(get(p, "yd")) + ":" + d(get(p, "zd")) + ":" + get(p, "lifetime") + ":" + size + ":" + colour
            + ":" + sprite + ":" + f(get(p, "gravity")) + ":" + f(get(p, "friction")) + ":"
            + Double.doubleToLongBits(bb.minX) + ":" + Double.doubleToLongBits(bb.maxY);
    }

    static void report(String what, List<Particle> made, int draws) throws Exception {
        for (int i = 0; i < made.size(); i++) {
            Particle p = made.get(i);
            OUT.println("seed " + what + "#" + i + " " + seedOf(p, draws));
            OUT.println("spawn " + what + "#" + i + " " + spawn(p));
        }
    }

    // The seed the particle's generator started from, given how often it was drawn.
    static long seedOf(Particle p, int draws) throws Exception {
        Object random = get(p, "random");
        long state = ((AtomicLong) get(random, "seed")).get();
        for (int i = 0; i < draws; i++) state = ((state - 0xBL) * INVERSE) & MASK;
        return state ^ MULTIPLIER;
    }

    static String spawn(Particle p) throws Exception {
        AABB bb = p.getBoundingBox();
        return d(get(p, "x")) + ":" + d(get(p, "y")) + ":" + d(get(p, "z")) + ":" + d(get(p, "xd")) + ":"
            + d(get(p, "yd")) + ":" + d(get(p, "zd")) + ":" + get(p, "lifetime") + ":" + f(get(p, "quadSize")) + ":"
            + f(get(p, "uo")) + ":" + f(get(p, "vo")) + ":" + f(get(p, "rCol")) + ":" + f(get(p, "gCol")) + ":"
            + f(get(p, "bCol")) + ":" + Double.doubleToLongBits(bb.minX) + ":" + Double.doubleToLongBits(bb.maxY);
    }

    static String motion(Particle p) throws Exception {
        return d(get(p, "x")) + ":" + d(get(p, "y")) + ":" + d(get(p, "z")) + ":" + d(get(p, "xd")) + ":"
            + d(get(p, "yd")) + ":" + d(get(p, "zd")) + ":" + ((Boolean) get(p, "onGround") ? 1 : 0) + ":"
            + (p.isAlive() ? 1 : 0);
    }

    static String d(Object v) { return Long.toString(Double.doubleToLongBits((Double) v)); }
    static String d(double v) { return Long.toString(Double.doubleToLongBits(v)); }
    static String f(Object v) { return Integer.toString(Float.floatToIntBits((Float) v)); }

    static Field field(Class<?> type, String name) throws NoSuchFieldException {
        for (Class<?> c = type; c != null; c = c.getSuperclass()) {
            try {
                Field field = c.getDeclaredField(name);
                field.setAccessible(true);
                return field;
            } catch (NoSuchFieldException ignored) {
            }
        }
        throw new NoSuchFieldException(name);
    }

    static Object get(Object target, String name) throws Exception {
        return field(target.getClass(), name).get(target);
    }

    static void set(Object target, String name, Object value) throws Exception {
        Field f = field(target.getClass(), name);
        if (f.getType() == float.class) U.putFloat(target, U.objectFieldOffset(f), (Float) value);
        else U.putObject(target, U.objectFieldOffset(f), value);
    }

    static sun.misc.Unsafe unsafe() {
        try {
            Field f = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            return (sun.misc.Unsafe) f.get(null);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
