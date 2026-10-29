#include "../big_brother.h"
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../limits.h"
#include "../protos.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
extern struct weather_data weather_info;

obj_data* has_key(const char_data* character, int key);
void do_put(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_get(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_look(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void Crash_follower_save(const struct char_data* ch, FILE* fp);
extern struct index_data* mob_index;
void do_examine(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_open(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_close(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_lock(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_unlock(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_pick(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void check_break_key(obj_data* obj, char_data* ch);
void clear_char(struct char_data* ch, int mode);
void clear_object(struct obj_data* obj);
void move_wearables_to_corpse(obj_data* corpse);
void prohibit_item_stay_zone_move(char_data* ch, int room);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_remove(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void recalc_worn_weight(char_data* character);
void remove_random_item(const struct char_data* ch, const struct obj_data* corpse);
extern struct script_head* script_table;
extern int top_of_script_table;

namespace {

constexpr int kRoomVnum = 5000;
constexpr int kKeyVnum = 100;
constexpr int kBreakKeyVnum = 101;
constexpr int kRingVnum = 200;
constexpr int kBagVnum = 300;
constexpr int kBeltVnum = 400;
constexpr int kFingerVnum = 500;
constexpr int kBreadVnum = 600;
constexpr int kQuiverVnum = 700;

struct proto_spec {
    int vnum;
    const char* name;
    const char* shortd;
};

const proto_spec kSpecs[] = {
    { kKeyVnum, "key brass", "a brass key" },
    { kBreakKeyVnum, "key tin", "a tin key" },
    { kRingVnum, "keyring ring iron", "an iron keyring" },
    { kBagVnum, "bag leather", "a leather bag" },
    { kBeltVnum, "belt waist", "a waist belt" },
    { kFingerVnum, "ring gold", "a gold ring" },
    { kBreadVnum, "bread loaf", "a loaf of bread" },
    { kQuiverVnum, "quiver leather", "a leather quiver" },
};
constexpr int kProtoCount = sizeof(kSpecs) / sizeof(kSpecs[0]);

/* Shared by the keyring tests: one room, one player with a descriptor that
 * captures output, and a small set of prototypes. */
class KeyringTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_obj_proto = obj_proto;
        m_saved_obj_index = obj_index;
        m_saved_top_of_objt = top_of_objt;
        m_saved_object_list = object_list;
        m_saved_top_of_world = top_of_world;
        m_saved_descriptor_list = descriptor_list;
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(1);
        m_saved_number = world[0].number;
        m_saved_light = world[0].light;
        m_saved_contents = world[0].contents;
        m_saved_people = world[0].people;

        descriptor_list = nullptr;
        object_list = nullptr;
        top_of_world = 0;
        world[0].number = kRoomVnum;
        world[0].light = 1;
        world[0].contents = nullptr;

        for (int i = 0; i < kProtoCount; ++i) {
            obj_data& proto = m_obj_proto[i];
            clear_object(&proto);
            proto.item_number = i;
            std::strncpy(m_names[i], kSpecs[i].name, sizeof(m_names[i]) - 1);
            std::strncpy(m_shorts[i], kSpecs[i].shortd, sizeof(m_shorts[i]) - 1);
            proto.name = m_names[i];
            proto.short_description = m_shorts[i];
            proto.obj_flags.weight = 1;
            proto.obj_flags.wear_flags = ITEM_TAKE;
            m_obj_index[i].virt = kSpecs[i].vnum;
        }
        set_proto(kKeyVnum, ITEM_KEY, 1);
        set_proto(kBreakKeyVnum, ITEM_KEY, 1);
        proto_for(kBreakKeyVnum).obj_flags.extra_flags |= ITEM_BREAKABLE;
        set_proto(kRingVnum, ITEM_KEYRING, 2);
        proto_for(kRingVnum).obj_flags.value[0] = 100;
        proto_for(kRingVnum).obj_flags.wear_flags |= ITEM_HOLD | ITEM_WEAR_BELT;
        set_proto(kBagVnum, ITEM_CONTAINER, 5);
        proto_for(kBagVnum).obj_flags.value[0] = 100;
        set_proto(kBeltVnum, ITEM_WORN, 1);
        proto_for(kBeltVnum).obj_flags.wear_flags |= ITEM_WEAR_WAISTE;
        set_proto(kFingerVnum, ITEM_WORN, 1);
        proto_for(kFingerVnum).obj_flags.wear_flags |= ITEM_WEAR_FINGER;
        set_proto(kBreadVnum, ITEM_FOOD, 1);
        set_proto(kQuiverVnum, ITEM_CONTAINER, 2);
        proto_for(kQuiverVnum).obj_flags.value[0] = 100;
        proto_for(kQuiverVnum).obj_flags.wear_flags |= ITEM_WEAR_BACK;

        obj_proto = m_obj_proto;
        obj_index = m_obj_index;
        top_of_objt = kProtoCount - 1;

        clear_char(&m_player, 0);
        m_player.player.name = m_player_name;
        m_player.player.race = RACE_HUMAN;
        m_player.player.level = 10;
        m_player.tmpabilities.str = 18;
        m_player.tmpabilities.dex = 18;
        m_player.in_room = 0;
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.descriptor = 1;
        m_descriptor.character = &m_player;
        m_player.desc = &m_descriptor;

        world[0].people = &m_player;
        m_player.next_in_room = nullptr;
    }

    void TearDown() override
    {
        /* Unequip so extract_obj does not leave dangling slots behind. */
        for (int i = 0; i < MAX_WEAR; ++i)
            m_player.equipment[i] = nullptr;
        m_player.carrying = nullptr;
        while (object_list != nullptr)
            extract_obj(object_list);

        world[0].number = m_saved_number;
        world[0].light = m_saved_light;
        world[0].contents = m_saved_contents;
        world[0].people = m_saved_people;
        obj_proto = m_saved_obj_proto;
        obj_index = m_saved_obj_index;
        top_of_objt = m_saved_top_of_objt;
        object_list = m_saved_object_list;
        top_of_world = m_saved_top_of_world;
        descriptor_list = m_saved_descriptor_list;
        release_cleared_character(&m_player);
        release_test_descriptor(&m_descriptor);
    }

    /* A fresh object from the prototype with this vnum, not yet placed. */
    obj_data* make(int vnum)
    {
        for (int i = 0; i < kProtoCount; ++i)
            if (m_obj_index[i].virt == vnum)
                return read_object(i, REAL);
        return nullptr;
    }

    std::string output() const { return std::string(m_descriptor.output); }
    void clear_output()
    {
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    obj_data& proto_for(int vnum)
    {
        for (int i = 0; i < kProtoCount; ++i)
            if (m_obj_index[i].virt == vnum)
                return m_obj_proto[i];
        return m_obj_proto[0];
    }

    char_data m_player {};
    descriptor_data m_descriptor {};

private:
    void set_proto(int vnum, int type, int weight)
    {
        obj_data& proto = proto_for(vnum);
        proto.obj_flags.type_flag = type;
        proto.obj_flags.weight = weight;
    }

    char m_names[kProtoCount][32] {};
    char m_shorts[kProtoCount][32] {};
    char m_player_name[16] = "Keeper";
    obj_data m_obj_proto[kProtoCount] {};
    index_data m_obj_index[kProtoCount] {};

    obj_data* m_saved_obj_proto = nullptr;
    index_data* m_saved_obj_index = nullptr;
    int m_saved_top_of_objt = 0;
    obj_data* m_saved_object_list = nullptr;
    int m_saved_top_of_world = 0;
    descriptor_data* m_saved_descriptor_list = nullptr;
    int m_saved_number = 0;
    byte m_saved_light = 0;
    obj_data* m_saved_contents = nullptr;
    char_data* m_saved_people = nullptr;
};

} // namespace

TEST_F(KeyringTest, HasKeyFindsLooseKeyAsBefore)
{
    obj_data* key = make(kKeyVnum);
    obj_to_char(key, &m_player);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), key);
}

TEST_F(KeyringTest, HasKeyFindsHeldKeyAsBefore)
{
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, key, HOLD);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), key);
}

TEST_F(KeyringTest, HasKeyFindsKeyOnCarriedKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    obj_to_char(ring, &m_player);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), key);
}

TEST_F(KeyringTest, HasKeyFindsKeyOnHeldKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    equip_char(&m_player, ring, HOLD);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), key);
}

TEST_F(KeyringTest, HasKeyFindsKeyOnBeltKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    equip_char(&m_player, ring, WEAR_BELT_2);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), key);
}

TEST_F(KeyringTest, HasKeyIgnoresKeyringInsideBag)
{
    obj_data* bag = make(kBagVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    obj_to_obj(ring, bag);
    obj_to_char(bag, &m_player);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), nullptr);
}

TEST_F(KeyringTest, HasKeyIgnoresKeyInsideBag)
{
    obj_data* bag = make(kBagVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, bag);
    obj_to_char(bag, &m_player);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), nullptr);
}

TEST_F(KeyringTest, HasKeySkipsBrokenKeyOnKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    SET_BIT(key->obj_flags.extra_flags, ITEM_BROKEN);
    obj_to_obj(key, ring);
    obj_to_char(ring, &m_player);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), nullptr);
}

TEST_F(KeyringTest, BreakableKeyOnKeyringBreaksAndStaysOn)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kBreakKeyVnum);
    obj_to_obj(key, ring);
    obj_to_char(ring, &m_player);
    obj_data* found = has_key(&m_player, kBreakKeyVnum);
    ASSERT_EQ(found, key);
    check_break_key(found, &m_player);
    EXPECT_TRUE(IS_SET(key->obj_flags.extra_flags, ITEM_BROKEN));
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(has_key(&m_player, kBreakKeyVnum), nullptr);
}

namespace {
void run_put(char_data* ch, const char* text)
{
    char buf[256];
    std::strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    do_put(ch, buf, nullptr, 0, 0);
}
}

TEST_F(KeyringTest, PutKeyOnCarriedKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_char(ring, &m_player);
    obj_to_char(key, &m_player);
    int count = IS_CARRYING_N(&m_player);
    int weight = IS_CARRYING_W(&m_player);
    run_put(&m_player, "key keyring");
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(IS_CARRYING_N(&m_player), count - 1);
    EXPECT_EQ(IS_CARRYING_W(&m_player), weight);
    EXPECT_NE(output().find("You put"), std::string::npos);
}

TEST_F(KeyringTest, PutKeyOnBeltKeyring)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_char(key, &m_player);
    int weight = IS_CARRYING_W(&m_player);
    run_put(&m_player, "key keyring");
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(IS_CARRYING_W(&m_player), weight);
}

TEST_F(KeyringTest, PutFindsBeltKeyringNotFingerRing)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* finger = make(kFingerVnum); /* "ring gold" */
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    /* A ring that is not a keyring on the first belt slot: the belt step
     * passes over it to the keyring on the second. */
    equip_char(&m_player, finger, WEAR_BELT_1);
    equip_char(&m_player, ring, WEAR_BELT_2);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key ring");
    EXPECT_EQ(key->in_obj, ring);
}

TEST_F(KeyringTest, PutSecondBeltKeyringByNumber)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring1 = make(kRingVnum);
    obj_data* ring2 = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring1, WEAR_BELT_1);
    equip_char(&m_player, ring2, WEAR_BELT_2);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key 2.keyring");
    EXPECT_EQ(key->in_obj, ring2);
}

TEST_F(KeyringTest, PutNonKeyOnKeyringRefused)
{
    obj_data* ring = make(kRingVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(ring, &m_player);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "bread keyring");
    EXPECT_EQ(bread->carried_by, &m_player);
    EXPECT_NE(output().find("Only keys can go on a keyring.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, PutAllOnKeyringTakesOnlyKeys)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kBreakKeyVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(ring, &m_player);
    obj_to_char(key1, &m_player);
    obj_to_char(key2, &m_player);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "all keyring");
    EXPECT_EQ(key1->in_obj, ring);
    EXPECT_EQ(key2->in_obj, ring);
    EXPECT_EQ(bread->carried_by, &m_player);
    EXPECT_EQ(output().find("Only keys"), std::string::npos);
}

TEST_F(KeyringTest, PutAllDotOnKeyringTakesMatchingKeys)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kBreakKeyVnum);
    obj_to_char(ring, &m_player);
    obj_to_char(key1, &m_player);
    obj_to_char(key2, &m_player);
    run_put(&m_player, "all.brass keyring");
    EXPECT_EQ(key1->in_obj, ring);
    EXPECT_EQ(key2->carried_by, &m_player);
}

TEST_F(KeyringTest, PutAllOnKeyringWithNoKeys)
{
    obj_data* ring = make(kRingVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(ring, &m_player);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "all keyring");
    EXPECT_EQ(bread->carried_by, &m_player);
    EXPECT_NE(output().find("You don't seem to have any keys to put on it.\n\r"),
        std::string::npos);
}

TEST_F(KeyringTest, PutOnKeyringIgnoresClosedBit)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    ring->obj_flags.value[1] = CONT_CLOSED;
    obj_to_char(ring, &m_player);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key keyring");
    EXPECT_EQ(key->in_obj, ring);
}

TEST_F(KeyringTest, PutOnHeldKeyringNotReached)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, ring, HOLD);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key keyring");
    EXPECT_EQ(key->carried_by, &m_player);
    EXPECT_NE(output().find("You don't see a keyring here.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, PutIntoBagUnchanged)
{
    obj_data* bag = make(kBagVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(bag, &m_player);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "bread bag");
    EXPECT_EQ(bread->in_obj, bag);

    obj_data* held = make(kBagVnum);
    obj_data* bread2 = make(kBreadVnum);
    equip_char(&m_player, held, HOLD);
    obj_to_char(bread2, &m_player);
    clear_output();
    run_put(&m_player, "bread bag");
    /* Only the carried bag is reachable; the held one is not. */
    EXPECT_EQ(bread2->in_obj, bag);
    EXPECT_NE(held->contains, bread2);
}

TEST_F(KeyringTest, PutWontFitOnFullKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kBreakKeyVnum);
    ring->obj_flags.value[0] = 3;
    obj_to_char(ring, &m_player);
    obj_to_char(key1, &m_player);
    obj_to_char(key2, &m_player);
    run_put(&m_player, "brass keyring");
    clear_output();
    run_put(&m_player, "tin keyring");
    EXPECT_EQ(key1->in_obj, ring);
    EXPECT_EQ(key2->carried_by, &m_player);
    EXPECT_NE(output().find("won't fit"), std::string::npos);
}

namespace {
void run_get(char_data* ch, const char* text)
{
    char buf[256];
    std::strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    do_get(ch, buf, nullptr, 0, 0);
}
}

TEST_F(KeyringTest, GetKeyFromCarriedKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_char(ring, &m_player);
    obj_to_obj(key, ring);
    int count = IS_CARRYING_N(&m_player);
    int weight = IS_CARRYING_W(&m_player);
    run_get(&m_player, "key keyring");
    EXPECT_EQ(key->carried_by, &m_player);
    EXPECT_EQ(key->in_obj, nullptr);
    EXPECT_EQ(IS_CARRYING_N(&m_player), count + 1);
    EXPECT_EQ(IS_CARRYING_W(&m_player), weight);
    EXPECT_NE(output().find("You get"), std::string::npos);
}

TEST_F(KeyringTest, GetKeyFromBeltKeyring)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(key, ring);
    int count = IS_CARRYING_N(&m_player);
    int weight = IS_CARRYING_W(&m_player);
    run_get(&m_player, "key keyring");
    EXPECT_EQ(key->carried_by, &m_player);
    EXPECT_EQ(key->in_obj, nullptr);
    EXPECT_EQ(IS_CARRYING_N(&m_player), count + 1);
    EXPECT_EQ(IS_CARRYING_W(&m_player), weight);
}

/* Hands already full: do_get refuses before any container is searched.
 * The per-item limit is GetAllFromBeltKeyringStopsAtItemLimit. */
TEST_F(KeyringTest, GetFromBeltKeyringWithFullHandsRefusedUpFront)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(key, ring);
    while (IS_CARRYING_N(&m_player) < CAN_CARRY_N(&m_player))
        obj_to_char(make(kBreadVnum), &m_player);
    run_get(&m_player, "key keyring");
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(key->carried_by, nullptr);
    /* do_get refuses up front when the hands are full. */
    EXPECT_NE(output().find("Your hands are already full!"), std::string::npos);
}

TEST_F(KeyringTest, GetAllFromBeltKeyring)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kBreakKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(key1, ring);
    obj_to_obj(key2, ring);
    run_get(&m_player, "all keyring");
    EXPECT_EQ(key1->carried_by, &m_player);
    EXPECT_EQ(key2->carried_by, &m_player);
    EXPECT_EQ(ring->contains, nullptr);
}

TEST_F(KeyringTest, GetAllFromCarriedKeyringsViaAllDot)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_char(ring, &m_player);
    obj_to_obj(key, ring);
    run_get(&m_player, "key all.keyring");
    EXPECT_EQ(key->carried_by, &m_player);
}

TEST_F(KeyringTest, GetFromKeyringIgnoresClosedBit)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    ring->obj_flags.value[1] = CONT_CLOSED;
    obj_to_char(ring, &m_player);
    obj_to_obj(key, ring);
    run_get(&m_player, "key keyring");
    EXPECT_EQ(key->carried_by, &m_player);
}

TEST_F(KeyringTest, GetFromHeldKeyringNotReached)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, ring, HOLD);
    obj_to_obj(key, ring);
    run_get(&m_player, "key keyring");
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_NE(output().find("You don't have a keyring.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, GetFromBagUnchanged)
{
    obj_data* bag = make(kBagVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(bag, &m_player);
    obj_to_obj(bread, bag);
    run_get(&m_player, "bread bag");
    EXPECT_EQ(bread->carried_by, &m_player);
    EXPECT_EQ(bread->in_obj, nullptr);
}

namespace {
void run_cmd(void (*fn)(char_data*, char*, waiting_type*, int, int), char_data* ch,
    const char* text, int cmd = 0, int subcmd = 0)
{
    char buf[256];
    std::strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    fn(ch, buf, nullptr, cmd, subcmd);
}
}

TEST_F(KeyringTest, LookInBeltKeyringListsKeys)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(key, ring);
    run_cmd(do_look, &m_player, "in keyring", 15);
    EXPECT_NE(output().find("keyring (worn) : \n\r"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, LookInCarriedKeyringListsKeys)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_char(ring, &m_player);
    obj_to_obj(key, ring);
    run_cmd(do_look, &m_player, "in keyring", 15);
    EXPECT_NE(output().find("keyring (carried) : \n\r"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, LookInKeyringIgnoresClosedBit)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    ring->obj_flags.value[1] = CONT_CLOSED;
    obj_to_char(ring, &m_player);
    obj_to_obj(key, ring);
    run_cmd(do_look, &m_player, "in keyring", 15);
    EXPECT_EQ(output().find("It is closed."), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, ExamineBeltKeyringShowsInside)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(key, ring);
    run_cmd(do_examine, &m_player, "keyring");
    EXPECT_NE(output().find("When you look inside"), std::string::npos);
    EXPECT_NE(output().find("keyring (worn) : "), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, OpenKeyringSaysAlwaysOpen)
{
    obj_data* ring = make(kRingVnum);
    obj_to_char(ring, &m_player);
    run_cmd(do_open, &m_player, "keyring");
    EXPECT_NE(output().find("A keyring is always open.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, CloseKeyringSaysAlwaysOpen)
{
    obj_data* ring = make(kRingVnum);
    obj_to_char(ring, &m_player);
    run_cmd(do_close, &m_player, "keyring");
    EXPECT_NE(output().find("A keyring is always open.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, LockKeyringHasNoLock)
{
    obj_data* ring = make(kRingVnum);
    obj_to_char(ring, &m_player);
    run_cmd(do_lock, &m_player, "keyring");
    EXPECT_NE(output().find("A keyring has no lock.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, UnlockKeyringHasNoLock)
{
    obj_data* ring = make(kRingVnum);
    obj_to_char(ring, &m_player);
    run_cmd(do_unlock, &m_player, "keyring");
    EXPECT_NE(output().find("A keyring has no lock.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, PickKeyringHasNoLock)
{
    obj_data* ring = make(kRingVnum);
    obj_to_char(ring, &m_player);
    run_cmd(do_pick, &m_player, "keyring");
    EXPECT_NE(output().find("A keyring has no lock.\n\r"), std::string::npos);
    EXPECT_EQ(output().find("That's not a container."), std::string::npos);
}

TEST_F(KeyringTest, LookInBagUnchanged)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* bag = make(kBagVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(bag, &m_player);
    obj_to_obj(bread, bag);
    run_cmd(do_look, &m_player, "in bag", 15);
    EXPECT_NE(output().find("bag (carried) : \n\r"), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
}

TEST_F(KeyringTest, CorpseParseMovesBaggedKeyringWhole)
{
    obj_data* corpse = make(kBagVnum);
    corpse->obj_flags.value[3] = 1;
    obj_data* bag = make(kBagVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(bag, corpse);
    obj_to_obj(ring, bag);
    obj_to_obj(key, ring);
    move_wearables_to_corpse(corpse);
    EXPECT_EQ(ring->in_obj, corpse);
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(bag->in_obj, corpse);
}

TEST_F(KeyringTest, CorpseParseLeavesTopLevelKeyringIntact)
{
    obj_data* corpse = make(kBagVnum);
    corpse->obj_flags.value[3] = 1;
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(ring, corpse);
    obj_to_obj(key, ring);
    move_wearables_to_corpse(corpse);
    EXPECT_EQ(ring->in_obj, corpse);
    EXPECT_EQ(key->in_obj, ring);
}

TEST_F(KeyringTest, CorpseParseStillPullsLooseKeysFromBag)
{
    obj_data* corpse = make(kBagVnum);
    corpse->obj_flags.value[3] = 1;
    obj_data* bag = make(kBagVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(bag, corpse);
    obj_to_obj(key, bag);
    move_wearables_to_corpse(corpse);
    EXPECT_EQ(key->in_obj, corpse);
}

TEST_F(KeyringTest, StayZoneKeyOnKeyringIsDropped)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    SET_BIT(key->obj_flags.extra_flags, ITEM_STAY_ZONE);
    obj_to_char(ring, &m_player);
    obj_to_obj(key, ring);
    prohibit_item_stay_zone_move(&m_player, 0);
    EXPECT_EQ(key->in_room, 0);
    EXPECT_EQ(key->in_obj, nullptr);
    EXPECT_EQ(ring->carried_by, &m_player);
}

TEST_F(KeyringTest, StayZoneKeyOnBeltKeyringIsDropped)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* stay = make(kKeyVnum);
    obj_data* other = make(kBreakKeyVnum);
    SET_BIT(stay->obj_flags.extra_flags, ITEM_STAY_ZONE);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(stay, ring);
    obj_to_obj(other, ring);
    prohibit_item_stay_zone_move(&m_player, 0);
    EXPECT_EQ(stay->in_room, 0);
    EXPECT_EQ(stay->in_obj, nullptr);
    EXPECT_EQ(other->in_obj, ring);
    EXPECT_EQ(m_player.equipment[WEAR_BELT_1], ring);
    EXPECT_NE(output().find("You drop a brass key.\n\r"), std::string::npos);
}

TEST_F(KeyringTest, StayZoneKeyOnHeldKeyringIsDropped)
{
    obj_data* ring = make(kRingVnum);
    obj_data* stay = make(kKeyVnum);
    SET_BIT(stay->obj_flags.extra_flags, ITEM_STAY_ZONE);
    equip_char(&m_player, ring, HOLD);
    obj_to_obj(stay, ring);
    prohibit_item_stay_zone_move(&m_player, 0);
    EXPECT_EQ(stay->in_room, 0);
    EXPECT_EQ(m_player.equipment[HOLD], ring);
}

TEST_F(KeyringTest, StayZoneItemInHeldBagUnchanged)
{
    obj_data* bag = make(kBagVnum);
    obj_data* stay = make(kKeyVnum);
    SET_BIT(stay->obj_flags.extra_flags, ITEM_STAY_ZONE);
    proto_for(kBagVnum).obj_flags.wear_flags |= ITEM_HOLD;
    bag->obj_flags.wear_flags |= ITEM_HOLD;
    equip_char(&m_player, bag, HOLD);
    obj_to_obj(stay, bag);
    prohibit_item_stay_zone_move(&m_player, 0);
    EXPECT_EQ(stay->in_obj, bag);
}

/* One numbered list: the normal search first (inventory, room), then belt
 * keyrings. Keyring A is on the belt, keyring B is carried. */
class KeyringListTest : public KeyringTest {
protected:
    void SetUp() override
    {
        KeyringTest::SetUp();
        m_player.specials.position = POSITION_STANDING;
        belt = make(kBeltVnum);
        ring_a = make(kRingVnum);
        ring_b = make(kRingVnum);
        std::strcpy(desc_a, "Ring A on the belt.\n\r");
        std::strcpy(desc_b, "Ring B in the pack.\n\r");
        ring_a->action_description = desc_a;
        ring_b->action_description = desc_b;
        equip_char(&m_player, belt, WEAR_WAISTE);
        equip_char(&m_player, ring_a, WEAR_BELT_1);
        obj_to_char(ring_b, &m_player);
    }

    /* Brass key on A, tin key on B. */
    void load_keys()
    {
        key_a = make(kKeyVnum);
        key_b = make(kBreakKeyVnum);
        obj_to_obj(key_a, ring_a);
        obj_to_obj(key_b, ring_b);
    }

    obj_data* belt = nullptr;
    obj_data* ring_a = nullptr;
    obj_data* ring_b = nullptr;
    obj_data* key_a = nullptr;
    obj_data* key_b = nullptr;
    char desc_a[32] {};
    char desc_b[32] {};
};

TEST_F(KeyringListTest, PutPlainNameIsCarriedKeyring)
{
    obj_data* key = make(kKeyVnum);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key keyring");
    EXPECT_EQ(key->in_obj, ring_b);
}

TEST_F(KeyringListTest, PutFirstIsCarriedKeyring)
{
    obj_data* key = make(kKeyVnum);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key 1.keyring");
    EXPECT_EQ(key->in_obj, ring_b);
}

TEST_F(KeyringListTest, PutSecondIsBeltKeyring)
{
    obj_data* key = make(kKeyVnum);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key 2.keyring");
    EXPECT_EQ(key->in_obj, ring_a);
}

TEST_F(KeyringListTest, PutThirdFindsNothing)
{
    obj_data* key = make(kKeyVnum);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key 3.keyring");
    EXPECT_EQ(key->carried_by, &m_player);
    EXPECT_NE(output().find("You don't see a 3.keyring here.\n\r"), std::string::npos);
}

TEST_F(KeyringListTest, GetPlainNameIsCarriedKeyring)
{
    load_keys();
    run_get(&m_player, "key keyring");
    EXPECT_EQ(key_b->carried_by, &m_player);
    EXPECT_EQ(key_a->in_obj, ring_a);
}

TEST_F(KeyringListTest, GetSecondIsBeltKeyring)
{
    load_keys();
    run_get(&m_player, "key 2.keyring");
    EXPECT_EQ(key_a->carried_by, &m_player);
    EXPECT_EQ(key_b->in_obj, ring_b);
}

TEST_F(KeyringListTest, LookInFirstIsCarriedKeyring)
{
    load_keys();
    run_cmd(do_look, &m_player, "in 1.keyring", 15);
    EXPECT_NE(output().find("(carried)"), std::string::npos);
    EXPECT_NE(output().find("a tin key"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringListTest, LookInPlainNameIsCarriedKeyring)
{
    load_keys();
    run_cmd(do_look, &m_player, "in keyring", 15);
    EXPECT_NE(output().find("(carried)"), std::string::npos);
    EXPECT_NE(output().find("a tin key"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringListTest, LookInSecondIsBeltKeyring)
{
    load_keys();
    run_cmd(do_look, &m_player, "in 2.keyring", 15);
    EXPECT_NE(output().find("(worn)"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
    EXPECT_EQ(output().find("a tin key"), std::string::npos);
}

TEST_F(KeyringListTest, ExaminePlainNameIsCarriedKeyring)
{
    load_keys();
    run_cmd(do_examine, &m_player, "keyring");
    EXPECT_NE(output().find("Ring B in the pack."), std::string::npos);
    EXPECT_EQ(output().find("Ring A on the belt."), std::string::npos);
    EXPECT_NE(output().find("a tin key"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringListTest, ExamineSecondIsBeltKeyring)
{
    load_keys();
    run_cmd(do_examine, &m_player, "2.keyring");
    EXPECT_NE(output().find("Ring A on the belt."), std::string::npos);
    EXPECT_EQ(output().find("Ring B in the pack."), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
    EXPECT_EQ(output().find("a tin key"), std::string::npos);
}

/* Puts a mob named name ("guard keyring") in the room, after player. */
static void add_keyring_guard(char_data& player, const char* name = "guard keyring")
{
    static char_data mob;
    static char mob_name[32];
    std::strncpy(mob_name, name, sizeof(mob_name) - 1);
    static char mob_short[] = "a keyring guard";
    static char mob_long[] = "A keyring guard stands here.\n\r";
    static char mob_desc[] = "He is the guard.\n\r";
    release_cleared_character(&mob); /* what the previous call's clear_char gave it */
    clear_char(&mob, MOB_VOID);
    mob.player.name = mob_name;
    mob.player.short_descr = mob_short;
    mob.player.long_descr = mob_long;
    mob.player.description = mob_desc;
    mob.player.race = RACE_HUMAN;
    mob.player.level = 10;
    mob.specials2.act = MOB_ISNPC;
    mob.specials.position = POSITION_STANDING;
    mob.in_room = 0;
    mob.next_in_room = nullptr;
    player.next_in_room = &mob;
}

TEST_F(KeyringListTest, ExaminePersonBeatsBeltKeyring)
{
    load_keys();
    add_keyring_guard(m_player);
    run_cmd(do_examine, &m_player, "keyring");
    m_player.next_in_room = nullptr;
    EXPECT_NE(output().find("He is the guard."), std::string::npos);
    EXPECT_EQ(output().find("Ring A on the belt."), std::string::npos);
    /* After a person, the contents step searches objects: the carried
     * keyring before the belt one. */
    EXPECT_NE(output().find("a tin key"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, ExamineFillWordKeepsOldLookIn)
{
    // As on release-frodo: examine picks the bag (fill word skipped), but the
    // contents step reads the raw first word "the" and finds nothing.
    m_player.specials.position = POSITION_STANDING;
    obj_data* bag = make(kBagVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(bag, &m_player);
    obj_to_obj(bread, bag);
    run_cmd(do_examine, &m_player, "the bag");
    EXPECT_NE(output().find("When you look inside"), std::string::npos);
    EXPECT_NE(output().find("You do not see that item here."), std::string::npos);
    EXPECT_EQ(output().find("a loaf of bread"), std::string::npos);
}

TEST_F(KeyringTest, ExamineHeldBagUnchanged)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* bag = make(kBagVnum);
    obj_data* bread = make(kBreadVnum);
    bag->obj_flags.wear_flags |= ITEM_HOLD;
    equip_char(&m_player, bag, HOLD);
    obj_to_obj(bread, bag);
    run_cmd(do_examine, &m_player, "bag");
    EXPECT_NE(output().find("When you look inside"), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
}

/* As the old search: people and objects are numbered apart, so "2.keyring"
 * with one keyring guard is the second keyring object. */
TEST_F(KeyringListTest, ExamineNumberCountsObjectsNotPeople)
{
    load_keys();
    add_keyring_guard(m_player);
    run_cmd(do_examine, &m_player, "2.keyring");
    m_player.next_in_room = nullptr;
    EXPECT_EQ(output().find("He is the guard."), std::string::npos);
    EXPECT_NE(output().find("Ring A on the belt."), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
    EXPECT_EQ(output().find("a tin key"), std::string::npos);
}

TEST_F(KeyringListTest, ExamineThirdFindsNothing)
{
    load_keys();
    add_keyring_guard(m_player);
    run_cmd(do_examine, &m_player, "3.keyring");
    m_player.next_in_room = nullptr;
    EXPECT_EQ(output().find("He is the guard."), std::string::npos);
    EXPECT_EQ(output().find("Ring A on the belt."), std::string::npos);
    EXPECT_EQ(output().find("Ring B in the pack."), std::string::npos);
    EXPECT_EQ(output().find("When you look inside"), std::string::npos);
}

TEST_F(KeyringListTest, ExamineFirstIsCarriedKeyring)
{
    load_keys();
    run_cmd(do_examine, &m_player, "1.keyring");
    EXPECT_NE(output().find("Ring B in the pack."), std::string::npos);
    EXPECT_EQ(output().find("Ring A on the belt."), std::string::npos);
    EXPECT_NE(output().find("a tin key"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

/* "get <x> all.keyring": carried keyrings first, then the belt. */
TEST_F(KeyringListTest, GetAllDotKeyringReachesBeltAfterCarried)
{
    load_keys();
    run_get(&m_player, "all all.keyring");
    EXPECT_EQ(key_a->carried_by, &m_player);
    EXPECT_EQ(key_b->carried_by, &m_player);
    size_t tin = output().find("a tin key");
    size_t brass = output().find("a brass key");
    ASSERT_NE(tin, std::string::npos);
    ASSERT_NE(brass, std::string::npos);
    EXPECT_LT(tin, brass);
}

/* The contents step shows the item examine found: after a person, the old
 * search finds the carried bag, and look in's belt-first search must not
 * swap in the belt keyring that shares the word. */
TEST_F(KeyringTest, ExaminePersonThenBagNotBeltKeyring)
{
    m_player.specials.position = POSITION_STANDING;
    static char bag_name[] = "bag iron"; /* outlives the test for TearDown */
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_data* bag = make(kBagVnum);
    obj_data* bread = make(kBreadVnum);
    bag->name = bag_name;
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(key, ring);
    obj_to_char(bag, &m_player);
    obj_to_obj(bread, bag);
    add_keyring_guard(m_player, "guard iron");
    run_cmd(do_examine, &m_player, "iron");
    m_player.next_in_room = nullptr;
    EXPECT_NE(output().find("He is the guard."), std::string::npos);
    EXPECT_NE(output().find("bag (carried) : \n\r"), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

/* ===== Worn weight follows a worn keyring's keys ===== */

namespace {
/* What recalc_worn_weight (the Crash_load fix-up) makes of the gear now. */
void expect_worn_weight_matches_recalc(char_data& player)
{
    int worn = GET_WORN_WEIGHT(&player);
    int encumb = GET_ENCUMB_WEIGHT(&player);
    recalc_worn_weight(&player);
    EXPECT_EQ(worn, GET_WORN_WEIGHT(&player));
    EXPECT_EQ(encumb, GET_ENCUMB_WEIGHT(&player));
}
}

TEST_F(KeyringTest, WornKeyringPutRemoveGetWearLeavesWornWeight)
{
    /* Odd key weight: belt slots add weight / 2, so rounding shows. */
    proto_for(kKeyVnum).obj_flags.weight = 3;
    m_player.player.level = 50;
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    obj_to_char(ring, &m_player);
    for (int i = 0; i < 4; ++i)
        obj_to_char(make(kKeyVnum), &m_player);
    run_cmd(do_wear, &m_player, "keyring");
    ASSERT_EQ(m_player.equipment[WEAR_BELT_1], ring);
    int worn = GET_WORN_WEIGHT(&m_player);
    int encumb = GET_ENCUMB_WEIGHT(&m_player);
    int carry = IS_CARRYING_W(&m_player);
    for (int cycle = 0; cycle < 20; ++cycle) {
        run_cmd(do_put, &m_player, "all keyring");
        ASSERT_EQ(ring->contains != nullptr, true) << output();
        run_cmd(do_remove, &m_player, "keyring");
        run_cmd(do_get, &m_player, "all keyring");
        run_cmd(do_wear, &m_player, "keyring");
        ASSERT_EQ(m_player.equipment[WEAR_BELT_1], ring) << output();
    }
    EXPECT_EQ(GET_WORN_WEIGHT(&m_player), worn);
    EXPECT_EQ(GET_ENCUMB_WEIGHT(&m_player), encumb);
    EXPECT_EQ(IS_CARRYING_W(&m_player), carry);
}

TEST_F(KeyringTest, BeltKeyringPutAndGetMatchRecalc)
{
    proto_for(kKeyVnum).obj_flags.weight = 3;
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_char(make(kKeyVnum), &m_player);
    obj_to_char(make(kKeyVnum), &m_player);
    expect_worn_weight_matches_recalc(m_player);
    run_cmd(do_put, &m_player, "key keyring");
    expect_worn_weight_matches_recalc(m_player);
    run_cmd(do_put, &m_player, "key keyring");
    ASSERT_EQ(IS_CARRYING_N(&m_player), 0);
    expect_worn_weight_matches_recalc(m_player);
    run_cmd(do_get, &m_player, "key keyring");
    expect_worn_weight_matches_recalc(m_player);
    run_cmd(do_get, &m_player, "key keyring");
    ASSERT_EQ(ring->contains, nullptr);
    expect_worn_weight_matches_recalc(m_player);
}

TEST_F(KeyringTest, HeldKeyringKeysMatchRecalc)
{
    proto_for(kKeyVnum).obj_flags.weight = 3;
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, HOLD);
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kKeyVnum);
    obj_to_obj(key1, ring);
    expect_worn_weight_matches_recalc(m_player);
    obj_to_obj(key2, ring);
    expect_worn_weight_matches_recalc(m_player);
    obj_from_obj(key1);
    obj_to_char(key1, &m_player);
    expect_worn_weight_matches_recalc(m_player);
    obj_from_obj(key2);
    obj_to_char(key2, &m_player);
    expect_worn_weight_matches_recalc(m_player);
}

TEST_F(KeyringTest, WornQuiverWeightUnchanged)
{
    /* Worn quivers keep the old behavior (a group decision): the worn
     * weight does not follow what goes in or out. */
    obj_data* quiver = make(kQuiverVnum);
    equip_char(&m_player, quiver, WEAR_BACK);
    obj_data* arrow = make(kBreadVnum);
    int worn = GET_WORN_WEIGHT(&m_player);
    int encumb = GET_ENCUMB_WEIGHT(&m_player);
    obj_to_obj(arrow, quiver);
    EXPECT_EQ(GET_WORN_WEIGHT(&m_player), worn);
    EXPECT_EQ(GET_ENCUMB_WEIGHT(&m_player), encumb);
    obj_from_obj(arrow);
    obj_to_char(arrow, &m_player);
    EXPECT_EQ(GET_WORN_WEIGHT(&m_player), worn);
    EXPECT_EQ(GET_ENCUMB_WEIGHT(&m_player), encumb);
}

/* ===== Long words and the item limit ===== */

TEST_F(KeyringTest, GetLongWordFromBag)
{
    obj_to_char(make(kBagVnum), &m_player);
    std::string word(240, 'a');
    run_cmd(do_get, &m_player, (word + " bag").c_str());
    EXPECT_NE(output().find("There doesn't seem to be an " + word + " in a leather bag."),
        std::string::npos);
}

TEST_F(KeyringTest, GetLongWordFromBeltKeyring)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    equip_char(&m_player, make(kRingVnum), WEAR_BELT_1);
    std::string word(240, 'a');
    run_cmd(do_get, &m_player, (word + " keyring").c_str());
    EXPECT_NE(output().find("There doesn't seem to be an " + word + " in an iron keyring."),
        std::string::npos);
}

TEST_F(KeyringTest, ExamineLongWordNotFound)
{
    run_cmd(do_examine, &m_player, "bbb");
    std::string normal = output();
    ASSERT_FALSE(normal.empty());
    clear_output();
    run_cmd(do_examine, &m_player, std::string(240, 'b').c_str());
    EXPECT_EQ(output(), normal);
}

TEST_F(KeyringTest, GetAllFromBagStopsAtItemLimit)
{
    obj_data* bag = make(kBagVnum);
    obj_to_char(bag, &m_player);
    obj_to_obj(make(kBreadVnum), bag);
    obj_to_obj(make(kBreadVnum), bag);
    while (IS_CARRYING_N(&m_player) < CAN_CARRY_N(&m_player) - 1)
        obj_to_char(make(kKeyVnum), &m_player);
    run_cmd(do_get, &m_player, "all bag");
    EXPECT_NE(output().find("a loaf of bread: You can't hold any more items.\n\r"),
        std::string::npos);
    ASSERT_NE(bag->contains, nullptr);
    EXPECT_EQ(bag->contains->next_content, nullptr);
    EXPECT_EQ(IS_CARRYING_N(&m_player), CAN_CARRY_N(&m_player));
}

TEST_F(KeyringTest, GetAllFromBeltKeyringStopsAtItemLimit)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(make(kKeyVnum), ring);
    obj_to_obj(make(kKeyVnum), ring);
    while (IS_CARRYING_N(&m_player) < CAN_CARRY_N(&m_player) - 1)
        obj_to_char(make(kBreadVnum), &m_player);
    run_cmd(do_get, &m_player, "all keyring");
    const size_t full = output().find("a brass key: You can't hold any more items.\n\r");
    EXPECT_NE(full, std::string::npos);
    EXPECT_EQ(output().find("You can't hold", full + std::strlen("a brass key: You can't hold")),
        std::string::npos);
    ASSERT_NE(ring->contains, nullptr);
    EXPECT_EQ(ring->contains->next_content, nullptr);
    EXPECT_EQ(IS_CARRYING_N(&m_player), CAN_CARRY_N(&m_player));
}

/* Saving a follower must not shrink a container it holds: the save takes
 * contents off the weight while writing, then must put it back. */
TEST_F(KeyringTest, FollowerSaveKeepsHeldKeyringWeight)
{
    char_data mob {};
    clear_char(&mob, 0);
    ClearedCharacterRelease mob_release(&mob);
    char mob_name[] = "orc";
    mob.player.name = mob_name;
    SET_BIT(mob.specials2.act, MOB_ISNPC);
    mob.nr = 0;
    mob.in_room = 0;
    FILE* fp = tmpfile(); /* before any shared state is changed */
    ASSERT_NE(fp, nullptr);
    index_data fake_mob_index[1] {};
    fake_mob_index[0].virt = 9999;
    follow_type fol {};
    fol.follower = &mob;
    obj_data* ring = make(kRingVnum);

    /* Puts the shared state back however the test ends. */
    struct Restore {
        index_data* saved_mob_index = mob_index;
        char_data& player;
        char_data& mob;
        obj_data* ring;
        ~Restore()
        {
            if (mob.equipment[HOLD])
                unequip_char(&mob, HOLD);
            extract_obj(ring);
            player.followers = nullptr;
            mob_index = saved_mob_index;
        }
    } restore { mob_index, m_player, mob, ring };
    mob_index = fake_mob_index;
    m_player.followers = &fol;

    equip_char(&mob, ring, HOLD);
    for (int i = 0; i < 4; ++i)
        obj_to_obj(make(kKeyVnum), ring);
    const int start = GET_OBJ_WEIGHT(ring);
    for (int i = 0; i < 5; ++i)
        Crash_follower_save(&m_player, fp);
    fclose(fp);
    EXPECT_EQ(GET_OBJ_WEIGHT(ring), start);
}

/* ===== One put/get path for bags, quivers and keyrings ===== */

TEST_F(KeyringTest, PutKeyOnKeyringFindsKeyNotKeyring)
{
    /* The key is picked up first, so the keyring is first in inventory and
     * "key" (a prefix of "keyring") would name the keyring. */
    obj_data* key = make(kKeyVnum);
    obj_data* ring = make(kRingVnum);
    obj_to_char(key, &m_player);
    obj_to_char(ring, &m_player);
    ASSERT_EQ(m_player.carrying, ring);
    run_put(&m_player, "key keyring");
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(output().find("Only keys"), std::string::npos);
}

TEST_F(KeyringTest, PutNumberedKeyCountsOnlyKeys)
{
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kKeyVnum);
    obj_data* ring = make(kRingVnum);
    obj_to_char(key1, &m_player);
    obj_to_char(key2, &m_player);
    obj_to_char(ring, &m_player);
    run_put(&m_player, "2.key keyring");
    EXPECT_EQ(key1->in_obj, ring);
    EXPECT_EQ(key2->carried_by, &m_player);
}

TEST_F(KeyringTest, PutAllDotPrefixOnKeyring)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kBreakKeyVnum);
    obj_to_char(ring, &m_player);
    obj_to_char(key1, &m_player);
    obj_to_char(key2, &m_player);
    run_put(&m_player, "all.bras keyring");
    EXPECT_EQ(key1->in_obj, ring);
    EXPECT_EQ(key2->carried_by, &m_player);
}

TEST_F(KeyringTest, PutNonArrowInQuiverStillRefused)
{
    obj_data* quiver = make(kQuiverVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(quiver, &m_player);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "bread quiver");
    EXPECT_EQ(bread->carried_by, &m_player);
    EXPECT_EQ(output(), "Only arrows can go into a quiver.\n\r");
}

TEST_F(KeyringTest, PutArrowInQuiverTakesNewestMatchAsBefore)
{
    /* Quivers keep the old lookup: the newest match is refused. */
    obj_data* quiver = make(kQuiverVnum);
    obj_data* arrow = make(kKeyVnum);
    arrow->obj_flags.type_flag = ITEM_MISSILE;
    obj_data* key = make(kKeyVnum);
    obj_to_char(quiver, &m_player);
    obj_to_char(arrow, &m_player);
    obj_to_char(key, &m_player);
    run_put(&m_player, "brass quiver");
    EXPECT_EQ(arrow->carried_by, &m_player);
    EXPECT_EQ(key->carried_by, &m_player);
    EXPECT_EQ(output(), "Only arrows can go into a quiver.\n\r");
}

TEST_F(KeyringTest, PutAllInQuiverStopsAtFirstNonArrow)
{
    obj_data* quiver = make(kQuiverVnum);
    obj_data* arrow = make(kKeyVnum);
    arrow->obj_flags.type_flag = ITEM_MISSILE;
    obj_data* bread = make(kBreadVnum);
    obj_to_char(quiver, &m_player);
    obj_to_char(bread, &m_player);
    obj_to_char(arrow, &m_player); /* carrying: arrow, bread, quiver */
    run_put(&m_player, "all quiver");
    EXPECT_EQ(arrow->in_obj, quiver); /* the arrow before the bread goes in */
    EXPECT_EQ(bread->carried_by, &m_player);
    const std::string out = output();
    const size_t refusal = out.find("Only arrows can go into a quiver.\n\r");
    ASSERT_NE(refusal, std::string::npos);
    EXPECT_EQ(out.find("Only arrows", refusal + 1), std::string::npos);
}

TEST_F(KeyringTest, PutArrowInQuiver)
{
    obj_data* quiver = make(kQuiverVnum);
    obj_data* arrow = make(kKeyVnum);
    arrow->obj_flags.type_flag = ITEM_MISSILE;
    obj_to_char(quiver, &m_player);
    obj_to_char(arrow, &m_player);
    run_put(&m_player, "brass quiver");
    EXPECT_EQ(arrow->in_obj, quiver);
}

TEST_F(KeyringTest, PutAllDotArrowsInQuiver)
{
    obj_data* quiver = make(kQuiverVnum);
    obj_data* arrow1 = make(kKeyVnum);
    obj_data* arrow2 = make(kKeyVnum);
    arrow1->obj_flags.type_flag = ITEM_MISSILE;
    arrow2->obj_flags.type_flag = ITEM_MISSILE;
    obj_to_char(quiver, &m_player);
    obj_to_char(arrow1, &m_player);
    obj_to_char(arrow2, &m_player);
    run_put(&m_player, "all.brass quiver");
    EXPECT_EQ(arrow1->in_obj, quiver);
    EXPECT_EQ(arrow2->in_obj, quiver);
}

TEST_F(KeyringTest, GetKeyFromAllDotKeyringReachesBelt)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    int weight = IS_CARRYING_W(&m_player);
    run_get(&m_player, "key all.keyring");
    EXPECT_EQ(key->carried_by, &m_player);
    EXPECT_EQ(IS_CARRYING_W(&m_player), weight);
    EXPECT_EQ(output().find("can't seem to find"), std::string::npos);
}

/* Plain "all" leaves worn things alone, as before keyrings. */
TEST_F(KeyringTest, GetKeyFromAllLeavesBelt)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    run_get(&m_player, "key all");
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(output(), "You can't seem to find any containers.\n\r");
}

/* Looting with "get all all" takes from the bag and leaves the belt keyring
 * (full or empty) quiet. */
TEST_F(KeyringTest, GetAllAllLeavesBeltKeyring)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* empty_ring = make(kRingVnum);
    equip_char(&m_player, empty_ring, WEAR_BELT_2);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    obj_data* bag = make(kBagVnum);
    obj_to_char(bag, &m_player);
    obj_data* bread = make(kBreadVnum);
    obj_to_obj(bread, bag);
    run_get(&m_player, "all all");
    EXPECT_EQ(bread->carried_by, &m_player);
    EXPECT_EQ(key->in_obj, ring);
    EXPECT_EQ(output().find("keyring"), std::string::npos);
}

/* A keyword shared by a carried bag and a belt keyring finds the bag, for
 * put, get, look in and examine. */
TEST_F(KeyringTest, SharedWordFindsCarriedBagBeforeBeltKeyring)
{
    m_player.specials.position = POSITION_STANDING;
    static char bag_name[] = "bag iron"; /* outlives the test for TearDown */
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum); /* "keyring ring iron" */
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    obj_data* bag = make(kBagVnum);
    bag->name = bag_name;
    obj_to_char(bag, &m_player);

    obj_data* bread = make(kBreadVnum);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "bread iron");
    EXPECT_EQ(bread->in_obj, bag);
    EXPECT_EQ(output().find("Only keys"), std::string::npos);

    clear_output();
    run_get(&m_player, "bread iron");
    EXPECT_EQ(bread->carried_by, &m_player);

    obj_from_char(bread);
    obj_to_obj(bread, bag);
    clear_output();
    run_cmd(do_look, &m_player, "in iron", 15);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);

    clear_output();
    run_cmd(do_examine, &m_player, "iron");
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

/* Hands fill up partway: one "can't hold" line, not one per item left. */
TEST_F(KeyringTest, GetAllFromBagWithFullHandsSaysItOnce)
{
    obj_data* bag = make(kBagVnum);
    obj_to_char(bag, &m_player);
    for (int i = 0; i < 3; ++i)
        obj_to_obj(make(kBreadVnum), bag);
    while (IS_CARRYING_N(&m_player) < CAN_CARRY_N(&m_player) - 1)
        obj_to_char(make(kKeyVnum), &m_player); /* room for one loaf */
    clear_output();
    run_cmd(do_get, &m_player, "all bag");
    const std::string out = output();
    size_t first = out.find("You can't hold any more items.");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(out.find("You can't hold any more items.", first + 1), std::string::npos);
}

/* The item-type setting on the standard search counts only that type. */
TEST_F(KeyringTest, ObjInListVisCountsOnlyTheType)
{
    static char amulet_name[] = "key amulet"; /* outlives the test for TearDown */
    obj_data* amulet = make(kBreadVnum);
    amulet->name = amulet_name;
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kKeyVnum);
    obj_to_char(key2, &m_player);
    obj_to_char(key1, &m_player);
    obj_to_char(amulet, &m_player); /* list: amulet, key1, key2 */
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "key", m_player.carrying, 9999), amulet);
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "key", m_player.carrying, 9999, ITEM_KEY), key1);
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "2.key", m_player.carrying, 9999, ITEM_KEY), key2);
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "3.key", m_player.carrying, 9999, ITEM_KEY), nullptr);
}

TEST_F(KeyringTest, GetFromAllSkipsHeldBag)
{
    obj_data* bag = make(kBagVnum);
    equip_char(&m_player, bag, HOLD);
    obj_data* bread = make(kBreadVnum);
    obj_to_obj(bread, bag);
    run_get(&m_player, "bread all");
    EXPECT_EQ(bread->in_obj, bag);
    EXPECT_EQ(output(), "You can't seem to find any containers.\n\r");
}

TEST_F(KeyringTest, HasKeySkipsKeyWithNoPrototype)
{
    obj_data* key = make(kKeyVnum);
    key->item_number = -1;
    obj_to_char(key, &m_player);
    EXPECT_EQ(has_key(&m_player, kKeyVnum), nullptr);
    key->item_number = 0;
}

/* ===== A belt keyring answers to short starts for put and get only ===== */

TEST_F(KeyringTest, PutKeyOnBeltKeyringByShortStart)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key = make(kKeyVnum);
    obj_to_char(key, &m_player);
    run_put(&m_player, "key keyr");
    EXPECT_EQ(key->in_obj, ring);
}

TEST_F(KeyringTest, GetKeyFromBeltKeyringByShortStart)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    run_get(&m_player, "key keyr");
    ASSERT_EQ(key->carried_by, &m_player);
    obj_from_char(key);
    obj_to_obj(key, ring);
    run_get(&m_player, "key all.keyr");
    EXPECT_EQ(key->carried_by, &m_player);
}

TEST_F(KeyringTest, ExamineAndLookInBeltKeyringByShortStart)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    run_cmd(do_examine, &m_player, "keyr");
    EXPECT_NE(output().find("When you look inside"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
    clear_output();
    run_cmd(do_look, &m_player, "in keyr", 15);
    EXPECT_NE(output().find("keyring (worn) : \n\r"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, BeltKeyringFoundWhileBlindLikeOtherWornGear)
{
    obj_data* belt = make(kBeltVnum);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, belt, WEAR_WAISTE);
    equip_char(&m_player, ring, WEAR_BELT_1);
    m_player.specials.affected_by |= AFF_BLIND;
    char_data* tar_ch = nullptr;
    obj_data* found = nullptr;
    EXPECT_EQ(find_container(&m_player, "keyring", FIND_OBJ_INV | FIND_OBJ_ROOM, &tar_ch, &found),
        FIND_OBJ_BELT_KEYRING);
    EXPECT_EQ(found, ring);
    m_player.specials.affected_by &= ~AFF_BLIND;
}

/* ---- 10-21 adversarial review: pins for the decided search order ---- */

/* The room comes before the belt: a bag on the ground that shares a word
 * with the belt keyring wins, for put, get, look in and examine. */
TEST_F(KeyringTest, SharedWordFindsRoomBagBeforeBeltKeyring)
{
    m_player.specials.position = POSITION_STANDING;
    static char bag_name[] = "bag iron"; /* outlives the test for TearDown */
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum); /* "keyring ring iron" */
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    obj_data* bag = make(kBagVnum);
    bag->name = bag_name;
    obj_to_room(bag, 0);

    obj_data* bread = make(kBreadVnum);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "bread iron");
    EXPECT_EQ(bread->in_obj, bag);
    EXPECT_EQ(output().find("Only keys"), std::string::npos);

    clear_output();
    run_get(&m_player, "bread iron");
    EXPECT_EQ(bread->carried_by, &m_player);
    EXPECT_EQ(key->in_obj, ring);

    obj_from_char(bread);
    obj_to_obj(bread, bag);
    clear_output();
    run_cmd(do_look, &m_player, "in iron", 15);
    EXPECT_NE(output().find("(here)"), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);

    clear_output();
    run_cmd(do_examine, &m_player, "iron");
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

/* Numbering: the Nth in the inventory, else the Nth in the room (the old
 * search), then belt keyrings numbered on after the longer of the two. */
class KeyringNumberTest : public KeyringTest {
protected:
    obj_data* find(const char* arg)
    {
        char_data* tar_ch = nullptr;
        obj_data* found = nullptr;
        find_container(&m_player, arg, FIND_OBJ_INV | FIND_OBJ_ROOM, &tar_ch, &found);
        return found;
    }
    obj_data* carried(int n)
    {
        obj_data* last = nullptr;
        for (int i = 0; i < n; ++i)
            obj_to_char(last = make(kRingVnum), &m_player);
        return last;
    }
    obj_data* in_room()
    {
        obj_data* ring = make(kRingVnum);
        obj_to_room(ring, 0);
        return ring;
    }
    obj_data* on_belt()
    {
        equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
        obj_data* ring = make(kRingVnum);
        equip_char(&m_player, ring, WEAR_BELT_1);
        return ring;
    }
};

TEST_F(KeyringNumberTest, OneCarriedOneRoomOneBelt)
{
    obj_data* belt = on_belt();
    in_room();
    obj_data* pack = carried(1);
    EXPECT_EQ(find("keyring"), pack);
    EXPECT_EQ(find("1.keyring"), pack);
    EXPECT_EQ(find("2.keyring"), belt);
    EXPECT_EQ(find("3.keyring"), nullptr);
}

TEST_F(KeyringNumberTest, NoneCarriedOneRoomOneBelt)
{
    obj_data* belt = on_belt();
    obj_data* floor = in_room();
    EXPECT_EQ(find("keyring"), floor);
    EXPECT_EQ(find("1.keyring"), floor);
    EXPECT_EQ(find("2.keyring"), belt);
    EXPECT_EQ(find("3.keyring"), nullptr);
}

TEST_F(KeyringNumberTest, OneCarriedTwoRoomOneBelt)
{
    obj_data* belt = on_belt();
    obj_data* second_floor = in_room();
    in_room(); /* obj_to_room adds at the head: this one is the room's first */
    obj_data* pack = carried(1);
    EXPECT_EQ(find("1.keyring"), pack);
    EXPECT_EQ(find("2.keyring"), second_floor);
    EXPECT_EQ(find("3.keyring"), belt);
    EXPECT_EQ(find("4.keyring"), nullptr);
}

TEST_F(KeyringNumberTest, LookInSecondReachesBelt)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* belt = on_belt();
    obj_to_obj(make(kKeyVnum), belt); /* brass on the belt */
    obj_data* floor = in_room();
    obj_to_obj(make(kBreakKeyVnum), floor); /* tin in the room */
    run_cmd(do_look, &m_player, "in 2.keyring", 15);
    EXPECT_NE(output().find("(worn)"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
    EXPECT_EQ(output().find("a tin key"), std::string::npos);
}

/* A worn keyring does not hide other worn gear whose name starts the same:
 * the keyring stays last. */
TEST_F(KeyringTest, WornPouchBeforeBeltKeyringWithSameStart)
{
    m_player.specials.position = POSITION_STANDING;
    static char ring_name[] = "leather keyring";
    static char pouch_name[] = "leather pouch";
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    ring->name = ring_name;
    obj_data* pouch = make(kBagVnum);
    pouch->name = pouch_name;
    equip_char(&m_player, ring, WEAR_BELT_1);
    equip_char(&m_player, pouch, WEAR_BELT_2);
    obj_to_obj(make(kKeyVnum), ring);
    obj_to_obj(make(kBreadVnum), pouch);
    run_cmd(do_look, &m_player, "in leather", 15);
    EXPECT_NE(output().find("(used)"), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
    clear_output();
    run_cmd(do_examine, &m_player, "leather");
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

/* ---- Every player: look in / examine / get / put unchanged ---- */

TEST_F(KeyringTest, ExamineCorpseOnGroundShowsInside)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* corpse = make(kBagVnum);
    corpse->obj_flags.value[3] = 1;
    obj_to_room(corpse, 0);
    obj_to_obj(make(kBreadVnum), corpse);
    run_cmd(do_examine, &m_player, "bag");
    EXPECT_NE(output().find("When you look inside"), std::string::npos);
    EXPECT_NE(output().find("(here)"), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
}

TEST_F(KeyringTest, LookInAndExamineDrinkContainer)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* flask = make(kBagVnum);
    flask->obj_flags.type_flag = ITEM_DRINKCON;
    flask->obj_flags.value[0] = 10;
    flask->obj_flags.value[1] = 5;
    flask->obj_flags.value[2] = 0;
    obj_to_char(flask, &m_player);
    run_cmd(do_look, &m_player, "in bag", 15);
    EXPECT_NE(output().find("full of a"), std::string::npos);
    clear_output();
    run_cmd(do_examine, &m_player, "bag");
    EXPECT_NE(output().find("When you look inside"), std::string::npos);
    EXPECT_NE(output().find("full of a"), std::string::npos);
    flask->obj_flags.value[1] = 0;
    clear_output();
    run_cmd(do_look, &m_player, "in bag", 15);
    EXPECT_NE(output().find("It is empty."), std::string::npos);
    clear_output();
    run_cmd(do_examine, &m_player, "bag");
    EXPECT_NE(output().find("It is empty."), std::string::npos);
}

TEST_F(KeyringTest, LookInAndExamineClosedBag)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* bag = make(kBagVnum);
    obj_to_char(bag, &m_player);
    obj_to_obj(make(kBreadVnum), bag);
    SET_BIT(bag->obj_flags.value[1], CONT_CLOSED);
    run_cmd(do_look, &m_player, "in bag", 15);
    EXPECT_NE(output().find("It is closed."), std::string::npos);
    EXPECT_EQ(output().find("a loaf of bread"), std::string::npos);
    clear_output();
    run_cmd(do_examine, &m_player, "bag");
    EXPECT_NE(output().find("It is closed."), std::string::npos);
    EXPECT_EQ(output().find("a loaf of bread"), std::string::npos);
}

TEST_F(KeyringTest, LookInNonContainer)
{
    m_player.specials.position = POSITION_STANDING;
    obj_to_char(make(kBreadVnum), &m_player);
    run_cmd(do_look, &m_player, "in bread", 15);
    EXPECT_EQ(output(), "That is not a container.\n\r");
}

TEST_F(KeyringTest, LookInHeldBagSaysUsed)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* bag = make(kBagVnum);
    bag->obj_flags.wear_flags |= ITEM_HOLD;
    equip_char(&m_player, bag, HOLD);
    obj_to_obj(make(kBreadVnum), bag);
    run_cmd(do_look, &m_player, "in bag", 15);
    EXPECT_NE(output().find("bag (used) : "), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
}

TEST_F(KeyringTest, GetFromAllDotLeavesHeldBag)
{
    obj_data* bag = make(kBagVnum);
    bag->obj_flags.wear_flags |= ITEM_HOLD;
    equip_char(&m_player, bag, HOLD);
    obj_data* bread = make(kBreadVnum);
    obj_to_obj(bread, bag);
    run_get(&m_player, "bread all.bag");
    EXPECT_EQ(bread->in_obj, bag);
    EXPECT_EQ(output(), "You can't seem to find any bags here.\n\r");
}

TEST_F(KeyringTest, PutAllInBagMovesAllButTheBag)
{
    obj_data* bag = make(kBagVnum);
    obj_data* bread = make(kBreadVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_char(bag, &m_player);
    obj_to_char(bread, &m_player);
    obj_to_char(key, &m_player);
    run_put(&m_player, "all bag");
    EXPECT_EQ(bread->in_obj, bag);
    EXPECT_EQ(key->in_obj, bag);
    EXPECT_EQ(bag->carried_by, &m_player);
    EXPECT_EQ(bag->in_obj, nullptr);
}

TEST_F(KeyringTest, PutAllDotMissingInBag)
{
    obj_to_char(make(kBagVnum), &m_player);
    run_put(&m_player, "all.zzz bag");
    EXPECT_EQ(output(), "You don't seem to have any zzzs.\n\r");
}

/* A corpse on the ground keeps one line per item it could not give (the
 * player looting their own corpse sees what is still in it). */
TEST_F(KeyringTest, FloorCorpseFullHandsListsEveryItemLeft)
{
    game_rules::big_brother::create(weather_info, &world); /* made at boot; looting a corpse asks it */
    obj_data* corpse = make(kBagVnum);
    corpse->obj_flags.value[3] = 1;
    obj_to_room(corpse, 0);
    for (int i = 0; i < 3; ++i)
        obj_to_obj(make(kBreadVnum), corpse);
    while (IS_CARRYING_N(&m_player) < CAN_CARRY_N(&m_player) - 1)
        obj_to_char(make(kKeyVnum), &m_player); /* room for one loaf */
    clear_output();
    run_cmd(do_get, &m_player, "all bag");
    const std::string out = output();
    int lines = 0;
    for (size_t at = out.find("You can't carry that many items."); at != std::string::npos;
         at = out.find("You can't carry that many items.", at + 1))
        ++lines;
    EXPECT_EQ(lines, 2);
}

/* ---- Held keyring: the old worn-gear search, as for a held bag ---- */

TEST_F(KeyringTest, LookInHeldKeyringSaysUsed)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, HOLD);
    obj_to_obj(make(kKeyVnum), ring);
    run_cmd(do_look, &m_player, "in keyring", 15);
    EXPECT_NE(output().find("(used)"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, HeldKeyringPlainNameBeltKeyringByNumber)
{
    m_player.specials.position = POSITION_STANDING;
    obj_data* held = make(kRingVnum);
    equip_char(&m_player, held, HOLD);
    obj_to_obj(make(kKeyVnum), held);
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* belt = make(kRingVnum);
    equip_char(&m_player, belt, WEAR_BELT_1);
    obj_to_obj(make(kBreakKeyVnum), belt);
    run_cmd(do_look, &m_player, "in keyring", 15);
    EXPECT_NE(output().find("(used)"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
    clear_output();
    run_cmd(do_look, &m_player, "in 1.keyring", 15);
    EXPECT_NE(output().find("(worn)"), std::string::npos);
    EXPECT_NE(output().find("a tin key"), std::string::npos);
}

/* ---- Death and decay ---- */

TEST_F(KeyringTest, DeathRandomRemovalNeverTakesKeyrings)
{
    obj_data* corpse = make(kBagVnum);
    corpse->obj_flags.value[3] = 1;
    obj_to_room(corpse, 0);
    obj_data* rings[25];
    for (auto& ring : rings) {
        ring = make(kRingVnum);
        obj_to_obj(make(kKeyVnum), ring);
        obj_to_obj(ring, corpse);
    }
    for (int i = 0; i < 100; ++i)
        remove_random_item(&m_player, corpse);
    int left = 0;
    for (obj_data* o = corpse->contains; o; o = o->next_content) {
        ++left;
        EXPECT_NE(o->contains, nullptr); /* its key is still on it */
    }
    EXPECT_EQ(left, 25);
}

TEST_F(KeyringTest, DecayingKeyringSpillsItsKeys)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    obj_to_room(ring, 0);
    spill_decayed_contents(ring);
    EXPECT_EQ(ring->contains, nullptr);
    EXPECT_EQ(key->in_obj, nullptr);
    EXPECT_EQ(key->in_room, 0);
}

TEST_F(KeyringTest, DecayingCarriedKeyringSpillsKeysToTheRoom)
{
    obj_data* ring = make(kRingVnum);
    obj_data* key = make(kKeyVnum);
    obj_to_obj(key, ring);
    obj_to_char(ring, &m_player);
    spill_decayed_contents(ring);
    EXPECT_EQ(key->in_room, 0);
    EXPECT_EQ(key->carried_by, nullptr);
}

/* ---- 10-21 second adversarial review: more pins ---- */

/* A belt keyring whose name starts with the word is found by the worn-gear
 * step; it still waits behind a bag on the floor. */
TEST_F(KeyringTest, BeltKeyringByFirstWordWaitsBehindFloorBag)
{
    m_player.specials.position = POSITION_STANDING;
    static char ring_name[] = "leather keyring";
    static char bag_name[] = "leather bag";
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    ring->name = ring_name;
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(make(kKeyVnum), ring);
    obj_data* bag = make(kBagVnum);
    bag->name = bag_name;
    obj_to_room(bag, 0);
    obj_to_obj(make(kBreadVnum), bag);
    run_cmd(do_look, &m_player, "in leather", 15);
    EXPECT_NE(output().find("(here)"), std::string::npos);
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
    clear_output();
    run_cmd(do_examine, &m_player, "leather");
    EXPECT_NE(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

TEST_F(KeyringTest, GetAllDotFromBagWithFullHandsSaysItOnce)
{
    obj_data* bag = make(kBagVnum);
    obj_to_char(bag, &m_player);
    for (int i = 0; i < 3; ++i)
        obj_to_obj(make(kKeyVnum), bag);
    while (IS_CARRYING_N(&m_player) < CAN_CARRY_N(&m_player) - 1)
        obj_to_char(make(kBreadVnum), &m_player); /* room for one key */
    clear_output();
    run_cmd(do_get, &m_player, "all.key bag");
    const std::string out = output();
    const size_t first = out.find("You can't hold any more items.");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(out.find("You can't hold any more items.", first + 1), std::string::npos);
    int left = 0;
    for (obj_data* o = bag->contains; o; o = o->next_content)
        ++left;
    EXPECT_EQ(left, 2);
}

/* A belt keyring's keys are already carried: at the weight limit a key
 * still comes off, by name and through all.keyring. */
TEST_F(KeyringTest, BeltKeyringKeyComesOffAtTheWeightLimit)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kKeyVnum);
    obj_to_obj(key1, ring);
    obj_to_obj(key2, ring);
    IS_CARRYING_W(&m_player) = CAN_CARRY_W(&m_player);
    run_get(&m_player, "key keyring");
    EXPECT_EQ(key2->carried_by, &m_player);
    run_get(&m_player, "key all.keyring");
    EXPECT_EQ(key1->carried_by, &m_player);
    EXPECT_EQ(output().find("can't carry that much weight"), std::string::npos);
}

class KeyringTwoBeltsTest : public KeyringTest {
protected:
    void SetUp() override
    {
        KeyringTest::SetUp();
        m_player.specials.position = POSITION_STANDING;
        equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
        ring1 = make(kRingVnum);
        ring3 = make(kRingVnum);
        equip_char(&m_player, ring1, WEAR_BELT_1);
        equip_char(&m_player, ring3, WEAR_BELT_3);
        brass = make(kKeyVnum);
        tin = make(kBreakKeyVnum);
        obj_to_obj(brass, ring1);
        obj_to_obj(tin, ring3);
    }
    obj_data* ring1 = nullptr;
    obj_data* ring3 = nullptr;
    obj_data* brass = nullptr;
    obj_data* tin = nullptr;
};

TEST_F(KeyringTwoBeltsTest, GetAllFromAllDotKeyringEmptiesBoth)
{
    run_get(&m_player, "all all.keyring");
    EXPECT_EQ(brass->carried_by, &m_player);
    EXPECT_EQ(tin->carried_by, &m_player);
}

TEST_F(KeyringTwoBeltsTest, SecondKeyringIsOnTheThirdBeltSlot)
{
    run_cmd(do_look, &m_player, "in 2.keyring", 15);
    EXPECT_NE(output().find("(worn)"), std::string::npos);
    EXPECT_NE(output().find("a tin key"), std::string::npos);
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
}

/* One "can't hold" line per keyring, as get all all gives one per container. */
TEST_F(KeyringTwoBeltsTest, FullHandsSayItOncePerKeyring)
{
    obj_to_obj(make(kKeyVnum), ring1);
    obj_to_obj(make(kKeyVnum), ring3);
    while (IS_CARRYING_N(&m_player) < CAN_CARRY_N(&m_player) - 1)
        obj_to_char(make(kBreadVnum), &m_player); /* room for one key */
    clear_output();
    run_get(&m_player, "all all.keyring");
    const std::string out = output();
    int lines = 0;
    for (size_t at = out.find("You can't hold any more items."); at != std::string::npos;
         at = out.find("You can't hold any more items.", at + 1))
        ++lines;
    EXPECT_EQ(lines, 2);
}

/* get <x> all.keyring: a keyring on the floor before one on the belt. */
TEST_F(KeyringTest, GetAllDotKeyringFloorBeforeBelt)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* belt_ring = make(kRingVnum);
    equip_char(&m_player, belt_ring, WEAR_BELT_1);
    obj_to_obj(make(kKeyVnum), belt_ring); /* brass on the belt */
    obj_data* floor_ring = make(kRingVnum);
    obj_to_room(floor_ring, 0);
    obj_to_obj(make(kBreakKeyVnum), floor_ring); /* tin on the floor */
    run_get(&m_player, "all all.keyring");
    const size_t tin = output().find("a tin key");
    const size_t brass = output().find("a brass key");
    ASSERT_NE(tin, std::string::npos);
    ASSERT_NE(brass, std::string::npos);
    EXPECT_LT(tin, brass);
}

/* Blind: examine shows no keys, a key you cannot see is not put on, and a
 * key comes off a belt keyring as from a carried bag. */
TEST_F(KeyringTest, BlindPlayerAndBeltKeyring)
{
    m_player.specials.position = POSITION_STANDING;
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* brass = make(kKeyVnum);
    obj_to_obj(brass, ring);
    obj_data* tin = make(kBreakKeyVnum);
    obj_to_char(tin, &m_player);
    m_player.specials.affected_by |= AFF_BLIND;

    run_cmd(do_examine, &m_player, "keyring");
    EXPECT_EQ(output().find("a brass key"), std::string::npos);
    clear_output();
    run_put(&m_player, "key keyring");
    EXPECT_EQ(tin->carried_by, &m_player);
    clear_output();
    run_get(&m_player, "key keyring");
    EXPECT_EQ(brass->carried_by, &m_player);
    m_player.specials.affected_by &= ~AFF_BLIND;
}

/* Keys go on a keyring; everything else in. */
TEST_F(KeyringTest, PutSaysOnForAKeyringInForABag)
{
    obj_data* ring = make(kRingVnum);
    obj_data* bag = make(kBagVnum);
    obj_to_char(ring, &m_player);
    obj_to_char(bag, &m_player);
    obj_to_char(make(kKeyVnum), &m_player);
    run_put(&m_player, "key keyring");
    EXPECT_NE(output().find("You put a brass key on an iron keyring."), std::string::npos);
    obj_to_char(make(kBreadVnum), &m_player);
    clear_output();
    run_put(&m_player, "bread bag");
    EXPECT_NE(output().find("You put a loaf of bread in a leather bag."), std::string::npos);
}

TEST_F(KeyringTest, KeyWontFitOnAFullKeyringSaysOn)
{
    obj_data* ring = make(kRingVnum);
    ring->obj_flags.value[0] = GET_OBJ_WEIGHT(ring); /* no room for anything */
    obj_to_char(ring, &m_player);
    obj_to_char(make(kKeyVnum), &m_player);
    run_put(&m_player, "key keyring");
    EXPECT_NE(output().find("A brass key won't fit on an iron keyring."), std::string::npos);
}

/* An examine script that destroys the object: the contents step searches
 * again and finds nothing, as before (it must not use the destroyed object). */
TEST_F(KeyringTest, ExamineScriptThatDestroysTheObject)
{
    constexpr int kScript = 9901;
    /* As script 2299 (the strange branch): destroy the object, then return
     * false so the description step stops (issue #469 is the case without). */
    std::vector<script_data> nodes(5);
    nodes[0].command_type = ON_EXAMINE_OBJECT;
    nodes[1].command_type = SCRIPT_BEGIN;
    nodes[2].command_type = SCRIPT_EXTRACT_OBJ;
    nodes[2].param[0] = SCRIPT_PARAM_OB1;
    nodes[3].command_type = SCRIPT_RETURN_FALSE;
    nodes[4].command_type = SCRIPT_END;
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].number = static_cast<int>(i) + 1;
        nodes[i].next = (i + 1 < nodes.size()) ? &nodes[i + 1] : nullptr;
        nodes[i].prev = (i > 0) ? &nodes[i - 1] : nullptr;
    }
    script_head head {};
    head.number = kScript;
    head.script = &nodes[0];
    script_head* saved_table = script_table;
    const int saved_top = top_of_script_table;
    script_table = &head;
    top_of_script_table = 0;

    m_player.specials.position = POSITION_STANDING;
    obj_data* bag = make(kBagVnum);
    bag->obj_flags.script_number = kScript;
    obj_to_char(bag, &m_player);
    obj_to_obj(make(kBreadVnum), bag);
    run_cmd(do_examine, &m_player, "bag");

    script_table = saved_table;
    top_of_script_table = saved_top;
    EXPECT_EQ(m_player.carrying, nullptr); /* the script destroyed the bag */
    EXPECT_EQ(output().find("a loaf of bread"), std::string::npos);
    EXPECT_EQ(output().find("(carried)"), std::string::npos);
}

/* ---- 10-21 third adversarial review: last coverage gaps ---- */

/* "get <x> all": a carried keyring and one on the floor count like bags. */
TEST_F(KeyringTest, GetFromAllReachesCarriedAndFloorKeyrings)
{
    obj_data* carried = make(kRingVnum);
    obj_data* brass = make(kKeyVnum);
    obj_to_obj(brass, carried);
    obj_to_char(carried, &m_player);
    run_get(&m_player, "key all");
    EXPECT_EQ(brass->carried_by, &m_player);

    obj_data* floor = make(kRingVnum);
    obj_data* tin = make(kBreakKeyVnum);
    obj_to_obj(tin, floor);
    obj_to_room(floor, 0);
    clear_output();
    run_get(&m_player, "tin all");
    EXPECT_EQ(tin->carried_by, &m_player);
}

/* "get <x> all.bag" searches belt keyrings only when they answer to "bag". */
TEST_F(KeyringTest, GetFromAllDotBagLeavesBeltKeyring)
{
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* brass = make(kKeyVnum);
    obj_to_obj(brass, ring);
    obj_data* bag = make(kBagVnum);
    obj_to_char(bag, &m_player);
    obj_to_obj(make(kBreadVnum), bag);
    run_get(&m_player, "key all.bag");
    EXPECT_EQ(brass->in_obj, ring);
    EXPECT_EQ(output().find("keyring"), std::string::npos);
}

/* A floor keyring the player cannot see takes no number. */
TEST_F(KeyringTest, UnseenFloorKeyringTakesNoNumber)
{
    obj_data* floor = make(kRingVnum);
    SET_BIT(floor->obj_flags.extra_flags, ITEM_INVISIBLE);
    obj_to_room(floor, 0);
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_data* brass = make(kKeyVnum);
    obj_to_obj(brass, ring);
    run_get(&m_player, "key 1.keyring");
    EXPECT_EQ(brass->carried_by, &m_player);
}

TEST_F(KeyringTest, PutAllDotNonKeyOnKeyringSaysNoKeys)
{
    obj_to_char(make(kRingVnum), &m_player);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(bread, &m_player);
    run_put(&m_player, "all.bread keyring");
    EXPECT_EQ(output(), "You don't seem to have any keys to put on it.\n\r");
    EXPECT_EQ(bread->carried_by, &m_player);
}

/* Someone watching sees the key go on the keyring. */
TEST_F(KeyringTest, OnlookerSeesKeyPutOnKeyring)
{
    char_data watcher {};
    clear_char(&watcher, 0);
    ClearedCharacterRelease watcher_release(&watcher);
    char watcher_name[] = "Watcher";
    watcher.player.name = watcher_name;
    watcher.player.race = RACE_HUMAN;
    watcher.player.level = 10;
    watcher.specials.position = POSITION_STANDING;
    watcher.in_room = 0;
    descriptor_data seat {};
    seat.output = seat.small_outbuf;
    seat.small_outbuf[0] = '\0';
    seat.bufspace = SMALL_BUFSIZE - 1;
    seat.connected = CON_PLYNG;
    seat.descriptor = 2;
    seat.character = &watcher;
    watcher.desc = &seat;
    m_player.next_in_room = &watcher;

    obj_to_char(make(kRingVnum), &m_player);
    obj_to_char(make(kKeyVnum), &m_player);
    run_put(&m_player, "key keyring");
    m_player.next_in_room = nullptr;
    EXPECT_NE(std::string(seat.output).find("Keeper puts a brass key on an iron keyring."),
        std::string::npos);
}

/* The worn-gear search matches the start of a name only: a worn "pouch
 * keyring" does not beat the belt keyring. */
TEST_F(KeyringTest, WornPouchNamedKeyringSecondDoesNotBeatBeltKeyring)
{
    m_player.specials.position = POSITION_STANDING;
    static char pouch_name[] = "pouch keyring";
    equip_char(&m_player, make(kBeltVnum), WEAR_WAISTE);
    obj_data* ring = make(kRingVnum);
    equip_char(&m_player, ring, WEAR_BELT_1);
    obj_to_obj(make(kKeyVnum), ring);
    obj_data* pouch = make(kBagVnum);
    pouch->name = pouch_name;
    equip_char(&m_player, pouch, WEAR_BELT_2);
    obj_to_obj(make(kBreadVnum), pouch);
    run_cmd(do_look, &m_player, "in keyring", 15);
    EXPECT_NE(output().find("(worn)"), std::string::npos);
    EXPECT_NE(output().find("a brass key"), std::string::npos);
    EXPECT_EQ(output().find("a loaf of bread"), std::string::npos);
}

/* The item-type setting also holds on the by-position path (num < 9999). */
TEST_F(KeyringTest, ObjInListVisByPositionCountsOnlyTheType)
{
    obj_data* key1 = make(kKeyVnum);
    obj_data* key2 = make(kKeyVnum);
    obj_data* bread = make(kBreadVnum);
    obj_to_char(key2, &m_player);
    obj_to_char(bread, &m_player);
    obj_to_char(key1, &m_player); /* list: key1, bread, key2 */
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "", m_player.carrying, 2), bread);
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "", m_player.carrying, 2, ITEM_KEY), key2);
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "", m_player.carrying, 1, ITEM_KEY), key1);
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "", m_player.carrying, 3, ITEM_KEY), nullptr);
    EXPECT_EQ(get_obj_in_list_vis(&m_player, "", m_player.carrying, 1, ITEM_FOOD), bread);
}
