#include "../db.h"
#include "../object_text.h"
#include "../structs.h"
#include "scripted_room_fixture.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

void get_corpse_desc(object_text::fields& out_text, const char_data* ch, int attacktype);
obj_data* make_physical_corpse(char_data* character, const char_data* killer, int attack_type);

using namespace scripted_room;

namespace {

/* The attack get_corpse_desc describes as leaving a corpse "cleanly slain". */
constexpr int CLEANLY_SLAIN_ATTACK = 25;

/* What create_money shows for one amount. */
struct money_text {
    // The amount of coins.
    int amount;
    // The name, short description, room line and the keyword and text of its extra description.
    const char* name;
    const char* short_description;
    const char* description;
    const char* look_keyword;
    const char* look_text;
};

constexpr const char* COINS_NAME = "coins money gold";

/* Each amount either side of where the words change. */
const money_text MONEY_TEXTS[] = {
    { 1, "coin money copper", "a coin", "One miserable copper coin is lying here.", "coin gold",
        "It's just one miserable little copper coin." },
    { 2, COINS_NAME, "a small pile of coins", "A small pile of coins is lying here.", COINS_NAME,
        "There are 2 copper coins." },
    { COPP_IN_SILV - 1, COINS_NAME, "a small pile of coins", "A small pile of coins is lying here.",
        COINS_NAME, "There are 99 copper coins." },
    { COPP_IN_SILV, COINS_NAME, "a small pile of coins", "A small pile of coins is lying here.",
        COINS_NAME, "There are about 1 silver coins." },
    { COPP_IN_SILV + 1, COINS_NAME, "a pile of coins", "A pile of coins is lying here.", COINS_NAME,
        "There are about 1 silver coins." },
    { COPP_IN_GOLD, COINS_NAME, "a pile of coins", "A pile of coins is lying here.", COINS_NAME,
        "It looks to be about 1 gold coins." },
    { COPP_IN_GOLD + 1, COINS_NAME, "a large heap of coins", "A large heap of coins is lying here.",
        COINS_NAME, "It looks to be about 1 gold coins." },
    { 25000, COINS_NAME, "a large heap of coins", "A large heap of coins is lying here.", COINS_NAME,
        "You guess there are, maybe, 20 gold coins." },
    { 25001, COINS_NAME, "a huge mound of coins", "A huge mound of coins is lying here.", COINS_NAME,
        "You guess there are, maybe, 20 gold coins." },
    { 500000, COINS_NAME, "a huge mound of coins", "A huge mound of coins is lying here.", COINS_NAME,
        "There is a lot of gold." },
    { 500001, COINS_NAME, "an enormous mountain of coins",
        "An enormous mountain of money is lying here.", COINS_NAME, "There is a lot of gold." },
};

/* A text with every field and two extra descriptions. */
object_text::fields make_lantern_fields()
{
    object_text::fields text;
    text.name = "lantern brass";
    text.short_description = "a brass lantern";
    text.description = "A brass lantern stands here.";
    text.action_description = "It is dented.";
    text.extra_descriptions.push_back({ "dent", "A deep dent." });
    text.extra_descriptions.push_back({ "handle", "A worn handle." });
    return text;
}

} // namespace

/* Each field is shown as its text, and a field with no value as a null pointer. */
TEST(ObjectText, ShowsEachFieldAndNullForOneWithNoValue)
{
    object_text::fields fields = make_lantern_fields();
    fields.action_description.reset();
    const std::shared_ptr<const object_text> text = create_object_text(std::move(fields));

    EXPECT_STREQ(text->name(), "lantern brass");
    EXPECT_STREQ(text->short_description(), "a brass lantern");
    EXPECT_STREQ(text->description(), "A brass lantern stands here.");
    EXPECT_EQ(text->action_description(), nullptr);
}

/* The extra descriptions form a list in the order they were given. */
TEST(ObjectText, LinksTheExtraDescriptionsInOrder)
{
    const std::shared_ptr<const object_text> text = create_object_text(make_lantern_fields());

    const extra_descr_data* const first = text->extra_descriptions();
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first->keyword, "dent");
    EXPECT_STREQ(first->description, "A deep dent.");
    ASSERT_NE(first->next, nullptr);
    EXPECT_STREQ(first->next->keyword, "handle");
    EXPECT_EQ(first->next->next, nullptr);
}

/* A text with no extra descriptions shows a null list. */
TEST(ObjectText, ShowsANullListWithNoExtraDescriptions)
{
    const std::shared_ptr<const object_text> text = create_object_text(object_text::fields());

    EXPECT_EQ(text->extra_descriptions(), nullptr);
}

/* set_text points every text field of the object into the text it is given. */
TEST(ObjectText, SetTextPointsTheFieldsIntoTheText)
{
    const std::shared_ptr<const object_text> text = create_object_text(make_lantern_fields());
    obj_data lantern {};

    lantern.set_text(text);

    EXPECT_EQ(lantern.name, text->name());
    EXPECT_EQ(lantern.short_description, text->short_description());
    EXPECT_EQ(lantern.description, text->description());
    EXPECT_EQ(lantern.action_description, text->action_description());
    EXPECT_EQ(lantern.ex_description, text->extra_descriptions());
    EXPECT_EQ(lantern.shared_text(), text);
}

/* A null text leaves every text field null. */
TEST(ObjectText, SetTextWithNoTextClearsTheFields)
{
    obj_data lantern {};
    lantern.set_text(create_object_text(make_lantern_fields()));

    lantern.set_text(nullptr);

    EXPECT_EQ(lantern.name, nullptr);
    EXPECT_EQ(lantern.short_description, nullptr);
    EXPECT_EQ(lantern.description, nullptr);
    EXPECT_EQ(lantern.action_description, nullptr);
    EXPECT_EQ(lantern.ex_description, nullptr);
    EXPECT_EQ(lantern.shared_text(), nullptr);
}

/* A text is freed only when the last object showing it lets go of it. */
TEST(ObjectText, LastsUntilTheLastObjectShowingItLetsGo)
{
    std::weak_ptr<const object_text> watched;
    obj_data copy {};
    {
        obj_data prototype {};
        prototype.set_text(create_object_text(make_lantern_fields()));
        watched = prototype.shared_text();
        copy = prototype;
        prototype.set_text(create_object_text(object_text::fields()));
        EXPECT_FALSE(watched.expired()) << "the copy still shows it";
        EXPECT_STREQ(copy.short_description, "a brass lantern");
    }

    copy.set_text(nullptr);

    EXPECT_TRUE(watched.expired());
}

/* The fields read back from an object are what it shows, with a null extra-description keyword or
 * description read as empty. */
TEST(ObjectText, FieldsOfAnObjectAreWhatItShows)
{
    char name[] = "lantern";
    char keyword[] = "dent";
    extra_descr_data blank {};
    extra_descr_data dent {};
    dent.keyword = keyword;
    dent.next = &blank;
    obj_data lantern {};
    lantern.name = name;
    lantern.ex_description = &dent;

    const object_text::fields fields = object_text_fields_of(lantern);

    EXPECT_EQ(fields.name, std::optional<std::string>("lantern"));
    EXPECT_FALSE(fields.short_description.has_value());
    EXPECT_FALSE(fields.action_description.has_value());
    ASSERT_EQ(fields.extra_descriptions.size(), 2u);
    EXPECT_EQ(fields.extra_descriptions[0].keyword, "dent");
    EXPECT_EQ(fields.extra_descriptions[0].description, "");
    EXPECT_EQ(fields.extra_descriptions[1].keyword, "");
}

/* An object holding a text shares that one; one whose fields were set by hand gets a new text
 * holding what they show. */
TEST(ObjectText, SharedTextOfAnObjectIsItsOwnOrACopyOfItsFields)
{
    obj_data held {};
    held.set_text(create_object_text(make_lantern_fields()));
    char name[] = "torch";
    obj_data by_hand {};
    by_hand.name = name;

    const std::shared_ptr<const object_text> shared = shared_text_of(held);
    const std::shared_ptr<const object_text> built = shared_text_of(by_hand);

    EXPECT_EQ(shared, held.shared_text());
    ASSERT_NE(built, nullptr);
    EXPECT_STREQ(built->name(), "torch");
    EXPECT_NE(built->name(), name);
}

/* An object whose text was set by hand reaching free_obj is logged: nothing frees that text. */
TEST(ObjectText, FreeingAnObjectWithTextSetByHandLogsIt)
{
    char name[] = "torch";
    char short_description[] = "a torch";
    obj_data* const by_hand = new obj_data();
    clear_object(by_hand);
    by_hand->name = name;
    by_hand->short_description = short_description;

    testing::internal::CaptureStderr();
    free_obj(by_hand);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_NE(logged.find("SYSERR: free_obj: object -1 (a torch) has text set by hand"),
        std::string::npos)
        << logged;
}

/* An object showing a shared text, as every object the game makes does, is freed quietly. */
TEST(ObjectText, FreeingAnObjectWithASharedTextLogsNothing)
{
    obj_data* const held = new obj_data();
    clear_object(held);
    held->set_text(create_object_text(make_lantern_fields()));

    testing::internal::CaptureStderr();
    free_obj(held);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(logged.find("SYSERR"), std::string::npos) << logged;
}

/* Coins show the words for their amount, in a text of their own. */
TEST(CreateMoney, ShowsTheWordsForItsAmount)
{
    obj_data* const saved_object_list = object_list;
    for (const money_text& expected : MONEY_TEXTS) {
        SCOPED_TRACE(expected.amount);
        object_list = nullptr;

        obj_data* const coins = create_money(expected.amount);

        EXPECT_STREQ(coins->name, expected.name);
        EXPECT_STREQ(coins->short_description, expected.short_description);
        EXPECT_STREQ(coins->description, expected.description);
        EXPECT_EQ(coins->action_description, nullptr);
        ASSERT_NE(coins->ex_description, nullptr);
        EXPECT_STREQ(coins->ex_description->keyword, expected.look_keyword);
        EXPECT_STREQ(coins->ex_description->description, expected.look_text);
        EXPECT_EQ(coins->ex_description->next, nullptr);
        ASSERT_NE(coins->shared_text(), nullptr);
        EXPECT_EQ(coins->shared_text().use_count(), 1) << "no other object shows the coins' text";
        free_obj(coins);
    }
    object_list = saved_object_list;
}

namespace {

/* A corpse's words, in room 0 of the scripted room. */
class CorpseTextTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        m_saved_sector = world[0].sector_type;
        world[0].sector_type = SECT_CITY;
    }

    void TearDown() override
    {
        world[0].sector_type = m_saved_sector;
        ScriptedRoomFixture::TearDown();
    }

private:
    int m_saved_sector = 0; // room 0's sector, restored in TearDown
};

} // namespace

/* A mob's corpse names the mob and says how it died. */
TEST_F(CorpseTextTest, AMobsCorpseNamesTheMob)
{
    char_data* const mob = make_mob("a warg", 0);
    object_text::fields text;

    get_corpse_desc(text, mob, CLEANLY_SLAIN_ATTACK);

    EXPECT_EQ(text.short_description, std::optional<std::string>("the cleanly slain corpse of a warg"));
    EXPECT_EQ(text.description, std::optional<std::string>("The cleanly slain corpse of a warg is lying here."));
}

/* A player's corpse shows the player's race, not the player's name. */
TEST_F(CorpseTextTest, APlayersCorpseShowsItsRace)
{
    object_text::fields text;

    get_corpse_desc(text, &m_player, CLEANLY_SLAIN_ATTACK);

    EXPECT_EQ(text.short_description, std::optional<std::string>("the cleanly slain corpse of *a Human*"));
}

/* In water a corpse floats. */
TEST_F(CorpseTextTest, ACorpseInWaterFloats)
{
    char_data* const mob = make_mob("a warg", 0);
    for (const int sector : { SECT_WATER_NOSWIM, SECT_WATER_SWIM, SECT_UNDERWATER }) {
        SCOPED_TRACE(sector);
        world[0].sector_type = sector;
        object_text::fields text;

        get_corpse_desc(text, mob, CLEANLY_SLAIN_ATTACK);

        EXPECT_EQ(text.description, std::optional<std::string>("The cleanly slain corpse of a warg is floating here."));
        EXPECT_EQ(text.short_description, std::optional<std::string>("the cleanly slain corpse of a warg"));
    }
}

/* A mob with no name leaves a corpse that says so, as printf showed one. */
TEST_F(CorpseTextTest, ACorpseOfAMobWithNoNameSaysSo)
{
    char_data* const mob = make_mob("a warg", 0);
    char* const short_description = mob->player.short_descr;
    mob->player.short_descr = nullptr;
    object_text::fields text;

    get_corpse_desc(text, mob, CLEANLY_SLAIN_ATTACK);

    mob->player.short_descr = short_description;
    EXPECT_EQ(text.short_description, std::optional<std::string>("the cleanly slain corpse of (null)"));
}

/* A corpse made in a death has a text of its own, holding its name and those words. */
TEST_F(CorpseTextTest, ACorpseHasATextOfItsOwn)
{
    obj_data* const corpse = make_physical_corpse(&m_player, &m_player, CLEANLY_SLAIN_ATTACK);

    ASSERT_NE(corpse, nullptr);
    EXPECT_STREQ(corpse->name, "corpse");
    EXPECT_STREQ(corpse->short_description, "the cleanly slain corpse of *a Human*");
    EXPECT_STREQ(corpse->description, "The cleanly slain corpse of *a Human* is lying here.");
    EXPECT_EQ(corpse->action_description, nullptr);
    ASSERT_NE(corpse->shared_text(), nullptr);
    EXPECT_EQ(corpse->shared_text().use_count(), 1) << "no other object shows the corpse's text";
}
