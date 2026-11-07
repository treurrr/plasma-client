import java.io.PrintStream;
import java.lang.reflect.Field;
import java.util.Set;
import java.util.TreeSet;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.entity.BlockEntityType;

// Which blocks carry which block entity, asked of the game rather than guessed
// from their names: `BlockEntityType.validBlocks` is the whole answer, and a
// copper chest being a chest and a wall sign being a sign both fall out of it.
public final class ExportBlockEntities {
    static final PrintStream OUT = System.out;

    @SuppressWarnings("unchecked")
    public static void main(String[] args) throws Exception {
        net.minecraft.SharedConstants.tryDetectVersion();
        net.minecraft.server.Bootstrap.bootStrap();
        Field valid = BlockEntityType.class.getDeclaredField("validBlocks");
        valid.setAccessible(true);
        Set<String> lines = new TreeSet<>();
        for (BlockEntityType<?> type : BuiltInRegistries.BLOCK_ENTITY_TYPE) {
            Identifier name = BuiltInRegistries.BLOCK_ENTITY_TYPE.getKey(type);
            for (Block block : (Set<Block>) valid.get(type)) {
                lines.add(BuiltInRegistries.BLOCK.getKey(block) + "\t" + name);
            }
        }
        for (String line : lines) OUT.println(line);
    }
}
