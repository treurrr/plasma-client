import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.function.Consumer;
import net.minecraft.client.model.geom.EntityModelSet;
import net.minecraft.client.model.geom.ModelLayers;
import net.minecraft.client.model.object.chest.ChestModel;
import net.minecraft.client.model.object.equipment.ShieldModel;
import net.minecraft.client.model.object.skull.SkullModelBase;
import net.minecraft.client.model.object.statue.CopperGolemStatueModel;
import net.minecraft.client.renderer.block.model.BlockModel;
import net.minecraft.client.renderer.block.model.ItemTransform;
import net.minecraft.client.renderer.blockentity.BannerRenderer;
import net.minecraft.client.renderer.blockentity.BedRenderer;
import net.minecraft.client.renderer.blockentity.DecoratedPotRenderer;
import net.minecraft.client.renderer.blockentity.ShulkerBoxRenderer;
import net.minecraft.client.renderer.blockentity.SkullBlockRenderer;
import net.minecraft.client.renderer.special.BannerSpecialRenderer;
import net.minecraft.client.renderer.special.BedSpecialRenderer;
import net.minecraft.client.renderer.special.ChestSpecialRenderer;
import net.minecraft.client.renderer.special.ConduitSpecialRenderer;
import net.minecraft.client.renderer.special.CopperGolemStatueSpecialRenderer;
import net.minecraft.client.renderer.special.DecoratedPotSpecialRenderer;
import net.minecraft.client.renderer.special.ShieldSpecialRenderer;
import net.minecraft.client.renderer.special.ShulkerBoxSpecialRenderer;
import net.minecraft.client.renderer.special.SkullSpecialRenderer;
import net.minecraft.client.renderer.special.SpecialModelRenderer;
import net.minecraft.core.Direction;
import net.minecraft.resources.Identifier;
import net.minecraft.world.item.DyeColor;
import net.minecraft.world.item.ItemDisplayContext;
import net.minecraft.world.level.block.SkullBlock;
import org.joml.Matrix4f;
import org.joml.Vector3fc;

// What the game draws for an item whose model is a block entity's: the `gui`
// display transform of each base model, out of the player's own jar, and every
// vertex each special renderer places, out of the renderer itself. Positions
// are sorted, because a part's children hang in a hash map and come out in no
// order worth matching; the multiset is what has to be the same. The probe
// prints the same values from the entity model table and our own placements.
public final class SpecialItemOracle {
    // Taken before the game starts, which wraps the real one in its log.
    static final java.io.PrintStream OUT = System.out;

    static int bits(float v) {
        return Float.floatToIntBits(v);
    }

    // The model chain in the jar, which is where the display transforms live.
    static BlockModel model(String reference) throws Exception {
        String path = "/assets/minecraft/models/" + reference.split(":", 2)[reference.contains(":") ? 1 : 0]
            + ".json";
        try (InputStream in = SpecialItemOracle.class.getResourceAsStream(path)) {
            if (in == null) throw new IllegalStateException("no " + path);
            return BlockModel.fromStream(new InputStreamReader(in, "UTF-8"));
        }
    }

    // `ResolvedModel.findTopTransform`: the nearest ancestor that places this
    // context, which is how every chest item shares one turn in a slot.
    static ItemTransform gui(String reference) throws Exception {
        for (int depth = 0; depth < 16; depth++) {
            BlockModel model = model(reference);
            if (model.transforms() != null) {
                ItemTransform found = model.transforms().getTransform(ItemDisplayContext.GUI);
                if (found != ItemTransform.NO_TRANSFORM) return found;
            }
            if (model.parent() == null) break;
            reference = model.parent().toString();
        }
        return ItemTransform.NO_TRANSFORM;
    }

    static void transform(String name, String base) throws Exception {
        com.mojang.blaze3d.vertex.PoseStack stack = new com.mojang.blaze3d.vertex.PoseStack();
        gui(base).apply(false, stack.last());
        Matrix4f m = stack.last().pose();
        StringBuilder out = new StringBuilder();
        for (int column = 0; column < 4; column++) {
            for (int row = 0; row < 4; row++) {
                if (out.length() > 0) out.append(',');
                out.append(bits(m.get(column, row)));
            }
        }
        OUT.println("gui " + name + " " + out);
    }

    static void vertices(String name, SpecialModelRenderer<?> renderer,
                         net.minecraft.client.model.geom.ModelPart... also) {
        List<String> found = new ArrayList<>();
        Consumer<Vector3fc> collect = v -> found.add(
            bits(v.x()) + "," + bits(v.y()) + "," + bits(v.z()));
        renderer.getExtents(collect);
        // A renderer's extents cover only what the game needs them for; a part
        // it still draws is asked for here as well.
        for (net.minecraft.client.model.geom.ModelPart part : also) {
            part.getExtentsForGui(new com.mojang.blaze3d.vertex.PoseStack(), collect);
        }
        Collections.sort(found);
        OUT.println("count " + name + " " + found.size());
        for (int i = 0; i < found.size(); i++) {
            OUT.println("vertex " + name + "/" + i + " " + found.get(i));
        }
    }

    // Every corner of every cube in one layer, in its own part's space, with
    // the texture window and the facing it carries. Sorted for the same reason
    // the placed vertices are.
    @SuppressWarnings("unchecked")
    static void geometry(String layer, net.minecraft.client.model.geom.ModelPart root)
            throws Exception {
        java.lang.reflect.Field cubes =
            net.minecraft.client.model.geom.ModelPart.class.getDeclaredField("cubes");
        java.lang.reflect.Field children =
            net.minecraft.client.model.geom.ModelPart.class.getDeclaredField("children");
        cubes.setAccessible(true);
        children.setAccessible(true);
        List<String> found = new ArrayList<>();
        List<net.minecraft.client.model.geom.ModelPart> queue = new ArrayList<>();
        queue.add(root);
        while (!queue.isEmpty()) {
            net.minecraft.client.model.geom.ModelPart part = queue.remove(queue.size() - 1);
            queue.addAll(((java.util.Map<String, net.minecraft.client.model.geom.ModelPart>)
                children.get(part)).values());
            for (net.minecraft.client.model.geom.ModelPart.Cube cube
                     : (List<net.minecraft.client.model.geom.ModelPart.Cube>) cubes.get(part)) {
                for (net.minecraft.client.model.geom.ModelPart.Polygon polygon : cube.polygons) {
                    String facing = Math.round(polygon.normal().x() * 127) + ","
                        + Math.round(polygon.normal().y() * 127) + ","
                        + Math.round(polygon.normal().z() * 127);
                    for (net.minecraft.client.model.geom.ModelPart.Vertex v : polygon.vertices()) {
                        found.add(bits(v.worldX()) + "," + bits(v.worldY()) + ","
                            + bits(v.worldZ()) + "," + bits(v.u()) + "," + bits(v.v())
                            + "," + facing);
                    }
                }
            }
        }
        Collections.sort(found);
        OUT.println("cubes " + layer + " " + found.size());
        for (int i = 0; i < found.size(); i++) {
            OUT.println("cube " + layer + "/" + i + " " + found.get(i));
        }
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.SharedConstants.tryDetectVersion();
        net.minecraft.server.Bootstrap.bootStrap();
        EntityModelSet models = EntityModelSet.vanilla();

        transform("chest", "minecraft:item/chest");
        transform("bed", "minecraft:item/white_bed");
        transform("banner", "minecraft:item/template_banner");
        transform("shulker_box", "minecraft:item/white_shulker_box");
        transform("skull", "minecraft:item/template_skull");
        transform("dragon_head", "minecraft:item/dragon_head");
        transform("conduit", "minecraft:item/conduit");
        transform("decorated_pot", "minecraft:item/decorated_pot");
        transform("statue", "minecraft:item/template_copper_golem_statue");
        transform("shield", "minecraft:item/shield");

        vertices("chest", new ChestSpecialRenderer(
            null, new ChestModel(models.bakeLayer(ModelLayers.CHEST)), null, 0.0F));
        vertices("bed", new BedSpecialRenderer(new BedRenderer(null, models), null));
        vertices("banner", new BannerSpecialRenderer(DyeColor.WHITE, new BannerRenderer(models, null)));
        vertices("shulker_box", new ShulkerBoxSpecialRenderer(
            new ShulkerBoxRenderer(models, null), 0.0F, Direction.UP, null));
        for (SkullBlock.Types kind : SkullBlock.Types.values()) {
            SkullModelBase model = SkullBlockRenderer.createModel(models, kind);
            vertices("skull_" + kind.getSerializedName(),
                     new SkullSpecialRenderer(model, 0.0F, null));
        }
        vertices("conduit", new ConduitSpecialRenderer(
            null, models.bakeLayer(ModelLayers.CONDUIT_SHELL)));
        net.minecraft.client.model.geom.ModelPart sides =
            models.bakeLayer(ModelLayers.DECORATED_POT_SIDES);
        vertices("decorated_pot", new DecoratedPotSpecialRenderer(
            new DecoratedPotRenderer(models, null)),
            sides.getChild("front"), sides.getChild("back"), sides.getChild("left"),
            sides.getChild("right"));
        vertices("statue", new CopperGolemStatueSpecialRenderer(
            new CopperGolemStatueModel(models.bakeLayer(ModelLayers.COPPER_GOLEM)),
            Identifier.withDefaultNamespace("textures/entity/copper_golem/copper_golem.png")));
        vertices("shield", new ShieldSpecialRenderer(
            null, new ShieldModel(models.bakeLayer(ModelLayers.SHIELD))));

        geometry("chest", models.bakeLayer(ModelLayers.CHEST));
        geometry("bed_head", models.bakeLayer(ModelLayers.BED_HEAD));
        geometry("bed_foot", models.bakeLayer(ModelLayers.BED_FOOT));
        geometry("standing_banner", models.bakeLayer(ModelLayers.STANDING_BANNER));
        geometry("standing_banner#flag", models.bakeLayer(ModelLayers.STANDING_BANNER_FLAG));
        geometry("shulker_box", models.bakeLayer(ModelLayers.SHULKER_BOX));
        geometry("skeleton_skull", models.bakeLayer(ModelLayers.SKELETON_SKULL));
        geometry("wither_skeleton_skull", models.bakeLayer(ModelLayers.WITHER_SKELETON_SKULL));
        geometry("player_head", models.bakeLayer(ModelLayers.PLAYER_HEAD));
        geometry("zombie_head", models.bakeLayer(ModelLayers.ZOMBIE_HEAD));
        geometry("creeper_head", models.bakeLayer(ModelLayers.CREEPER_HEAD));
        geometry("piglin_head", models.bakeLayer(ModelLayers.PIGLIN_HEAD));
        geometry("dragon_skull", models.bakeLayer(ModelLayers.DRAGON_SKULL));
        geometry("conduit#shell", models.bakeLayer(ModelLayers.CONDUIT_SHELL));
        geometry("decorated_pot_base", models.bakeLayer(ModelLayers.DECORATED_POT_BASE));
        geometry("decorated_pot_sides", models.bakeLayer(ModelLayers.DECORATED_POT_SIDES));
        geometry("copper_golem", models.bakeLayer(ModelLayers.COPPER_GOLEM));
        geometry("shield", models.bakeLayer(ModelLayers.SHIELD));
    }
}
