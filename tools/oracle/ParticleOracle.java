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
import net.minecraft.client.renderer.block.BlockModelShaper;
import net.minecraft.client.renderer.block.BlockRenderDispatcher;
import net.minecraft.client.renderer.block.model.BlockStateModel;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
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
    static final sun.misc.Unsafe U = unsafe();
    static final Map<BlockPos, BlockState> BLOCKS = new HashMap<>();
    static final long MULTIPLIER = 0x5DEECE66DL;
    static final long INVERSE = 0xDFE05BCB1365L;
    static final long MASK = (1L << 48) - 1;

    public static class FakeLevel extends ClientLevel {
        FakeLevel() {
            super(null, null, null, null, 0, 0, null, false, 0L, 0);
        }

        @Override
        public BlockState getBlockState(BlockPos pos) {
            return BLOCKS.getOrDefault(pos, Blocks.AIR.defaultBlockState());
        }

        @Override
        public BlockGetter getChunkForCollisions(int x, int z) {
            return GETTER;
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
            System.out.println("destroy " + id + " " + made.size());
            report(name, made, 9);
            for (int tick = 0; tick < 20; tick++) {
                for (int i = 0; i < made.size(); i++) {
                    Particle p = made.get(i);
                    p.tick();
                    System.out.println("tick " + name + "#" + i + "@" + tick + " " + motion(p));
                }
            }
        }

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
                System.out.println("crack " + Block.getId(state) + "," + face.get3DDataValue() + "," + seed + " " + made.size());
                report(name + "/" + face, made, 9);
            }
            BLOCKS.remove(at);
        }
    }

    static void report(String what, List<Particle> made, int draws) throws Exception {
        for (int i = 0; i < made.size(); i++) {
            Particle p = made.get(i);
            System.out.println("seed " + what + "#" + i + " " + seedOf(p, draws));
            System.out.println("spawn " + what + "#" + i + " " + spawn(p));
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
