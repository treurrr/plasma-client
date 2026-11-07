import java.io.PrintStream;
import java.lang.reflect.Field;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import net.minecraft.client.model.geom.LayerDefinitions;
import net.minecraft.client.model.geom.ModelLayerLocation;
import net.minecraft.client.model.geom.PartPose;
import net.minecraft.client.model.geom.builders.CubeDefinition;
import net.minecraft.client.model.geom.builders.CubeDeformation;
import net.minecraft.client.model.geom.builders.LayerDefinition;
import net.minecraft.client.model.geom.builders.MaterialDefinition;
import net.minecraft.client.model.geom.builders.MeshDefinition;
import net.minecraft.client.model.geom.builders.PartDefinition;
import net.minecraft.client.model.geom.builders.UVPair;
import net.minecraft.core.Direction;
import org.joml.Vector3fc;

// Asks the game for every model layer it builds and prints their part trees in
// the format of entity_models-<protocol>.txt. Runs against the player's own jar.
public final class ExportModels {
    private static Field field(Class<?> type, String name) throws Exception {
        Field f = type.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    private static final Field MESH, MATERIAL, TEX_W, TEX_H, CUBES, POSE, CHILDREN;
    private static final Field ORIGIN, DIMENSIONS, GROW, MIRROR, TEX_COORD, TEX_SCALE, FACES;
    private static final Field GROW_X, GROW_Y, GROW_Z;

    static {
        try {
            MESH = field(LayerDefinition.class, "mesh");
            MATERIAL = field(LayerDefinition.class, "material");
            TEX_W = field(MaterialDefinition.class, "xTexSize");
            TEX_H = field(MaterialDefinition.class, "yTexSize");
            CUBES = field(PartDefinition.class, "cubes");
            POSE = field(PartDefinition.class, "partPose");
            CHILDREN = field(PartDefinition.class, "children");
            ORIGIN = field(CubeDefinition.class, "origin");
            DIMENSIONS = field(CubeDefinition.class, "dimensions");
            GROW = field(CubeDefinition.class, "grow");
            MIRROR = field(CubeDefinition.class, "mirror");
            TEX_COORD = field(CubeDefinition.class, "texCoord");
            TEX_SCALE = field(CubeDefinition.class, "texScale");
            FACES = field(CubeDefinition.class, "visibleFaces");
            GROW_X = field(CubeDeformation.class, "growX");
            GROW_Y = field(CubeDeformation.class, "growY");
            GROW_Z = field(CubeDeformation.class, "growZ");
        } catch (Exception e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static String f(float v) {
        if (v == (long) v && Math.abs(v) < 1e7f) return Long.toString((long) v);
        return Float.toString(v);
    }

    private static String three(float x, float y, float z) {
        return f(x) + " " + f(y) + " " + f(z);
    }

    private static String faces(Set<Direction> visible) {
        StringBuilder out = new StringBuilder();
        for (Direction d : Direction.values()) {
            if (visible.contains(d)) out.append(d.getName().charAt(0));
        }
        return out.toString();
    }

    @SuppressWarnings("unchecked")
    private static void part(PrintStream out, String path, PartDefinition part, int texW, int texH)
            throws Exception {
        PartPose pose = (PartPose) POSE.get(part);
        StringBuilder line = new StringBuilder("part ").append(path.isEmpty() ? "." : path);
        if (pose.x() != 0 || pose.y() != 0 || pose.z() != 0) {
            line.append(" pivot ").append(three(pose.x(), pose.y(), pose.z()));
        }
        if (pose.xRot() != 0 || pose.yRot() != 0 || pose.zRot() != 0) {
            line.append(" rotate ").append(three(pose.xRot(), pose.yRot(), pose.zRot()));
        }
        if (pose.xScale() != 1 || pose.yScale() != 1 || pose.zScale() != 1) {
            line.append(" scale ").append(three(pose.xScale(), pose.yScale(), pose.zScale()));
        }
        out.println(line);
        for (CubeDefinition cube : (List<CubeDefinition>) CUBES.get(part)) {
            Vector3fc o = (Vector3fc) ORIGIN.get(cube);
            Vector3fc d = (Vector3fc) DIMENSIONS.get(cube);
            CubeDeformation g = (CubeDeformation) GROW.get(cube);
            UVPair uv = (UVPair) TEX_COORD.get(cube);
            UVPair scale = (UVPair) TEX_SCALE.get(cube);
            Set<Direction> visible = (Set<Direction>) FACES.get(cube);
            StringBuilder box = new StringBuilder("  box ");
            box.append(three(o.x(), o.y(), o.z())).append("  ").append(three(d.x(), d.y(), d.z()))
                .append("  uv ").append((int) uv.u()).append(' ').append((int) uv.v());
            float gx = GROW_X.getFloat(g), gy = GROW_Y.getFloat(g), gz = GROW_Z.getFloat(g);
            if (gx != 0 || gy != 0 || gz != 0) {
                box.append(" grow ").append(f(gx));
                if (gy != gx || gz != gx) box.append(' ').append(f(gy)).append(' ').append(f(gz));
            }
            if ((boolean) MIRROR.get(cube)) box.append(" mirror");
            if (scale.u() != 1 || scale.v() != 1) {
                box.append(" texsize ").append(f(texW * scale.u())).append(' ').append(f(texH * scale.v()));
            }
            if (visible.size() != 6) box.append(" faces ").append(faces(visible));
            out.println(box);
        }
        Map<String, PartDefinition> children =
            new TreeMap<>((Map<String, PartDefinition>) CHILDREN.get(part));
        for (Map.Entry<String, PartDefinition> child : children.entrySet()) {
            part(out, path.isEmpty() ? child.getKey() : path + "/" + child.getKey(), child.getValue(),
                 texW, texH);
        }
    }

    private static String name(ModelLayerLocation location) {
        String model = location.model().getPath();
        if (!location.model().getNamespace().equals("minecraft")) {
            model = location.model().getNamespace() + ":" + model;
        }
        return location.layer().equals("main") ? model : model + "#" + location.layer();
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.SharedConstants.tryDetectVersion();
        net.minecraft.server.Bootstrap.bootStrap();
        Map<String, LayerDefinition> layers = new TreeMap<>();
        for (Map.Entry<ModelLayerLocation, LayerDefinition> entry :
             LayerDefinitions.createRoots().entrySet()) {
            layers.put(name(entry.getKey()), entry.getValue());
        }
        PrintStream out = new PrintStream(System.out, false, "UTF-8");
        for (Map.Entry<String, LayerDefinition> layer : layers.entrySet()) {
            MeshDefinition mesh = (MeshDefinition) MESH.get(layer.getValue());
            MaterialDefinition material = (MaterialDefinition) MATERIAL.get(layer.getValue());
            int texW = TEX_W.getInt(material);
            int texH = TEX_H.getInt(material);
            out.println();
            out.println("[" + layer.getKey() + "]");
            out.println("texture " + texW + " " + texH);
            part(out, "", mesh.getRoot(), texW, texH);
        }
        out.flush();
    }
}
