import java.io.PrintStream;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import net.minecraft.client.animation.AnimationChannel;
import net.minecraft.client.animation.AnimationDefinition;
import net.minecraft.client.animation.Keyframe;
import org.joml.Vector3fc;

// Asks the game for every keyframe animation it ships and prints them in the
// format of animations-<protocol>.txt. Runs against the player's own jar; the
// jar is also read to find the classes, since nothing registers them.
public final class ExportAnimations {
    private static final String PACKAGE = "net/minecraft/client/animation/definitions/";

    private static String f(float v) {
        if (v == (long) v && Math.abs(v) < 1e7f) return Long.toString((long) v);
        return Float.toString(v);
    }

    private static String three(Vector3fc v) {
        return f(v.x()) + " " + f(v.y()) + " " + f(v.z());
    }

    private static String target(AnimationChannel.Target t) {
        if (t == AnimationChannel.Targets.POSITION) return "position";
        if (t == AnimationChannel.Targets.ROTATION) return "rotation";
        if (t == AnimationChannel.Targets.SCALE) return "scale";
        throw new IllegalArgumentException("unknown animation target " + t);
    }

    private static String interpolation(AnimationChannel.Interpolation i) {
        if (i == AnimationChannel.Interpolations.LINEAR) return "linear";
        if (i == AnimationChannel.Interpolations.CATMULLROM) return "catmullrom";
        throw new IllegalArgumentException("unknown interpolation " + i);
    }

    private static List<String> classNames(String jar) throws Exception {
        List<String> names = new ArrayList<>();
        try (ZipFile zip = new ZipFile(jar)) {
            for (Enumeration<? extends ZipEntry> entries = zip.entries(); entries.hasMoreElements();) {
                String name = entries.nextElement().getName();
                if (!name.startsWith(PACKAGE) || !name.endsWith("Animation.class")) continue;
                names.add(name.substring(0, name.length() - 6).replace('/', '.'));
            }
        }
        names.sort(null);
        return names;
    }

    private static void definition(PrintStream out, String name, AnimationDefinition definition) {
        out.println();
        out.println("[" + name + "]");
        out.println("length " + f(definition.lengthInSeconds()));
        if (definition.looping()) out.println("loop");
        Map<String, List<AnimationChannel>> bones = new TreeMap<>(definition.boneAnimations());
        for (Map.Entry<String, List<AnimationChannel>> bone : bones.entrySet()) {
            for (AnimationChannel channel : bone.getValue()) {
                out.println("channel " + bone.getKey() + " " + target(channel.target()));
                for (Keyframe key : channel.keyframes()) {
                    StringBuilder line = new StringBuilder("  key ").append(f(key.timestamp()));
                    if (!key.preTarget().equals(key.postTarget())) {
                        line.append(' ').append(three(key.preTarget())).append(" ->");
                    }
                    line.append(' ').append(three(key.postTarget()));
                    out.println(line.append(' ').append(interpolation(key.interpolation())));
                }
            }
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("usage: ExportAnimations <client.jar>");
        PrintStream out = new PrintStream(System.out, false, "UTF-8");
        for (String className : classNames(args[0])) {
            Class<?> type = Class.forName(className);
            Field[] fields = type.getFields();
            Arrays.sort(fields, (a, b) -> a.getName().compareTo(b.getName()));
            for (Field field : fields) {
                if (!Modifier.isStatic(field.getModifiers())) continue;
                if (field.getType() != AnimationDefinition.class) continue;
                definition(out, type.getSimpleName() + "." + field.getName(),
                           (AnimationDefinition) field.get(null));
            }
        }
        out.flush();
    }
}
