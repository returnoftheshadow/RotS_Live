/* Game functions that must give back the memory of what they let go: freeing a mob with no
 * prototype, a zone reset that gives a mob another script, and the text objects stop showing.
 *
 * Not portable standard C++, by owner ruling of 2026-10-26: a free cannot be observed without
 * changing the game, so these tests count the allocator's bytes in use (allocated_bytes.h) and
 * are skipped where it cannot tell. Each does its operation many times, so a leak far outgrows
 * what the allocator keeps cached. */
#include "../mail.h"
#include "../note_writing.h"
#include "../object_text.h"
#include "allocated_bytes.h"
#include "scoped_mail_file.h"
#include "scripted_room_fixture.h"

#include <string>
#include <utility>

ACMD(do_examine);
ACMD(do_say);
SPECIAL(reciter);
void name_from_drinkcon(struct obj_data* object);
void name_to_drinkcon(struct obj_data* object, int liquid_type);
int new_room(const struct char_data* character, int number);
void free_room(struct char_data* character);
void free_zone(struct char_data* character);
void free_script(struct char_data* character);
void new_obj(const struct char_data* character);
void free_object(struct char_data* character);
void postmaster_receive_mail(
    struct char_data* character, int command_number, const char* argument, char_data* host);

using namespace scripted_room;

namespace {

/* How many times a test repeats the operation it measures. */
constexpr int ROUNDS = 128;
/* Rounds run before measuring: glibc keeps the first few freed blocks of each size cached, and
 * mallinfo2 counts them as in use. */
constexpr int WARM_UP_ROUNDS = 32;
/* The fewest bytes a leak would keep each round: str_dup's 256-byte block for a short text. */
constexpr std::size_t SMALLEST_LEAK_PER_ROUND = 256;
/* Growth the allocator may show over all the rounds without a leak: a quarter of the smallest. */
constexpr std::size_t ALLOWED_GROWTH = ROUNDS * SMALLEST_LEAK_PER_ROUND / 4;
/* An amount create_money shows as a large heap of coins, with an extra description of its own. */
constexpr int COINS_IN_A_LARGE_HEAP = 5000;

class MemoryReleaseTest : public ScriptedRoomFixture {
protected:
    /* How many bytes in use grew over ROUNDS calls of <round>, measured after WARM_UP_ROUNDS
     * calls, so neither what the game allocates once nor what the allocator caches counts. */
    template <typename Round>
    static std::size_t growth_over_rounds(Round round)
    {
        for (int warm_up = 0; warm_up < WARM_UP_ROUNDS; ++warm_up) {
            round();
        }
        const std::size_t before = *allocated_bytes();
        for (int measured = 0; measured < ROUNDS; ++measured) {
            round();
        }
        const std::size_t after = *allocated_bytes();
        /* Less in use than before is no growth. */
        return after > before ? after - before : 0;
    }

    /* Empties what the player was sent, so the output of many rounds never makes
     * write_to_output allocate a large buffer. */
    void clear_output()
    {
        release_test_descriptor(&m_descriptor);
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

private:
};

} // namespace

TEST_F(MemoryReleaseTest, FreeingAMobWithNoPrototypeGivesBackItsKeywords)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([this]() -> void {
        extract_char(make_mob("listener", 0));
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(MemoryReleaseTest, OpeningTheTextEditorFreesTheTextItCopies)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([this]() -> void {
        char* text = str_dup("Meet me at the ford.\n\r");
        string_add_init(&m_descriptor, &text);
        char finish[] = "%e";
        string_add(&m_descriptor, finish);
        RELEASE(text);
        clear_output();
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

namespace {

/* shape_center_room's field 9: the chosen exit's description. */
constexpr int ROOM_EXIT_DESCRIPTION_FIELD = 9;
/* shape_center_room's field 14: the room's first extra description. */
constexpr int ROOM_EXTRA_DESCRIPTION_FIELD = 14;
/* shape_center_zone's field 21: the zone's description. */
constexpr int ZONE_DESCRIPTION_FIELD = 21;
/* shape_center_zone's field 22: the zone's map. */
constexpr int ZONE_MAP_FIELD = 22;
/* shape_center_script's field 21: the script's description. */
constexpr int SCRIPT_DESCRIPTION_FIELD = 21;
/* shape_center_obj's field 1: the object's aliases, the keywords that find it. */
constexpr int OBJECT_ALIASES_FIELD = 1;
/* shape_center_obj's field 4: the look text, shown by look and examine. */
constexpr int OBJECT_LOOK_TEXT_FIELD = 4;

/* A builder in room 0 at a shape editor, sent output through the fixture's connection. */
class ShapeEditorMemoryTest : public MemoryReleaseTest {
protected:
    void SetUp() override
    {
        MemoryReleaseTest::SetUp();
        m_builder->player.name = str_dup("Builder");
        m_builder->in_room = 0;
        m_builder->desc = &m_descriptor;
        m_descriptor.character = m_builder;
    }

    void TearDown() override
    {
        m_builder->desc = nullptr;
        m_descriptor.character = &m_player;
        m_descriptor.str = nullptr;
        MemoryReleaseTest::TearDown();
    }

    /* Opens the editor's current field in the text editor, finishes it unchanged with %e, and has
     * the editor take the text back, as a builder does who opens a text and saves it at once.
     * <shape_center> is the editor's handler for what the builder types. */
    void save_field_unchanged(void (*shape_center)(char_data*, char*))
    {
        char prompt[] = "";
        shape_center(m_builder, prompt);
        char finish[] = "%e";
        string_add(&m_descriptor, finish);
        shape_center(m_builder, prompt);
        clear_output();
    }

    /* Starts the room editor on a new room; free_room frees the editor state, so it is allocated. */
    shape_room* start_room_editor()
    {
        shape_room* editor = nullptr;
        CREATE1(editor, shape_room);
        editor->procedure = SHAPE_EDIT;
        m_builder->temp = editor;
        new_room(m_builder, ROOM_VNUM);
        SET_BIT(editor->flags, SHAPE_ROOM_LOADED);
        return editor;
    }

    /* Starts the zone editor on a zone with a description and a map; free_zone frees the editor
     * state, so it is allocated. */
    shape_zone* start_zone_editor()
    {
        shape_zone* editor = nullptr;
        CREATE1(editor, shape_zone);
        editor->procedure = SHAPE_EDIT;
        SET_BIT(editor->flags, SHAPE_ZONE_LOADED);
        editor->zone_descr = str_dup("Trees crowd the path.\n\r");
        editor->zone_map = str_dup("[ ]-[ ]\n\r");
        m_builder->temp = editor;
        return editor;
    }

    /* Starts the object editor on a new object; free_object frees the editor state, so it is
     * allocated. */
    shape_object* start_object_editor()
    {
        shape_object* editor = nullptr;
        CREATE(editor, shape_object, 1);
        editor->procedure = SHAPE_EDIT;
        m_builder->temp = editor;
        new_obj(m_builder);
        SET_BIT(editor->flags, SHAPE_OBJECT_LOADED);
        return editor;
    }

    owned_character m_builder; // the builder at the editor
};

} // namespace

TEST_F(ShapeEditorMemoryTest, SavingARoomExitDescriptionFreesTheOneItReplaces)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    shape_room* const editor = start_room_editor();
    room_direction_data* room_exit = nullptr;
    CREATE1(room_exit, room_direction_data);
    room_exit->general_description = str_dup("A heavy door.\n\r");
    room_exit->keyword = str_dup("door");
    editor->room->dir_option[NORTH] = room_exit;
    editor->exit_chosen = NORTH;

    const std::size_t growth = growth_over_rounds([this, editor]() -> void {
        editor->editflag = ROOM_EXIT_DESCRIPTION_FIELD;
        save_field_unchanged(shape_center_room);
    });

    EXPECT_STREQ(editor->room->dir_option[NORTH]->general_description, "A heavy door.\n\r");
    free_room(m_builder);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(ShapeEditorMemoryTest, SavingARoomExtraDescriptionFreesTheOneItReplaces)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    shape_room* const editor = start_room_editor();
    extra_descr_data* extra = nullptr;
    CREATE1(extra, extra_descr_data);
    extra->keyword = str_dup("torch");
    extra->description = str_dup("It burns low.\n\r");
    editor->room->ex_description = extra;

    const std::size_t growth = growth_over_rounds([this, editor]() -> void {
        editor->editflag = ROOM_EXTRA_DESCRIPTION_FIELD;
        save_field_unchanged(shape_center_room);
    });

    EXPECT_STREQ(editor->room->ex_description->description, "It burns low.\n\r");
    free_room(m_builder);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(ShapeEditorMemoryTest, SavingAZoneDescriptionFreesTheOneItReplaces)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    shape_zone* const editor = start_zone_editor();

    const std::size_t growth = growth_over_rounds([this, editor]() -> void {
        editor->editflag = ZONE_DESCRIPTION_FIELD;
        save_field_unchanged(shape_center_zone);
    });

    EXPECT_STREQ(editor->zone_descr, "Trees crowd the path.\n\r");
    free_zone(m_builder);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(ShapeEditorMemoryTest, SavingAZoneMapFreesTheOneItReplaces)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    shape_zone* const editor = start_zone_editor();

    const std::size_t growth = growth_over_rounds([this, editor]() -> void {
        editor->editflag = ZONE_MAP_FIELD;
        save_field_unchanged(shape_center_zone);
    });

    EXPECT_STREQ(editor->zone_map, "[ ]-[ ]\n\r");
    free_zone(m_builder);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(ShapeEditorMemoryTest, SavingAScriptDescriptionFreesTheOneItReplaces)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    /* free_script frees the editor state, so it is allocated. */
    shape_script* editor = nullptr;
    CREATE1(editor, shape_script);
    editor->procedure = SHAPE_EDIT;
    SET_BIT(editor->flags, SHAPE_SCRIPT_LOADED);
    editor->name = str_dup("The ferryman");
    editor->description = str_dup("He waits at the ford.\n\r");
    m_builder->temp = editor;

    const std::size_t growth = growth_over_rounds([this, editor]() -> void {
        editor->editflag = SCRIPT_DESCRIPTION_FIELD;
        save_field_unchanged(shape_center_script);
    });

    EXPECT_STREQ(editor->description, "He waits at the ford.\n\r");
    free_script(m_builder);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

/* Leaving the object editor frees the object it was editing, with every text field, edited or not,
 * and its extra descriptions. */
TEST_F(ShapeEditorMemoryTest, LeavingTheObjectEditorGivesBackTheObjectsText)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([this]() -> void {
        shape_object* const editor = start_object_editor();
        extra_descr_data* dent = nullptr;
        CREATE(dent, extra_descr_data, 1);
        dent->keyword = str_dup("dent");
        dent->description = str_dup("A deep dent.");
        editor->object->ex_description = dent;
        /* The first call shows the prompt, the second takes the new aliases: enough words that a
         * copy kept each round outgrows ALLOWED_GROWTH. */
        char prompt[] = "";
        std::string aliases;
        while (aliases.size() < SMALLEST_LEAK_PER_ROUND) {
            aliases += "lantern ";
        }
        editor->editflag = OBJECT_ALIASES_FIELD;
        shape_center_obj(m_builder, prompt);
        shape_center_obj(m_builder, &aliases[0]);
        free_object(m_builder);
        clear_output();
    });

    EXPECT_EQ(m_builder->temp, nullptr);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

/* Saving an object's look text frees the text it replaces, which the editing session owns. */
TEST_F(ShapeEditorMemoryTest, SavingAnObjectsLookTextFreesTheOneItReplaces)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    shape_object* const editor = start_object_editor();
    RELEASE(editor->object->action_description);
    editor->object->action_description = str_dup("A plain brass lantern.\n\r");

    const std::size_t growth = growth_over_rounds([this, editor]() -> void {
        editor->editflag = OBJECT_LOOK_TEXT_FIELD;
        save_field_unchanged(shape_center_obj);
    });

    EXPECT_STREQ(editor->object->action_description, "A plain brass lantern.\n\r");
    free_object(m_builder);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

/* A prototype given a new text, as implement_object gives it one, frees the text it showed once
 * the copies made before still showing it are gone. */
TEST_F(MemoryReleaseTest, APrototypesReplacedTextGoesWithItsLastCopy)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    obj_data& stone = prototype_for(STONE_VNUM);

    const std::size_t growth = growth_over_rounds([this, &stone]() -> void {
        obj_data* first = make_object(STONE_VNUM, 0);
        obj_data* second = make_object(STONE_VNUM, 0);
        stone.set_text(create_object_text(object_text_fields_of(stone)));
        extract_obj(first);
        extract_obj(second);
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(MemoryReleaseTest, FillingAndEmptyingADrinkContainerGivesBackEachName)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([this]() -> void {
        obj_data* flask = make_object(FLASK_VNUM, 0);
        name_from_drinkcon(flask);
        name_to_drinkcon(flask, LIQ_WATER);
        extract_obj(flask);
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(MemoryReleaseTest, CoinsGiveBackTheirText)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([]() -> void {
        extract_obj(create_money(COINS_IN_A_LARGE_HEAP));
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

TEST_F(MemoryReleaseTest, WritingANoteGivesBackTheEditorsBufferAndTheNotesText)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([this]() -> void {
        obj_data* note = make_object(STONE_VNUM, 0);
        start_writing_note(m_descriptor, *note);
        /* Long enough that a buffer kept each round outgrows ALLOWED_GROWTH. */
        std::string line(SMALLEST_LEAK_PER_ROUND, 'x');
        string_add(&m_descriptor, &line[0]);
        char finish[] = "%e";
        string_add(&m_descriptor, finish);
        extract_obj(note);
        clear_output();
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

/* A note freed under its writer frees what the writer had typed. */
TEST_F(MemoryReleaseTest, FreeingANoteBeingWrittenOnGivesBackTheEditorsBuffer)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    descriptor_list = &m_descriptor;

    const std::size_t growth = growth_over_rounds([this]() -> void {
        obj_data* note = make_object(STONE_VNUM, 0);
        start_writing_note(m_descriptor, *note);
        /* Long enough that a buffer kept each round outgrows ALLOWED_GROWTH. */
        std::string line(SMALLEST_LEAK_PER_ROUND, 'x');
        string_add(&m_descriptor, &line[0]);
        extract_obj(note);
        clear_output();
    });

    descriptor_list = nullptr;
    EXPECT_EQ(m_descriptor.written_note_text, nullptr);
    EXPECT_LT(growth, ALLOWED_GROWTH);
}

/* A name a script gives an object goes in the object's text, so the copy the script made of it is
 * freed, and the object's text goes with the object. */
TEST_F(MemoryReleaseTest, RenamingAnObjectByScriptGivesBackTheName)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    /* Long enough that a name kept each round outgrows ALLOWED_GROWTH. */
    const std::string new_name(SMALLEST_LEAK_PER_ROUND, 'x');
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_ASSIGN_STR, new_name.c_str(), { SCRIPT_PARAM_OB1_NAME } } });

    const std::size_t growth = growth_over_rounds([this]() -> void {
        obj_data* stone = make_scripted(STONE_VNUM);
        obj_to_char(stone, &m_player);
        run_command(do_examine, "stone");
        /* free_obj leaves an object's script state alone, for a script still running; that is not
         * what this test measures. */
        RELEASE(stone->obj_flags.script_info);
        extract_obj(stone);
        clear_output();
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

/* A letter copies the words the mail system read for it, frees them, and frees its text when it goes. */
TEST_F(MemoryReleaseTest, ReceivingALetterGivesBackItsWords)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    const scoped_mail_file mail_file;
    char_data* const postmaster = make_mob("postmaster", 0);
    char sender[] = "Frodo";

    const std::size_t growth = growth_over_rounds([this, postmaster, &sender]() -> void {
        /* Long enough that words kept each round outgrow ALLOWED_GROWTH. */
        std::string words(SMALLEST_LEAK_PER_ROUND, 'x');
        store_mail(m_player.player.name, sender, &words[0]);
        postmaster_receive_mail(&m_player, 0, "", postmaster);
        extract_obj(m_player.carrying);
        clear_output();
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}

namespace {

/* What the scroll holds: a line to say, then one to act out. */
constexpr const char* RECITER_POEM = "The first line.\n\r+$n bows.\n\r";
/* What the reciter has left once it has said the first line. */
constexpr const char* POEM_AFTER_FIRST_LINE = "+$n bows.\n\r";
/* A roll that keeps the reciter from skipping its turn. */
constexpr double RECITING_ROLL = 0.99;

class ReciterTest : public MemoryReleaseTest {
protected:
    /* Gives <mob> a scroll holding <poem>, in a text only that scroll shows, as a script's rename
     * or a restyle leaves it. */
    obj_data* give_scroll(char_data* mob, const char* poem)
    {
        obj_data* scroll = make_object(STONE_VNUM, 0);
        object_text::fields text = object_text_fields_of(*scroll);
        text.name = "textscroll";
        text.extra_descriptions.push_back({ "textscroll", poem });
        scroll->set_text(create_object_text(std::move(text)));
        obj_to_char(scroll, mob);
        return scroll;
    }

    /* One turn of <mob>'s reciter procedure, which it does not skip. */
    void take_turn(char_data* mob)
    {
        push_test_random_value(RECITING_ROLL);
        char argument[] = "";
        reciter(mob, mob, 0, argument, 0, nullptr);
    }
};

} // namespace

/* The first turn picks up the scroll's poem; each later one performs a line, and the turn after the
 * last line ends the poem. */
TEST_F(ReciterTest, PerformsEachLineInTurnThenEndsThePoem)
{
    char_data* mob = make_mob("listener", 0);
    give_scroll(mob, RECITER_POEM);

    take_turn(mob);
    EXPECT_STREQ(mob->specials.recite_lines, RECITER_POEM);
    EXPECT_FALSE(output_has("The first line."));

    take_turn(mob);
    EXPECT_TRUE(output_has("The first line.")) << output();
    EXPECT_STREQ(mob->specials.recite_lines, POEM_AFTER_FIRST_LINE);

    take_turn(mob);
    EXPECT_TRUE(output_has("bows.")) << output();

    take_turn(mob);
    EXPECT_EQ(mob->specials.recite_lines, nullptr);
    extract_char(mob);
}

/* The reciter reads its own copy of the poem, so the scroll's shared text is never read mid-poem. */
TEST_F(ReciterTest, ReadsItsOwnCopyOfThePoem)
{
    char_data* mob = make_mob("listener", 0);
    const obj_data* scroll = give_scroll(mob, RECITER_POEM);

    take_turn(mob);

    EXPECT_NE(mob->specials.recite_lines, scroll->ex_description->description);
    extract_char(mob);
}

/* A scroll that goes mid-poem takes its text with it when nothing else shows that text; the reciter
 * carries on from its copy. */
TEST_F(ReciterTest, CarriesOnWhenItsScrollIsGone)
{
    char_data* mob = make_mob("listener", 0);
    obj_data* scroll = give_scroll(mob, RECITER_POEM);
    take_turn(mob);

    extract_obj(scroll);
    take_turn(mob);

    EXPECT_TRUE(output_has("The first line.")) << output();
    extract_char(mob);
}

TEST_F(ReciterTest, RecitingAPoemToItsEndKeepsNoCopy)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }
    char_data* mob = make_mob("listener", 0);
    give_scroll(mob, RECITER_POEM);

    const std::size_t growth = growth_over_rounds([&]() -> void {
        const int turns_in_a_poem = 4;
        for (int turn = 0; turn < turns_in_a_poem; ++turn) {
            take_turn(mob);
        }
        clear_output();
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
    extract_char(mob);
}

TEST_F(ReciterTest, FreeingAReciterMidPoemGivesBackItsCopy)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "this allocator cannot report the bytes in use";
    }

    const std::size_t growth = growth_over_rounds([&]() -> void {
        char_data* mob = make_mob("listener", 0);
        obj_data* scroll = give_scroll(mob, RECITER_POEM);
        take_turn(mob);
        extract_obj(scroll);
        extract_char(mob);
        clear_output();
    });

    EXPECT_LT(growth, ALLOWED_GROWTH);
}
