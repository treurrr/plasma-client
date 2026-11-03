import com.mojang.serialization.Lifecycle;
import io.netty.buffer.ByteBufUtil;
import io.netty.buffer.Unpooled;
import java.io.PrintStream;
import com.mojang.datafixers.util.Either;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalInt;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Stream;
import net.minecraft.SharedConstants;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.core.GlobalPos;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.HolderSet;
import net.minecraft.core.MappedRegistry;
import net.minecraft.core.Registry;
import net.minecraft.core.RegistryAccess;
import net.minecraft.core.RegistrationInfo;
import net.minecraft.core.Rotations;
import net.minecraft.core.component.DataComponentPatch;
import net.minecraft.core.component.DataComponents;
import net.minecraft.core.particles.BlockParticleOption;
import net.minecraft.core.particles.ColorParticleOption;
import net.minecraft.core.particles.DustColorTransitionOptions;
import net.minecraft.core.particles.DustParticleOptions;
import net.minecraft.core.particles.ItemParticleOption;
import net.minecraft.core.particles.ParticleOptions;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.core.particles.PowerParticleOption;
import net.minecraft.core.particles.SculkChargeParticleOptions;
import net.minecraft.core.particles.ShriekParticleOption;
import net.minecraft.core.particles.SpellParticleOption;
import net.minecraft.core.particles.TrailParticleOption;
import net.minecraft.core.particles.VibrationParticleOption;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.nbt.NbtOps;
import net.minecraft.nbt.TagParser;
import net.minecraft.network.RegistryFriendlyByteBuf;
import net.minecraft.network.chat.Component;
import net.minecraft.network.protocol.common.ClientboundCustomReportDetailsPacket;
import net.minecraft.network.protocol.common.ClientboundServerLinksPacket;
import net.minecraft.network.protocol.game.ClientboundRecipeBookAddPacket;
import net.minecraft.network.protocol.game.ClientboundRecipeBookSettingsPacket;
import net.minecraft.network.protocol.game.ClientboundServerDataPacket;
import net.minecraft.network.syncher.EntityDataAccessor;
import net.minecraft.network.syncher.EntityDataSerializer;
import net.minecraft.network.syncher.EntityDataSerializers;
import net.minecraft.network.syncher.SynchedEntityData;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.RegistryOps;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.Bootstrap;
import net.minecraft.server.ServerLinks;
import net.minecraft.stats.RecipeBookSettings;
import net.minecraft.tags.ItemTags;
import net.minecraft.tags.TagKey;
import net.minecraft.util.Unit;
import net.minecraft.world.entity.EntityReference;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.entity.animal.armadillo.Armadillo;
import net.minecraft.world.entity.animal.golem.CopperGolemState;
import net.minecraft.world.entity.animal.sniffer.Sniffer;
import net.minecraft.world.entity.decoration.painting.PaintingVariant;
import net.minecraft.world.entity.npc.villager.VillagerData;
import net.minecraft.world.inventory.RecipeBookType;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.effect.MobEffect;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.item.component.MapPostProcessing;
import net.minecraft.world.item.component.ResolvableProfile;
import net.minecraft.world.item.crafting.Ingredient;
import net.minecraft.world.item.crafting.RecipeBookCategories;
import net.minecraft.world.item.crafting.display.FurnaceRecipeDisplay;
import net.minecraft.world.item.crafting.display.RecipeDisplay;
import net.minecraft.world.item.crafting.display.RecipeDisplayEntry;
import net.minecraft.world.item.crafting.display.RecipeDisplayId;
import net.minecraft.world.item.crafting.display.ShapedCraftingRecipeDisplay;
import net.minecraft.world.item.crafting.display.ShapelessCraftingRecipeDisplay;
import net.minecraft.world.item.crafting.display.SlotDisplay;
import net.minecraft.world.item.crafting.display.SmithingRecipeDisplay;
import net.minecraft.world.item.crafting.display.StonecutterRecipeDisplay;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.WeatheringCopper;
import net.minecraft.world.level.gameevent.BlockPositionSource;
import net.minecraft.world.phys.Vec3;
import org.joml.Quaternionf;
import org.joml.Vector3f;

// Encodes item stacks, entity data values, particles, displays and data packets
// with the game's own stream codecs, and prints per case the bytes and what
// they hold, in wire order: "<case> <kind>:<hex> <values>". codec_probe decodes
// the same bytes with Plasma's layout table and must print the same values.
//
// Values: a list is [a,b], a record of several fields {a,b}, a record of one
// field name=value, an absent optional or a unit -, an enum or a registry case
// by its name, an id as its number, a float or double as f<bits of the double>,
// a stack (count,item,component=value,...,!removed), nbt and text components
// as {key:value} with sorted keys and "strings".
public class CodecOracle {
    static RegistryAccess access;
    static PrintStream out;

    public static void main(String[] args) throws Exception {
        out = System.out;
        System.setOut(System.err);
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        access = registries();
        components();
        metadata();
        particles();
        displays();
        packets();
        out.flush();
    }

    // The game's static registries, and the datapack ones as vanilla builds
    // them, each numbered in key order the way a synced registry is.
    static RegistryAccess registries() {
        HolderLookup.Provider lookup = VanillaRegistries.createLookup();
        List<Registry<?>> all = new ArrayList<>();
        BuiltInRegistries.REGISTRY.forEach(all::add);
        lookup.listRegistryKeys().forEach(key -> {
            if (BuiltInRegistries.REGISTRY.containsKey(key.identifier())) return;
            all.add(copy(lookup, key));
        });
        return new RegistryAccess.ImmutableRegistryAccess(all);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    static Registry<?> copy(HolderLookup.Provider lookup, ResourceKey<? extends Registry<?>> key) {
        MappedRegistry registry = new MappedRegistry(key, Lifecycle.stable());
        lookup.lookupOrThrow((ResourceKey) key).listElements()
            .sorted(Comparator.comparing((Holder.Reference<?> h) -> h.key().identifier().toString()))
            .forEach(h -> registry.register(((Holder.Reference<?>) h).key(), ((Holder.Reference<?>) h).value(),
                                            RegistrationInfo.BUILT_IN));
        return registry.freeze();
    }

    // ---- output ----

    static String hex(Consumer<RegistryFriendlyByteBuf> write) {
        RegistryFriendlyByteBuf buf = new RegistryFriendlyByteBuf(Unpooled.buffer(), access);
        write.accept(buf);
        return ByteBufUtil.hexDump(buf);
    }

    static void line(String name, String kind, String hex, String expected) {
        out.println(name + " " + kind + ":" + hex + " " + expected);
    }

    static String esc(String text) {
        StringBuilder b = new StringBuilder();
        for (char c : text.toCharArray()) {
            if (c == ' ' || c == ',' || c == '%' || c == '\n') b.append(String.format("%%%02X", (int) c));
            else b.append(c);
        }
        return b.toString();
    }

    static String j(String... parts) { return String.join(",", parts); }
    static String rec(String... parts) { return "{" + j(parts) + "}"; }
    static String list(String... parts) { return "[" + j(parts) + "]"; }
    static String f(float v) { return "f" + Double.doubleToLongBits(v); }
    static String d(double v) { return "f" + Double.doubleToLongBits(v); }
    static String q(String s) { return "\"" + esc(s) + "\""; }
    static String n(long v) { return Long.toString(v); }
    static String e(Enum<?> v) { return v.name().toLowerCase(Locale.ROOT); }

    static String item(String name) {
        return n(BuiltInRegistries.ITEM.getId(BuiltInRegistries.ITEM.getValue(Identifier.parse(name))));
    }

    static <T> String id(ResourceKey<? extends Registry<T>> registry, String name) {
        Registry<T> r = access.lookupOrThrow(registry);
        T value = r.getValue(Identifier.parse(name));
        if (value == null) throw new IllegalArgumentException(registry + " has no " + name);
        return n(r.getId(value));
    }

    static String stack(int count, String item, String... components) {
        if (count == 0) return "(0)";
        return "(" + j(n(count), item(item)) + (components.length == 0 ? "" : "," + j(components)) + ")";
    }

    // ---- data components ----

    // Without a datapack no tag is bound; a tag that is named stays a name on
    // the wire, so an empty one encodes the same.
    static HolderLookup.Provider tagless() {
        return new HolderLookup.Provider() {
            public Stream<ResourceKey<? extends Registry<?>>> listRegistryKeys() { return access.listRegistryKeys(); }

            @SuppressWarnings({"unchecked", "rawtypes"})
            public <T> Optional<HolderLookup.RegistryLookup<T>> lookup(ResourceKey<? extends Registry<? extends T>> key) {
                return access.lookup(key).map(found -> {
                    Registry<T> registry = (Registry<T>) found;
                    return new HolderLookup.RegistryLookup.Delegate<T>() {
                        public HolderLookup.RegistryLookup<T> parent() { return registry; }

                        public Optional<HolderSet.Named<T>> get(TagKey<T> tag) {
                            return Optional.of(registry.get(tag).orElseGet(() -> HolderSet.emptyNamed(registry, tag)));
                        }
                    };
                });
            }
        };
    }

    static ItemStack parse(String item, String components) throws Exception {
        String snbt = "{id:'" + item + "',count:1" + (components.isEmpty() ? "" : ",components:{" + components + "}") + "}";
        return ItemStack.CODEC.parse(RegistryOps.create(NbtOps.INSTANCE, tagless()), TagParser.parseCompoundFully(snbt))
            .getOrThrow();
    }

    static void stackCase(String name, ItemStack stack, String expected) {
        line(name, "stack", hex(buf -> ItemStack.OPTIONAL_STREAM_CODEC.encode(buf, stack)), expected);
    }

    // One component on one item, its value expected in wire order.
    static void c(String component, String item, String snbt, String value) throws Exception {
        String name = component.contains(":") ? component : "minecraft:" + component;
        stackCase(name.replace("minecraft:", ""), parse(item, "'" + name + "':" + snbt),
                  stack(1, item, name + "=" + value));
    }

    static void components() throws Exception {
        String sound = "minecraft:sound_event";
        c("custom_data", "stick", "{plasma:1b,s:'x'}", "{plasma:1,s:\"x\"}");
        c("max_stack_size", "stick", "7", "7");
        c("max_damage", "diamond_sword", "123", "123");
        c("damage", "diamond_sword", "17", "17");
        c("unbreakable", "stick", "{}", "-");
        c("use_effects", "stick", "{can_sprint:true,interact_vibrations:false,speed_multiplier:0.25f}",
          rec("true", "false", f(0.25f)));
        c("custom_name", "stick", "{text:'Blade',color:'red',bold:true}", "{bold:1,color:\"red\",text:\"Blade\"}");
        c("minimum_attack_charge", "stick", "0.25f", f(0.25f));
        c("damage_type", "stick", "'minecraft:arrow'", "holder=" + id(Registries.DAMAGE_TYPE, "arrow"));
        c("item_name", "stick", "'Named'", q("Named"));
        c("item_model", "stick", "'minecraft:diamond'", "minecraft:diamond");
        c("lore", "stick", "['a',{text:'b',italic:false}]", list(q("a"), "{italic:0,text:\"b\"}"));
        c("rarity", "stick", "'epic'", "epic");
        c("enchantments", "diamond_sword", "{'minecraft:sharpness':5}",
          list(rec(id(Registries.ENCHANTMENT, "sharpness"), "5")));
        c("can_place_on", "stick", "{blocks:'minecraft:stone'}",
          list(rec(list(n(BuiltInRegistries.BLOCK.getId(Blocks.STONE))), "-", "-", "[]", "[]")));
        c("can_break", "stick",
          "{blocks:'#minecraft:logs',state:{axis:'y'},nbt:{a:1b},components:{'minecraft:damage':3},"
              + "predicates:{'minecraft:damage':{durability:{min:1}}}}",
          list(rec("#minecraft:logs", list(rec("axis", "exact=value=y")), "{a:1}",
                   list(rec("minecraft:damage", "3")),
                   list(rec("predicate=" + n(BuiltInRegistries.DATA_COMPONENT_PREDICATE_TYPE.getId(
                                BuiltInRegistries.DATA_COMPONENT_PREDICATE_TYPE.getValue(Identifier.parse("damage")))),
                            "{durability:{min:1}}")))));
        c("attribute_modifiers", "stick",
          "[{type:'minecraft:attack_damage',id:'plasma:extra',amount:3.0d,operation:'add_value',slot:'mainhand'},"
              + "{type:'minecraft:armor',id:'plasma:o',amount:0.5d,operation:'add_multiplied_total',slot:'any',"
              + "display:{type:'override',value:'custom'}}]",
          list(rec(id(Registries.ATTRIBUTE, "attack_damage"), "plasma:extra", d(3.0), "add_value", "mainhand", "default=-"),
               rec(id(Registries.ATTRIBUTE, "armor"), "plasma:o", d(0.5), "add_multiplied_total", "any",
                   "override=text=" + q("custom"))));
        c("custom_model_data", "stick", "{floats:[1.5f],flags:[1b],strings:['a'],colors:[255]}",
          rec(list(f(1.5f)), list("true"), list("a"), list("255")));
        c("tooltip_display", "stick", "{hide_tooltip:false,hidden_components:['minecraft:rarity']}",
          rec("false", list(n(BuiltInRegistries.DATA_COMPONENT_TYPE.getId(DataComponents.RARITY)))));
        c("repair_cost", "stick", "9", "9");
        ItemStack locked = new ItemStack(Items.STICK);
        locked.set(DataComponents.CREATIVE_SLOT_LOCK, Unit.INSTANCE);
        stackCase("creative_slot_lock", locked, stack(1, "stick", "minecraft:creative_slot_lock=-"));
        c("enchantment_glint_override", "stick", "true", "true");
        c("intangible_projectile", "stick", "{}", "{}");
        c("food", "stick", "{nutrition:3,saturation:1.5f,can_always_eat:true}", rec("3", f(1.5f), "true"));
        c("consumable", "stick",
          "{consume_seconds:1.5f,animation:'spear',sound:'minecraft:entity.generic.eat',has_consume_particles:false,"
              + "on_consume_effects:[{type:'minecraft:apply_effects',effects:[{id:'minecraft:speed',amplifier:1,duration:20}],"
              + "probability:0.5f},{type:'minecraft:remove_effects',effects:'minecraft:poison'},"
              + "{type:'minecraft:teleport_randomly',diameter:8.0f},"
              + "{type:'minecraft:play_sound',sound:{sound_id:'minecraft:plasma.custom',range:4.0f}},"
              + "{type:'minecraft:clear_all_effects'}]}",
          rec(f(1.5f), "spear", id(Registries.SOUND_EVENT, "entity.generic.eat"), "false",
              list(rec("minecraft:apply_effects",
                       list(rec(id(Registries.MOB_EFFECT, "speed"), rec("1", "20", "false", "true", "true", "-"))),
                       f(0.5f)),
                   rec("minecraft:remove_effects", list(id(Registries.MOB_EFFECT, "poison"))),
                   rec("minecraft:teleport_randomly", f(8.0f)),
                   rec("minecraft:play_sound", rec("minecraft:plasma.custom", f(4.0f))),
                   "type=minecraft:clear_all_effects")));
        c("use_remainder", "stick", "{id:'minecraft:bowl',count:2}", stack(2, "bowl"));
        c("use_cooldown", "stick", "{seconds:2.0f,cooldown_group:'plasma:g'}", rec(f(2.0f), "plasma:g"));
        c("damage_resistant", "stick", "{types:'#minecraft:is_fire'}", "types=minecraft:is_fire");
        c("tool", "stick",
          "{rules:[{blocks:'#minecraft:mineable/pickaxe',speed:4.0f,correct_for_drops:true},"
              + "{blocks:['minecraft:stone','minecraft:dirt']}],default_mining_speed:2.0f,damage_per_block:3,"
              + "can_destroy_blocks_in_creative:false}",
          rec(list(rec("#minecraft:mineable/pickaxe", f(4.0f), "true"),
                   rec(list(n(BuiltInRegistries.BLOCK.getId(Blocks.STONE)), n(BuiltInRegistries.BLOCK.getId(Blocks.DIRT))),
                       "-", "-")),
              f(2.0f), "3", "false"));
        c("weapon", "stick", "{item_damage_per_attack:2,disable_blocking_for_seconds:3.0f}", rec("2", f(3.0f)));
        c("attack_range", "stick",
          "{min_reach:0.5f,max_reach:4.0f,min_creative_reach:0.25f,max_creative_reach:5.0f,hitbox_margin:0.125f,mob_factor:1.5f}",
          rec(f(0.5f), f(4.0f), f(0.25f), f(5.0f), f(0.125f), f(1.5f)));
        c("enchantable", "stick", "{value:10}", "10");
        c("equippable", "stick",
          "{slot:'head',equip_sound:'minecraft:item.armor.equip_gold',asset_id:'minecraft:gold',"
              + "camera_overlay:'minecraft:misc/pumpkinblur',allowed_entities:'minecraft:player',dispensable:false,"
              + "swappable:true,damage_on_hurt:false,equip_on_interact:true,can_be_sheared:true,"
              + "shearing_sound:'minecraft:item.shears.snip'}",
          rec("head", id(Registries.SOUND_EVENT, "item.armor.equip_gold"), "minecraft:gold", "minecraft:misc/pumpkinblur",
              list(id(Registries.ENTITY_TYPE, "player")), "false", "true", "false", "true", "true",
              id(Registries.SOUND_EVENT, "item.shears.snip")));
        c("repairable", "stick", "{items:'#minecraft:planks'}", "items=#minecraft:planks");
        c("glider", "stick", "{}", "-");
        c("tooltip_style", "stick", "'plasma:style'", "plasma:style");
        c("death_protection", "stick", "{death_effects:[{type:'minecraft:clear_all_effects'}]}",
          "effects=" + list("type=minecraft:clear_all_effects"));
        c("blocks_attacks", "stick",
          "{block_delay_seconds:0.5f,disable_cooldown_scale:2.0f,damage_reductions:[{base:1.0f,factor:0.5f,"
              + "type:'#minecraft:is_fire',horizontal_blocking_angle:45.0f}],item_damage:{threshold:3.0f,base:1.0f,"
              + "factor:0.25f},bypassed_by:'#minecraft:bypasses_shield',block_sound:'minecraft:item.shield.block',"
              + "disabled_sound:'minecraft:item.shield.break'}",
          rec(f(0.5f), f(2.0f), list(rec(f(45.0f), "#minecraft:is_fire", f(1.0f), f(0.5f))),
              rec(f(3.0f), f(1.0f), f(0.25f)), "minecraft:bypasses_shield",
              id(Registries.SOUND_EVENT, "item.shield.block"), id(Registries.SOUND_EVENT, "item.shield.break")));
        c("piercing_weapon", "stick",
          "{deals_knockback:false,dismounts:true,sound:'minecraft:item.trident.throw',hit_sound:'minecraft:item.trident.hit'}",
          rec("false", "true", id(Registries.SOUND_EVENT, "item.trident.throw"), id(Registries.SOUND_EVENT, "item.trident.hit")));
        c("kinetic_weapon", "stick",
          "{contact_cooldown_ticks:10,delay_ticks:2,dismount_conditions:{max_duration_ticks:5,min_speed:1.0f,"
              + "min_relative_speed:0.25f},damage_conditions:{max_duration_ticks:6,min_speed:0.75f,"
              + "min_relative_speed:0.5f},forward_movement:0.5f,damage_multiplier:1.5f,sound:'minecraft:item.trident.throw'}",
          rec("10", "2", rec("5", f(1.0f), f(0.25f)), "-", rec("6", f(0.75f), f(0.5f)), f(0.5f), f(1.5f),
              id(Registries.SOUND_EVENT, "item.trident.throw"), "-"));
        c("swing_animation", "stick", "{type:'stab',duration:5}", rec("stab", "5"));
        c("stored_enchantments", "enchanted_book", "{'minecraft:mending':1}",
          list(rec(id(Registries.ENCHANTMENT, "mending"), "1")));
        c("dyed_color", "leather_chestplate", "16711680", "16711680");
        c("map_color", "filled_map", "255", "255");
        c("map_id", "filled_map", "5", "5");
        c("map_decorations", "filled_map", "{a:{type:'minecraft:mansion',x:1.0d,z:2.0d,rotation:0.5f}}",
          "{a:{rotation:" + f(0.5f) + ",type:\"minecraft:mansion\",x:" + d(1.0) + ",z:" + d(2.0) + "}}");
        ItemStack scaled = new ItemStack(Items.FILLED_MAP);
        scaled.set(DataComponents.MAP_POST_PROCESSING, MapPostProcessing.SCALE);
        stackCase("map_post_processing", scaled, stack(1, "filled_map", "minecraft:map_post_processing=scale"));
        c("charged_projectiles", "crossbow", "[{id:'minecraft:arrow'}]", list(stack(1, "arrow")));
        c("bundle_contents", "bundle",
          "[{id:'minecraft:stone',count:3},{id:'minecraft:diamond',count:2,components:{'minecraft:custom_name':'inner'}}]",
          list(stack(3, "stone"), stack(2, "diamond", "minecraft:custom_name=" + q("inner"))));
        c("potion_contents", "potion",
          "{potion:'minecraft:swiftness',custom_color:255,custom_effects:[{id:'minecraft:speed',amplifier:1,duration:100,"
              + "ambient:true,show_particles:false,show_icon:true}],custom_name:'fast'}",
          rec(id(Registries.POTION, "swiftness"), "255",
              list(rec(id(Registries.MOB_EFFECT, "speed"), rec("1", "100", "true", "false", "true", "-"))), "fast"));
        Holder<MobEffect> speed = holder(Registries.MOB_EFFECT, "speed");
        MobEffectInstance hidden = new MobEffectInstance(speed, 100, 1, false, true, false, new MobEffectInstance(speed, 400, 2));
        line("potion_effect_hidden", "named.potion_effect", hex(buf -> MobEffectInstance.STREAM_CODEC.encode(buf, hidden)),
             rec(id(Registries.MOB_EFFECT, "speed"),
                 rec("1", "100", "false", "true", "false", rec("2", "400", "false", "true", "true", "-"))));
        c("potion_duration_scale", "stick", "0.5f", f(0.5f));
        c("suspicious_stew_effects", "suspicious_stew", "[{id:'minecraft:night_vision',duration:100}]",
          list(rec(id(Registries.MOB_EFFECT, "night_vision"), "100")));
        c("writable_book_content", "writable_book", "{pages:['hello',{raw:'w',filtered:'x'}]}",
          "pages=" + list(rec("hello", "-"), rec("w", "x")));
        c("written_book_content", "written_book",
          "{title:'T',author:'A',generation:1,pages:['p1',{raw:'p2',filtered:'q2'}],resolved:true}",
          rec(rec("T", "-"), "A", "1", list(rec(q("p1"), "-"), rec(q("p2"), q("q2"))), "true"));
        c("trim", "diamond_helmet", "{material:'minecraft:amethyst',pattern:'minecraft:sentry'}",
          rec(id(Registries.TRIM_MATERIAL, "amethyst"), id(Registries.TRIM_PATTERN, "sentry")));
        stackCase("trim_inline", parse("stick", "'minecraft:trim':{material:{asset_name:'plasma',description:'Plasma',"
                                                + "override_armor_assets:{'minecraft:gold':'pd'}},pattern:{asset_id:"
                                                + "'minecraft:coast',description:'Inline',decal:true}}"),
                  stack(1, "stick", "minecraft:trim=" + rec(rec("plasma", list(rec("minecraft:gold", "pd")), q("Plasma")),
                                                           rec("minecraft:coast", q("Inline"), "true"))));
        c("debug_stick_state", "debug_stick", "{'minecraft:oak_log':'axis'}", "{minecraft:oak_log:\"axis\"}");
        c("entity_data", "stick", "{id:'minecraft:pig',Health:5.0f}",
          rec(id(Registries.ENTITY_TYPE, "pig"), "{Health:" + f(5.0f) + "}"));
        c("bucket_entity_data", "axolotl_bucket", "{Variant:2}", "{Variant:2}");
        c("block_entity_data", "stick", "{id:'minecraft:chest',CustomName:'x'}",
          rec(id(Registries.BLOCK_ENTITY_TYPE, "chest"), "{CustomName:\"x\"}"));
        c("instrument", "goat_horn", "'minecraft:ponder_goat_horn'", "holder=" + id(Registries.INSTRUMENT, "ponder_goat_horn"));
        stackCase("instrument_inline", parse("stick", "'minecraft:instrument':{sound_event:'minecraft:item.goat_horn.sound.0',"
                                                      + "use_duration:7.0f,range:256.0f,description:'Horn'}"),
                  stack(1, "stick", "minecraft:instrument=holder=" + rec(id(Registries.SOUND_EVENT, "item.goat_horn.sound.0"),
                                                                        f(7.0f), f(256.0f), q("Horn"))));
        c("provides_trim_material", "stick", "'minecraft:gold'", "holder=" + id(Registries.TRIM_MATERIAL, "gold"));
        c("ominous_bottle_amplifier", "ominous_bottle", "3", "3");
        c("jukebox_playable", "stick", "'minecraft:13'", "holder=" + id(Registries.JUKEBOX_SONG, "13"));
        c("provides_banner_patterns", "stick", "'#minecraft:pattern_item/globe'", "minecraft:pattern_item/globe");
        c("recipes", "knowledge_book", "['minecraft:crafting_table']", list(q("minecraft:crafting_table")));
        c("lodestone_tracker", "compass", "{target:{pos:[I;1,2,3],dimension:'minecraft:overworld'},tracked:false}",
          rec(rec("minecraft:overworld", "1,2,3"), "false"));
        c("firework_explosion", "firework_star",
          "{shape:'creeper',colors:[I;16711680],fade_colors:[I;1,2],has_trail:true,has_twinkle:false}",
          rec("creeper", list("16711680"), list("1", "2"), "true", "false"));
        c("fireworks", "firework_rocket", "{flight_duration:2b,explosions:[{shape:'star',colors:[I;255]}]}",
          rec("2", list(rec("star", list("255"), "[]", "false", "false"))));
        c("profile", "player_head",
          "{name:'PlasmaA',id:[I;1,2,3,4],properties:[{name:'textures',value:'e30=',signature:'sig'}]}",
          rec("complete=" + rec("00000001-0000-0002-0000-000300000004", "PlasmaA", list(rec("textures", "e30=", "sig"))),
              "-", "-", "-", "-"));
        stackCase("profile_partial", parse("player_head", "'minecraft:profile':{name:'X',texture:'minecraft:entity/steve',model:'slim'}"),
                  stack(1, "player_head", "minecraft:profile=" + rec("partial=" + rec("X", "-", "[]"),
                                                                     "minecraft:entity/steve", "-", "-", "slim=-")));
        c("note_block_sound", "player_head", "'minecraft:block.note_block.harp'", "minecraft:block.note_block.harp");
        c("banner_patterns", "white_banner", "[{pattern:'minecraft:stripe_top',color:'red'}]",
          list(rec(id(Registries.BANNER_PATTERN, "stripe_top"), "red")));
        stackCase("banner_patterns_inline",
                  parse("white_banner", "'minecraft:banner_patterns':[{pattern:{asset_id:'minecraft:creeper',"
                                        + "translation_key:'plasma.inline'},color:'green'}]"),
                  stack(1, "white_banner", "minecraft:banner_patterns="
                                           + list(rec(rec("minecraft:creeper", "plasma.inline"), "green"))));
        c("base_color", "shield", "'blue'", "blue");
        c("pot_decorations", "decorated_pot",
          "['minecraft:brick','minecraft:angler_pottery_sherd','minecraft:brick','minecraft:skull_pottery_sherd']",
          list(item("brick"), item("angler_pottery_sherd"), item("brick"), item("skull_pottery_sherd")));
        c("container", "shulker_box",
          "[{slot:0,item:{id:'minecraft:stone',count:1}},{slot:2,item:{id:'minecraft:dirt',count:4}}]",
          list(stack(1, "stone"), stack(0, ""), stack(4, "dirt")));
        c("block_state", "beehive", "{honey_level:'3'}", list(rec("honey_level", "3")));
        c("bees", "beehive", "[{entity_data:{id:'minecraft:bee'},ticks_in_hive:1,min_ticks_in_hive:2}]",
          list(rec(id(Registries.ENTITY_TYPE, "bee"), "{}", "1", "2")));
        c("lock", "shulker_box", "{items:'minecraft:diamond'}", "{items:\"minecraft:diamond\"}");
        c("container_loot", "chest", "{loot_table:'minecraft:chests/simple_dungeon',seed:5L}",
          "{loot_table:\"minecraft:chests/simple_dungeon\",seed:5}");
        c("break_sound", "stick", "'minecraft:block.glass.break'", id(Registries.SOUND_EVENT, "block.glass.break"));
        c("villager/variant", "stick", "'minecraft:desert'", id(Registries.VILLAGER_TYPE, "desert"));
        c("wolf/variant", "stick", "'minecraft:ashen'", id(Registries.WOLF_VARIANT, "ashen"));
        c("wolf/sound_variant", "stick", "'minecraft:big'", id(Registries.WOLF_SOUND_VARIANT, "big"));
        c("wolf/collar", "stick", "'red'", "red");
        c("fox/variant", "stick", "'snow'", "snow");
        c("salmon/size", "stick", "'large'", "large");
        c("parrot/variant", "stick", "'blue'", "blue");
        c("tropical_fish/pattern", "stick", "'glitter'", "glitter");
        c("tropical_fish/base_color", "stick", "'red'", "red");
        c("tropical_fish/pattern_color", "stick", "'blue'", "blue");
        c("mooshroom/variant", "stick", "'brown'", "brown");
        c("rabbit/variant", "stick", "'evil'", "evil");
        c("pig/variant", "stick", "'minecraft:cold'", id(Registries.PIG_VARIANT, "cold"));
        c("cow/variant", "stick", "'minecraft:warm'", id(Registries.COW_VARIANT, "warm"));
        c("chicken/variant", "stick", "'minecraft:cold'", "holder=" + id(Registries.CHICKEN_VARIANT, "cold"));
        c("zombie_nautilus/variant", "stick", "'minecraft:warm'",
          "holder=" + id(Registries.ZOMBIE_NAUTILUS_VARIANT, "warm"));
        c("frog/variant", "stick", "'minecraft:cold'", id(Registries.FROG_VARIANT, "cold"));
        c("horse/variant", "stick", "'gray'", "gray");
        c("painting/variant", "stick", "'minecraft:kebab'", id(Registries.PAINTING_VARIANT, "kebab"));
        c("llama/variant", "stick", "'gray'", "gray");
        c("axolotl/variant", "stick", "'blue'", "blue");
        c("cat/variant", "stick", "'minecraft:black'", id(Registries.CAT_VARIANT, "black"));
        c("cat/collar", "stick", "'lime'", "lime");
        c("sheep/color", "stick", "'pink'", "pink");
        c("shulker/color", "stick", "'green'", "green");

        stackCase("removed", parse("diamond_sword", "'!minecraft:attribute_modifiers':{}"),
                  stack(1, "diamond_sword", "!minecraft:attribute_modifiers"));
        stackCase("empty", ItemStack.EMPTY, "(0)");
        stackCase("plain", new ItemStack(Items.DIAMOND, 64), stack(64, "diamond"));
    }

    // ---- entity data ----

    static <T> void m(String name, int index, EntityDataSerializer<T> serializer, T value, String expected) {
        SynchedEntityData.DataValue<T> data = SynchedEntityData.DataValue.create(new EntityDataAccessor<>(index, serializer), value);
        String bytes = hex(buf -> {
            data.write(buf);
            buf.writeByte(255);
        });
        line("meta." + name, "meta", bytes, index + ":" + EntityDataSerializers.getSerializedId(serializer) + "=" + expected);
    }

    static <T> Holder<T> holder(ResourceKey<? extends Registry<T>> registry, String name) {
        Registry<T> r = access.lookupOrThrow(registry);
        return r.wrapAsHolder(r.getValue(Identifier.parse(name)));
    }

    static String state(net.minecraft.world.level.block.state.BlockState state) { return n(Block.getId(state)); }

    static void metadata() {
        BlockPos pos = new BlockPos(-5, 70, 123456);
        String posText = "-5,70,123456";
        m("byte", 0, EntityDataSerializers.BYTE, (byte) -3, "-3");
        m("int", 1, EntityDataSerializers.INT, 300000, "300000");
        m("long", 2, EntityDataSerializers.LONG, 1L << 40, n(1L << 40));
        m("float", 3, EntityDataSerializers.FLOAT, 0.75f, f(0.75f));
        m("string", 4, EntityDataSerializers.STRING, "hi there", esc("hi there"));
        m("component", 5, EntityDataSerializers.COMPONENT, Component.literal("x"), q("x"));
        m("optional_component", 6, EntityDataSerializers.OPTIONAL_COMPONENT, Optional.of(Component.literal("y")), q("y"));
        m("optional_component_none", 6, EntityDataSerializers.OPTIONAL_COMPONENT, Optional.empty(), "-");
        m("item_stack", 7, EntityDataSerializers.ITEM_STACK, new ItemStack(Items.DIAMOND, 3), stack(3, "diamond"));
        m("block_state", 8, EntityDataSerializers.BLOCK_STATE, Blocks.OAK_STAIRS.defaultBlockState(),
          state(Blocks.OAK_STAIRS.defaultBlockState()));
        m("optional_block_state", 9, EntityDataSerializers.OPTIONAL_BLOCK_STATE, Optional.of(Blocks.STONE.defaultBlockState()),
          state(Blocks.STONE.defaultBlockState()));
        m("optional_block_state_none", 9, EntityDataSerializers.OPTIONAL_BLOCK_STATE, Optional.empty(), "0");
        m("boolean", 10, EntityDataSerializers.BOOLEAN, true, "true");
        m("particle", 11, EntityDataSerializers.PARTICLE, new DustParticleOptions(0xFF0000, 1.5f),
          rec("minecraft:dust", "16711680", f(1.5f)));
        m("particles", 12, EntityDataSerializers.PARTICLES, List.<ParticleOptions>of(ParticleTypes.FLAME, new ShriekParticleOption(4)),
          list("type=minecraft:flame", rec("minecraft:shriek", "4")));
        m("rotations", 13, EntityDataSerializers.ROTATIONS, new Rotations(1.0f, -2.5f, 3.25f), rec(f(1.0f), f(-2.5f), f(3.25f)));
        m("block_pos", 14, EntityDataSerializers.BLOCK_POS, pos, posText);
        m("optional_block_pos", 15, EntityDataSerializers.OPTIONAL_BLOCK_POS, Optional.of(pos), posText);
        m("optional_block_pos_none", 15, EntityDataSerializers.OPTIONAL_BLOCK_POS, Optional.empty(), "-");
        m("direction", 16, EntityDataSerializers.DIRECTION, Direction.WEST, "west");
        UUID uuid = new UUID(0x0123456789abcdefL, 0xfedcba9876543210L);
        m("optional_living_entity", 17, EntityDataSerializers.OPTIONAL_LIVING_ENTITY_REFERENCE,
          Optional.of(EntityReference.of(uuid)), uuid.toString());
        m("optional_global_pos", 18, EntityDataSerializers.OPTIONAL_GLOBAL_POS, Optional.of(GlobalPos.of(Level.NETHER, pos)),
          rec("minecraft:the_nether", posText));
        m("villager_data", 19, EntityDataSerializers.VILLAGER_DATA,
          new VillagerData(holder(Registries.VILLAGER_TYPE, "snow"), holder(Registries.VILLAGER_PROFESSION, "cleric"), 3),
          rec(id(Registries.VILLAGER_TYPE, "snow"), id(Registries.VILLAGER_PROFESSION, "cleric"), "3"));
        m("optional_unsigned_int", 20, EntityDataSerializers.OPTIONAL_UNSIGNED_INT, OptionalInt.of(7), "7");
        m("optional_unsigned_int_none", 20, EntityDataSerializers.OPTIONAL_UNSIGNED_INT, OptionalInt.empty(), "-");
        m("pose", 21, EntityDataSerializers.POSE, Pose.CROAKING, e(Pose.CROAKING));
        m("cat_variant", 22, EntityDataSerializers.CAT_VARIANT, holder(Registries.CAT_VARIANT, "jellie"),
          id(Registries.CAT_VARIANT, "jellie"));
        m("chicken_variant", 23, EntityDataSerializers.CHICKEN_VARIANT, holder(Registries.CHICKEN_VARIANT, "warm"),
          id(Registries.CHICKEN_VARIANT, "warm"));
        m("cow_variant", 24, EntityDataSerializers.COW_VARIANT, holder(Registries.COW_VARIANT, "cold"),
          id(Registries.COW_VARIANT, "cold"));
        m("wolf_variant", 25, EntityDataSerializers.WOLF_VARIANT, holder(Registries.WOLF_VARIANT, "spotted"),
          id(Registries.WOLF_VARIANT, "spotted"));
        m("wolf_sound_variant", 26, EntityDataSerializers.WOLF_SOUND_VARIANT, holder(Registries.WOLF_SOUND_VARIANT, "grumpy"),
          id(Registries.WOLF_SOUND_VARIANT, "grumpy"));
        m("frog_variant", 27, EntityDataSerializers.FROG_VARIANT, holder(Registries.FROG_VARIANT, "warm"),
          id(Registries.FROG_VARIANT, "warm"));
        m("pig_variant", 28, EntityDataSerializers.PIG_VARIANT, holder(Registries.PIG_VARIANT, "warm"),
          id(Registries.PIG_VARIANT, "warm"));
        m("zombie_nautilus_variant", 29, EntityDataSerializers.ZOMBIE_NAUTILUS_VARIANT,
          holder(Registries.ZOMBIE_NAUTILUS_VARIANT, "warm"), id(Registries.ZOMBIE_NAUTILUS_VARIANT, "warm"));
        m("painting_variant", 30, EntityDataSerializers.PAINTING_VARIANT, holder(Registries.PAINTING_VARIANT, "kebab"),
          id(Registries.PAINTING_VARIANT, "kebab"));
        m("painting_variant_inline", 30, EntityDataSerializers.PAINTING_VARIANT,
          Holder.direct(new PaintingVariant(3, 2, Identifier.parse("plasma:art"), Optional.of(Component.literal("t")),
                                            Optional.empty())),
          rec("3", "2", "plasma:art", q("t"), "-"));
        m("armadillo_state", 31, EntityDataSerializers.ARMADILLO_STATE, Armadillo.ArmadilloState.SCARED,
          e(Armadillo.ArmadilloState.SCARED));
        m("sniffer_state", 32, EntityDataSerializers.SNIFFER_STATE, Sniffer.State.SEARCHING, e(Sniffer.State.SEARCHING));
        m("weathering_copper_state", 33, EntityDataSerializers.WEATHERING_COPPER_STATE, WeatheringCopper.WeatherState.WEATHERED,
          e(WeatheringCopper.WeatherState.WEATHERED));
        m("copper_golem_state", 34, EntityDataSerializers.COPPER_GOLEM_STATE, CopperGolemState.DROPPING_NO_ITEM,
          e(CopperGolemState.DROPPING_NO_ITEM));
        m("vector3", 35, EntityDataSerializers.VECTOR3, new Vector3f(1.0f, 2.5f, -3.0f), rec(f(1.0f), f(2.5f), f(-3.0f)));
        m("quaternion", 36, EntityDataSerializers.QUATERNION, new Quaternionf(0.1f, 0.2f, 0.3f, 0.9f),
          rec(f(0.1f), f(0.2f), f(0.3f), f(0.9f)));
        m("resolvable_profile", 37, EntityDataSerializers.RESOLVABLE_PROFILE, ResolvableProfile.createUnresolved("Steve"),
          rec("partial=" + rec("Steve", "-", "[]"), "-", "-", "-", "-"));
        m("humanoid_arm", 38, EntityDataSerializers.HUMANOID_ARM, HumanoidArm.LEFT, "left");
    }

    // ---- particles, through the particle serializer ----

    static void p(String name, ParticleOptions options, String expected) {
        m("particle." + name, 0, EntityDataSerializers.PARTICLE, options, expected);
    }

    static void particles() {
        String stone = state(Blocks.STONE.defaultBlockState());
        p("block", new BlockParticleOption(ParticleTypes.BLOCK, Blocks.STONE.defaultBlockState()), rec("minecraft:block", stone));
        p("block_marker", new BlockParticleOption(ParticleTypes.BLOCK_MARKER, Blocks.STONE.defaultBlockState()),
          rec("minecraft:block_marker", stone));
        p("falling_dust", new BlockParticleOption(ParticleTypes.FALLING_DUST, Blocks.STONE.defaultBlockState()),
          rec("minecraft:falling_dust", stone));
        p("dust_pillar", new BlockParticleOption(ParticleTypes.DUST_PILLAR, Blocks.STONE.defaultBlockState()),
          rec("minecraft:dust_pillar", stone));
        p("block_crumble", new BlockParticleOption(ParticleTypes.BLOCK_CRUMBLE, Blocks.STONE.defaultBlockState()),
          rec("minecraft:block_crumble", stone));
        p("dragon_breath", PowerParticleOption.create(ParticleTypes.DRAGON_BREATH, 0.5f), rec("minecraft:dragon_breath", f(0.5f)));
        p("dust", new DustParticleOptions(0x123456, 2.0f), rec("minecraft:dust", n(0x123456), f(2.0f)));
        p("dust_color_transition", new DustColorTransitionOptions(0xFF0000, 0x00FF00, 1.25f),
          rec("minecraft:dust_color_transition", n(0xFF0000), n(0x00FF00), f(1.25f)));
        p("effect", SpellParticleOption.create(ParticleTypes.EFFECT, 0xFF112233, 0.5f),
          rec("minecraft:effect", n(0xFF112233), f(0.5f)));
        p("instant_effect", SpellParticleOption.create(ParticleTypes.INSTANT_EFFECT, 0xFF445566, 0.25f),
          rec("minecraft:instant_effect", n(0xFF445566), f(0.25f)));
        p("entity_effect", ColorParticleOption.create(ParticleTypes.ENTITY_EFFECT, 0xFF336699),
          rec("minecraft:entity_effect", n(0xFF336699)));
        p("tinted_leaves", ColorParticleOption.create(ParticleTypes.TINTED_LEAVES, 0xFF00AA00),
          rec("minecraft:tinted_leaves", n(0xFF00AA00)));
        p("flash", ColorParticleOption.create(ParticleTypes.FLASH, 0xFFFFFFFF), rec("minecraft:flash", n(0xFFFFFFFF)));
        p("sculk_charge", new SculkChargeParticleOptions(0.25f), rec("minecraft:sculk_charge", f(0.25f)));
        p("item", new ItemParticleOption(ParticleTypes.ITEM, new ItemStack(Items.APPLE)), rec("minecraft:item", stack(1, "apple")));
        p("vibration", new VibrationParticleOption(new BlockPositionSource(new BlockPos(1, -2, 3)), 20),
          rec("minecraft:vibration", rec("minecraft:block", "1,-2,3"), "20"));
        p("trail", new TrailParticleOption(new Vec3(1.5, 2.0, -3.0), 0xFF00FF00, 40),
          rec("minecraft:trail", d(1.5), d(2.0), d(-3.0), n(0xFF00FF00), "40"));
        p("shriek", new ShriekParticleOption(5), rec("minecraft:shriek", "5"));
        p("flame", ParticleTypes.FLAME, "type=minecraft:flame");
    }

    // ---- recipe displays ----

    static void slot(String name, SlotDisplay display, String expected) {
        line("slot_display." + name, "named.slot_display", hex(buf -> SlotDisplay.STREAM_CODEC.encode(buf, display)), expected);
    }

    static void recipe(String name, RecipeDisplay display, String expected) {
        line("recipe_display." + name, "named.recipe_display", hex(buf -> RecipeDisplay.STREAM_CODEC.encode(buf, display)),
             expected);
    }

    static SlotDisplay.ItemSlotDisplay itemSlot(net.minecraft.world.item.Item item) {
        return new SlotDisplay.ItemSlotDisplay(BuiltInRegistries.ITEM.wrapAsHolder(item));
    }

    static void displays() {
        String stoneSlot = rec("minecraft:item", item("stone"));
        String dirtSlot = rec("minecraft:item", item("dirt"));
        String table = rec("minecraft:item", item("crafting_table"));
        slot("empty", SlotDisplay.Empty.INSTANCE, "type=minecraft:empty");
        slot("any_fuel", SlotDisplay.AnyFuel.INSTANCE, "type=minecraft:any_fuel");
        slot("item", itemSlot(Items.STONE), stoneSlot);
        slot("item_stack", new SlotDisplay.ItemStackSlotDisplay(new ItemStack(Items.DIAMOND, 2)),
             rec("minecraft:item_stack", stack(2, "diamond")));
        slot("tag", new SlotDisplay.TagSlotDisplay(ItemTags.PLANKS), rec("minecraft:tag", "minecraft:planks"));
        slot("smithing_trim", new SlotDisplay.SmithingTrimDemoSlotDisplay(itemSlot(Items.STONE), itemSlot(Items.DIRT),
                                                                         holder(Registries.TRIM_PATTERN, "coast")),
             rec("minecraft:smithing_trim", stoneSlot, dirtSlot, id(Registries.TRIM_PATTERN, "coast")));
        slot("with_remainder", new SlotDisplay.WithRemainder(itemSlot(Items.STONE), itemSlot(Items.DIRT)),
             rec("minecraft:with_remainder", stoneSlot, dirtSlot));
        slot("composite", new SlotDisplay.Composite(List.of(itemSlot(Items.STONE), SlotDisplay.Empty.INSTANCE)),
             rec("minecraft:composite", list(stoneSlot, "type=minecraft:empty")));

        recipe("crafting_shapeless", new ShapelessCraftingRecipeDisplay(List.of(itemSlot(Items.STONE), itemSlot(Items.DIRT)),
                                                                        itemSlot(Items.DIRT), itemSlot(Items.CRAFTING_TABLE)),
               rec("minecraft:crafting_shapeless", list(stoneSlot, dirtSlot), dirtSlot, table));
        recipe("crafting_shaped", new ShapedCraftingRecipeDisplay(2, 1, List.of(itemSlot(Items.STONE), SlotDisplay.Empty.INSTANCE),
                                                                  itemSlot(Items.DIRT), itemSlot(Items.CRAFTING_TABLE)),
               rec("minecraft:crafting_shaped", "2", "1", list(stoneSlot, "type=minecraft:empty"), dirtSlot, table));
        recipe("furnace", new FurnaceRecipeDisplay(itemSlot(Items.STONE), SlotDisplay.AnyFuel.INSTANCE, itemSlot(Items.DIRT),
                                                   itemSlot(Items.FURNACE), 200, 0.35f),
               rec("minecraft:furnace", stoneSlot, "type=minecraft:any_fuel", dirtSlot,
                   rec("minecraft:item", item("furnace")), "200", f(0.35f)));
        recipe("stonecutter", new StonecutterRecipeDisplay(itemSlot(Items.STONE), itemSlot(Items.DIRT), itemSlot(Items.STONECUTTER)),
               rec("minecraft:stonecutter", stoneSlot, dirtSlot, rec("minecraft:item", item("stonecutter"))));
        recipe("smithing", new SmithingRecipeDisplay(itemSlot(Items.STONE), itemSlot(Items.DIRT), itemSlot(Items.STONE),
                                                     itemSlot(Items.DIRT), itemSlot(Items.SMITHING_TABLE)),
               rec("minecraft:smithing", stoneSlot, dirtSlot, stoneSlot, dirtSlot, rec("minecraft:item", item("smithing_table"))));
    }

    // ---- packets that are only data ----

    static void packets() {
        RecipeDisplayEntry entry = new RecipeDisplayEntry(
            new RecipeDisplayId(42),
            new StonecutterRecipeDisplay(itemSlot(Items.STONE), itemSlot(Items.DIRT), itemSlot(Items.STONECUTTER)),
            OptionalInt.of(5), RecipeBookCategories.STONECUTTER,
            Optional.of(List.of(Ingredient.of(Items.STONE, Items.DIRT))));
        RecipeDisplayEntry bare = new RecipeDisplayEntry(new RecipeDisplayId(43), new StonecutterRecipeDisplay(
            itemSlot(Items.DIRT), itemSlot(Items.STONE), itemSlot(Items.STONECUTTER)), OptionalInt.empty(),
            RecipeBookCategories.CRAFTING_MISC, Optional.empty());
        ClientboundRecipeBookAddPacket add = new ClientboundRecipeBookAddPacket(
            List.of(new ClientboundRecipeBookAddPacket.Entry(entry, true, false),
                    new ClientboundRecipeBookAddPacket.Entry(bare, false, true)), true);
        String category = n(BuiltInRegistries.RECIPE_BOOK_CATEGORY.getId(RecipeBookCategories.STONECUTTER));
        String misc = n(BuiltInRegistries.RECIPE_BOOK_CATEGORY.getId(RecipeBookCategories.CRAFTING_MISC));
        String stoneSlot = rec("minecraft:item", item("stone"));
        String dirtSlot = rec("minecraft:item", item("dirt"));
        String cutter = rec("minecraft:item", item("stonecutter"));
        line("packet.recipe_book_add", "named.recipe_book_add",
             hex(buf -> ClientboundRecipeBookAddPacket.STREAM_CODEC.encode(buf, add)),
             rec(list(rec("42", rec("minecraft:stonecutter", stoneSlot, dirtSlot, cutter), "5", category,
                          list(list(item("stone"), item("dirt"))), "1"),
                      rec("43", rec("minecraft:stonecutter", dirtSlot, stoneSlot, cutter), "-", misc, "-", "2")),
                 "true"));

        RecipeBookSettings settings = new RecipeBookSettings();
        settings.setOpen(RecipeBookType.CRAFTING, true);
        settings.setFiltering(RecipeBookType.FURNACE, true);
        settings.setOpen(RecipeBookType.SMOKER, true);
        line("packet.recipe_book_settings", "named.recipe_book_settings",
             hex(buf -> ClientboundRecipeBookSettingsPacket.STREAM_CODEC.encode(buf, new ClientboundRecipeBookSettingsPacket(settings))),
             rec("true", "false", "false", "true", "false", "false", "true", "false"));

        ClientboundServerLinksPacket links = new ClientboundServerLinksPacket(List.of(
            new ServerLinks.UntrustedEntry(Either.left(ServerLinks.KnownLinkType.BUG_REPORT), "https://a.example/bugs"),
            new ServerLinks.UntrustedEntry(Either.right(Component.literal("Shop")), "https://b.example")));
        line("packet.server_links", "named.server_links",
             hex(buf -> ClientboundServerLinksPacket.STREAM_CODEC.encode(buf, links)),
             list(rec("builtin=" + e(ServerLinks.KnownLinkType.BUG_REPORT).replace("bug_report", "report_bug"),
                      "https://a.example/bugs"),
                  rec("custom=" + q("Shop"), "https://b.example")));

        ClientboundCustomReportDetailsPacket details = new ClientboundCustomReportDetailsPacket(Map.of("Plugin", "v1"));
        line("packet.custom_report_details", "named.custom_report_details",
             hex(buf -> ClientboundCustomReportDetailsPacket.STREAM_CODEC.encode(buf, details)), list(rec("Plugin", "v1")));

        ClientboundServerDataPacket data = new ClientboundServerDataPacket(Component.literal("motd"),
                                                                           Optional.of(new byte[] {65, 66, 67}));
        line("packet.server_data", "named.server_data",
             hex(buf -> ClientboundServerDataPacket.STREAM_CODEC.encode(buf, data)), rec(q("motd"), "ABC"));
    }
}
