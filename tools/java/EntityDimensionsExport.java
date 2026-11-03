import java.util.Map;
import java.util.TreeMap;
import net.minecraft.SharedConstants;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.entity.EntityDimensions;
import net.minecraft.world.entity.EntityType;

public class EntityDimensionsExport {
    public static void main(String[] args) {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        Map<String, String> rows = new TreeMap<>();
        for (EntityType<?> type : BuiltInRegistries.ENTITY_TYPE) {
            EntityDimensions size = type.getDimensions();
            String row = size.width() + " " + size.height() + " " + size.eyeHeight() + " "
                + type.getCategory().getName() + " " + (type.canSummon() ? "summonable" : "not_summonable")
                + (type.fireImmune() ? " fire_immune" : "");
            rows.put(BuiltInRegistries.ENTITY_TYPE.getKey(type).toString(), row);
        }
        for (Map.Entry<String, String> row : rows.entrySet()) {
            System.out.println("ROW " + row.getKey() + " " + row.getValue());
        }
    }
}
