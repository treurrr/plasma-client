import com.google.gson.JsonElement;
import com.google.gson.JsonParser;
import java.io.InputStreamReader;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import net.minecraft.SharedConstants;
import net.minecraft.core.Holder;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.HolderSet;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.data.registries.VanillaRegistries;
import net.minecraft.network.chat.contents.TranslatableContents;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.Bootstrap;
import net.minecraft.tags.TagKey;
import net.minecraft.world.flag.FeatureFlags;
import net.minecraft.world.item.CreativeModeTab;
import net.minecraft.world.item.CreativeModeTabs;
import net.minecraft.world.item.ItemStack;

// Asks the game for its creative tabs - which, where, with what icon and in
// what order their items come - and prints one "tab" line per tab followed by
// one "item" line per stack, then the search tab's "search" lines.
// tools/bake_creative.py keeps the output beside the other baked data.
public class CreativeTabsExport {
    // The game sends System.out through its logger once bootstrapped; the
    // export goes to the real stdout instead.
    static final java.io.PrintStream OUT = new java.io.PrintStream(
        new java.io.FileOutputStream(java.io.FileDescriptor.out), true, StandardCharsets.UTF_8);

    // A data pack's tags are not loaded without a server, and a few tabs ask a
    // holder whether it is in one; they are read from the jar and bound here.
    static <T> HolderLookup.RegistryLookup<T> bind(HolderLookup.Provider lookup,
                         ResourceKey<? extends Registry<? extends T>> key, String folder) throws Exception {
        String prefix = "data/minecraft/tags/" + folder + "/";
        Map<String, List<String>> raw = new HashMap<>();
        String jar = Bootstrap.class.getProtectionDomain().getCodeSource().getLocation().getPath();
        try (ZipFile zip = new ZipFile(jar)) {
            for (ZipEntry entry : Collections.list(zip.entries())) {
                String name = entry.getName();
                if (!name.startsWith(prefix) || !name.endsWith(".json")) continue;
                String tag = name.substring(prefix.length(), name.length() - 5);
                List<String> values = new ArrayList<>();
                JsonElement json = JsonParser.parseReader(
                    new InputStreamReader(zip.getInputStream(entry), StandardCharsets.UTF_8));
                for (JsonElement value : json.getAsJsonObject().getAsJsonArray("values")) {
                    values.add(value.isJsonObject() ? value.getAsJsonObject().get("id").getAsString()
                                                    : value.getAsString());
                }
                raw.put(tag, values);
            }
        }
        Map<String, Set<String>> members = new HashMap<>();
        for (String tag : raw.keySet()) members.put(tag, expand(tag, raw, new HashSet<>()));
        Method method = Holder.Reference.class.getDeclaredMethod("bindTags", Collection.class);
        method.setAccessible(true);
        @SuppressWarnings("unchecked")
        HolderLookup.RegistryLookup<T> registry = (HolderLookup.RegistryLookup<T>) lookup.lookupOrThrow(key);
        @SuppressWarnings("unchecked")
        ResourceKey<? extends Registry<T>> registryKey = (ResourceKey<? extends Registry<T>>) key;
        registry.listElements().forEach(holder -> {
            List<TagKey<T>> tags = new ArrayList<>();
            String id = holder.key().identifier().toString();
            for (Map.Entry<String, Set<String>> tag : members.entrySet()) {
                if (tag.getValue().contains(id)) {
                    tags.add(TagKey.create(registryKey, Identifier.withDefaultNamespace(tag.getKey())));
                }
            }
            try {
                method.invoke(holder, tags);
            } catch (ReflectiveOperationException error) {
                throw new RuntimeException(error);
            }
        });
        Method fill = HolderSet.Named.class.getDeclaredMethod("bind", List.class);
        fill.setAccessible(true);
        java.lang.reflect.Constructor<?> make = HolderSet.Named.class.getDeclaredConstructor(
            net.minecraft.core.HolderOwner.class, TagKey.class);
        make.setAccessible(true);
        Map<String, HolderSet.Named<T>> sets = new HashMap<>();
        for (Map.Entry<String, Set<String>> tag : members.entrySet()) {
            @SuppressWarnings("unchecked")
            HolderSet.Named<T> set = (HolderSet.Named<T>) make.newInstance(registry,
                TagKey.create(registryKey, Identifier.withDefaultNamespace(tag.getKey())));
            List<Holder<T>> holders = registry.listElements()
                .filter(h -> tag.getValue().contains(h.key().identifier().toString()))
                .collect(Collectors.toList());
            fill.invoke(set, holders);
            sets.put(tag.getKey(), set);
        }
        return new HolderLookup.RegistryLookup.Delegate<T>() {
            @Override
            public HolderLookup.RegistryLookup<T> parent() {
                return registry;
            }

            @Override
            public Optional<HolderSet.Named<T>> get(TagKey<T> tag) {
                return Optional.ofNullable(sets.get(tag.location().getPath()));
            }
        };
    }

    static Set<String> expand(String tag, Map<String, List<String>> raw, Set<String> seen) {
        Set<String> out = new HashSet<>();
        if (!seen.add(tag) || !raw.containsKey(tag)) return out;
        for (String value : raw.get(tag)) {
            if (value.startsWith("#")) {
                out.addAll(expand(value.substring(1).replace("minecraft:", ""), raw, seen));
            } else {
                out.add(value.contains(":") ? value : "minecraft:" + value);
            }
        }
        return out;
    }

    static String line(String kind, ItemStack stack) {
        return String.join("\t", kind, BuiltInRegistries.ITEM.getKey(stack.getItem()).getPath(),
            String.valueOf(stack.getCount()),
            stack.getComponentsPatch().isEmpty() ? "-" : stack.getHoverName().getString());
    }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        HolderLookup.Provider vanilla = VanillaRegistries.createLookup();
        Map<ResourceKey<?>, HolderLookup.RegistryLookup<?>> tagged = new HashMap<>();
        tagged.put(Registries.PAINTING_VARIANT, bind(vanilla, Registries.PAINTING_VARIANT, "painting_variant"));
        tagged.put(Registries.INSTRUMENT, bind(vanilla, Registries.INSTRUMENT, "instrument"));
        tagged.put(Registries.ENCHANTMENT, bind(vanilla, Registries.ENCHANTMENT, "enchantment"));
        HolderLookup.Provider lookup = HolderLookup.Provider.create(vanilla.listRegistries()
            .map(r -> tagged.containsKey(r.key()) ? tagged.get(r.key()) : r));
        CreativeModeTabs.tryRebuildTabContents(FeatureFlags.DEFAULT_FLAGS, true, lookup);
        for (CreativeModeTab tab : CreativeModeTabs.allTabs()) {
            String key = BuiltInRegistries.CREATIVE_MODE_TAB.getKey(tab).getPath();
            String title = tab.getDisplayName().getContents() instanceof TranslatableContents t
                ? t.getKey() : tab.getDisplayName().getString();
            OUT.println(String.join("\t", "tab", key, tab.getType().name(), tab.row().name(),
                String.valueOf(tab.column()), String.valueOf(tab.isAlignedRight()),
                String.valueOf(tab.showTitle()), String.valueOf(tab.canScroll()),
                String.valueOf(tab.shouldDisplay()), tab.getBackgroundTexture().getPath(),
                BuiltInRegistries.ITEM.getKey(tab.getIconItem().getItem()).getPath(), title));
            for (ItemStack stack : tab.getDisplayItems()) OUT.println(line("item", stack));
        }
        for (ItemStack stack : CreativeModeTabs.searchTab().getSearchTabDisplayItems()) {
            OUT.println(line("search", stack));
        }
    }
}
