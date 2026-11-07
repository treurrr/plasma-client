import com.mojang.blaze3d.vertex.PoseStack;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import net.minecraft.SharedConstants;
import net.minecraft.client.renderer.ItemInHandRenderer;
import net.minecraft.client.renderer.block.model.ItemTransform;
import net.minecraft.server.Bootstrap;
import net.minecraft.world.entity.HumanoidArm;
import org.joml.Matrix4f;
import org.joml.Vector3f;

// The game's own first person hand for hand_probe: where the arm and the held
// item sit over a swing and over an equip, the eat and brush jiggles, the hurt
// reel, the walk bob, and the matrix a display transform makes.
//
// Every one of these is matrix arithmetic on a PoseStack, so the renderer is
// allocated without a game behind it and its private methods are called
// through reflection. Each line ends with the sixteen floats of the pose as
// their bits, so a probe either reproduces the matrix exactly or does not.
public class HandOracle {
    static final java.io.PrintStream OUT = System.out;
    static final sun.misc.Unsafe U = unsafe();
    static ItemInHandRenderer HAND;
    static net.minecraft.world.item.ItemStack FOOD;
    static int USE_REMAINING = 0;

    public static void main(String[] args) {
        try {
            run();
        } catch (Throwable t) {
            System.err.println("FAILED " + t.getClass().getName() + ": " + t.getMessage());
            for (StackTraceElement e : t.getStackTrace()) System.err.println("  at " + e);
            Throwable c = t.getCause();
            while (c != null) {
                System.err.println("caused by " + c.getClass().getName() + ": " + c.getMessage());
                for (StackTraceElement e : c.getStackTrace()) System.err.println("  at " + e);
                c = c.getCause();
            }
            System.exit(1);
        }
    }

    static void run() throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        HAND = (ItemInHandRenderer) U.allocateInstance(ItemInHandRenderer.class);
        // renderPlayerArm reads this.minecraft.player halfway through its
        // poses: a game with no player in it lets every pose run, and the
        // throw lands on the arm's own model, which is not what this measures.
        Field game = ItemInHandRenderer.class.getDeclaredField("minecraft");
        U.putObject(HAND, U.objectFieldOffset(game),
                       U.allocateInstance(net.minecraft.client.Minecraft.class));
        FOOD = new net.minecraft.world.item.ItemStack(net.minecraft.world.item.Items.COOKED_BEEF);
        int duration = FOOD.getUseDuration(player(0));
        OUT.println("useduration " + duration);

        for (HumanoidArm arm : new HumanoidArm[] {HumanoidArm.RIGHT, HumanoidArm.LEFT}) {
            String side = arm == HumanoidArm.RIGHT ? "right" : "left";
            for (int i = 0; i <= 8; i++) {
                float equipped = i / 8.0F;
                PoseStack pose = new PoseStack();
                call("applyItemArmTransform", pose, arm, equipped);
                print("armrest " + side + " " + equipped, pose);
            }
            for (int i = 0; i <= 20; i++) {
                float swing = i / 20.0F;
                PoseStack pose = new PoseStack();
                call("applyItemArmAttackTransform", pose, arm, swing);
                print("armattack " + side + " " + swing, pose);

                PoseStack swung = new PoseStack();
                Method m = ItemInHandRenderer.class.getDeclaredMethod(
                    "swingArm", float.class, PoseStack.class, int.class, HumanoidArm.class);
                m.setAccessible(true);
                m.invoke(HAND, swing, swung, arm == HumanoidArm.RIGHT ? 1 : -1, arm);
                print("swing " + side + " " + swing, swung);

                for (int e = 0; e <= 4; e++) {
                    float equipped = e / 4.0F;
                    PoseStack armPose = new PoseStack();
                    playerArm(armPose, equipped, swing, arm);
                    print("playerarm " + side + " " + equipped + " " + swing, armPose);
                }
            }
            for (int t = 1; t <= duration; t++) {
                for (int p = 0; p <= 2; p++) {
                    float partial = p / 2.0F;
                    PoseStack pose = new PoseStack();
                    eat(pose, partial, arm, t);
                    print("eat " + side + " " + t + " " + partial, pose);
                    PoseStack sweep = new PoseStack();
                    brush(sweep, partial, arm, t);
                    print("brush " + side + " " + t + " " + partial, sweep);
                }
            }
        }

        for (int t = 0; t <= 10; t++) {
            for (int p = 0; p <= 2; p++) {
                float partial = p / 2.0F;
                PoseStack pose = new PoseStack();
                hurt(pose, partial, t, 10, 37.5F, 1.0);
                print("hurt " + t + " " + partial, pose);
            }
        }
        for (int d = 0; d <= 20; d += 2) {
            for (int b = 0; b <= 4; b++) {
                float walk = d / 2.0F;
                float amount = b / 4.0F;
                PoseStack pose = new PoseStack();
                bob(pose, walk, amount);
                print("bobview " + walk + " " + amount, pose);
            }
        }

        // The display transforms models give an item: first person in either
        // hand for item/generated, item/handheld and block/block, and the slot.
        float[][] transforms = {
            {0, -90, 25, 1.13F, 3.2F, 1.13F, 0.68F, 0.68F, 0.68F},
            {0, 90, -25, 1.13F, 3.2F, 1.13F, 0.68F, 0.68F, 0.68F},
            {0, 45, 0, 0, 0, 0, 0.40F, 0.40F, 0.40F},
            {0, 225, 0, 0, 0, 0, 0.40F, 0.40F, 0.40F},
            {30, 225, 0, 0, 0, 0, 0.625F, 0.625F, 0.625F},
            {0, -90, 55, 0, 4.0F, 0.5F, 0.85F, 0.85F, 0.85F},
        };
        for (float[] t : transforms) {
            ItemTransform transform = new ItemTransform(
                new Vector3f(t[0], t[1], t[2]),
                new Vector3f(t[3] * 0.0625F, t[4] * 0.0625F, t[5] * 0.0625F),
                new Vector3f(t[6], t[7], t[8]));
            for (int hand = 0; hand < 2; hand++) {
                PoseStack pose = new PoseStack();
                transform.apply(hand == 1, pose.last());
                print("display " + t[0] + " " + t[1] + " " + t[2] + " " + t[3] + " " + t[4] + " "
                          + t[5] + " " + t[6] + " " + hand,
                      pose);
            }
        }
    }

    static void call(String name, PoseStack pose, HumanoidArm arm, float value) throws Exception {
        Method m = ItemInHandRenderer.class.getDeclaredMethod(
            name, PoseStack.class, HumanoidArm.class, float.class);
        m.setAccessible(true);
        m.invoke(HAND, pose, arm, value);
    }

    static void playerArm(PoseStack pose, float equipped, float swing, HumanoidArm arm) {
        try {
            Method m = ItemInHandRenderer.class.getDeclaredMethod(
                "renderPlayerArm", PoseStack.class,
                Class.forName("net.minecraft.client.renderer.SubmitNodeCollector"), int.class,
                float.class, float.class, HumanoidArm.class);
            m.setAccessible(true);
            m.invoke(HAND, pose, null, 0, equipped, swing, arm);
        } catch (Exception expected) {
            // The model it ends on is not what this measures.
        }
    }

    static void eat(PoseStack pose, float partial, HumanoidArm arm, int remaining)
            throws Exception {
        Method m = ItemInHandRenderer.class.getDeclaredMethod(
            "applyEatTransform", PoseStack.class, float.class, HumanoidArm.class,
            net.minecraft.world.item.ItemStack.class,
            net.minecraft.world.entity.player.Player.class);
        m.setAccessible(true);
        m.invoke(HAND, pose, partial, arm, FOOD, player(remaining));
    }

    static void brush(PoseStack pose, float partial, HumanoidArm arm, int remaining)
            throws Exception {
        Method m = ItemInHandRenderer.class.getDeclaredMethod(
            "applyBrushTransform", PoseStack.class, float.class, HumanoidArm.class,
            net.minecraft.world.entity.player.Player.class);
        m.setAccessible(true);
        m.invoke(HAND, pose, partial, arm, player(remaining));
    }

    // bobHurt and bobView read the camera entity and the options, so they are
    // driven here through the game's own Mth and Axis rather than called:
    // every operation they do, in the order they do it.
    static void hurt(PoseStack pose, float partial, int hurtTime, int hurtDuration, float dir,
                     double strength) throws Exception {
        float f = hurtTime - partial;
        if (f >= 0.0F) {
            f /= (float) hurtDuration;
            f = net.minecraft.util.Mth.sin(f * f * f * f * (float) Math.PI);
            pose.mulPose(com.mojang.math.Axis.YP.rotationDegrees(-dir));
            pose.mulPose(com.mojang.math.Axis.ZP.rotationDegrees((float) (-f * 14.0 * strength)));
            pose.mulPose(com.mojang.math.Axis.YP.rotationDegrees(dir));
        }
    }

    static void bob(PoseStack pose, float walk, float amount) throws Exception {
        float s = net.minecraft.util.Mth.sin(walk * (float) Math.PI);
        float c = net.minecraft.util.Mth.cos(walk * (float) Math.PI);
        float c2 = net.minecraft.util.Mth.cos(walk * (float) Math.PI - 0.2F);
        pose.translate(s * amount * 0.5F, -Math.abs(c * amount), 0.0F);
        pose.mulPose(com.mojang.math.Axis.ZP.rotationDegrees(s * amount * 3.0F));
        pose.mulPose(com.mojang.math.Axis.XP.rotationDegrees(Math.abs(c2 * amount) * 5.0F));
    }

    // A player whose only answer these transforms ask for is how long it has
    // left to use what it holds.
    public static class FakePlayer extends net.minecraft.client.player.LocalPlayer {
        FakePlayer() {
            super(null, null, null, null, null, null, false);
        }

        @Override
        public int getUseItemRemainingTicks() {
            return USE_REMAINING;
        }
    }

    static FakePlayer player(int remaining) throws Exception {
        USE_REMAINING = remaining;
        return (FakePlayer) U.allocateInstance(FakePlayer.class);
    }

    static void print(String what, PoseStack pose) {
        Matrix4f m = pose.last().pose();
        float[] v = new float[16];
        m.get(v);
        StringBuilder out = new StringBuilder(what).append(' ');
        for (int i = 0; i < 16; i++) {
            if (i > 0) out.append(',');
            out.append(Float.floatToIntBits(v[i]));
        }
        OUT.println(out);
    }

    static sun.misc.Unsafe unsafe() {
        try {
            Field f = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
            f.setAccessible(true);
            return (sun.misc.Unsafe) f.get(null);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
