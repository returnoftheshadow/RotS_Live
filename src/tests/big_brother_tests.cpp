#include "../big_brother.h"
#include "../db.h"
#include "../interpre.h"
#include "../owned_character.h"
#include "../structs.h"
#include "../utils.h"
#include "../world_clock.h"
#include "test_cleanup.h"

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
// The character numbers of the PK pair the freed-memory tests make, apart from AfkPlayerContext's.
constexpr int PK_PAIR_PLAYER_NUMBER = 4301;
constexpr int PK_PAIR_OPPONENT_NUMBER = 4302;
// The level both of the pair have, so each is in the other's PK range.
constexpr int PK_PAIR_LEVEL = 30;
// How long after its PK the successor goes AFK: well inside big_brother::PK_AFK_PROTECTION_DELAY,
// so a PK time left at its address would refuse it protection.
constexpr std::chrono::seconds AFK_DELAY_AFTER_PK(1);

// A player character and a PK opponent in level range of it, neither on any list.
void make_pk_pair(char_data& player, char_data& opponent)
{
    player.abs_number = PK_PAIR_PLAYER_NUMBER;
    player.player.level = PK_PAIR_LEVEL;
    opponent.abs_number = PK_PAIR_OPPONENT_NUMBER;
    opponent.player.level = PK_PAIR_LEVEL;
}

// A heap character the game frees with free_char under "setfree off", which keeps its memory so
// a successor can stand at the same address, and a PK opponent. Big Brother forgets both, and
// the character (or its successor) is freed, on destruction whether or not the test finished.
class FreedCharacterContext {
public:
    FreedCharacterContext()
    {
        big_brother::create(weather_info, &world);
        make_pk_pair(*character, opponent);
    }

    ~FreedCharacterContext()
    {
        big_brother& rules = big_brother::instance();
        rules.on_character_disconnected(character);
        rules.on_character_disconnected(&opponent);
    }

    // Frees the character as the game does, keeping its memory for a successor.
    void free_keeping_memory()
    {
        // Under "setfree off" free_char forgets clear_char's arrays without freeing them.
        release_cleared_character(character);
        global_release_flag = 0;
        free_char(character);
        global_release_flag = 1;
    }

    // Stands a new character at the freed one's address.
    char_data* successor()
    {
        make_pk_pair(*character, opponent);
        return character;
    }

    // The character to be freed, then its successor at the same address.
    owned_character character;
    // The other side of any PK engagement, and the attacker the protection is checked against.
    char_data opponent {};
};

// A dead player's corpse, protected by Big Brother after a player killed him, that the game
// frees with free_obj under "setfree off" so a successor can stand at its address. Big Brother
// forgets the corpse and both characters, and the corpse is deleted, on destruction whether or
// not the test finished.
class FreedCorpseContext {
public:
    FreedCorpseContext()
    {
        big_brother::create(weather_info, &world);
        make_pk_pair(dead, killer);
        corpse = new obj_data {};
        corpse->contains = &bread;
        big_brother::instance().on_character_died(&dead, &killer, corpse);
    }

    ~FreedCorpseContext()
    {
        global_release_flag = 1;
        big_brother& rules = big_brother::instance();
        corpse->contains = nullptr;
        if (m_freed) {
            delete corpse;
        } else {
            free_obj(corpse);
        }
        rules.on_character_disconnected(&dead);
        rules.on_character_disconnected(&killer);
    }

    // Frees the corpse as the game does, keeping its memory for a successor.
    void free_keeping_memory()
    {
        corpse->contains = nullptr;
        global_release_flag = 0;
        free_obj(corpse);
        global_release_flag = 1;
        m_freed = true;
    }

    // The dead player, whose looting protection outlives the corpse.
    char_data dead {};
    // The player who killed him, so the corpse is protected from everyone.
    char_data killer {};
    // What the corpse holds; Big Brother protects only a corpse with something in it.
    obj_data bread {};
    // The corpse to be freed, and its successor's memory once it has been.
    obj_data* corpse = nullptr;

private:
    // Whether free_keeping_memory has run, leaving the memory to delete on destruction.
    bool m_freed = false;
};
} // namespace

/* Big Brother holds AFK protection under a character's address. free_char must make it forget
 * them: the allocator may put the next character at that address. */
TEST(BigBrotherFreedCharacter, ACharacterFreedWhileAfkLeavesNoProtectionAtItsAddress)
{
    FreedCharacterContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_afked(context.character, REFERENCE_TIME);
    ASSERT_FALSE(rules.is_target_valid(&context.opponent, context.character))
        << "AFK protection was not granted";

    context.free_keeping_memory();
    char_data* const successor = context.successor();

    EXPECT_TRUE(rules.is_target_valid(&context.opponent, successor))
        << "a character at a freed character's address inherited its AFK protection";
}

/* Big Brother holds the time of a character's last PK engagement under its address. free_char
 * must make it forget it, or a character later at that address is refused AFK protection for an
 * engagement it never had. */
TEST(BigBrotherFreedCharacter, ACharacterFreedSoonAfterPkLeavesNoPkTimeAtItsAddress)
{
    FreedCharacterContext context;
    big_brother& rules = big_brother::instance();
    rules.on_character_attacked_player(context.character, &context.opponent, REFERENCE_TIME);

    context.free_keeping_memory();
    char_data* const successor = context.successor();
    rules.on_character_afked(successor, REFERENCE_TIME + AFK_DELAY_AFTER_PK);

    EXPECT_FALSE(rules.is_target_valid(&context.opponent, successor))
        << "a character at a freed character's address was refused AFK protection for its PK";
}

/* Big Brother holds a corpse's protection under the corpse's address. free_obj must make it
 * forget it: the allocator may put the next object at that address. The dead player's looting
 * protection is held by id and stays. */
TEST(BigBrotherFreedObject, ACorpseFreedWhileProtectedLeavesNoProtectionAtItsAddress)
{
    FreedCorpseContext context;
    big_brother& rules = big_brother::instance();
    ASSERT_TRUE(rules.is_corpse_protected(&context.killer, context.corpse))
        << "the corpse was not protected";

    context.free_keeping_memory();
    obj_data* const successor = context.corpse;

    EXPECT_FALSE(rules.is_corpse_protected(&context.killer, successor))
        << "an object at a freed corpse's address inherited its protection";
    EXPECT_TRUE(rules.is_target_looting(&context.dead))
        << "freeing the corpse ended the dead player's looting protection";
}
