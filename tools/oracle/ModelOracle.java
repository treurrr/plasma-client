import java.lang.reflect.Field;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import net.minecraft.client.model.geom.LayerDefinitions;
import net.minecraft.client.model.geom.ModelLayerLocation;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.client.model.geom.builders.LayerDefinition;
import net.minecraft.client.model.player.PlayerModel;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;
import net.minecraft.world.entity.HumanoidArm;

// The game's own baked model layers and its player animation on fixed inputs,
// printed as bit patterns: every polygon corner of the layers named, then
// every part of the player model after setupAnim for each case. The probe
// prints the same from data/entity_models.txt and our animation.
public final class ModelOracle {
    static final String[] LAYERS = {"player", "player_slim", "zombie", "creeper", "pig", "chicken"};

    static Field field(Class<?> type, String name) throws Exception {
        Field f = type.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static int bits(float v) {
        return Float.floatToIntBits(v);
    }

    @SuppressWarnings("unchecked")
    static void geometry(String layer, String path, ModelPart part, Field cubes, Field children)
            throws Exception {
        List<ModelPart.Cube> list = (List<ModelPart.Cube>) cubes.get(part);
        for (int c = 0; c < list.size(); c++) {
            ModelPart.Polygon[] polygons = list.get(c).polygons;
            for (int p = 0; p < polygons.length; p++) {
                ModelPart.Polygon polygon = polygons[p];
                String at = layer + "/" + path + "/" + c + "/" + p;
                System.out.println("normal " + at + " " + Math.round(polygon.normal().x() * 127) + ","
                    + Math.round(polygon.normal().y() * 127) + "," + Math.round(polygon.normal().z() * 127));
                for (int k = 0; k < 4; k++) {
                    ModelPart.Vertex v = polygon.vertices()[k];
                    System.out.println("corner " + at + "/" + k + " " + bits(v.worldX()) + ","
                        + bits(v.worldY()) + "," + bits(v.worldZ()) + "," + bits(v.u()) + "," + bits(v.v()));
                }
            }
        }
        Map<String, ModelPart> sorted = new TreeMap<>((Map<String, ModelPart>) children.get(part));
        for (Map.Entry<String, ModelPart> child : sorted.entrySet()) {
            geometry(layer, path.equals("root") ? child.getKey() : path + "/" + child.getKey(),
                     child.getValue(), cubes, children);
        }
    }

    @SuppressWarnings("unchecked")
    static void pose(String label, String path, ModelPart part, Field children) throws Exception {
        if (!path.equals("root")) {
            System.out.println("pose " + label + "/" + path + " " + bits(part.x) + "," + bits(part.y) + ","
                + bits(part.z) + "," + bits(part.xRot) + "," + bits(part.yRot) + "," + bits(part.zRot) + ","
                + (part.visible ? 1 : 0));
        }
        Map<String, ModelPart> sorted = new TreeMap<>((Map<String, ModelPart>) children.get(part));
        for (Map.Entry<String, ModelPart> child : sorted.entrySet()) {
            pose(label, path.equals("root") ? child.getKey() : path + "/" + child.getKey(),
                 child.getValue(), children);
        }
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.SharedConstants.tryDetectVersion();
        net.minecraft.server.Bootstrap.bootStrap();
        Field cubes = field(ModelPart.class, "cubes");
        Field children = field(ModelPart.class, "children");
        Map<String, LayerDefinition> layers = new TreeMap<>();
        for (Map.Entry<ModelLayerLocation, LayerDefinition> e : LayerDefinitions.createRoots().entrySet()) {
            if (e.getKey().layer().equals("main")) layers.put(e.getKey().model().getPath(), e.getValue());
        }
        for (String name : LAYERS) geometry(name, "root", layers.get(name).bakeRoot(), cubes, children);

        // walk position, walk speed, head yaw, pitch, age, attack, crouch, swim, left arm swings, passenger
        float[][] cases = {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {3.7f, 1, -30, 20, 37.5f, 0, 0, 0, 0, 0},
            {11.25f, 0.43f, 47, -61, 1234.6f, 0.3f, 0, 0, 0, 0},
            {5, 0.8f, 10, 5, 99.9f, 0.75f, 1, 0, 0, 0},
            {8, 0.2f, -5, -89, 3.3f, 0.5f, 0, 0, 1, 0},
            {6.5f, 1, 0, 40, 20, 0, 0, 0.6f, 0, 0},
            {17.2f, 1, 0, 0, 21, 0.2f, 0, 1, 0, 0},
            {24.9f, 0.9f, 15, 15, 22, 0, 1, 0.35f, 1, 0},
            {2, 0.5f, 0, 0, 5, 0, 0, 0, 0, 1},
        };
        for (int slim = 0; slim < 2; slim++) {
            String layer = slim == 1 ? "player_slim" : "player";
            PlayerModel model = new PlayerModel(layers.get(layer).bakeRoot(), slim == 1);
            for (int i = 0; i < cases.length; i++) {
                float[] c = cases[i];
                AvatarRenderState state = new AvatarRenderState();
                state.walkAnimationPos = c[0];
                state.walkAnimationSpeed = c[1];
                state.yRot = c[2];
                state.xRot = c[3];
                state.ageInTicks = c[4];
                state.attackTime = c[5];
                state.isCrouching = c[6] != 0;
                state.swimAmount = c[7];
                state.attackArm = c[8] != 0 ? HumanoidArm.LEFT : HumanoidArm.RIGHT;
                state.isPassenger = c[9] != 0;
                model.setupAnim(state);
                pose(layer + "#" + i, "root", model.root(), children);
            }
        }
    }
}
