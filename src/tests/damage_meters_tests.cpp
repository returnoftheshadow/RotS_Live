#include "../damage_meters.h"
#include "../spells.h"
#include "../structs.h"

#include <gtest/gtest.h>
#include <string>

extern struct char_data* combat_list;

namespace {
// Two NPC fighters listed on combat_list for one test, which the context restores afterwards.
class CombatListContext {
public:
    CombatListContext()
    {
        first_fighter.specials2.act = MOB_ISNPC;
        first_fighter.player.short_descr = first_fighter_name;
        second_fighter.specials2.act = MOB_ISNPC;
        second_fighter.player.short_descr = second_fighter_name;

        first_fighter.next_fighting = &second_fighter;
        second_fighter.next_fighting = nullptr;
        previous_combat_list = combat_list;
        combat_list = &first_fighter;
    }

    ~CombatListContext() { combat_list = previous_combat_list; }

    // The head of combat_list.
    char_data first_fighter {};
    // The fighter after first_fighter on combat_list.
    char_data second_fighter {};

private:
    // Storage for first_fighter's short description, which the damage reports print.
    char first_fighter_name[16] = "first_fighter";
    // Storage for second_fighter's short description, which the damage reports print.
    char second_fighter_name[16] = "second_fighter";
    // The combat_list in place before this context replaced it.
    char_data* previous_combat_list = nullptr;
};
} // namespace

TEST(DamageMeters, AddsTheGivenSecondsToEveryFightersPersonalMeter)
{
    CombatListContext context;
    // A personal report shows the combat time only once some damage is recorded.
    context.first_fighter.damage_details.add_damage(TYPE_HIT, 10);
    context.second_fighter.damage_details.add_damage(TYPE_HIT, 10);

    tick_damage_meters(1.25f);
    tick_damage_meters(2.5f);

    const std::string first_report
        = context.first_fighter.damage_details.get_damage_report(&context.first_fighter);
    const std::string second_report
        = context.second_fighter.damage_details.get_damage_report(&context.second_fighter);
    EXPECT_NE(first_report.find("Combat Time: 3.75s"), std::string::npos) << first_report;
    EXPECT_NE(second_report.find("Combat Time: 3.75s"), std::string::npos) << second_report;
}

TEST(DamageMeters, AddsTheGivenSecondsToTheGroupMeter)
{
    CombatListContext context;
    group_data group(&context.first_fighter);
    group.track_damage(&context.first_fighter, 30);

    tick_damage_meters(1.25f);
    tick_damage_meters(2.5f);

    // 30 damage over 1.25 + 2.5 seconds is 8 damage per second.
    const std::string group_report = group.get_damage_report();
    EXPECT_NE(group_report.find("DPS: 8.00"), std::string::npos) << group_report;
}
