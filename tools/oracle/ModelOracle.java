import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import net.minecraft.client.model.HumanoidModel;
import net.minecraft.client.model.Model;
import net.minecraft.client.model.animal.allay.AllayModel;
import net.minecraft.client.model.animal.armadillo.ArmadilloModel;
import net.minecraft.client.model.animal.axolotl.AxolotlModel;
import net.minecraft.client.model.animal.bee.BeeModel;
import net.minecraft.client.model.animal.bee.BeeStingerModel;
import net.minecraft.client.model.animal.chicken.ChickenModel;
import net.minecraft.client.model.animal.chicken.ColdChickenModel;
import net.minecraft.client.model.animal.cow.CowModel;
import net.minecraft.client.model.animal.equine.DonkeyModel;
import net.minecraft.client.model.animal.equine.EquineSaddleModel;
import net.minecraft.client.model.animal.equine.HorseModel;
import net.minecraft.client.model.animal.feline.CatModel;
import net.minecraft.client.model.animal.feline.OcelotModel;
import net.minecraft.client.model.animal.fox.FoxModel;
import net.minecraft.client.model.animal.golem.CopperGolemModel;
import net.minecraft.client.model.animal.golem.IronGolemModel;
import net.minecraft.client.model.animal.golem.SnowGolemModel;
import net.minecraft.client.model.animal.pig.ColdPigModel;
import net.minecraft.client.model.animal.pig.PigModel;
import net.minecraft.client.model.animal.sheep.SheepFurModel;
import net.minecraft.client.model.animal.sheep.SheepModel;
import net.minecraft.client.model.animal.wolf.WolfModel;
import net.minecraft.client.model.geom.LayerDefinitions;
import net.minecraft.client.model.geom.ModelLayerLocation;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.client.model.geom.builders.LayerDefinition;
import net.minecraft.client.model.monster.breeze.BreezeModel;
import net.minecraft.client.model.monster.creeper.CreeperModel;
import net.minecraft.client.model.monster.enderman.EndermanModel;
import net.minecraft.client.model.monster.skeleton.BoggedModel;
import net.minecraft.client.model.monster.skeleton.SkeletonModel;
import net.minecraft.client.model.monster.slime.MagmaCubeModel;
import net.minecraft.client.model.monster.slime.SlimeModel;
import net.minecraft.client.model.monster.spider.SpiderModel;
import net.minecraft.client.model.monster.witch.WitchModel;
import net.minecraft.client.model.monster.zombie.DrownedModel;
import net.minecraft.client.model.monster.zombie.ZombieModel;
import net.minecraft.client.model.monster.zombie.ZombieVillagerModel;
import net.minecraft.client.model.npc.VillagerModel;
import net.minecraft.client.model.object.armorstand.ArmorStandArmorModel;
import net.minecraft.client.model.object.armorstand.ArmorStandModel;
import net.minecraft.client.model.object.statue.CopperGolemStatueModel;
import net.minecraft.client.model.player.PlayerModel;
import net.minecraft.client.renderer.entity.state.AllayRenderState;
import net.minecraft.client.renderer.entity.state.ArmadilloRenderState;
import net.minecraft.client.renderer.entity.state.ArmorStandRenderState;
import net.minecraft.client.renderer.entity.state.ArmedEntityRenderState;
import net.minecraft.client.renderer.entity.state.AvatarRenderState;
import net.minecraft.client.renderer.entity.state.AxolotlRenderState;
import net.minecraft.client.renderer.entity.state.BeeRenderState;
import net.minecraft.client.renderer.entity.state.BoggedRenderState;
import net.minecraft.client.renderer.entity.state.BreezeRenderState;
import net.minecraft.client.renderer.entity.state.CatRenderState;
import net.minecraft.client.renderer.entity.state.ChickenRenderState;
import net.minecraft.client.renderer.entity.state.CopperGolemRenderState;
import net.minecraft.client.renderer.entity.state.CreeperRenderState;
import net.minecraft.client.renderer.entity.state.DonkeyRenderState;
import net.minecraft.client.renderer.entity.state.EndermanRenderState;
import net.minecraft.client.renderer.entity.state.EquineRenderState;
import net.minecraft.client.renderer.entity.state.FelineRenderState;
import net.minecraft.client.renderer.entity.state.FoxRenderState;
import net.minecraft.client.renderer.entity.state.HumanoidRenderState;
import net.minecraft.client.renderer.entity.state.IronGolemRenderState;
import net.minecraft.client.renderer.entity.state.LivingEntityRenderState;
import net.minecraft.client.renderer.entity.state.SheepRenderState;
import net.minecraft.client.renderer.entity.state.SkeletonRenderState;
import net.minecraft.client.renderer.entity.state.SlimeRenderState;
import net.minecraft.client.renderer.entity.state.VillagerRenderState;
import net.minecraft.client.renderer.entity.state.WitchRenderState;
import net.minecraft.client.renderer.entity.state.WolfRenderState;
import net.minecraft.client.renderer.entity.state.ZombieRenderState;
import net.minecraft.client.renderer.entity.state.ZombieVillagerRenderState;
import net.minecraft.core.Direction;
import net.minecraft.core.Rotations;
import net.minecraft.util.Unit;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.item.SwingAnimationType;

// The game's own baked model layers and its entity animation on fixed inputs,
// printed as bit patterns: every polygon corner of every layer in
// model_layers.txt, then every part of every layer named in model_cases.txt
// after setupAnim for each of its cases. The probe prints the same from the
// baked entity_models table and our own models.
public final class ModelOracle {
    record Layer(String name, String kind) {
    }

    record Case(String layer, String kind, List<String> keys, List<Float> values) {
        float get(String key, float fallback) {
            for (int i = 0; i < this.keys.size(); i++) {
                if (this.keys.get(i).equals(key)) {
                    return this.values.get(i);
                }
            }
            return fallback;
        }
    }

    static Field field(Class<?> type, String name) throws Exception {
        Field f = type.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static int bits(float v) {
        return Float.floatToIntBits(v);
    }

    static Path oracleDir() {
        return Path.of(System.getProperty("oracle.dir", "."));
    }

    static List<String> rows(String file) throws Exception {
        List<String> out = new ArrayList<>();
        for (String line : Files.readAllLines(oracleDir().resolve(file))) {
            String trimmed = line.strip();
            if (!trimmed.isEmpty() && !trimmed.startsWith("#")) {
                out.add(trimmed);
            }
        }
        return out;
    }

    @SuppressWarnings("unchecked")
    static void geometry(String layer, String path, boolean root, ModelPart part, Field cubes,
                         Field children) throws Exception {
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
            geometry(layer, root ? child.getKey() : path + "/" + child.getKey(), false,
                     child.getValue(), cubes, children);
        }
    }

    @SuppressWarnings("unchecked")
    static void pose(String label, String path, boolean root, ModelPart part, Field children)
            throws Exception {
        System.out.println("pose " + label + "/" + path + " " + bits(part.x) + "," + bits(part.y)
            + "," + bits(part.z) + "," + bits(part.xRot) + "," + bits(part.yRot) + ","
            + bits(part.zRot) + "," + bits(part.xScale) + "," + bits(part.yScale) + ","
            + bits(part.zScale) + "," + (part.visible ? 1 : 0) + "," + (part.skipDraw ? 1 : 0));
        Map<String, ModelPart> sorted = new TreeMap<>((Map<String, ModelPart>) children.get(part));
        for (Map.Entry<String, ModelPart> child : sorted.entrySet()) {
            pose(label, root ? child.getKey() : path + "/" + child.getKey(), false, child.getValue(),
                 children);
        }
    }

    // The vanilla class that animates a layer, as the renderer that draws it
    // builds it: `ArmorModelSet.bake` gives an armour sub-layer the mob's own
    // model class, not a plain `HumanoidModel`.
    static Model<?> model(String kind, ModelPart root) {
        return switch (kind) {
            case "player" -> new PlayerModel(root, false);
            case "player_slim" -> new PlayerModel(root, true);
            case "zombie" -> new ZombieModel<ZombieRenderState>(root);
            case "drowned" -> new DrownedModel(root);
            case "zombie_villager" -> new ZombieVillagerModel<ZombieVillagerRenderState>(root);
            case "skeleton" -> new SkeletonModel<SkeletonRenderState>(root);
            case "bogged" -> new BoggedModel(root);
            case "villager" -> new VillagerModel(root);
            case "witch" -> new WitchModel(root);
            case "humanoid" -> new HumanoidModel<HumanoidRenderState>(root);
            case "pig" -> new PigModel(root);
            case "cold_pig" -> new ColdPigModel(root);
            case "cow" -> new CowModel(root);
            case "sheep" -> new SheepModel(root);
            case "sheep_fur" -> new SheepFurModel(root);
            case "chicken" -> new ChickenModel(root);
            case "cold_chicken" -> new ColdChickenModel(root);
            case "wolf" -> new WolfModel(root);
            case "cat" -> new CatModel(root);
            case "ocelot" -> new OcelotModel(root);
            case "fox" -> new FoxModel(root);
            case "horse" -> new HorseModel(root);
            case "donkey" -> new DonkeyModel(root);
            case "equine_saddle" -> new EquineSaddleModel(root);
            case "creeper" -> new CreeperModel(root);
            case "spider" -> new SpiderModel(root);
            case "enderman" -> new EndermanModel<EndermanRenderState>(root);
            case "slime" -> new SlimeModel(root);
            case "magma_cube" -> new MagmaCubeModel(root);
            case "iron_golem" -> new IronGolemModel(root);
            case "snow_golem" -> new SnowGolemModel(root);
            case "bee" -> new BeeModel(root);
            case "bee_stinger" -> new BeeStingerModel(root);
            case "axolotl" -> new AxolotlModel(root);
            case "allay" -> new AllayModel(root);
            case "armadillo" -> new ArmadilloModel(root);
            case "breeze" -> new BreezeModel(root);
            case "copper_golem" -> new CopperGolemModel(root);
            case "copper_golem_statue" -> new CopperGolemStatueModel(root);
            case "armor_stand" -> new ArmorStandModel(root);
            case "armor_stand_armor" -> new ArmorStandArmorModel(root);
            default -> throw new IllegalArgumentException("no model class for " + kind);
        };
    }

    static LivingEntityRenderState state(String kind) {
        return switch (kind) {
            case "player", "player_slim" -> new AvatarRenderState();
            case "zombie", "drowned" -> new ZombieRenderState();
            case "zombie_villager" -> new ZombieVillagerRenderState();
            case "skeleton" -> new SkeletonRenderState();
            case "bogged" -> new BoggedRenderState();
            case "villager" -> new VillagerRenderState();
            case "witch" -> new WitchRenderState();
            case "humanoid" -> new HumanoidRenderState();
            case "pig", "cold_pig", "cow", "spider", "snow_golem" -> new LivingEntityRenderState();
            case "sheep", "sheep_fur" -> new SheepRenderState();
            case "chicken", "cold_chicken" -> new ChickenRenderState();
            case "wolf" -> new WolfRenderState();
            case "cat" -> new CatRenderState();
            case "ocelot" -> new FelineRenderState();
            case "fox" -> new FoxRenderState();
            case "horse", "equine_saddle" -> new EquineRenderState();
            case "donkey" -> new DonkeyRenderState();
            case "creeper" -> new CreeperRenderState();
            case "enderman" -> new EndermanRenderState();
            case "slime", "magma_cube" -> new SlimeRenderState();
            case "iron_golem" -> new IronGolemRenderState();
            case "bee" -> new BeeRenderState();
            case "axolotl" -> new AxolotlRenderState();
            case "allay" -> new AllayRenderState();
            case "armadillo" -> new ArmadilloRenderState();
            case "breeze" -> new BreezeRenderState();
            case "copper_golem" -> new CopperGolemRenderState();
            case "armor_stand", "armor_stand_armor" -> new ArmorStandRenderState();
            default -> throw new IllegalArgumentException("no render state for " + kind);
        };
    }

    // One field of one render state by the name `MobState` gives it. The most
    // derived classes come first so a name two unrelated states share - a cat
    // and a fox both sit - lands on the one the case is for.
    static void set(LivingEntityRenderState st, String key, float v) {
        boolean flag = v != 0.0F;
        int whole = (int) v;
        if (st instanceof AvatarRenderState s) {
            switch (key) {
                case "spectator": s.isSpectator = flag; return;
                case "show_hat": s.showHat = flag; return;
                case "show_jacket": s.showJacket = flag; return;
                case "show_left_sleeve": s.showLeftSleeve = flag; return;
                case "show_right_sleeve": s.showRightSleeve = flag; return;
                case "show_left_pants": s.showLeftPants = flag; return;
                case "show_right_pants": s.showRightPants = flag; return;
                case "fall_flying_ticks": s.fallFlyingTimeInTicks = v; return;
                default: break;
            }
        }
        // `ArmorStandRenderState.yRot` shadows the living one: the renderer
        // fills it from the entity's own yaw, which is also its body rot, so
        // one case value sets both.
        if (st instanceof ArmorStandRenderState s) {
            switch (key) {
                case "body_rot": s.bodyRot = v; s.yRot = v; return;
                case "wiggle": s.wiggle = v; return;
                case "marker": s.isMarker = flag; return;
                case "small": s.isSmall = flag; return;
                case "show_arms": s.showArms = flag; return;
                case "show_base_plate": s.showBasePlate = flag; return;
                default: break;
            }
            if (key.startsWith("stand_")) {
                String rest = key.substring(6);
                int split = rest.lastIndexOf('_');
                String limb = rest.substring(0, split);
                char axis = rest.charAt(rest.length() - 1);
                Rotations was = switch (limb) {
                    case "head" -> s.headPose;
                    case "body" -> s.bodyPose;
                    case "left_arm" -> s.leftArmPose;
                    case "right_arm" -> s.rightArmPose;
                    case "left_leg" -> s.leftLegPose;
                    case "right_leg" -> s.rightLegPose;
                    default -> throw new IllegalArgumentException("no stand pose " + limb);
                };
                Rotations now = new Rotations(axis == 'x' ? v : was.x(),
                                              axis == 'y' ? v : was.y(),
                                              axis == 'z' ? v : was.z());
                switch (limb) {
                    case "head": s.headPose = now; return;
                    case "body": s.bodyPose = now; return;
                    case "left_arm": s.leftArmPose = now; return;
                    case "right_arm": s.rightArmPose = now; return;
                    case "left_leg": s.leftLegPose = now; return;
                    case "right_leg": s.rightLegPose = now; return;
                    default: break;
                }
            }
        }
        if (st instanceof BoggedRenderState s && key.equals("sheared")) {
            s.isSheared = flag;
            return;
        }
        if (st instanceof SkeletonRenderState s) {
            switch (key) {
                case "aggressive": s.isAggressive = flag; return;
                case "shaking": s.isShaking = flag; return;
                case "holding_bow": s.isHoldingBow = flag; return;
                default: break;
            }
        }
        if (st instanceof ZombieRenderState s) {
            switch (key) {
                case "aggressive": s.isAggressive = flag; return;
                case "converting": s.isConverting = flag; return;
                default: break;
            }
        }
        if (st instanceof EndermanRenderState s) {
            switch (key) {
                case "creepy": s.isCreepy = flag; return;
                case "carries_block":
                    s.carriedBlock = flag ? net.minecraft.world.level.block.Blocks.STONE.defaultBlockState() : null;
                    return;
                default: break;
            }
        }
        if (st instanceof HumanoidRenderState s) {
            switch (key) {
                case "swim_amount": s.swimAmount = v; return;
                case "speed_value": s.speedValue = v; return;
                case "max_crossbow_charge_duration": s.maxCrossbowChargeDuration = v; return;
                case "ticks_using_item": s.ticksUsingItem = v; return;
                case "attack_arm_left": s.attackArm = flag ? HumanoidArm.LEFT : HumanoidArm.RIGHT; return;
                case "use_item_off_hand":
                    s.useItemHand = flag ? InteractionHand.OFF_HAND : InteractionHand.MAIN_HAND;
                    return;
                case "crouching": s.isCrouching = flag; return;
                case "fall_flying": s.isFallFlying = flag; return;
                case "visually_swimming": s.isVisuallySwimming = flag; return;
                case "passenger": s.isPassenger = flag; return;
                case "using_item": s.isUsingItem = flag; return;
                default: break;
            }
        }
        if (st instanceof AllayRenderState s) {
            switch (key) {
                case "dancing": s.isDancing = flag; return;
                case "spinning": s.isSpinning = flag; return;
                case "spinning_progress": s.spinningProgress = v; return;
                case "holding_animation_progress": s.holdingAnimationProgress = v; return;
                default: break;
            }
        }
        if (st instanceof CopperGolemRenderState s) {
            switch (key) {
                case "golem_idle": s.idleAnimationState.start(whole); return;
                case "golem_get_item": s.interactionGetItem.start(whole); return;
                case "golem_get_no_item": s.interactionGetNoItem.start(whole); return;
                case "golem_drop_item": s.interactionDropItem.start(whole); return;
                case "golem_drop_no_item": s.interactionDropNoItem.start(whole); return;
                default: break;
            }
        }
        if (st instanceof ArmedEntityRenderState s) {
            switch (key) {
                case "main_arm_right": s.mainArm = flag ? HumanoidArm.RIGHT : HumanoidArm.LEFT; return;
                case "right_arm_pose": s.rightArmPose = HumanoidModel.ArmPose.values()[whole]; return;
                case "left_arm_pose": s.leftArmPose = HumanoidModel.ArmPose.values()[whole]; return;
                case "swing_animation": s.swingAnimationType = SwingAnimationType.values()[whole]; return;
                case "attack_time": s.attackTime = v; return;
                case "right_hand_item_empty":
                    if (!flag) s.rightHandItemState.newLayer();
                    return;
                case "left_hand_item_empty":
                    if (!flag) s.leftHandItemState.newLayer();
                    return;
                default: break;
            }
        }
        if (st instanceof VillagerRenderState s && key.equals("unhappy")) {
            s.isUnhappy = flag;
            return;
        }
        if (st instanceof WitchRenderState s) {
            switch (key) {
                case "entity_id": s.entityId = whole; return;
                case "holding_item": s.isHoldingItem = flag; return;
                case "holding_potion": s.isHoldingPotion = flag; return;
                default: break;
            }
        }
        if (st instanceof CreeperRenderState s) {
            switch (key) {
                case "swelling": s.swelling = v; return;
                case "powered": s.isPowered = flag; return;
                default: break;
            }
        }
        if (st instanceof SlimeRenderState s) {
            switch (key) {
                case "squish": s.squish = v; return;
                case "slime_size": s.size = whole; return;
                default: break;
            }
        }
        if (st instanceof SheepRenderState s) {
            switch (key) {
                case "head_eat_position_scale": s.headEatPositionScale = v; return;
                case "head_eat_angle_scale": s.headEatAngleScale = v; return;
                case "sheared": s.isSheared = flag; return;
                default: break;
            }
        }
        if (st instanceof ChickenRenderState s) {
            switch (key) {
                case "flap": s.flap = v; return;
                case "flap_speed": s.flapSpeed = v; return;
                default: break;
            }
        }
        if (st instanceof WolfRenderState s) {
            switch (key) {
                case "angry": s.isAngry = flag; return;
                case "sitting": s.isSitting = flag; return;
                case "tail_angle": s.tailAngle = v; return;
                case "head_roll_angle": s.headRollAngle = v; return;
                case "shake_anim": s.shakeAnim = v; return;
                case "wet_shade": s.wetShade = v; return;
                default: break;
            }
        }
        if (st instanceof CatRenderState s
                && key.equals("lying_on_top_of_sleeping_player")) {
            s.isLyingOnTopOfSleepingPlayer = flag;
            return;
        }
        if (st instanceof FelineRenderState s) {
            switch (key) {
                case "crouching": s.isCrouching = flag; return;
                case "sprinting": s.isSprinting = flag; return;
                case "sitting": s.isSitting = flag; return;
                case "lie_down_amount": s.lieDownAmount = v; return;
                case "lie_down_amount_tail": s.lieDownAmountTail = v; return;
                case "relax_state_one_amount": s.relaxStateOneAmount = v; return;
                default: break;
            }
        }
        if (st instanceof FoxRenderState s) {
            switch (key) {
                case "head_roll_angle": s.headRollAngle = v; return;
                case "crouch_amount": s.crouchAmount = v; return;
                case "crouching": s.isCrouching = flag; return;
                case "sleeping": s.isSleeping = flag; return;
                case "sitting": s.isSitting = flag; return;
                case "faceplanted": s.isFaceplanted = flag; return;
                case "pouncing": s.isPouncing = flag; return;
                default: break;
            }
        }
        if (st instanceof DonkeyRenderState s && key.equals("has_chest")) {
            s.hasChest = flag;
            return;
        }
        if (st instanceof EquineRenderState s) {
            switch (key) {
                case "ridden": s.isRidden = flag; return;
                case "animate_tail": s.animateTail = flag; return;
                case "eat_animation": s.eatAnimation = v; return;
                case "stand_animation": s.standAnimation = v; return;
                case "feeding_animation": s.feedingAnimation = v; return;
                default: break;
            }
        }
        if (st instanceof IronGolemRenderState s) {
            switch (key) {
                case "attack_ticks_remaining": s.attackTicksRemaining = v; return;
                case "offer_flower_tick": s.offerFlowerTick = whole; return;
                default: break;
            }
        }
        if (st instanceof BeeRenderState s) {
            switch (key) {
                case "roll_amount": s.rollAmount = v; return;
                case "has_stinger": s.hasStinger = flag; return;
                case "on_ground": s.isOnGround = flag; return;
                case "angry": s.isAngry = flag; return;
                case "has_nectar": s.hasNectar = flag; return;
                default: break;
            }
        }
        if (st instanceof AxolotlRenderState s) {
            switch (key) {
                case "playing_dead_factor": s.playingDeadFactor = v; return;
                case "moving_factor": s.movingFactor = v; return;
                case "in_water_factor": s.inWaterFactor = v; return;
                case "on_ground_factor": s.onGroundFactor = v; return;
                default: break;
            }
        }
        if (st instanceof ArmadilloRenderState s) {
            switch (key) {
                case "hiding_in_shell": s.isHidingInShell = flag; return;
                case "roll_out": s.rollOutAnimationState.start(whole); return;
                case "roll_up": s.rollUpAnimationState.start(whole); return;
                case "peek": s.peekAnimationState.start(whole); return;
                default: break;
            }
        }
        if (st instanceof BreezeRenderState s) {
            switch (key) {
                case "breeze_idle": s.idle.start(whole); return;
                case "breeze_shoot": s.shoot.start(whole); return;
                case "breeze_slide": s.slide.start(whole); return;
                case "breeze_slide_back": s.slideBack.start(whole); return;
                case "breeze_inhale": s.inhale.start(whole); return;
                case "breeze_long_jump": s.longJump.start(whole); return;
                default: break;
            }
        }
        switch (key) {
            case "age_in_ticks": st.ageInTicks = v; return;
            case "bounding_box_width": st.boundingBoxWidth = v; return;
            case "bounding_box_height": st.boundingBoxHeight = v; return;
            case "body_rot": st.bodyRot = v; return;
            case "y_rot": st.yRot = v; return;
            case "x_rot": st.xRot = v; return;
            case "death_time": st.deathTime = v; return;
            case "walk_position": st.walkAnimationPos = v; return;
            case "walk_speed": st.walkAnimationSpeed = v; return;
            case "scale": st.scale = v; return;
            case "age_scale": st.ageScale = v; return;
            case "baby": st.isBaby = flag; return;
            case "in_water": st.isInWater = flag; return;
            case "upside_down": st.isUpsideDown = flag; return;
            case "fully_frozen": st.isFullyFrozen = flag; return;
            case "auto_spin_attack": st.isAutoSpinAttack = flag; return;
            default:
                throw new IllegalArgumentException(
                    st.getClass().getSimpleName() + " has no field for " + key);
        }
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    static void animate(String kind, Model<?> model, Case one) {
        if (kind.equals("copper_golem_statue")) {
            ((CopperGolemStatueModel) model).setupAnim(Direction.values()[(int) one.get("facing", 3)]);
            return;
        }
        if (kind.equals("bee_stinger")) {
            ((BeeStingerModel) model).setupAnim(Unit.INSTANCE);
            return;
        }
        LivingEntityRenderState st = state(kind);
        for (int i = 0; i < one.keys().size(); i++) {
            set(st, one.keys().get(i), one.values().get(i));
        }
        ((Model) model).setupAnim(st);
    }

    static String name(ModelLayerLocation location) {
        String model = location.model().getPath();
        if (!location.model().getNamespace().equals("minecraft")) {
            model = location.model().getNamespace() + ":" + model;
        }
        return location.layer().equals("main") ? model : model + "#" + location.layer();
    }

    public static void main(String[] args) throws Exception {
        net.minecraft.SharedConstants.tryDetectVersion();
        net.minecraft.server.Bootstrap.bootStrap();
        Field cubes = field(ModelPart.class, "cubes");
        Field children = field(ModelPart.class, "children");

        Map<String, LayerDefinition> definitions = new TreeMap<>();
        for (Map.Entry<ModelLayerLocation, LayerDefinition> e : LayerDefinitions.createRoots().entrySet()) {
            definitions.put(name(e.getKey()), e.getValue());
        }

        List<Layer> layers = new ArrayList<>();
        Map<String, String> kinds = new TreeMap<>();
        for (String row : rows("model_layers.txt")) {
            String[] parts = row.split("\\s+");
            layers.add(new Layer(parts[0], parts[1]));
            kinds.put(parts[0], parts[1]);
        }

        List<Case> cases = new ArrayList<>();
        for (String row : rows("model_cases.txt")) {
            String[] parts = row.split("\\s+");
            String layer = parts[0];
            String kind = kinds.get(layer);
            if (kind == null) {
                throw new IllegalArgumentException(layer + " is not in model_layers.txt");
            }
            List<String> keys = new ArrayList<>();
            List<Float> values = new ArrayList<>();
            for (int i = 1; i < parts.length; i++) {
                int equals = parts[i].indexOf('=');
                keys.add(parts[i].substring(0, equals));
                values.add(Float.parseFloat(parts[i].substring(equals + 1)));
            }
            cases.add(new Case(layer, kind, keys, values));
        }

        for (Layer layer : layers) {
            LayerDefinition definition = definitions.get(layer.name());
            if (definition == null) {
                throw new IllegalArgumentException("the game has no layer " + layer.name());
            }
            geometry(layer.name(), ".", true, definition.bakeRoot(), cubes, children);
        }

        String current = null;
        Model<?> model = null;
        ModelPart root = null;
        for (int i = 0; i < cases.size(); i++) {
            Case one = cases.get(i);
            if (!one.layer().equals(current)) {
                current = one.layer();
                root = definitions.get(current).bakeRoot();
                model = model(one.kind(), root);
            }
            animate(one.kind(), model, one);
            pose(one.layer() + "#" + i, ".", true, root, children);
        }
    }
}
