import com.mojang.blaze3d.vertex.PoseStack;
import com.mojang.blaze3d.vertex.VertexConsumer;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import net.minecraft.client.gui.Font;
import net.minecraft.client.model.Model;
import net.minecraft.client.model.geom.EntityModelSet;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.OrderedSubmitNodeCollector;
import net.minecraft.client.renderer.block.MovingBlockRenderState;
import net.minecraft.client.renderer.block.model.BakedQuad;
import net.minecraft.client.renderer.block.model.BlockStateModel;
import net.minecraft.client.renderer.blockentity.BannerRenderer;
import net.minecraft.client.renderer.blockentity.BedRenderer;
import net.minecraft.client.renderer.blockentity.BellRenderer;
import net.minecraft.client.renderer.blockentity.BlockEntityRenderer;
import net.minecraft.client.renderer.blockentity.BlockEntityRendererProvider;
import net.minecraft.client.renderer.blockentity.ChestRenderer;
import net.minecraft.client.renderer.blockentity.ConduitRenderer;
import net.minecraft.client.renderer.blockentity.CopperGolemStatueBlockRenderer;
import net.minecraft.client.renderer.blockentity.DecoratedPotRenderer;
import net.minecraft.client.renderer.blockentity.EnchantTableRenderer;
import net.minecraft.client.renderer.blockentity.HangingSignRenderer;
import net.minecraft.client.renderer.blockentity.LecternRenderer;
import net.minecraft.client.renderer.blockentity.ShulkerBoxRenderer;
import net.minecraft.client.renderer.blockentity.SignRenderer;
import net.minecraft.client.renderer.blockentity.SkullBlockRenderer;
import net.minecraft.client.renderer.blockentity.state.BannerRenderState;
import net.minecraft.client.renderer.blockentity.state.BedRenderState;
import net.minecraft.client.renderer.blockentity.state.BellRenderState;
import net.minecraft.client.renderer.blockentity.state.BlockEntityRenderState;
import net.minecraft.client.renderer.blockentity.state.ChestRenderState;
import net.minecraft.client.renderer.blockentity.state.CondiutRenderState;
import net.minecraft.client.renderer.blockentity.state.CopperGolemStatueRenderState;
import net.minecraft.client.renderer.blockentity.state.DecoratedPotRenderState;
import net.minecraft.client.renderer.blockentity.state.EnchantTableRenderState;
import net.minecraft.client.renderer.blockentity.state.LecternRenderState;
import net.minecraft.client.renderer.blockentity.state.ShulkerBoxRenderState;
import net.minecraft.client.renderer.blockentity.state.SignRenderState;
import net.minecraft.client.renderer.blockentity.state.SkullBlockRenderState;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.client.renderer.item.ItemStackRenderState;
import net.minecraft.client.renderer.rendertype.RenderType;
import net.minecraft.client.renderer.state.CameraRenderState;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.renderer.feature.ModelFeatureRenderer;
import net.minecraft.core.Direction;
import net.minecraft.network.chat.Component;
import net.minecraft.util.FormattedCharSequence;
import net.minecraft.world.item.DyeColor;
import net.minecraft.world.item.ItemDisplayContext;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.CopperGolemStatueBlock;
import net.minecraft.world.level.block.SkullBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BellAttachType;
import net.minecraft.world.level.block.state.properties.BedPart;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.block.state.properties.ChestType;
import net.minecraft.world.level.block.state.properties.RotationSegment;
import net.minecraft.world.phys.Vec3;
import org.joml.Matrix4f;
import org.joml.Quaternionf;
import org.jspecify.annotations.Nullable;

// What the game places a block entity's model with: the pose stack each of its
// renderers submits a model on, and the part poses the model's own setupAnim
// leaves behind. The probe puts the same block states through our own
// placement and prints the same numbers.
public final class BlockEntityOracle {
    static final java.io.PrintStream OUT = System.out;
    static Field CHILDREN;

    static int bits(float v) {
        return Float.floatToIntBits(v);
    }

    // Everything a renderer hands over, kept rather than drawn.
    static final class Recorder implements SubmitNodeCollector {
        final List<String> lines = new ArrayList<>();
        String label = "";
        int piece = 0;

        private void place(PoseStack pose) {
            Matrix4f m = pose.last().pose();
            StringBuilder out = new StringBuilder();
            for (int column = 0; column < 4; column++) {
                for (int row = 0; row < 4; row++) {
                    if (out.length() > 0) out.append(',');
                    out.append(bits(m.get(column, row)));
                }
            }
            lines.add("place " + label + "/" + piece + " " + out);
        }

        @SuppressWarnings("unchecked")
        private void parts(ModelPart root) {
            Map<String, ModelPart> flat = new TreeMap<>();
            collect(flat, "root", root);
            for (Map.Entry<String, ModelPart> entry : flat.entrySet()) {
                ModelPart p = entry.getValue();
                lines.add("pose " + label + "/" + piece + "/" + entry.getKey() + " " + bits(p.x)
                    + "," + bits(p.y) + "," + bits(p.z) + "," + bits(p.xRot) + "," + bits(p.yRot)
                    + "," + bits(p.zRot) + "," + bits(p.xScale) + "," + bits(p.yScale) + ","
                    + bits(p.zScale));
            }
        }

        @SuppressWarnings("unchecked")
        private static void collect(Map<String, ModelPart> out, String path, ModelPart part) {
            out.put(path, part);
            try {
                Map<String, ModelPart> children = (Map<String, ModelPart>) CHILDREN.get(part);
                for (Map.Entry<String, ModelPart> child : children.entrySet()) {
                    collect(out, path.equals("root") ? child.getKey()
                                                     : path + "/" + child.getKey(),
                            child.getValue());
                }
            } catch (ReflectiveOperationException e) {
                throw new RuntimeException(e);
            }
        }

        public <S> void submitModel(Model<? super S> model, S state, PoseStack pose, RenderType type,
                                    int light, int overlay, int colour,
                                    @Nullable TextureAtlasSprite sprite, int outline,
                                    ModelFeatureRenderer.@Nullable CrumblingOverlay crumbling) {
            model.setupAnim(state);
            place(pose);
            parts(model.root());
            piece++;
        }

        public void submitModelPart(ModelPart part, PoseStack pose, RenderType type, int light,
                                    int overlay, @Nullable TextureAtlasSprite sprite, boolean a,
                                    boolean b, int colour,
                                    ModelFeatureRenderer.@Nullable CrumblingOverlay crumbling,
                                    int outline) {
            place(pose);
            parts(part);
            piece++;
        }

        public OrderedSubmitNodeCollector order(int order) { return this; }
        public void submitShadow(PoseStack a, float b, List<EntityRenderState.ShadowPiece> c) {}
        public void submitNameTag(PoseStack a, @Nullable Vec3 b, int c, Component d, boolean e,
                                  int f, double g, CameraRenderState h) {}
        public void submitText(PoseStack a, float b, float c, FormattedCharSequence d, boolean e,
                               Font.DisplayMode f, int g, int h, int i, int j) {}
        public void submitFlame(PoseStack a, EntityRenderState b, Quaternionf c) {}
        public void submitLeash(PoseStack a, EntityRenderState.LeashState b) {}
        public void submitBlock(PoseStack a, BlockState b, int c, int d, int e) {}
        public void submitMovingBlock(PoseStack a, MovingBlockRenderState b) {}
        public void submitBlockModel(PoseStack a, RenderType b, BlockStateModel c, float d, float e,
                                     float f, int g, int h, int i) {}
        public void submitItem(PoseStack a, ItemDisplayContext b, int c, int d, int e, int[] f,
                               List<BakedQuad> g, RenderType h, ItemStackRenderState.FoilType i) {}
        public void submitCustomGeometry(PoseStack a, RenderType b,
                                         SubmitNodeCollector.CustomGeometryRenderer c) {}
        public void submitParticleGroup(SubmitNodeCollector.ParticleGroupRenderer a) {}
    }

    static Recorder recorder = new Recorder();

    @SuppressWarnings({"unchecked", "rawtypes"})
    static void run(String label, BlockState state, BlockEntityRenderer renderer,
                    BlockEntityRenderState rendered) {
        rendered.blockState = state;
        rendered.lightCoords = 15728880;
        recorder.label = label;
        recorder.piece = 0;
        OUT.println("state " + label + " " + Block.getId(state));
        renderer.submit(rendered, new PoseStack(), recorder, new CameraRenderState());
        for (String line : recorder.lines) OUT.println(line);
        recorder.lines.clear();
    }

    static ChestRenderState chest(BlockState state, ChestRenderState.ChestMaterialType material) {
        ChestRenderState out = new ChestRenderState();
        out.type = state.hasProperty(BlockStateProperties.CHEST_TYPE)
            ? state.getValue(BlockStateProperties.CHEST_TYPE) : ChestType.SINGLE;
        out.angle = state.getValue(BlockStateProperties.HORIZONTAL_FACING).toYRot();
        out.material = material;
        out.open = 0.0F;
        return out;
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.SharedConstants.tryDetectVersion();
        net.minecraft.server.Bootstrap.bootStrap();
        CHILDREN = ModelPart.class.getDeclaredField("children");
        CHILDREN.setAccessible(true);
        EntityModelSet models = EntityModelSet.vanilla();
        // Nothing here draws, so the only parts of the context a renderer
        // needs are the model layers and a material set that answers at all.
        net.minecraft.client.resources.model.MaterialSet materials = material -> null;
        BlockEntityRendererProvider.Context context = new BlockEntityRendererProvider.Context(
            null, null, null, null, null, models, null, materials, null);

        ChestRenderer<?> chests = new ChestRenderer<>(context);
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            for (ChestType type : ChestType.values()) {
                BlockState block = Blocks.CHEST.defaultBlockState()
                    .setValue(BlockStateProperties.HORIZONTAL_FACING, facing)
                    .setValue(BlockStateProperties.CHEST_TYPE, type);
                run("chest_" + facing.getName() + "_" + type.getSerializedName(), block, chests,
                    chest(block, ChestRenderState.ChestMaterialType.REGULAR));
            }
        }
        run("ender_chest", Blocks.ENDER_CHEST.defaultBlockState()
                .setValue(BlockStateProperties.HORIZONTAL_FACING, Direction.WEST),
            chests, chest(Blocks.ENDER_CHEST.defaultBlockState()
                              .setValue(BlockStateProperties.HORIZONTAL_FACING, Direction.WEST),
                          ChestRenderState.ChestMaterialType.ENDER_CHEST));

        BedRenderer beds = new BedRenderer(context);
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            for (BedPart part : BedPart.values()) {
                BlockState block = Blocks.RED_BED.defaultBlockState()
                    .setValue(BlockStateProperties.HORIZONTAL_FACING, facing)
                    .setValue(BlockStateProperties.BED_PART, part);
                BedRenderState rendered = new BedRenderState();
                rendered.color = DyeColor.RED;
                rendered.facing = facing;
                rendered.isHead = part == BedPart.HEAD;
                run("bed_" + facing.getName() + "_" + part.getSerializedName(), block, beds,
                    rendered);
            }
        }

        BannerRenderer banners = new BannerRenderer(context);
        for (int rotation : new int[]{0, 3, 8, 13}) {
            BlockState block = Blocks.RED_BANNER.defaultBlockState()
                .setValue(BlockStateProperties.ROTATION_16, rotation);
            BannerRenderState rendered = new BannerRenderState();
            rendered.baseColor = DyeColor.RED;
            rendered.patterns = net.minecraft.world.level.block.entity.BannerPatternLayers.EMPTY;
            rendered.standing = true;
            rendered.angle = -RotationSegment.convertToDegrees(rotation);
            rendered.phase = 0.0F;
            run("banner_" + rotation, block, banners, rendered);
        }
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            BlockState block = Blocks.RED_WALL_BANNER.defaultBlockState()
                .setValue(BlockStateProperties.HORIZONTAL_FACING, facing);
            BannerRenderState rendered = new BannerRenderState();
            rendered.baseColor = DyeColor.RED;
            rendered.patterns = net.minecraft.world.level.block.entity.BannerPatternLayers.EMPTY;
            rendered.standing = false;
            rendered.angle = -facing.toYRot();
            rendered.phase = 0.0F;
            run("wall_banner_" + facing.getName(), block, banners, rendered);
        }

        ShulkerBoxRenderer shulkers = new ShulkerBoxRenderer(context);
        for (Direction facing : Direction.values()) {
            BlockState block = Blocks.PURPLE_SHULKER_BOX.defaultBlockState()
                .setValue(BlockStateProperties.FACING, facing);
            ShulkerBoxRenderState rendered = new ShulkerBoxRenderState();
            rendered.direction = facing;
            rendered.color = DyeColor.PURPLE;
            rendered.progress = 0.0F;
            run("shulker_" + facing.getName(), block, shulkers, rendered);
        }

        SkullBlockRenderer skulls = new SkullBlockRenderer(context);
        for (int rotation : new int[]{0, 5, 11}) {
            BlockState block = Blocks.SKELETON_SKULL.defaultBlockState()
                .setValue(BlockStateProperties.ROTATION_16, rotation);
            SkullBlockRenderState rendered = new SkullBlockRenderState();
            rendered.skullType = SkullBlock.Types.SKELETON;
            rendered.rotationDegrees = RotationSegment.convertToDegrees(rotation);
            rendered.direction = null;
            run("skull_" + rotation, block, skulls, rendered);
        }
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            BlockState block = Blocks.PIGLIN_WALL_HEAD.defaultBlockState()
                .setValue(BlockStateProperties.HORIZONTAL_FACING, facing);
            SkullBlockRenderState rendered = new SkullBlockRenderState();
            rendered.skullType = SkullBlock.Types.PIGLIN;
            rendered.direction = facing;
            rendered.rotationDegrees = RotationSegment.convertToDegrees(
                RotationSegment.convertToSegment(facing.getOpposite()));
            run("piglin_wall_" + facing.getName(), block, skulls, rendered);
        }

        SignRenderer signs = new SignRenderer(context);
        for (int rotation : new int[]{0, 7}) {
            BlockState block = Blocks.OAK_SIGN.defaultBlockState()
                .setValue(BlockStateProperties.ROTATION_16, rotation);
            run("sign_" + rotation, block, signs, new SignRenderState());
        }
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            BlockState block = Blocks.SPRUCE_WALL_SIGN.defaultBlockState()
                .setValue(BlockStateProperties.HORIZONTAL_FACING, facing);
            run("wall_sign_" + facing.getName(), block, signs, new SignRenderState());
        }

        HangingSignRenderer hanging = new HangingSignRenderer(context);
        for (boolean attached : new boolean[]{false, true}) {
            BlockState block = Blocks.OAK_HANGING_SIGN.defaultBlockState()
                .setValue(BlockStateProperties.ROTATION_16, 9)
                .setValue(BlockStateProperties.ATTACHED, attached);
            run("hanging_" + attached, block, hanging, new SignRenderState());
        }
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            BlockState block = Blocks.OAK_WALL_HANGING_SIGN.defaultBlockState()
                .setValue(BlockStateProperties.HORIZONTAL_FACING, facing);
            run("wall_hanging_" + facing.getName(), block, hanging, new SignRenderState());
        }

        BellRenderer bells = new BellRenderer(context);
        for (BellAttachType attach : BellAttachType.values()) {
            BlockState block = Blocks.BELL.defaultBlockState()
                .setValue(BlockStateProperties.BELL_ATTACHMENT, attach)
                .setValue(BlockStateProperties.HORIZONTAL_FACING, Direction.SOUTH);
            BellRenderState rendered = new BellRenderState();
            rendered.ticks = 0.0F;
            rendered.shakeDirection = null;
            run("bell_" + attach.getSerializedName(), block, bells, rendered);
        }

        ConduitRenderer conduits = new ConduitRenderer(context);
        CondiutRenderState conduit = new CondiutRenderState();
        conduit.isActive = false;
        conduit.activeRotation = 0.0F;
        run("conduit", Blocks.CONDUIT.defaultBlockState(), conduits, conduit);

        DecoratedPotRenderer pots = new DecoratedPotRenderer(context);
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            DecoratedPotRenderState rendered = new DecoratedPotRenderState();
            rendered.direction = facing;
            rendered.wobbleProgress = -1.0F;
            run("pot_" + facing.getName(), Blocks.DECORATED_POT.defaultBlockState()
                    .setValue(BlockStateProperties.HORIZONTAL_FACING, facing), pots, rendered);
        }

        EnchantTableRenderer tables = new EnchantTableRenderer(context);
        EnchantTableRenderState table = new EnchantTableRenderState();
        run("enchanting_table", Blocks.ENCHANTING_TABLE.defaultBlockState(), tables, table);

        LecternRenderer lecterns = new LecternRenderer(context);
        for (Direction facing : Direction.Plane.HORIZONTAL) {
            BlockState block = Blocks.LECTERN.defaultBlockState()
                .setValue(BlockStateProperties.HORIZONTAL_FACING, facing)
                .setValue(BlockStateProperties.HAS_BOOK, true);
            LecternRenderState rendered = new LecternRenderState();
            rendered.hasBook = true;
            rendered.yRot = facing.getClockWise().toYRot();
            run("lectern_" + facing.getName(), block, lecterns, rendered);
        }

        CopperGolemStatueBlockRenderer statues = new CopperGolemStatueBlockRenderer(context);
        for (CopperGolemStatueBlock.Pose pose : CopperGolemStatueBlock.Pose.values()) {
            for (Direction facing : new Direction[]{Direction.NORTH, Direction.EAST}) {
                BlockState block = Blocks.COPPER_GOLEM_STATUE.defaultBlockState()
                    .setValue(BlockStateProperties.HORIZONTAL_FACING, facing)
                    .setValue(BlockStateProperties.COPPER_GOLEM_POSE, pose);
                CopperGolemStatueRenderState rendered = new CopperGolemStatueRenderState();
                rendered.pose = pose;
                rendered.direction = facing;
                run("statue_" + pose.getSerializedName() + "_" + facing.getName(), block, statues,
                    rendered);
            }
        }
    }
}
