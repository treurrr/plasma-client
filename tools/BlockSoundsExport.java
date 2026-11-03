import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.IdentityHashMap;
import java.util.Map;
import java.util.TreeMap;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.sounds.SoundEvent;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.SoundType;

// Asks the game which sound type each block's default state has, and prints
// one tab-separated row per block, sorted by registry name, for
// tools/gen_block_sounds.py to write.
public class BlockSoundsExport {
    static String event(SoundEvent sound) {
        return sound.location().getPath();
    }

    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        Map<SoundType, String> names = new IdentityHashMap<>();
        for (Field field : SoundType.class.getFields()) {
            if (Modifier.isStatic(field.getModifiers()) && field.getType() == SoundType.class) {
                names.putIfAbsent((SoundType) field.get(null), field.getName().toLowerCase());
            }
        }
        Map<String, String> rows = new TreeMap<>();
        for (Block block : BuiltInRegistries.BLOCK) {
            SoundType type = block.defaultBlockState().getSoundType();
            String name = names.getOrDefault(type, "unnamed");
            rows.put(BuiltInRegistries.BLOCK.getKey(block).toString(),
                name + "\t" + type.getVolume() + "\t" + type.getPitch() + "\t" + event(type.getBreakSound()) + "\t"
                    + event(type.getStepSound()) + "\t" + event(type.getPlaceSound()) + "\t" + event(type.getHitSound())
                    + "\t" + event(type.getFallSound()));
        }
        for (Map.Entry<String, String> row : rows.entrySet()) {
            System.out.println("ROW\t" + row.getKey() + "\t" + row.getValue());
        }
    }
}
