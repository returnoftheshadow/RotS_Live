#include "../big_brother.h"
#include "../db.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"
#include "../world_clock.h"

#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <string_view>

ACMD(do_afk);

extern struct descriptor_data* descriptor_list;
extern struct room_data world;
extern struct weather_data weather_info;

namespace {
using game_rules::big_brother;

constexpr std::string_view PROTECTION_GRANTED = "You feel the protection of the Gods";
constexpr std::string_view PROTECTION_REFUSED = "You have engaged in PK too recently";

// An arbitrary fixed instant the tests measure from, so each sets the time explicitly.
const WorldClock::time_point REFERENCE_TIME = WorldClock::time_point {} + std::chrono::hours(1);

// A playing character on descriptor_list, so Big Brother's messages to it land in its output
// buffer, and an opponent it can engage in PK. Big Brother forgets both on destruction.
class AfkPlayerContext {
public:
    AfkPlayerContext()
    {
        big_brother::create(weather_info, &world);

        player.abs_number = 4101;
        player.player.name = player_name;
        player.in_room = NOWHERE;
        player.desc = &connection;
        opponent.abs_number = 4102;
        opponent.player.name = opponent_name;

        connection.output = connection.small_outbuf;
        connection.small_outbuf[0] = '\0';
        connection.bufptr = 0;
        connection.bufspace = SMALL_BUFSIZE - 1;
        connection.connected = CON_PLYNG;
        connection.character = &player;
        connection.next = nullptr;
        previous_descriptor_list = descriptor_list;
        descriptor_list = &connection;
    }

    ~AfkPlayerContext()
    {
        big_brother& rules = big_brother::instance();
        rules.on_character_disconnected(&player);
        rules.on_character_disconnected(&opponent);
        descriptor_list = previous_descriptor_list;
    }

    // True when the player has been sent text containing message.
    bool was_told(std::string_view message) const
    {
        const std::string_view output(connection.small_outbuf);
        return output.find(message) != std::string_view::npos;
    }

    // Everything the player has been sent so far.
    std::string output() const { return std::string(connection.small_outbuf); }

    // The character going AFK; connected through connection and in no room.
    char_data player {};
    // The other side of the PK engagement; it never receives messages.
    char_data opponent {};

private:
    // Storage for the player's name, which Big Brother may log.
    char player_name[16] = "AfkPlayer";
    // Storage for the opponent's name, which Big Brother may log.
    char opponent_name[16] = "PkOpponent";
    // The player's connection, holding the output under test.
    descriptor_data connection {};
    // The descriptor list before this context replaced it, restored on destruction.
    descriptor_data* previous_descriptor_list = nullptr;
};
} // namespace

TEST(BigBrotherAfkProtection, GrantsProtectionWithNoPkEngagement)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();

    rules.on_character_afked(&context.player, REFERENCE_TIME);

    EXPECT_TRUE(context.was_told(PROTECTION_GRANTED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_REFUSED)) << context.output();
}

TEST(BigBrotherAfkProtection, RefusesProtectionJustBeforeTheDelayAfterAttacking)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.player, &context.opponent, REFERENCE_TIME);

    const WorldClock::time_point afk_time
        = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY - std::chrono::seconds(1);
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_REFUSED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_GRANTED)) << context.output();
}

TEST(BigBrotherAfkProtection, RefusesProtectionJustBeforeTheDelayAfterBeingAttacked)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.opponent, &context.player, REFERENCE_TIME);

    const WorldClock::time_point afk_time
        = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY - std::chrono::seconds(1);
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_REFUSED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_GRANTED)) << context.output();
}

TEST(BigBrotherAfkProtection, GrantsProtectionOnceTheDelayHasPassed)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.player, &context.opponent, REFERENCE_TIME);

    const WorldClock::time_point afk_time = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY;
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_GRANTED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_REFUSED)) << context.output();
}

TEST(BigBrotherAfkProtection, MeasuresTheDelayFromTheLatestEngagement)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(&context.player, &context.opponent, REFERENCE_TIME);
    const WorldClock::time_point second_attack_time
        = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY / 2;
    rules.on_character_attacked_player(&context.player, &context.opponent, second_attack_time);

    const WorldClock::time_point afk_time = REFERENCE_TIME + big_brother::PK_AFK_PROTECTION_DELAY;
    rules.on_character_afked(&context.player, afk_time);

    EXPECT_TRUE(context.was_told(PROTECTION_REFUSED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_GRANTED)) << context.output();
}

TEST(BigBrotherAfkProtection, AfkCommandGrantsProtectionOnceTheDelayHasPassed)
{
    AfkPlayerContext context;
    big_brother& rules = big_brother::instance();
    // Protection follows only if do_afk passes the current world time.
    const WorldClock::time_point now = WorldClock::now();
    const WorldClock::time_point engagement_time
        = now - big_brother::PK_AFK_PROTECTION_DELAY - std::chrono::seconds(1);
    rules.on_character_attacked_player(&context.player, &context.opponent, engagement_time);

    char no_argument[] = "";
    do_afk(&context.player, no_argument, nullptr, 0, 0);

    EXPECT_TRUE(context.was_told(PROTECTION_GRANTED)) << context.output();
    EXPECT_FALSE(context.was_told(PROTECTION_REFUSED)) << context.output();
}

namespace {
// A human player's corpse protected by Big Brother after an orc player killed him. The
// corpse holds a keyring, a bag, bread, a key and a last item that is never looted (so
// no loot clears the protection). Big Brother forgets the corpse and both characters on
// destruction.
class ProtectedCorpseContext {
public:
    ProtectedCorpseContext()
    {
        big_brother::create(weather_info, &world);

        dead.abs_number = 4201;
        dead.player.name = dead_name;
        dead.player.race = RACE_HUMAN;
        enemy.abs_number = 4202;
        enemy.player.name = enemy_name;
        enemy.player.race = RACE_ORC;

        set_item(keyring, ITEM_KEYRING, keyring_name, &bag);
        set_item(bag, ITEM_CONTAINER, bag_name, &bread);
        set_item(bread, ITEM_FOOD, bread_name, &key);
        set_item(key, ITEM_KEY, key_name, &last);
        set_item(last, ITEM_TRASH, last_name, nullptr);
        corpse.contains = &keyring;

        big_brother::instance().on_character_died(&dead, &enemy, &corpse);
    }

    ~ProtectedCorpseContext()
    {
        big_brother& rules = big_brother::instance();
        rules.on_corpse_decayed(&corpse);
        rules.on_character_disconnected(&dead);
        rules.on_character_disconnected(&enemy);
    }

    bool enemy_loots(obj_data& item)
    {
        return big_brother::instance().on_loot_item(&enemy, &corpse, &item);
    }

    char_data dead {};
    char_data enemy {};
    obj_data corpse {};
    obj_data keyring {};
    obj_data bag {};
    obj_data bread {};
    obj_data key {};
    obj_data last {};

private:
    static void set_item(obj_data& item, int type, char* name, obj_data* next)
    {
        item.obj_flags.type_flag = type;
        item.name = name;
        item.short_description = name;
        item.next_content = next;
    }

    char dead_name[16] = "Fallen";
    char enemy_name[16] = "Looter";
    char keyring_name[16] = "keyring iron";
    char bag_name[16] = "bag leather";
    char bread_name[16] = "bread loaf";
    char key_name[16] = "key brass";
    char last_name[16] = "trash bone";
};
} // namespace

TEST(BigBrotherCorpseLoot, EnemyMayLootKeyringAsOneItem)
{
    ProtectedCorpseContext context;

    EXPECT_TRUE(context.enemy_loots(context.keyring));
    EXPECT_TRUE(context.enemy_loots(context.bread));
    // The keyring and the bread used the two items an enemy may take.
    EXPECT_FALSE(context.enemy_loots(context.key));
}

TEST(BigBrotherCorpseLoot, EnemyMayNotLootBag)
{
    ProtectedCorpseContext context;

    EXPECT_FALSE(context.enemy_loots(context.bag));
    // The refused bag did not use up an item.
    EXPECT_TRUE(context.enemy_loots(context.bread));
    EXPECT_TRUE(context.enemy_loots(context.key));
}

namespace {
// A player character and a PK opponent in level range of it, neither on any list.
void make_pk_pair(char_data& player, char_data& opponent)
{
    player.abs_number = 4301;
    player.player.level = 30;
    opponent.abs_number = 4302;
    opponent.player.level = 30;
}
} // namespace

/* Big Brother holds AFK protection under a character's address. free_char must make it forget
 * them: the allocator may put the next character at that address. "setfree off" keeps the freed
 * memory, so the test can stand a character there. */
TEST(BigBrotherFreedCharacter, ACharacterFreedWhileAfkLeavesNoProtectionAtItsAddress)
{
    big_brother::create(weather_info, &world);
    big_brother& rules = big_brother::instance();
    char_data* freed = nullptr;
    CREATE1(freed, char_data);
    char_data attacker {};
    make_pk_pair(*freed, attacker);
    rules.on_character_afked(freed, REFERENCE_TIME);
    ASSERT_FALSE(rules.is_target_valid(&attacker, freed)) << "AFK protection was not granted";

    global_release_flag = 0;
    free_char(freed);
    global_release_flag = 1;
    char_data* const successor = freed;
    make_pk_pair(*successor, attacker);

    EXPECT_TRUE(rules.is_target_valid(&attacker, successor))
        << "a character at a freed character's address inherited its AFK protection";
    RELEASE(freed);
}

/* Big Brother holds a corpse's protection under the corpse's address. free_obj must make it
 * forget it: the allocator may put the next object at that address. "setfree off" keeps the
 * freed memory, so the test can stand an object there. */
TEST(BigBrotherFreedObject, ACorpseFreedWhileProtectedLeavesNoProtectionAtItsAddress)
{
    big_brother::create(weather_info, &world);
    big_brother& rules = big_brother::instance();
    char_data dead {};
    char_data killer {};
    make_pk_pair(dead, killer);
    obj_data bread {};
    obj_data* const corpse = new obj_data {};
    corpse->contains = &bread;
    rules.on_character_died(&dead, &killer, corpse);
    ASSERT_TRUE(rules.is_corpse_protected(&killer, corpse)) << "the corpse was not protected";

    corpse->contains = nullptr;
    global_release_flag = 0;
    free_obj(corpse);
    global_release_flag = 1;
    obj_data* const successor = corpse;

    EXPECT_FALSE(rules.is_corpse_protected(&killer, successor))
        << "an object at a freed corpse's address inherited its protection";
    delete successor;
    // The dead player's looting protection is held by id and outlives the corpse.
    rules.on_character_disconnected(&dead);
}
