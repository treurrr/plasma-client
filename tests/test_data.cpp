#include "mcpp/proto/commands.hpp"
#include "mcpp/proto/components.hpp"
#include "mcpp/proto/metadata.hpp"
#include "mcpp/session.hpp"
#include "mcpp/status.hpp"

#include <format>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Bytes cut from corpus/data-inventory.plrc: a 1.21.11 server's answer to a
// player whose inventory holds every kind of data component /give can make,
// standing in a crowd of every kind of mob.
namespace {

using mcpp::proto::Value;

constexpr std::string_view kContainerSetContent =
    "00012e000000000001f706010036041201de06010003c80101bc0701002a000000ff01d50702000d01090436080b01f2"
    "0901004602270e050f018f0a0200470b4601060001d00803002d0a0a000161050008726f746174696f6e000000000600"
    "01783ff00000000000000600017a40000000000000000800047479706500116d696e6563726166743a6d616e73696f6e"
    "00002c052b000000ff018c080100300203010000028207010006080005696e6e657201c50403004d0a04000473656564"
    "000000000000000508000a6c6f6f745f7461626c65001f6d696e6563726166743a6368657374732f73696d706c655f64"
    "756e67656f6e004906010100000000000001c50901003502496e0003426f7800010800017800004c0a0800056974656d"
    "7300116d696e6563726166743a6469616d6f6e640001bb0a01002f0201ff06000001da09000001dc0901002901170101"
    "d309010044010000000100000002000000030000000407506c61736d614101087465787475726573046533303d000000"
    "000001d30902000608000c7061727469616c2068656164440001075061727469616c00000000000001c80a01003b0105"
    "01bc0a010033010f6401bf02010048048108a50b8108ba0b01e40a02004b010b0a0001024a010b686f6e65795f6c6576"
    "656c0133018a0801004101136d696e6563726166743a6f766572776f726c64000000400000300200019c0a0100370a08"
    "00116d696e6563726166743a6f616b5f6c6f670004617869730001b20704001901fc06000017023f800000011a400000"
    "00010c706c61736d613a67726f7570183fc000000b9c0501050001000014000101003f00000001021203410000000400"
    "1c6d696e6563726166743a656e746974792e6361742e616d6269656e7401408000000201b20707001f0a1d0240400000"
    "01011c02001a6d696e6563726166743a6d696e6561626c652f7069636b6178650140800000010103010900003f800000"
    "01002100106d696e6563726166743a706c616e6b731b116d696e6563726166743a69735f66697265026401b207040020"
    "0448010e6d696e6563726166743a676f6c64011a6d696e6563726166743a6d6973632f70756d706b696e626c75720102"
    "9b010101010101e70a24010222230c706c61736d613a7374796c6501b2070100253f0000003f8000000242b400000100"
    "116d696e6563726166743a69735f66697265000000003f80000042b40000003f8000003f0000003f800000000000003f"
    "80000001196d696e6563726166743a62797061737365735f736869656c6401f30a01f40a01b20707000501003f000000"
    "073f0000001e00000000408000000000000040a000003dcccccd3f800000280205270a0201053f800000000000000106"
    "000000003f000000003f0000003fc00000019d0c00080100260101019d0c01970c01b20702000e01010201000000000f"
    "0201030908010105736e6f777901047472756500000001000e6d696e6563726166743a6c6f677300010a010001610100"
    "0103000101000a0a000a6475726162696c6974790300036d696e00000001000001b2070b0038640a003e010240090800"
    "000001000f6d696e6563726166743a737469636b451f6d696e6563726166743a626c6f636b2e6e6f74655f626c6f636b"
    "2e686172703f1c6d696e6563726166743a7061747465726e5f6974656d2f676c6f62653d023c0105160a00323f000000"
    "390a0500064865616c746840a00000003a010a0001b2070c005501520e530150005101580b570e54024f005a63568104"
    "590101b2070d005b0061196505620366065c02630460055e01015f00670d64015d010001b2070200460100116d696e65"
    "63726166743a637265657065720d706c61736d612e696e6c696e650d3c010006696e6c696e6500080006696e6c696e65"
    "01a8070002101c01e00b01003d0301a8070d0011013fc00000010101016101000000ff0b02080005706c61696e0a0800"
    "05636f6c6f720004676f6c640800047465787400067374796c65640100066974616c696300001002020c706c61736d61"
    "3a65787472614008000000000000000100000c706c61736d613a73686f776e3fe000000000000002000208000b637573"
    "746f6d206c696e650d0221052803030a1200010d150013030a0f6d696e6563726166743a737469636b04060a08000563"
    "6f6c6f720003726564080004746578740005426c616465010004626f6c6401000c03000a010006706c61736d61010001"
    "bb0702003605022a00ff000001ca0702000908000a4e616d6564206974656d36010901c509010035055469746c650006"
    "417574686f72010208000870616765206f6e65000a080005636f6c6f7200037265640800047465787400087061676520"
    "74776f00000101c409010034020568656c6c6f0005776f726c640001e108010031010d01000000ff0100016400010100"
    "010466617374058d0a010031011c00000003da0901004302020202000000ff0000ff0001000000010101040100ff0000"
    "00000001db09010042030100ff0000000000018f0a0200470e46010e0400";

constexpr std::string_view kCommands =
    "1a000a0102030405060708090a01010b026d6505010c0468656c7005010d046c69737401010e036d7367090004047465"
    "6c6c090004017701020f100672616e646f6d010111077465616d6d736709000802746d01011207747269676765720600"
    "06616374696f6e14060007636f6d6d616e6405020500057575696473020113077461726765747306020101140576616c"
    "756501011504726f6c6c0600076d6573736167651416021617096f626a65637469766518146d696e6563726166743a61"
    "736b5f7365727665720600076d6573736167651406000572616e67652706000572616e67652701011803616464010119"
    "0373657406000576616c7565030006000576616c7565030000";

constexpr std::string_view kServerData =
    "08000b506c61736d61207465737400";

constexpr std::string_view kRecipeBookSettings =
    "0000000000000000";

constexpr std::string_view kRecipeBookRemove =
    "01c606";

constexpr std::string_view kRecipeBookAdd =
    "01c606000104126d696e6563726166743a6f616b5f6c6f6773030424000002cc020800010100126d696e656372616674"
    "3a6f616b5f6c6f67730300";

constexpr std::string_view kTickingState =
    "41a0000000";

constexpr std::string_view kUpdateRecipes =
    "07166d696e6563726166743a736d6f6b65725f696e70757409a108d808d707cc09e109a208d6088102f0091b6d696e65"
    "63726166743a736d697468696e675f74656d706c61746513a10b970b9f0b980b960b9d0b940ba40b9e0b920ba00b9a0b"
    "9c0ba20b990b930ba30b9b0b950b176d696e6563726166743a6675726e6163655f696e7075749c01ac019a01eb03c001"
    "b904920a4de109459607d007ba01469701a201a601f003f103af01c00781029f079401d608d107ee0344ff029901a507"
    "cc09a307b901930a3ea3018b0709c6019707ea038907f2034c9407cf0748a501b101f5038801d40240be07e70947c807"
    "8707a607830a9e078701c207e703a00749bb01ab01d707a107e909f702c407ad01f10abd01be019801e803bf018a014b"
    "b90ad4019807c201b701e809f4039f01e903bf07860152b50aef03a108a001e2034f9c03c307bc019601ec039507b601"
    "c507c907940a41b201ce07a407ae014251a401f303a101a207013ba707fd02a208b60aea0250e702b801234ad6029c01"
    "e6034e8901c1079b018b018d0143f0098208b0018c01ed03a701a8039501c6078e01b301c707d8081b6d696e65637261"
    "66743a736d697468696e675f6164646974696f6e0b8c078207cd0586078507de0984078d0783078a0788071d6d696e65"
    "63726166743a626c6173745f6675726e6163655f696e7075743ec7074acf07464ba007a507c00743a2074ce8094850a3"
    "0795079707c9074fc50747be07b90a499f07bf07519807a707930ab60a9407920aa607409e07940ac207a1078907c107"
    "ce07c307c407b50aa407d007449607414252d1078b0745e7094d4ec807e9098707c607186d696e6563726166743a6361"
    "6d70666972655f696e70757409a108d808d707cc09e109a208d6088102f009176d696e6563726166743a736d69746869"
    "6e675f6261736525c807bd07cb07d407d507c207c707ea09ba07cf07c507cc07ca07ab07bc07c407f706aa07a807950a"
    "d307ac07d107bf07c107cd07d207bb07c607c907d007c307c007ce07b70aa907be07fe0102060302c405000002060301"
    "b305000002060301d203000002e90a0302ea0a000002e90a0301eb0a000002e90a0301d703000002b0020302a1020000"
    "02b0020301a303000002b0020301ca030000025b030462000002660301620000020903018103000002a8030301aa0300"
    "0002e90a0301f00a000002ed0a0301f00a000002e2030301e103000002b9040301ba040000029e030301a203000002c6"
    "010301c701000002f7020301fa02000002010301fa0200000211030119000002150301190000020c0301190000020c03"
    "0110000002090302c805000002090301b705000002090301da03000002230302a002000002230301d002000002230301"
    "c8030000025b0304bc0b0000025b0304660000025b03086e0000026603026e0000025b03046a0000026603016a000002"
    "b9040301bb04000002bb040302a702000002b9040302a702000002c6010301c801000002c80103029e02000002c60103"
    "029e02000002b4040302ab02000002b4040301b704000002090302ca05000002fd020302ca050000020a0302ca050000"
    "02090301b905000002fd020301b9050000020a0301b905000002090301dc03000002fd020301dc030000020a0301dc03"
    "000002090301fd020000020a0301fd02000002090302cb05000002fd020302cb05000002ff020302cb050000020a0302"
    "cb05000002090301ba05000002fd020301ba05000002ff020301ba050000020a0301ba05000002090301dd03000002fd"
    "020301dd03000002ff020301dd030000020a0301dd03000002090301ff02000002fd020301ff020000020a0301ff0200"
    "0002040302c705000002040301b605000002040301d603000002b4030302c005000002b3030302c005000002b4030301"
    "ae05000002b3030301ae05000002b4030301d503000002b3030301d503000002b3030301b4030000025f030463000002"
    "670301630000025f0304bd0b0000025f0304670000025f03086f0000026703026f0000025f03046b0000026703016b00"
    "0002020302c305000002020301b205000002020301ce03000002c0020302bf05000002c0020301ad05000002c0020301"
    "c903000002f8020302bd05000002f8020301ab05000002f8020301cd03000002fc020302a302000002fc020301a50300"
    "0002fc020301d003000002a8030302a402000002a8030301ac03000002a8030301d10300000261030465000002690301"
    "65000002610304bf0b0000026103046900000261030871000002690302710000026103046d0000026903016d00000206"
    "030107000002060302c605000002070302c605000002060301b505000002070301b505000002ea020301eb02000002e9"
    "0a0302f20a000002f10a0302f20a000002ed0a0302f20a000002e90a0301f30a000002f10a0301f30a000002ed0a0301"
    "f30a000002e90a0301d903000002f10a0301d903000002ed0a0301d903000002e90a0301f10a000002ed0a0301f10a00"
    "0002e90a0301ed0a000002e90a0302ee0a000002ed0a0302ee0a000002e90a0301ef0a000002ed0a0301ef0a000002e9"
    "0a0301d803000002ed0a0301d8030000020903010a000002090302c9050000020a0302c905000002090301b805000002"
    "0a0301b805000002090301db030000020a0301db0300000204030105000002040302be05000002050302be0500000204"
    "0301ac05000002050301ac0500000202030103000002020302bb05000002030302bb05000002020301a9050000020303"
    "01a9050000020c030111000002110302120000020c030212000002110301130000020c03011300000211030114000002"
    "0c030114000002b3040302aa02000002b3040301b604000002b2040302a902000002b2040301b504000002b2040301cb"
    "03000002c6020301c702000002c6020302a802000002c6020301c802000002e2030301e303000002e2030301e4030000"
    "02e2030302a502000002e2030301e503000002c2040302c505000002c2040301b405000002c2040301d303000002b904"
    "0302a602000002b9040301bc04000002b9040301cc030000029e030302a0030000029e0303019f030000029e030301a1"
    "03000002c60103029d02000002c6010301b603000002c6010301d403000002ac020302c205000002ac020301b1050000"
    "02ad020302bc05000002ad020301aa05000002ae020302c105000002ae020301b005000002af0203029c02000002f702"
    "0302a202000002010302a202000002f7020301a403000002010301a403000002f7020301cf03000002010301cf030000"
    "02010301f7020000020103029b02000002010301af0500000211030216000002150302160000020c0302160000021103"
    "0117000002150301170000020c03011700000211030118000002150301180000020c030118000002110301150000020c"
    "0301150000020c03020d0000020c03010e0000020c03010f000002720304760000027a030176000002720304c00b0000"
    "027203047a00000272030882010000027a030282010000027203047e0000027a03017e000002730304770000027b0301"
    "77000002730304c10b0000027303047b00000273030883010000027b030283010000027303047f0000027b03017f0000"
    "02750304790000027d030179000002750304c30b0000027503047d00000275030885010000027d030285010000027503"
    "0481010000027d03018101000002740304780000027c030178000002740304c20b0000027403047c0000027403088401"
    "0000027c0302840100000274030480010000027c030180010000026003046400000268030164000002600304be0b0000"
    "026003046800000260030870000002680302700000026003046c0000026803016c0000";

constexpr std::string_view kEntityData[] = {
    "81da0101017fff",  // varint
    "87db0108070182070000ff",  // item_stack
    "8bd901110010ff",  // byte
    "a7d9010408010508010903412000000f0001111900ff",  // frog_variant
    "dcd9010408010508010903412000000f0001121a00ff",  // pig_variant
    "dfda01040801050801061405090341a000000f260010007f1125000107506c61736d6141000000000000130601080007612064756d6d79ff",  // humanoid_arm
    "e0da01040801050801090341a000000f000510094120000041a0000041f0000012093f8000004000000040400000ff",  // rotations
    "e1da010408010508010b23000000003f800000000000000c23400000004000000040000000170701a80701000d012101180005ff",  // vector3
    "e2da01040801050801170ec11dff",  // block_state
    "e3d8010408010508010903412000000f0001110004120d0100000001000000020000000300000004160105ff",  // optional_living_entity
    "e3da010408010508010f000317050a080005636f6c6f72000372656408000474657874000568656c6c6f001801641901d209ff",  // component
    "e4da01090801ff",  // boolean
    "e5da0104080105080108033f8000000a100101ff",  // particle
    "e7da01040801050801080b01000bb83ffd1200be090800ff",  // optional_block_pos
    "ecda010408010508010b0f8728ff",  // optional_block_state
    "f5d9010408010508010903412000000f0001111602ff",  // cow_variant
    "f5da01040801050801080c01090701d00801002c05ff",  // direction
    "f6d9010408010508010903408000000f0001111b00ff",  // chicken_variant
    "f8d701090341200000ff",  // float
    "f9d9010408010508010903417000000f0001141c01ff",  // zombie_nautilus_variant
    "fbd901040801050801090341a000000f00011212000502ff",  // villager_data
};

std::vector<uint8_t> unhex(std::string_view hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

int failures = 0;

void check(bool condition, std::string_view what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

constexpr int32_t kProtocol = 774;

const Value* component(const mcpp::ItemStack& stack, std::string_view name) {
    return stack.components.find(name, kProtocol);
}

int64_t integer(const Value* value) { return value != nullptr ? value->as_int().value_or(-1) : -1; }

std::string text(const Value* value) {
    const auto* tag = value != nullptr ? value->as_nbt() : nullptr;
    return tag != nullptr ? mcpp::nbt::component_to_text(*tag) : std::string{};
}

size_t length(const Value* value) {
    const auto* list = value != nullptr ? value->as_list() : nullptr;
    return list != nullptr ? list->size() : 0;
}

// The inventory /give filled for the recording, one item per kind of data
// component: every slot is read, the carried stack after them, nothing left.
void test_inventory() {
    mcpp::ByteBuffer in(unhex(kContainerSetContent));
    check(in.read_varint() == 0, "the player's own container");
    in.read_varint();
    const int32_t count = in.read_varint();
    check(count == 46, std::format("{} slots", count));
    std::vector<mcpp::ItemStack> slots;
    for (int32_t i = 0; i < count; ++i) slots.push_back(mcpp::proto::read_item_stack(in, kProtocol));
    const mcpp::ItemStack carried = mcpp::proto::read_item_stack(in, kProtocol);
    check(in.remaining() == 0, std::format("{} bytes after the carried stack", in.remaining()));
    check(carried.count == 0, "nothing carried");
    for (size_t i = 9; i < slots.size(); ++i) check(slots[i].count > 0, std::format("slot {} filled", i));

    const auto& sword = slots[36];
    check(text(component(sword, "minecraft:custom_name")) == "Blade", "the sword's name");
    check(integer(component(sword, "minecraft:damage")) == 10, "the sword's damage");
    check(integer(component(sword, "minecraft:repair_cost")) == 3, "the sword's repair cost");
    const Value* rarity = component(sword, "minecraft:rarity");
    check(rarity != nullptr && rarity->label() == "epic", "the sword is epic");
    check(component(sword, "minecraft:unbreakable") != nullptr, "the sword is unbreakable");
    const Value* enchantments = component(sword, "minecraft:enchantments");
    check(length(enchantments) == 2, "two enchantments");
    if (length(enchantments) == 2) {
        int64_t levels = 0;
        for (const auto& e : *enchantments->as_list()) levels += integer(e.field("level"));
        check(levels == 8, "sharpness 5 and unbreaking 3");
    }
    const Value* lore = component(sword, "minecraft:lore");
    check(length(lore) == 2 && text(&(*lore->as_list())[1]) == "styled", "two lines of lore");
    check(length(component(sword, "minecraft:attribute_modifiers")) == 2, "two attribute modifiers");
    const Value* model = component(sword, "minecraft:item_model");
    check(model != nullptr && model->as_string() && *model->as_string() == "minecraft:stick", "item model");
    const Value* hidden = component(sword, "minecraft:tooltip_display");
    check(hidden != nullptr && length(hidden->field("hidden")) == 1, "one hidden tooltip part");

    check(integer(component(slots[37], "minecraft:dyed_color")) == 0xFF0000, "red tunic");
    check(integer(component(slots[7], "minecraft:dyed_color")) == 0x0000FF, "blue leggings");
    check(integer(component(slots[6], "minecraft:damage")) == 200, "worn elytra");

    const Value* book = component(slots[39], "minecraft:written_book_content");
    check(book != nullptr && *book->field("title")->field("raw")->as_string() == "Title", "book title");
    check(book != nullptr && *book->field("author")->as_string() == "Author", "book author");
    check(integer(book != nullptr ? book->field("generation") : nullptr) == 1, "a copy of the original");
    check(length(book != nullptr ? book->field("pages") : nullptr) == 2, "two pages");

    const Value* rockets = component(slots[43], "minecraft:fireworks");
    check(integer(rockets != nullptr ? rockets->field("flight_duration") : nullptr) == 2, "flight 2");
    const Value* explosions = rockets != nullptr ? rockets->field("explosions") : nullptr;
    check(length(explosions) == 2 && (*explosions->as_list())[0].field("shape")->label() == "star" &&
              (*explosions->as_list())[1].field("shape")->label() == "burst",
          "a star and a burst");

    const Value* bundle = component(slots[12], "minecraft:bundle_contents");
    check(length(bundle) == 2 && (*bundle->as_list())[0].as_stack()->count == 3 &&
              (*bundle->as_list())[1].as_stack()->count == 2,
          "three stone and two diamonds in the bundle");
    const Value* box = component(slots[13], "minecraft:container");
    check(length(box) == 6 && (*box->as_list())[5].as_stack()->count == 1, "a book in the box's sixth slot");

    const Value* head = component(slots[16], "minecraft:profile");
    const Value* complete = head != nullptr ? head->field("profile")->field("complete") : nullptr;
    check(complete != nullptr && *complete->field("name")->as_string() == "PlasmaA" &&
              *complete->field("uuid")->as_string() == "00000001-0000-0002-0000-000300000004",
          "a complete profile");
    const Value* partial = component(slots[17], "minecraft:profile");
    check(partial != nullptr && partial->field("profile")->field("partial") != nullptr, "a partial profile");

    const Value* rabbit = component(slots[31], "minecraft:rabbit/variant");
    check(rabbit != nullptr && rabbit->label() == "evil" && integer(rabbit) == 99, "the killer bunny");
    const Value* fish = component(slots[31], "minecraft:tropical_fish/pattern");
    check(fish != nullptr && fish->label() == "glitter", "a glitter pattern");
    const Value* swing = component(slots[28], "minecraft:swing_animation");
    check(swing != nullptr && swing->field("type")->label() == "stab", "a stab");
    const Value* range = component(slots[28], "minecraft:attack_range");
    check(range != nullptr && range->field("max_reach")->as_double() == 4.0, "reach 4");
    const Value* food = component(slots[24], "minecraft:consumable");
    check(food != nullptr && food->field("animation")->label() == "spear", "eaten like a spear");
    check(length(food != nullptr ? food->field("effects") : nullptr) == 5, "five consume effects");
    check(slots[34].components.added().empty() && slots[34].components.removed().size() == 2,
          "a sword without its modifiers and tool");
    check(integer(component(slots[35], "minecraft:ominous_bottle_amplifier")) == 3, "amplifier 3");
    check(integer(component(slots[11], "minecraft:map_id")) == 5, "map 5");
    const Value* tracker = component(slots[22], "minecraft:lodestone_tracker");
    const auto* target = tracker != nullptr ? tracker->field("target")->field("pos")->as_pos() : nullptr;
    check(target != nullptr && *target == mcpp::proto::BlockPos{1, 2, 3}, "lodestone at 1 2 3");
}

// set_entity_data from the crowd spawned beside the player, one packet for
// every serializer that appeared: each is read to its end marker exactly.
void test_entity_data() {
    std::set<mcpp::proto::MetaType> seen;
    for (const auto hex : kEntityData) {
        mcpp::ByteBuffer in(unhex(hex));
        in.read_varint();
        const auto metadata = mcpp::proto::read_metadata(in, kProtocol);
        check(in.remaining() == 0, std::format("entity data {} read to its end", hex.substr(0, 16)));
        for (const auto& entry : metadata.entries()) seen.insert(entry.type);
    }
    check(seen.size() >= 20, std::format("{} serializers seen", seen.size()));
}

void test_packets() {
    {
        mcpp::ByteBuffer in(unhex(kCommands));
        const auto tree = mcpp::proto::read_commands(in, kProtocol);
        check(in.remaining() == 0, "the command tree is read whole");
        const auto* root = tree.root_node();
        check(root != nullptr && !root->children.empty(), "a root with commands");
        bool msg = false;
        for (const auto child : root != nullptr ? root->children : std::vector<int32_t>{}) {
            check(child >= 0 && static_cast<size_t>(child) < tree.nodes.size(), "child in range");
            if (tree.nodes[static_cast<size_t>(child)].name == "msg") msg = true;
        }
        check(msg, "a player may /msg");
    }
    {
        mcpp::ByteBuffer in(unhex(kUpdateRecipes));
        const Value sets = mcpp::proto::read_named(in, "update_recipes", kProtocol);
        check(in.remaining() == 0, "update_recipes is read whole");
        // The game's seven property sets, and the server jar's 254 stonecutting recipes.
        check(length(sets.field("property_sets")) == 7, "seven property sets");
        check(length(sets.field("stonecutter")) == 254, "254 stonecutter recipes");
    }
    {
        mcpp::ByteBuffer in(unhex(kServerData));
        const Value data = mcpp::proto::read_named(in, "server_data", kProtocol);
        check(in.remaining() == 0 && text(data.field("motd")) == "Plasma test", "the server's motd");
        check(data.field("icon")->empty(), "no icon");
    }
    {
        mcpp::ByteBuffer in(unhex(kRecipeBookSettings));
        const Value settings = mcpp::proto::read_named(in, "recipe_book_settings", kProtocol);
        check(in.remaining() == 0 && settings.as_record()->size() == 8, "eight book switches");
        for (const auto& field : *settings.as_record()) check(field.value.as_bool() == false, "all off");
    }
    {
        mcpp::ByteBuffer in(unhex(kRecipeBookAdd));
        const Value added = mcpp::proto::read_named(in, "recipe_book_add", kProtocol);
        check(in.remaining() == 0 && length(added.field("entries")) > 0, "recipe_book_add is read whole");
    }
    {
        mcpp::ByteBuffer in(unhex(kRecipeBookRemove));
        check(in.read_varint() == 1, "one recipe taken");
        in.read_varint();
        check(in.remaining() == 0, "recipe_book_remove is read whole");
    }
    {
        mcpp::ByteBuffer in(unhex(kTickingState));
        check(in.read_f32() == 20.0f && !in.read_bool() && in.remaining() == 0, "20 ticks, not frozen");
    }
}

// Layouts the server has not sent here but other servers will, checked
// against the table rather than bytes: every named type it refers to exists.
// A server icon is a data url; the first bytes of every PNG, in base64.
void test_favicon() {
    const auto png = mcpp::status::decode_base64("iVBORw0KGgo=");
    const std::vector<uint8_t> magic = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    check(png == magic, "the png signature");
    check(mcpp::status::decode_base64("TWFu") == std::vector<uint8_t>{'M', 'a', 'n'}, "Man");
    check(mcpp::status::decode_base64("a*b").empty(), "not base64");
}

void test_table() {
    const std::string problem = mcpp::proto::check_layouts();
    check(problem.empty(), problem);
    check(mcpp::proto::component_name(mcpp::proto::component_type("minecraft:rabbit/variant", kProtocol),
                                      kProtocol) == "minecraft:rabbit/variant",
          "names and ids round trip");
}

} // namespace

int main() {
    test_inventory();
    test_entity_data();
    test_packets();
    test_favicon();
    test_table();
    if (failures != 0) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "data: all passed\n";
    return 0;
}
