import net.minecraft.util.Mth;
import net.minecraft.world.level.levelgen.LegacyRandomSource;

// Prints what the game's own Mth and seeded random return for fixed inputs,
// one value per line, for tools/oracle/compare.py to lay beside jmath_probe.
public class MathOracle {
    public static void main(String[] args) {
        double[] angles = {0.0, 0.3, -0.3, 1.0, Math.PI / 2, Math.PI, 2.5, -7.25, 100.0, 12345.678};
        for (double a : angles) {
            System.out.println("sin " + a + " " + Float.floatToIntBits(Mth.sin(a)));
            System.out.println("cos " + a + " " + Float.floatToIntBits(Mth.cos(a)));
        }
        long[] seeds = {0L, 42L, -1L, 123456789L, Long.MIN_VALUE};
        for (long seed : seeds) {
            LegacyRandomSource r = new LegacyRandomSource(seed);
            System.out.println("int " + seed + " " + r.nextInt());
            System.out.println("bound10 " + seed + " " + r.nextInt(10));
            System.out.println("bound16 " + seed + " " + r.nextInt(16));
            System.out.println("long " + seed + " " + r.nextLong());
            System.out.println("float " + seed + " " + Float.floatToIntBits(r.nextFloat()));
            System.out.println("double " + seed + " " + Double.doubleToLongBits(r.nextDouble()));
            System.out.println("bool " + seed + " " + r.nextBoolean());
        }
    }
}
