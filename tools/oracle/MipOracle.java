import com.mojang.blaze3d.platform.NativeImage;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.List;
import java.util.zip.ZipFile;
import net.minecraft.client.renderer.texture.MipmapGenerator;
import net.minecraft.client.renderer.texture.MipmapStrategy;
import net.minecraft.resources.Identifier;
import net.minecraft.util.ARGB;

// Every mip level the game builds for a handful of block textures, one line
// per level: the digest of its pixels, in rgba order, row by row.
public class MipOracle {
    private static final List<String[]> TEXTURES = List.of(
        new String[]{"block/stone", "auto", "0.0"},
        new String[]{"block/grass_block_top", "auto", "0.0"},
        new String[]{"block/dirt", "auto", "0.0"},
        new String[]{"block/sand", "auto", "0.0"},
        new String[]{"block/stone_bricks", "auto", "0.0"},
        new String[]{"block/oak_planks", "auto", "0.0"},
        new String[]{"block/gravel", "auto", "0.0"},
        new String[]{"block/snow", "auto", "0.0"},
        new String[]{"block/oak_leaves", "dark_cutout", "0.0"},
        new String[]{"block/spruce_leaves", "dark_cutout", "0.0"},
        new String[]{"block/glass", "mean", "0.0"},
        new String[]{"block/poppy", "strict_cutout", "0.0"},
        new String[]{"block/dandelion", "strict_cutout", "0.0"},
        new String[]{"block/kelp", "auto", "0.1"},
        new String[]{"block/short_grass", "auto", "0.0"},
        new String[]{"block/fern", "auto", "0.0"},
        new String[]{"block/iron_bars", "auto", "0.0"},
        new String[]{"block/ladder", "auto", "0.0"},
        new String[]{"block/torch", "auto", "0.0"},
        new String[]{"block/water_still", "auto", "0.0"},
        new String[]{"block/lava_flow", "auto", "0.0"},
        new String[]{"block/oak_shelf", "auto", "0.0"},
        new String[]{"block/sea_lantern", "auto", "0.0"},
        new String[]{"block/cobweb", "auto", "0.0"},
        new String[]{"block/vine", "auto", "0.0"}
    );

    public static void main(String[] args) throws Exception {
        String jar = System.getenv("PLASMA_REFERENCE_JAR");
        if (jar == null) {
            jar = System.getProperty("user.home") + "/mc-reference/1.21.11/client-named.jar";
        }
        StringBuilder out = new StringBuilder();
        try (ZipFile archive = new ZipFile(jar)) {
            for (String[] entry : TEXTURES) {
                String name = entry[0];
                MipmapStrategy strategy = MipmapStrategy.valueOf(entry[1].toUpperCase());
                float bias = Float.parseFloat(entry[2]);
                NativeImage source;
                try (InputStream stream = archive.getInputStream(
                        archive.getEntry("assets/minecraft/textures/" + name + ".png"))) {
                    source = NativeImage.read(stream);
                }
                NativeImage[] levels = MipmapGenerator.generateMipLevels(
                    Identifier.withDefaultNamespace(name), new NativeImage[]{source}, 4,
                    strategy, bias);
                for (int level = 0; level < levels.length; level++) {
                    out.append("mip ").append(name).append('/').append(level).append(' ')
                       .append(levels[level].getWidth()).append('x')
                       .append(levels[level].getHeight()).append(':')
                       .append(digest(levels[level])).append('\n');
                }
            }
        }
        System.out.print(out);
    }

    private static String digest(NativeImage image) throws Exception {
        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        byte[] row = new byte[image.getWidth() * 4];
        for (int y = 0; y < image.getHeight(); y++) {
            for (int x = 0; x < image.getWidth(); x++) {
                int colour = image.getPixel(x, y);
                row[x * 4] = (byte) ARGB.red(colour);
                row[x * 4 + 1] = (byte) ARGB.green(colour);
                row[x * 4 + 2] = (byte) ARGB.blue(colour);
                row[x * 4 + 3] = (byte) ARGB.alpha(colour);
            }
            sha.update(row);
        }
        StringBuilder hex = new StringBuilder();
        for (byte b : sha.digest()) {
            hex.append(String.format("%02x", b));
        }
        return hex.substring(0, 16);
    }
}
