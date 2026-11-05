import java.lang.reflect.Field;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import net.minecraft.SharedConstants;
import net.minecraft.client.player.RemotePlayer;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Holder;
import net.minecraft.core.NonNullList;
import net.minecraft.core.RegistryAccess;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.Bootstrap;
import net.minecraft.server.packs.PackType;
import net.minecraft.server.packs.repository.ServerPacksSource;
import net.minecraft.server.packs.resources.MultiPackResourceManager;
import net.minecraft.tags.FluidTags;
import net.minecraft.tags.TagKey;
import net.minecraft.tags.TagLoader;
import net.minecraft.world.effect.MobEffect;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.ai.attributes.AttributeMap;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.EmptyBlockGetter;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;

// How far each tick of digging gets, from the game's own getDestroyProgress
// with a player holding an item under effects, for dig_probe: block, item,
// haste, conduit power, mining fatigue (levels), the mining efficiency
// attribute, eye in water and on the ground, then every tick's sum until the
// block breaks. The block tags come first, as the server would send them.
public class DigOracle {
    static final java.io.PrintStream OUT = System.out;
    static final sun.misc.Unsafe U = unsafe();

    record Case(String block, String item, int haste, int conduit, int fatigue, double efficiency,
                boolean water, boolean ground) {}

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        var resources = new MultiPackResourceManager(PackType.SERVER_DATA,
            List.of(ServerPacksSource.createVanillaPackSource()));
        TagLoader.loadTagsForExistingRegistries(resources, RegistryAccess.fromRegistryOfRegistries(BuiltInRegistries.REGISTRY))
            .forEach(pending -> pending.apply());
        BuiltInRegistries.BLOCK.getTags().forEach(set -> {
            String ids = set.stream().map(h -> Integer.toString(BuiltInRegistries.BLOCK.getId(h.value())))
                .collect(Collectors.joining(","));
            OUT.println("tag " + set.key().location() + " " + (ids.isEmpty() ? "-" : ids));
        });

        Case[] cases = {
            new Case("minecraft:stone", "minecraft:air", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:stone", "minecraft:wooden_pickaxe", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:stone", "minecraft:iron_pickaxe", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:stone", "minecraft:iron_pickaxe", 0, 0, 0, 26.0, false, true),
            new Case("minecraft:stone", "minecraft:diamond_pickaxe", 2, 0, 0, 0.0, false, true),
            new Case("minecraft:stone", "minecraft:iron_pickaxe", 0, 1, 0, 0.0, true, true),
            new Case("minecraft:stone", "minecraft:iron_pickaxe", 0, 0, 0, 0.0, false, false),
            new Case("minecraft:obsidian", "minecraft:diamond_pickaxe", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:obsidian", "minecraft:iron_pickaxe", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:dirt", "minecraft:iron_shovel", 0, 0, 1, 0.0, false, true),
            new Case("minecraft:dirt", "minecraft:air", 0, 0, 0, 0.0, true, false),
            new Case("minecraft:oak_log", "minecraft:stone_axe", 0, 0, 2, 0.0, false, true),
            new Case("minecraft:oak_log", "minecraft:golden_axe", 0, 0, 0, 5.0, false, true),
            new Case("minecraft:cobweb", "minecraft:iron_sword", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:cobweb", "minecraft:shears", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:bamboo", "minecraft:iron_sword", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:oak_leaves", "minecraft:shears", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:deepslate", "minecraft:netherite_pickaxe", 3, 0, 0, 37.0, false, true),
            new Case("minecraft:iron_ore", "minecraft:stone_pickaxe", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:diamond_ore", "minecraft:stone_pickaxe", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:bedrock", "minecraft:diamond_pickaxe", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:torch", "minecraft:air", 0, 0, 0, 0.0, false, true),
            new Case("minecraft:sand", "minecraft:wooden_shovel", 0, 0, 4, 0.0, false, true),
            new Case("minecraft:netherrack", "minecraft:copper_pickaxe", 0, 0, 0, 2.0, false, true),
        };
        for (int c = 0; c < cases.length; c++) {
            Case k = cases[c];
            BlockState state = BuiltInRegistries.BLOCK.getValue(Identifier.parse(k.block())).defaultBlockState();
            Item item = BuiltInRegistries.ITEM.getValue(Identifier.parse(k.item()));
            Player player = player(new ItemStack(item), k);
            String input = Block.getId(state) + "," + BuiltInRegistries.ITEM.getId(item) + "," + k.haste() + ","
                + k.conduit() + "," + k.fatigue() + "," + k.efficiency() + "," + (k.water() ? 1 : 0) + ","
                + (k.ground() ? 1 : 0);
            OUT.println("case " + c + " " + input);
            float progress = 0.0F;
            for (int tick = 1; tick <= 400 && progress < 1.0F; tick++) {
                float step = state.getDestroyProgress(player, EmptyBlockGetter.INSTANCE, BlockPos.ZERO);
                progress += step;
                int stage = progress > 0.0F ? (int) (progress * 10.0F) : -1;
                OUT.println("tick " + c + "@" + tick + " " + Float.floatToIntBits(step) + ":"
                    + Float.floatToIntBits(progress) + ":" + stage);
                if (step == 0.0F) break;
            }
        }
    }

    static Player player(ItemStack held, Case k) throws Exception {
        Player p = (Player) U.allocateInstance(RemotePlayer.class);
        Inventory inventory = (Inventory) U.allocateInstance(Inventory.class);
        NonNullList<ItemStack> items = NonNullList.withSize(36, ItemStack.EMPTY);
        items.set(0, held);
        set(inventory, "items", items);
        set(p, "inventory", inventory);
        AttributeMap attributes = new AttributeMap(Player.createAttributes().build());
        attributes.getInstance(Attributes.MINING_EFFICIENCY).setBaseValue(k.efficiency());
        set(p, "attributes", attributes);
        HashMap<Holder<MobEffect>, MobEffectInstance> effects = new HashMap<>();
        if (k.haste() > 0) effects.put(MobEffects.HASTE, new MobEffectInstance(MobEffects.HASTE, 1000, k.haste() - 1));
        if (k.conduit() > 0) {
            effects.put(MobEffects.CONDUIT_POWER, new MobEffectInstance(MobEffects.CONDUIT_POWER, 1000, k.conduit() - 1));
        }
        if (k.fatigue() > 0) {
            effects.put(MobEffects.MINING_FATIGUE, new MobEffectInstance(MobEffects.MINING_FATIGUE, 1000, k.fatigue() - 1));
        }
        set(p, "activeEffects", effects);
        Set<TagKey<net.minecraft.world.level.material.Fluid>> eyes = new HashSet<>();
        if (k.water()) eyes.add(FluidTags.WATER);
        set(p, "fluidOnEyes", eyes);
        Field ground = field(p.getClass(), "onGround");
        U.putBoolean(p, U.objectFieldOffset(ground), k.ground());
        return p;
    }

    static Field field(Class<?> type, String name) throws NoSuchFieldException {
        for (Class<?> c = type; c != null; c = c.getSuperclass()) {
            try {
                return c.getDeclaredField(name);
            } catch (NoSuchFieldException ignored) {
            }
        }
        throw new NoSuchFieldException(name);
    }

    static void set(Object target, String name, Object value) throws Exception {
        U.putObject(target, U.objectFieldOffset(field(target.getClass(), name)), value);
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
