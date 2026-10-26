#include "../db.h"
#include "../handler.h"
#include "../obj2html.h"
#include "../object_skins.h"
#include "../structs.h"
#include "../utils.h"
#include "test_cleanup.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

void clear_object(struct obj_data* obj);
void name_to_drinkcon(struct obj_data* obj, int type);
void name_from_drinkcon(struct obj_data* obj);
int obj2html_skin(FILE* f, const struct obj_data* o, const struct obj_data* parent);
int obj2html(FILE* f, struct obj_data* o);
int obj2html_problem_table(FILE* f);

extern struct index_data* obj_index;
extern struct obj_data* obj_proto;
extern int top_of_objt;

namespace {
const int kBase = 4512, kSkinOwn = 4513, kSkin = 4514, kKey = 4600, kKeySkin = 4601;
}

class ObjectSkins : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_index = obj_index;
        m_saved_proto = obj_proto;
        m_saved_top = top_of_objt;
        const int vnums[5] = { kBase, kSkinOwn, kSkin, kKey, kKeySkin };
        for (int i = 0; i < 5; ++i) {
            m_index[i].virt = vnums[i];
            clear_object(&m_proto[i]);
            m_proto[i].item_number = i;
            m_proto[i].short_description = m_text[i];
            snprintf(m_text[i], sizeof(m_text[i]), "text %d", vnums[i]);
        }
        obj_data& base = m_proto[0];
        base.obj_flags.type_flag = ITEM_WEAPON;
        base.obj_flags.cost = 100;
        base.obj_flags.material = 4;
        base.obj_flags.weight = 5;
        base.obj_flags.value[0] = 7;
        base.obj_flags.value[3] = 2;
        base.obj_flags.version = 3;
        base.affected[0].location = APPLY_OB;
        base.affected[0].modifier = 2;
        m_proto[1].obj_flags.skin_base = kBase;
        m_proto[1].obj_flags.skin_own = SKIN_OWN_COST;
        m_proto[1].obj_flags.cost = 250;
        m_proto[2].obj_flags.skin_base = kBase;
        m_proto[3].obj_flags.type_flag = ITEM_KEY;
        m_proto[4].obj_flags.skin_base = kKey;
        m_proto[4].obj_flags.weight = 1;
        obj_index = m_index;
        obj_proto = m_proto;
        top_of_objt = 4;
        clear_char(&m_ch, MOB_VOID);
    }
    void TearDown() override
    {
        for (int slot = 0; slot < MAX_WEAR; ++slot)
            if (m_ch.equipment[slot])
                extract_obj(m_ch.equipment[slot]);
        while (m_ch.carrying)
            extract_obj(m_ch.carrying);
        for (obj_data* obj : m_loose)
            extract_obj(obj);
        obj_index = m_saved_index;
        obj_proto = m_saved_proto;
        top_of_objt = m_saved_top;
        release_cleared_character(&m_ch);
    }
    int rn(int vnum) { return real_object(vnum); }
    /* A live copy, the way the game loads one. */
    obj_data* load(int vnum) { return read_object(rn(vnum), REAL); }

    char_data m_ch {};
    std::vector<obj_data*> m_loose; /* live copies on no character, extracted in TearDown */

    index_data m_index[5] {};
    obj_data m_proto[5] {};
    char m_text[5][32] {};
    index_data* m_saved_index;
    obj_data* m_saved_proto;
    int m_saved_top;
};

TEST_F(ObjectSkins, CopyTakesEveryStatButText)
{
    obj_data& skin = m_proto[2];
    const char* text = skin.short_description;
    copy_skin_stats_from_base(skin, m_proto[0]);
    EXPECT_EQ(skin.obj_flags.value[0], 7);
    EXPECT_EQ(skin.obj_flags.value[3], 2);
    EXPECT_EQ(skin.obj_flags.weight, 5);
    EXPECT_EQ(skin.affected[0].location, APPLY_OB);
    EXPECT_EQ(skin.affected[0].modifier, 2);
    EXPECT_EQ(skin.obj_flags.version, 3);
    EXPECT_EQ(skin.obj_flags.cost, 100);
    EXPECT_EQ(skin.obj_flags.material, 4);
    EXPECT_EQ(skin.obj_flags.type_flag, ITEM_WEAPON);
    EXPECT_EQ(skin.short_description, text);
    EXPECT_EQ(skin.obj_flags.skin_base, kBase);
}

TEST_F(ObjectSkins, OwnCostAndMaterialSurviveCopy)
{
    obj_data& skin = m_proto[1];
    copy_skin_stats_from_base(skin, m_proto[0]);
    EXPECT_EQ(skin.obj_flags.cost, 250);
    EXPECT_EQ(skin.obj_flags.material, 4);
    skin.obj_flags.skin_own |= SKIN_OWN_MATERIAL;
    skin.obj_flags.material = 1;
    copy_skin_stats_from_base(skin, m_proto[0]);
    EXPECT_EQ(skin.obj_flags.material, 1);
    EXPECT_EQ(skin.obj_flags.skin_own, SKIN_OWN_COST | SKIN_OWN_MATERIAL);
}

TEST_F(ObjectSkins, BaseProblems)
{
    EXPECT_EQ(skin_base_problem(kSkin, kBase), "");
    EXPECT_EQ(skin_base_problem(kKeySkin, kKey), "base obj 4600 is a key");
    EXPECT_EQ(skin_base_problem(kSkin, 9999), "base obj 9999 not found");
    EXPECT_EQ(skin_base_problem(kSkin, kSkinOwn), "base obj 4513 is a skin of obj 4512");
    EXPECT_EQ(skin_base_problem(kBase, kBase), "an object can't be its own base");
    m_proto[3].obj_flags.type_flag = ITEM_NOTE;
    EXPECT_EQ(skin_base_problem(kKeySkin, kKey), "base obj 4600 is a note");
    EXPECT_EQ(check_skin_base(kKeySkin, kKey), skin_base_rule::base_is_note);
}

TEST_F(ObjectSkins, BootPassDemotesBadSkins)
{
    apply_object_skins();
    EXPECT_EQ(m_proto[4].obj_flags.skin_base, 0);
    EXPECT_EQ(m_proto[4].obj_flags.weight, 1);
    EXPECT_EQ(m_proto[1].obj_flags.skin_base, kBase);
    EXPECT_EQ(m_proto[1].obj_flags.weight, 5);
    EXPECT_EQ(m_proto[1].obj_flags.cost, 250);
    EXPECT_EQ(m_proto[2].obj_flags.weight, 5);
    EXPECT_EQ(m_proto[2].obj_flags.cost, 100);
    EXPECT_EQ(m_proto[2].obj_flags.version, 3);
}

TEST_F(ObjectSkins, FamilyAndList)
{
    EXPECT_EQ(skin_family_rnum(rn(kSkinOwn)), rn(kBase));
    EXPECT_EQ(skin_family_rnum(rn(kSkin)), rn(kBase));
    EXPECT_EQ(skin_family_rnum(rn(kKey)), rn(kKey));
    EXPECT_EQ(skin_family_rnum(-1), -1);
    std::vector<int> expect { rn(kSkinOwn), rn(kSkin) };
    EXPECT_EQ(skin_rnums_of(rn(kBase)), expect);
}

TEST_F(ObjectSkins, RefreshSkinsOfBase)
{
    m_proto[0].obj_flags.weight = 9;
    refresh_skins_of_base(rn(kBase));
    EXPECT_EQ(m_proto[1].obj_flags.weight, 9);
    EXPECT_EQ(m_proto[2].obj_flags.weight, 9);
}

TEST_F(ObjectSkins, RefreshSkipsInvalidSkin)
{
    m_proto[3].obj_flags.weight = 9;
    m_proto[3].obj_flags.value[0] = 42;
    refresh_skins_of_base(rn(kKey));
    EXPECT_EQ(m_proto[4].obj_flags.weight, 1) << "a key's stats never reach a skin";
    EXPECT_EQ(m_proto[4].obj_flags.value[0], 0);
    EXPECT_EQ(m_proto[4].obj_flags.type_flag, 0);
}

TEST_F(ObjectSkins, LevelLineReader)
{
    FILE* f = tmpfile();
    ASSERT_NE(f, nullptr);
    std::fputs("\n\r12 0 4 0 3 4512 1\n\rE\n5 0 1 0\n\r#4600\n7 1 2 3 4\n\r#1\n", f);
    std::rewind(f);
    int nums[7];
    char tok[64];

    EXPECT_EQ(read_object_level_line(f, nums), 7);
    const int want7[7] = { 12, 0, 4, 0, 3, 4512, 1 };
    for (int i = 0; i < 7; i++)
        EXPECT_EQ(nums[i], want7[i]);
    ASSERT_EQ(std::fscanf(f, " %63s", tok), 1);
    EXPECT_STREQ(tok, "E");

    EXPECT_EQ(read_object_level_line(f, nums), 4);
    EXPECT_EQ(nums[3], 0);
    EXPECT_EQ(nums[4], 0);
    EXPECT_EQ(nums[5], 0);
    EXPECT_EQ(nums[6], 0);
    ASSERT_EQ(std::fscanf(f, " %63s", tok), 1);
    EXPECT_STREQ(tok, "#4600");

    EXPECT_EQ(read_object_level_line(f, nums), 5);
    EXPECT_EQ(nums[4], 4);
    EXPECT_EQ(nums[5], 0);
    ASSERT_EQ(std::fscanf(f, " %63s", tok), 1);
    EXPECT_STREQ(tok, "#1");
    std::fclose(f);
}

/* ---- restyle_object: change a live copy, in place, into another look of its family ---- */

TEST_F(ObjectSkins, RestyleInInventoryKeepsPosition)
{
    apply_object_skins();
    obj_data* a = load(kKey);
    obj_data* sword = load(kBase);
    obj_data* b = load(kKey);
    obj_to_char(b, &m_ch);
    obj_to_char(sword, &m_ch);
    obj_to_char(a, &m_ch); /* carrying is [a, sword, b] */
    int weight = IS_CARRYING_W(&m_ch);
    int count = IS_CARRYING_N(&m_ch);

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(sword, rn(kSkin), &n), restyle_result::ok);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->item_number, rn(kSkin));
    EXPECT_EQ(n->short_description, m_proto[2].short_description);
    EXPECT_EQ(m_ch.carrying, a);
    EXPECT_EQ(a->next_content, n);
    EXPECT_EQ(n->next_content, b);
    EXPECT_EQ(b->next_content, nullptr);
    EXPECT_EQ(n->carried_by, &m_ch);
    EXPECT_EQ(n->in_room, NOWHERE);
    EXPECT_EQ(n->in_obj, nullptr);
    EXPECT_EQ(IS_CARRYING_W(&m_ch), weight);
    EXPECT_EQ(IS_CARRYING_N(&m_ch), count);
}

TEST_F(ObjectSkins, RestyleWornKeepsSlotAndStats)
{
    apply_object_skins();
    obj_data* sword = load(kBase);
    /* the attack-speed sums divide by these */
    m_ch.abilities.str = m_ch.tmpabilities.str = m_ch.constabilities.str = 20;
    m_ch.abilities.dex = m_ch.tmpabilities.dex = m_ch.constabilities.dex = 20;
    equip_char(&m_ch, sword, WIELD);
    int ob = GET_OB(&m_ch);
    int weight = IS_CARRYING_W(&m_ch);
    int worn = GET_WORN_WEIGHT(&m_ch);

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(sword, rn(kSkinOwn), &n), restyle_result::ok);
    EXPECT_EQ(m_ch.equipment[WIELD], n);
    EXPECT_EQ(n->carried_by, &m_ch);
    EXPECT_EQ(m_ch.carrying, nullptr) << "a worn object is not in the inventory";
    EXPECT_EQ(n->obj_flags.value[0], 7);
    EXPECT_EQ(n->affected[0].location, APPLY_OB);
    EXPECT_EQ(n->affected[0].modifier, 2);
    EXPECT_EQ(GET_OB(&m_ch), ob);
    EXPECT_EQ(IS_CARRYING_W(&m_ch), weight);
    EXPECT_EQ(GET_WORN_WEIGHT(&m_ch), worn);
}

TEST_F(ObjectSkins, RestyleKeepsStateTakesCostMaterial)
{
    apply_object_skins();
    obj_data* sword = load(kBase);
    sword->enchants[0].type = ENCHANT_OB;
    sword->enchants[0].align = ENCHANT_ALIGN_GOOD;
    sword->enchants[0].amount = 3;
    sword->affected[0].modifier = 5;
    SET_BIT(sword->obj_flags.extra_flags, ITEM_BROKEN);
    sword->obj_flags.timer = 17;
    sword->obj_flags.material = 9;
    sword->loaded_by = 42;
    sword->owner = 7;
    sword->touched = 1;

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(sword, rn(kSkinOwn), &n), restyle_result::ok);
    m_loose.push_back(n);
    EXPECT_EQ(n->enchants[0].type, ENCHANT_OB);
    EXPECT_EQ(n->enchants[0].align, ENCHANT_ALIGN_GOOD);
    EXPECT_EQ(n->enchants[0].amount, 3);
    EXPECT_EQ(n->affected[0].modifier, 5);
    EXPECT_TRUE(IS_SET(n->obj_flags.extra_flags, ITEM_BROKEN));
    EXPECT_EQ(n->obj_flags.timer, 17);
    EXPECT_EQ(n->loaded_by, 42);
    EXPECT_EQ(n->owner, 7);
    EXPECT_EQ(n->touched, 1);
    EXPECT_EQ(n->obj_flags.cost, 250) << "4513 keeps its own cost";
    EXPECT_EQ(n->obj_flags.material, 4) << "material comes from the target prototype";
    EXPECT_EQ(n->obj_flags.skin_base, kBase);
    EXPECT_EQ(n->obj_flags.skin_own, SKIN_OWN_COST);
    EXPECT_EQ(n->in_room, NOWHERE);
    EXPECT_EQ(n->carried_by, nullptr);
    EXPECT_EQ(n->in_obj, nullptr);

    /* and back to the base: the base's cost again */
    obj_data* back = nullptr;
    ASSERT_EQ(restyle_object(n, rn(kBase), &back), restyle_result::ok);
    m_loose.back() = back;
    EXPECT_EQ(back->obj_flags.cost, 100);
    EXPECT_EQ(back->obj_flags.skin_base, 0);
    EXPECT_EQ(back->obj_flags.skin_own, 0);
    EXPECT_EQ(back->enchants[0].amount, 3);
}

TEST_F(ObjectSkins, RestyleMovesContents)
{
    apply_object_skins();
    obj_data* bag = load(kBase);
    obj_data* c1 = load(kKey);
    obj_data* c2 = load(kKey);
    obj_to_obj(c1, bag, FALSE);
    obj_to_obj(c2, bag, FALSE); /* contains is [c2, c1] */
    EXPECT_EQ(obj_index[rn(kBase)].number, 1);
    EXPECT_EQ(obj_index[rn(kSkin)].number, 0);

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(bag, rn(kSkin), &n), restyle_result::ok);
    m_loose.push_back(n);
    EXPECT_EQ(n->contains, c2);
    EXPECT_EQ(c2->next_content, c1);
    EXPECT_EQ(c1->next_content, nullptr);
    EXPECT_EQ(c1->in_obj, n);
    EXPECT_EQ(c2->in_obj, n);
    EXPECT_EQ(obj_index[rn(kBase)].number, 0);
    EXPECT_EQ(obj_index[rn(kSkin)].number, 1);
    EXPECT_EQ(obj_index[rn(kKey)].number, 2) << "the contents are not reloaded";
}

TEST_F(ObjectSkins, RestyleInsideContainerKeepsPosition)
{
    apply_object_skins();
    obj_data* bag = load(kKey);
    obj_data* a = load(kKey);
    obj_data* sword = load(kBase);
    obj_data* b = load(kKey);
    obj_to_obj(b, bag, FALSE);
    obj_to_obj(sword, bag, FALSE);
    obj_to_obj(a, bag, FALSE); /* contains is [a, sword, b] */
    m_loose.push_back(bag);

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(sword, rn(kSkin), &n), restyle_result::ok);
    EXPECT_EQ(bag->contains, a);
    EXPECT_EQ(a->next_content, n);
    EXPECT_EQ(n->next_content, b);
    EXPECT_EQ(n->in_obj, bag);
    EXPECT_EQ(n->carried_by, nullptr);
    EXPECT_EQ(n->in_room, NOWHERE);
}

TEST_F(ObjectSkins, RestyleRefusals)
{
    apply_object_skins();
    obj_data* sword = load(kBase);
    obj_to_char(sword, &m_ch);
    obj_data sentinel;
    obj_data* out = &sentinel;

    EXPECT_EQ(restyle_object(sword, -1, &out), restyle_result::no_such_object);
    EXPECT_EQ(restyle_object(sword, top_of_objt + 1, &out), restyle_result::no_such_object);
    EXPECT_EQ(restyle_object(sword, rn(kBase), &out), restyle_result::same_object);
    EXPECT_EQ(restyle_object(sword, rn(kKey), &out), restyle_result::not_same_family);
    EXPECT_EQ(restyle_object(sword, rn(kKeySkin), &out), restyle_result::not_same_family)
        << "a skin of a key is not a skin";

    obj_data loose;
    clear_object(&loose);
    EXPECT_EQ(restyle_object(&loose, rn(kSkin), &out), restyle_result::not_a_prototype);

    EXPECT_EQ(out, &sentinel);
    EXPECT_EQ(m_ch.carrying, sword);
    EXPECT_EQ(sword->next_content, nullptr);
    EXPECT_EQ(sword->carried_by, &m_ch);
    EXPECT_EQ(sword->item_number, rn(kBase));
    EXPECT_EQ(obj_index[rn(kBase)].number, 1);
    EXPECT_EQ(obj_index[rn(kSkin)].number, 0);
    EXPECT_EQ(obj_index[rn(kKeySkin)].number, 0);
}

/* Object triggers keep using their object after the script ran, so a restyle changes the object
 * where it is instead of swapping in a new one. */
TEST_F(ObjectSkins, RestyleKeepsTheSameObject)
{
    apply_object_skins();
    obj_data* sword = load(kBase);
    obj_to_char(sword, &m_ch);

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(sword, rn(kSkin), &n), restyle_result::ok);
    EXPECT_EQ(n, sword);
    EXPECT_EQ(sword->item_number, rn(kSkin));
    EXPECT_EQ(sword->short_description, m_proto[2].short_description);
    EXPECT_EQ(m_ch.carrying, sword);
    EXPECT_EQ(obj_index[rn(kBase)].number, 0);
    EXPECT_EQ(obj_index[rn(kSkin)].number, 1);
}

/* Every text field points at the target prototype's text afterwards. */
TEST_F(ObjectSkins, RestyleTakesAllTextOfTarget)
{
    static char name[2][16] = { "sword long", "blade" }, desc[2][16] = { "lies", "rests" },
                act[2][16] = { "act a", "act b" };
    /* Static like the texts: the sword still holds one when TearDown frees it. */
    static extra_descr_data ex[2] {};
    for (int i = 0; i < 2; i++) {
        obj_data& p = m_proto[i == 0 ? 0 : 2];
        p.name = name[i];
        p.description = desc[i];
        p.action_description = act[i];
        p.ex_description = &ex[i];
    }
    apply_object_skins();
    obj_data* sword = load(kBase);
    obj_to_char(sword, &m_ch);

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(sword, rn(kSkin), &n), restyle_result::ok);
    EXPECT_EQ(sword->name, m_proto[2].name);
    EXPECT_EQ(sword->short_description, m_proto[2].short_description);
    EXPECT_EQ(sword->description, m_proto[2].description);
    EXPECT_EQ(sword->action_description, m_proto[2].action_description);
    EXPECT_EQ(sword->ex_description, m_proto[2].ex_description);
}

/* A filled drink container's name starts with its liquid ("water flask"): the new name keeps the
 * liquid word, so emptying it later strips the liquid, not a real keyword. */
TEST_F(ObjectSkins, RestyleFilledDrinkKeepsLiquidWord)
{
    static char flask[] = "flask", jug[] = "jug clay";
    m_proto[0].obj_flags.type_flag = ITEM_DRINKCON;
    m_proto[0].obj_flags.value[0] = 10;
    m_proto[0].name = flask;
    m_proto[2].name = jug;
    apply_object_skins();
    obj_data* drink = load(kBase);
    obj_to_char(drink, &m_ch);
    name_to_drinkcon(drink, 0);
    drink->obj_flags.value[1] = 5;
    ASSERT_STREQ(drink->name, "water flask");

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(drink, rn(kSkin), &n), restyle_result::ok);
    EXPECT_STREQ(drink->name, "water jug clay");
    EXPECT_NE(drink->name, m_proto[2].name) << "the copy owns its liquid name";
    EXPECT_TRUE(drink->owns_text(object_text::name));

    name_from_drinkcon(drink);
    EXPECT_STREQ(drink->name, "jug clay");
}

/* An emptied container (the liquid word stripped off) of an empty prototype takes the target's
 * name as it is. */
TEST_F(ObjectSkins, RestyleEmptiedDrinkTakesTargetName)
{
    static char flask[] = "flask", jug[] = "jug clay";
    m_proto[0].obj_flags.type_flag = ITEM_DRINKCON;
    m_proto[0].name = flask;
    m_proto[2].name = jug;
    apply_object_skins();
    obj_data* drink = load(kBase);
    obj_to_char(drink, &m_ch);
    name_to_drinkcon(drink, 0);
    name_from_drinkcon(drink);
    ASSERT_NE(drink->name, m_proto[0].name);

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(drink, rn(kSkin), &n), restyle_result::ok);
    EXPECT_EQ(drink->name, m_proto[2].name);
    EXPECT_FALSE(drink->owns_text(object_text::name)) << "the target's name is borrowed";
}

/* Prototypes that start full have the liquid as their first name word ("water flask leather").
 * The target's liquid word is dropped and the copy's own liquid goes in front only when the copy
 * is full, the way filling and emptying build the name. */
TEST_F(ObjectSkins, RestyleDrinkFromFullPrototypes)
{
    static char flask[] = "water flask leather", jug[] = "water jug clay";
    m_proto[0].obj_flags.type_flag = ITEM_DRINKCON;
    m_proto[0].obj_flags.value[0] = 10;
    m_proto[0].obj_flags.value[1] = 10;
    m_proto[0].name = flask;
    m_proto[2].name = jug;
    apply_object_skins();
    obj_data* n = nullptr;

    obj_data* full = load(kBase); /* still the prototype's water */
    obj_to_char(full, &m_ch);
    ASSERT_EQ(restyle_object(full, rn(kSkin), &n), restyle_result::ok);
    EXPECT_STREQ(full->name, "water jug clay");

    obj_data* empty = load(kBase);
    obj_to_char(empty, &m_ch);
    name_from_drinkcon(empty);
    empty->obj_flags.value[1] = 0;
    ASSERT_STREQ(empty->name, "flask leather");
    ASSERT_EQ(restyle_object(empty, rn(kSkin), &n), restyle_result::ok);
    EXPECT_STREQ(empty->name, "jug clay");

    obj_data* wine = load(kBase);
    obj_to_char(wine, &m_ch);
    name_from_drinkcon(wine);
    name_to_drinkcon(wine, 2);
    wine->obj_flags.value[2] = 2;
    ASSERT_STREQ(wine->name, "wine flask leather");
    ASSERT_EQ(restyle_object(wine, rn(kSkin), &n), restyle_result::ok);
    EXPECT_STREQ(wine->name, "wine jug clay");
    name_from_drinkcon(wine);
    EXPECT_STREQ(wine->name, "jug clay");
}

/* Filling and emptying a copy over and over frees each name it made, never its prototype's (a
 * static buffer here, so a free of it would abort). */
TEST_F(ObjectSkins, DrinkNameCyclesFreeOnlyNamesTheCopyMade)
{
    static char flask[] = "flask";
    m_proto[0].obj_flags.type_flag = ITEM_DRINKCON;
    m_proto[0].name = flask;
    obj_data* drink = load(kBase);
    obj_to_char(drink, &m_ch);
    EXPECT_FALSE(drink->owns_text(object_text::name));

    for (int cycle = 0; cycle < 3; ++cycle) {
        name_to_drinkcon(drink, 0);
        EXPECT_STREQ(drink->name, "water flask");
        EXPECT_TRUE(drink->owns_text(object_text::name));
        name_from_drinkcon(drink);
        EXPECT_STREQ(drink->name, "flask");
        EXPECT_NE(drink->name, m_proto[0].name);
    }
    EXPECT_STREQ(m_proto[0].name, "flask");
}

/* After an implement a copy can still point at the prototype's old text, which other copies share
 * and which the copy does not own: the restyle must not free it (a static buffer here, so a free
 * would abort). */
TEST_F(ObjectSkins, RestyleLeavesSharedOldTextAlone)
{
    static char old_name[] = "old sword", old_short[] = "an old sword", sword_name[] = "sword";
    m_proto[0].name = sword_name;
    apply_object_skins();
    obj_data* sword = load(kBase);
    obj_to_char(sword, &m_ch);
    sword->name = old_name;
    sword->short_description = old_short;

    obj_data* n = nullptr;
    ASSERT_EQ(restyle_object(sword, rn(kSkin), &n), restyle_result::ok);
    EXPECT_EQ(sword->name, m_proto[2].name);
    EXPECT_EQ(sword->short_description, m_proto[2].short_description);
    EXPECT_STREQ(old_name, "old sword");
}

TEST_F(ObjectSkins, LevelLineReaderLeavesNonNumericLine)
{
    const char* streams[] = { "\n\r#2310\nfoo~\n", "\n\rE\nkw~\n" };
    const char* tokens[] = { "#2310", "E" };
    for (int i = 0; i < 2; i++) {
        FILE* f = tmpfile();
        ASSERT_NE(f, nullptr);
        std::fputs(streams[i], f);
        std::rewind(f);
        int nums[7] = { 9, 9, 9, 9, 9, 9, 9 };
        char tok[64];
        EXPECT_EQ(read_object_level_line(f, nums), 0);
        for (int n : nums)
            EXPECT_EQ(n, 0);
        ASSERT_EQ(std::fscanf(f, " %63s", tok), 1);
        EXPECT_STREQ(tok, tokens[i]);
        std::fclose(f);
    }
}

/* obj2html writes a skin as a row under its parent. Only the cells a skin can set are filled:
 * vnum, name (with the parent's vnum), cost and material; the parent's stats stay blank. */
TEST_F(ObjectSkins, Obj2htmlSkinRowFillsOnlySkinCells)
{
    extern char* object_materials[];
    copy_skin_stats_from_base(m_proto[1], m_proto[0]);
    copy_skin_stats_from_base(m_proto[2], m_proto[0]);
    for (int i = 1; i <= 2; i++) {
        FILE* f = tmpfile();
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(obj2html_skin(f, &m_proto[i], &m_proto[0]), m_index[i].virt);
        std::rewind(f);
        std::string row;
        char chunk[256];
        while (std::fgets(chunk, sizeof chunk, f))
            row += chunk;
        std::fclose(f);
        char want[128];
        snprintf(want, sizeof want, "<TD ALIGN=LEFT> &nbsp;&nbsp;- text %d <I>(#%d)</I> </TD>", m_index[i].virt, kBase);
        EXPECT_NE(row.find(want), std::string::npos) << row;
        snprintf(want, sizeof want, "<TD> %d </TD>", m_proto[i].obj_flags.cost);
        EXPECT_NE(row.find(want), std::string::npos) << row;
        snprintf(want, sizeof want, "<TD> %s </TD>", object_materials[4]);
        EXPECT_NE(row.find(want), std::string::npos) << row;
        EXPECT_EQ(row.find("<TD> 5 </TD>"), std::string::npos) << row; /* parent's weight */
        EXPECT_EQ(row.find("<TD> 7 </TD>"), std::string::npos) << row; /* parent's OB */
        EXPECT_EQ(row.find("+2"), std::string::npos) << row; /* parent's affection */
        /* vnum, name, 5 weapon cells, weight, cost, level, material, affections */
        size_t cells = 0;
        for (size_t at = row.find("<TD"); at != std::string::npos; at = row.find("<TD", at + 1))
            ++cells;
        EXPECT_EQ(cells, 12u) << row;
    }
}

namespace {
std::string read_back(FILE* f)
{
    std::rewind(f);
    std::string out;
    char chunk[256];
    while (std::fgets(chunk, sizeof chunk, f))
        out += chunk;
    std::fclose(f);
    return out;
}
}

/* A keyring skin's row has as many cells as its parent's keyring row (one type cell, its
 * capacity), so the columns of the keyring table line up. */
TEST_F(ObjectSkins, Obj2htmlKeyringSkinRowMatchesParentRowCells)
{
    m_proto[0].obj_flags.type_flag = ITEM_KEYRING;
    m_proto[0].obj_flags.value[0] = 500;
    copy_skin_stats_from_base(m_proto[1], m_proto[0]);
    m_proto[1].obj_flags.type_flag = ITEM_KEYRING;
    auto cells = [](const std::string& row) {
        size_t n = 0;
        for (size_t at = row.find("<TD"); at != std::string::npos; at = row.find("<TD", at + 1))
            ++n;
        return n;
    };
    FILE* parent = tmpfile();
    ASSERT_NE(parent, nullptr);
    obj2html(parent, &m_proto[0]);
    FILE* skin = tmpfile();
    ASSERT_NE(skin, nullptr);
    obj2html_skin(skin, &m_proto[1], &m_proto[0]);
    std::string parent_row = read_back(parent);
    std::string skin_row = read_back(skin);
    EXPECT_NE(parent_row.find("<TD> 500 </TD>"), std::string::npos) << parent_row;
    EXPECT_EQ(cells(skin_row), cells(parent_row)) << parent_row << skin_row;
}

/* A lever with a bad direction, or a bad material, is written with the number in red (not read
 * past dirs[] or the material names). It is in the lever table already, so the problem table at
 * the end, which is for objects no table lists, leaves it out. */
TEST_F(ObjectSkins, Obj2htmlBadLeverDirectionAndMaterialAreShownInRed)
{
    obj_data& lever = m_proto[3];
    lever.obj_flags.type_flag = ITEM_LEVER;
    lever.obj_flags.value[1] = 2;
    FILE* f = tmpfile();
    ASSERT_NE(f, nullptr);
    obj2html(f, &lever);
    lever.obj_flags.value[1] = 9;
    obj2html(f, &lever);
    lever.obj_flags.material = 99;
    obj2html(f, &lever);
    std::string rows = read_back(f);
    EXPECT_NE(rows.find("<TD> south </TD>"), std::string::npos) << rows;
    EXPECT_NE(rows.find("<FONT COLOR=RED> 9 </FONT>"), std::string::npos) << rows;
    EXPECT_NE(rows.find("<FONT COLOR=RED> 99 </FONT>"), std::string::npos) << rows;
    /* every row has the same cells, a bad material included: vnum, name, room, direction,
     * weight, value, level, material, affections */
    size_t cells = 0;
    for (size_t at = rows.find("<TD"); at != std::string::npos; at = rows.find("<TD", at + 1))
        ++cells;
    EXPECT_EQ(cells, 27u) << rows;

    f = tmpfile();
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(obj2html_problem_table(f), 0);
    EXPECT_EQ(read_back(f).find("Problem Objects"), std::string::npos);
}

/* obj2html's type words: a type named at a level wins there, a type with a named type below it
 * comes next, and a level with neither takes everything. */
TEST(Obj2html, TypeWordsSelectTablesAndSubtypes)
{
    obj2html_type* weapons = &obj2html_types[ITEM_WEAPON - 1];
    obj2html_type* kinds = weapons->subtypes; /* [0] whips, [1] slashing, [2] piercing */
    obj2html_type* slash_hands = kinds[1].subtypes; /* [0] two-handed, [1] one-handed */
    obj2html_type* pierce_hands = kinds[2].subtypes;
    auto pick = [](std::initializer_list<const char*> words) {
        obj2html_clear(obj2html_types);
        for (const char* w : words)
            check_keywords(obj2html_types, w);
    };

    pick({ "weapon" });
    EXPECT_TRUE(obj2html_taken(obj2html_types, ITEM_WEAPON - 1));
    EXPECT_FALSE(obj2html_taken(obj2html_types, ITEM_DRINKCON - 1));
    EXPECT_TRUE(obj2html_taken(kinds, 1));
    EXPECT_TRUE(obj2html_taken(kinds, 2));
    EXPECT_TRUE(obj2html_taken(slash_hands, 0));
    EXPECT_TRUE(obj2html_taken(slash_hands, 1));

    pick({ "slash", "2h" });
    EXPECT_TRUE(obj2html_taken(obj2html_types, ITEM_WEAPON - 1));
    EXPECT_TRUE(obj2html_taken(kinds, 1));
    EXPECT_FALSE(obj2html_taken(kinds, 2)) << "two-handed slashing only, not every two-handed weapon";
    EXPECT_TRUE(obj2html_taken(slash_hands, 0));
    EXPECT_FALSE(obj2html_taken(slash_hands, 1));

    pick({ "2h" });
    EXPECT_TRUE(obj2html_taken(obj2html_types, ITEM_WEAPON - 1));
    EXPECT_TRUE(obj2html_taken(kinds, 1));
    EXPECT_TRUE(obj2html_taken(kinds, 2));
    EXPECT_TRUE(obj2html_taken(pierce_hands, 0));
    EXPECT_FALSE(obj2html_taken(pierce_hands, 1));

    pick({ "drinkcon" });
    EXPECT_FALSE(obj2html_taken(obj2html_types, ITEM_WEAPON - 1));
    EXPECT_TRUE(obj2html_taken(obj2html_types, ITEM_DRINKCON - 1));
    obj2html_clear(obj2html_types);
}
