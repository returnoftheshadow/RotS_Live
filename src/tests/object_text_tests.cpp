#include "../db.h"
#include "../object_text.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

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
