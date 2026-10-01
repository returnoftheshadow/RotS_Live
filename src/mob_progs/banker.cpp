#include "banker.h"

#include "../account_management_identity.h"
#include "../account_management_storage.h"
#include "../comm.h"
#include "../db.h"
#include "../game_boot_options.h"
#include "../handler.h"
#include "../interpre.h"
#include "../json_utils.h"
#include "../roster_cache.h"
#include "../structs.h"
#include "../utils.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern struct time_info_data time_info;
extern int no_specials;
extern int generic_scalp;

int get_number(char** name);

int Crash_is_unrentable(struct obj_data* obj);
struct obj_data* Crash_obj2char(struct char_data* ch, struct obj_file_elem* object);

using mob_options_detail::split_lines;
using mob_options_detail::trim;

namespace {

bool parse_number(const std::string& s, int* out)
{
    if (s.empty() || s.size() > 9)
        return false;
    for (char c : s)
        if (!isdigit((unsigned char)c))
            return false;
    *out = atoi(s.c_str());
    return true;
}

/* Days since 1970-01-01 for a calendar date (proleptic Gregorian). */
long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

/* The bank day a moment falls in: its local date, or the day before when
 * the local hour is still short of the start hour. */
long bank_day_index(time_t when, int start_hour)
{
    struct tm local { };
    localtime_r(&when, &local);
    long day = days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    if (local.tm_hour < start_hour)
        --day;
    return day;
}

} // namespace

banker_config parse_banker_options(const char* text, std::vector<vendor_problem>* problems)
{
    banker_config config;
    bool saw_hours = false, saw_fee = false, saw_maxdays = false, saw_markup = false;
    auto problem = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what });
    };
    auto strict = [&](int line, const char* key) {
        config.ok = false;
        problem(line, std::string("bad ") + key + " - banker disabled");
    };

    int line_no = 0;
    for (const std::string& raw : split_lines(text ? text : "")) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0)
            continue;
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));
        bool* seen = key == "hours" ? &saw_hours : key == "fee" ? &saw_fee
            : key == "maxdays"                                  ? &saw_maxdays
            : key == "racial_markup"                            ? &saw_markup
                                                                : nullptr;
        if (!seen || eq == std::string::npos) {
            problem(line_no, "unknown setting - line ignored");
            continue;
        }
        if (*seen) {
            problem(line_no, "duplicate " + key + " - line ignored");
            continue;
        }
        *seen = true;
        int number = 0;
        if (key == "hours") {
            if (!vendor_hours_parse(value, &config.hours)) {
                config.hours.clear();
                strict(line_no, "hours");
            }
        } else if (key == "fee") {
            if (!parse_number(value, &number) || number < 1 || number > BANKER_FEE_MAX)
                strict(line_no, "fee");
            else
                config.fee = number;
        } else if (key == "maxdays") {
            if (!parse_number(value, &number) || number < 1 || number > BANKER_MAXDAYS_MAX)
                strict(line_no, "maxdays");
            else
                config.maxdays = number;
        } else {
            if (value == "yes")
                config.markup = BANKER_MARKUP_DEFAULT;
            else if (!parse_number(value, &number) || number < 1 || number > BANKER_MARKUP_MAX)
                strict(line_no, "racial_markup");
            else
                config.markup = number;
        }
    }
    if (saw_fee && config.fee > 0 && !saw_maxdays) {
        config.ok = false;
        problem(0, "fee without maxdays - banker disabled");
    }
    return config;
}

int bank_side_for_race(int race)
{
    if (race >= RACE_HUMAN && race <= RACE_BEORNING)
        return BANK_SIDE_LIGHT;
    if (race == RACE_URUK || race == RACE_ORC || race == RACE_OLOGHAI)
        return BANK_SIDE_DARK;
    if (race == RACE_MAGUS || race == RACE_HARADRIM)
        return BANK_SIDE_THIRD;
    return BANK_SIDE_NONE;
}

const char* bank_side_file_name(int side)
{
    switch (side) {
    case BANK_SIDE_LIGHT:
        return "vault_light.json";
    case BANK_SIDE_DARK:
        return "vault_dark.json";
    case BANK_SIDE_THIRD:
        return "vault_third.json";
    default:
        return nullptr;
    }
}

int bank_days_stored(time_t deposited, time_t now, int start_hour)
{
    long days = bank_day_index(now, start_hour) - bank_day_index(deposited, start_hour);
    return days < 0 ? 0 : (int)days;
}

long long bank_fee(const banker_config& config, int days, int items, bool other_race)
{
    if (config.fee <= 0 || days <= 0 || items <= 0)
        return 0;
    long long fee = (long long)std::min(days, config.maxdays) * config.fee * items;
    if (other_race && config.markup > 0)
        fee = (fee * (100 + config.markup) + 99) / 100;
    return fee;
}

std::string serialize_bank_vault(const bank_vault& vault)
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"version\": " << BANK_VAULT_SCHEMA_VERSION << ",\n";
    out << "  \"coins\": " << vault.coins << ",\n";
    out << "  \"slots\": [\n";
    for (size_t s = 0; s < vault.slots.size(); ++s) {
        const bank_slot& slot = vault.slots[s];
        out << "    {\n";
        out << "      \"deposited\": " << slot.deposited << ",\n";
        out << "      \"objects\": [\n";
        for (size_t o = 0; o < slot.objects.size(); ++o) {
            objects_json::write_object_record_json(out, slot.objects[o], "        ");
            out << (o + 1 < slot.objects.size() ? ",\n" : "\n");
        }
        out << "      ]\n";
        out << "    }" << (s + 1 < vault.slots.size() ? ",\n" : "\n");
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

bool deserialize_bank_vault(const std::string& json, bank_vault* vault, std::string* error)
{
    using json_utils::JsonReader;
    bank_vault parsed;
    int version = 0;
    bool saw_version = false, saw_coins = false, saw_slots = false;
    JsonReader reader(json);
    bool ok = reader.parse_root_object(
        [&](const std::string& key, JsonReader* r, std::string* e) {
            if (key == "version")
                return saw_version = true, r->parse_integer(&version, e);
            if (key == "coins")
                return saw_coins = true, r->parse_integer(&parsed.coins, e);
            if (key == "slots") {
                saw_slots = true;
                return r->parse_array(
                    [&parsed](JsonReader* slot_reader, std::string* slot_error) {
                        bank_slot slot;
                        bool saw_deposited = false;
                        if (!slot_reader->parse_object(
                                [&slot, &saw_deposited](const std::string& slot_key, JsonReader* sr, std::string* se) {
                                    if (slot_key == "deposited")
                                        return saw_deposited = true, sr->parse_long(&slot.deposited, se);
                                    if (slot_key == "objects")
                                        return sr->parse_array(
                                            [&slot](JsonReader* object_reader, std::string* object_error) {
                                                objects_json::ObjectRecord record;
                                                if (!objects_json::parse_object_record_json(object_reader, &record, object_error))
                                                    return false;
                                                slot.objects.push_back(record);
                                                return true;
                                            },
                                            se);
                                    return sr->skip_value(se);
                                },
                                slot_error))
                            return false;
                        if (!saw_deposited || slot.deposited < 0) {
                            *slot_error = "Vault slot has no valid deposit time.";
                            return false;
                        }
                        if (slot.objects.empty() || slot.objects[0].wear_pos != 0) {
                            *slot_error = "Vault slot has no item at depth 0.";
                            return false;
                        }
                        for (size_t i = 1; i < slot.objects.size(); ++i)
                            if (slot.objects[i].wear_pos < 1 || slot.objects[i].wear_pos > slot.objects[i - 1].wear_pos + 1) {
                                *slot_error = "Vault slot has an impossible nesting depth.";
                                return false;
                            }
                        parsed.slots.push_back(slot);
                        return true;
                    },
                    e);
            }
            return r->skip_value(e);
        },
        error);
    if (!ok) {
        if (error && error->empty())
            *error = "Vault file is not valid JSON.";
        return false;
    }
    if (!saw_version || version != BANK_VAULT_SCHEMA_VERSION) {
        *error = "Vault file has an unknown version.";
        return false;
    }
    if (!saw_coins || parsed.coins < 0 || !saw_slots) {
        *error = "Vault file is missing coins or slots.";
        return false;
    }
    *vault = parsed;
    return true;
}

namespace {

std::function<std::string(const std::string&)> g_directory_resolver;
std::function<void(struct char_data*)> g_character_saver;
std::function<time_t()> g_clock;

/* Keyed "<normalized account name>#<side>". The ONLY copy of each vault. */
std::map<std::string, bank_vault> g_vaults;

std::string game_account_directory(const std::string& account_name)
{
    std::string dir = account::account_character_directory(".", account_name, "");
    struct stat st { };
    if (dir.empty() || stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return "";
    return dir;
}

bool vault_location(const std::string& account_name, int side, std::string* key, std::string* path, std::string* error)
{
    const char* file = bank_side_file_name(side);
    std::string name = account::normalize_account_name(account_name);
    if (!file || name.empty()) {
        *error = "No vault for that account and side.";
        return false;
    }
    std::string dir = g_directory_resolver ? g_directory_resolver(name) : game_account_directory(name);
    if (dir.empty()) {
        *error = "That account has no folder.";
        return false;
    }
    *key = name + "#" + std::to_string(side);
    *path = dir + "/" + file;
    return true;
}

} // namespace

void bank_set_directory_resolver(std::function<std::string(const std::string&)> resolver) { g_directory_resolver = resolver; }
void bank_set_character_saver(std::function<void(struct char_data*)> saver) { g_character_saver = saver; }
void bank_set_clock(std::function<time_t()> clock) { g_clock = clock; }
time_t bank_now() { return g_clock ? g_clock() : time(0); }

void bank_save_character(struct char_data* ch)
{
    if (g_character_saver) {
        g_character_saver(ch);
        return;
    }
    save_char(ch, NOWHERE, 0); /* the same pair do_save runs (act_othe.cpp) */
    Crash_crashsave(ch);
}

void bank_vault_forget_all() { g_vaults.clear(); }

bank_vault* bank_vault_open(const std::string& account_name, int side, std::string* error)
{
    std::string key, path;
    if (!vault_location(account_name, side, &key, &path, error))
        return nullptr;
    auto found = g_vaults.find(key);
    if (found == g_vaults.end()) {
        bank_vault vault;
        std::ifstream in(path, std::ios::binary);
        if (in.good()) {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            std::string read_error;
            if (!deserialize_bank_vault(buffer.str(), &vault, &read_error)) {
                vault = bank_vault();
                vault.readable = false;
                char line[512];
                snprintf(line, sizeof(line), "SYSERR: bank: unreadable vault file %s: %s", path.c_str(), read_error.c_str());
                log(line); /* once: the refused copy stays in the table */
            }
        }
        found = g_vaults.emplace(key, vault).first;
    }
    if (!found->second.readable) {
        *error = "That vault's file can't be read.";
        return nullptr;
    }
    return &found->second;
}

bool bank_vault_write(const std::string& account_name, int side, std::string* error)
{
    std::string key, path;
    if (!vault_location(account_name, side, &key, &path, error))
        return false;
    auto found = g_vaults.find(key);
    if (found == g_vaults.end() || !found->second.readable) {
        *error = "That vault is not open.";
        return false;
    }
    const std::string json = serialize_bank_vault(found->second);
    const std::string temp = path + ".tmp";
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    FILE* file = fd >= 0 ? fdopen(fd, "w") : nullptr;
    if (!file) {
        if (fd >= 0)
            close(fd);
        *error = std::string("Can't write the vault file: ") + strerror(errno);
        return false;
    }
    size_t written = fwrite(json.data(), 1, json.size(), file);
    if (fclose(file) != 0 || written != json.size() || rename(temp.c_str(), path.c_str()) != 0) {
        *error = std::string("Can't write the vault file: ") + strerror(errno);
        remove(temp.c_str());
        return false;
    }
    return true;
}

constexpr size_t BANK_NAME_COLUMN = 37; /* " #  " + 37 + 2 + a 35-column fee = 78 */

std::string format_bank_balance(const std::string& coins, int coin_limit_gold, int slots_used, int slots_max,
    const std::vector<bank_balance_row>& rows, bool show_fee)
{
    std::string out = "Coins: " + coins + " (limit " + std::to_string(coin_limit_gold) + " gold)\n\r";
    out += "Slots: " + std::to_string(slots_used) + " of " + std::to_string(slots_max) + " used\n\r";
    if (rows.empty())
        return out;
    out += "\n\r";
    const size_t fee_column = 4 + BANK_NAME_COLUMN + 2; /* where the fee text starts */
    std::string header = " #  Item";
    if (show_fee) {
        header.append(fee_column - header.size(), ' ');
        header += "Fee to withdraw";
    }
    out += header + "\n\r";
    for (size_t i = 0; i < rows.size(); ++i) {
        char number[8];
        snprintf(number, sizeof(number), "%2d  ", (int)(i + 1));
        std::string line = number; /* 100 and up is one wider: the name gives way */
        size_t width = fee_column - 2 - line.size();
        std::string note = rows[i].inside >= 0 ? " (sealed, " + std::to_string(rows[i].inside) + " inside)" : "";
        std::string name = rows[i].name;
        if (name.size() + note.size() > width)
            name.resize(width - note.size()); /* the note always survives */
        line += name + note;
        if (show_fee) {
            line.append(fee_column - line.size(), ' ');
            line += rows[i].fee;
        }
        out += line + "\n\r";
    }
    return out;
}

bool bank_obj_storable(struct obj_data* obj)
{
    if (Crash_is_unrentable(obj))
        return false;
    for (struct obj_data* inside = obj->contains; inside; inside = inside->next_content)
        if (!bank_obj_storable(inside))
            return false;
    return true;
}

namespace {

/* Mirrors Crash_obj2store (objsave.cpp), with nesting depth in wear_pos. */
void append_record(struct obj_data* obj, int depth, std::vector<objects_json::ObjectRecord>* out)
{
    objects_json::ObjectRecord record;
    record.item_number = obj->item_number >= 0 ? obj_index[obj->item_number].virt : obj->item_number;
    for (int i = 0; i < 5; ++i)
        record.values[i] = obj->obj_flags.value[i];
    record.extra_flags = obj->obj_flags.extra_flags;
    record.weight = obj->obj_flags.weight;
    record.timer = obj->obj_flags.timer;
    record.bitvector = obj->obj_flags.bitvector;
    record.loaded_by = obj->loaded_by;
    for (int i = 0; i < MAX_OBJ_AFFECT; ++i)
        record.affects[i] = { obj->affected[i].location, obj->affected[i].modifier };
    record.wear_pos = depth;
    if (record.item_number == generic_scalp) /* same stash Crash_obj2store uses */
        record.extra_flags = obj->obj_flags.value[4];
    out->push_back(record);
    for (struct obj_data* inside = obj->contains; inside; inside = inside->next_content)
        append_record(inside, depth + 1, out);
}

} // namespace

void bank_records_from_obj(struct obj_data* obj, std::vector<objects_json::ObjectRecord>* out)
{
    append_record(obj, 0, out);
}

struct obj_data* bank_obj_from_records(const std::vector<objects_json::ObjectRecord>& records)
{
    std::vector<struct obj_data*> at_depth;
    for (const objects_json::ObjectRecord& record : records) {
        struct obj_file_elem elem { };
        elem.item_number = record.item_number;
        for (int i = 0; i < 5; ++i)
            elem.value[i] = (sh_int)record.values[i];
        elem.extra_flags = record.extra_flags;
        elem.weight = record.weight;
        elem.timer = record.timer;
        elem.bitvector = record.bitvector;
        elem.loaded_by = record.loaded_by;
        for (int i = 0; i < MAX_OBJ_AFFECT; ++i) {
            elem.affected[i].location = record.affects[i].location;
            elem.affected[i].modifier = record.affects[i].modifier;
        }
        int depth = record.wear_pos;
        struct obj_data* obj = depth >= 0 && depth <= (int)at_depth.size() && (depth == 0) == at_depth.empty()
            ? Crash_obj2char(nullptr, &elem)
            : nullptr;
        if (!obj) { /* prototype gone, or impossible nesting: build nothing */
            if (!at_depth.empty())
                extract_obj(at_depth[0]); /* extract_obj takes the contents with it */
            return nullptr;
        }
        obj->touched = 1;
        /* PR #343: once object versions are merged, refresh `obj` here, the
         * same call Crash_load makes after Crash_obj2char. */
        if (depth > 0)
            obj_to_obj(obj, at_depth[depth - 1], TRUE);
        at_depth.resize(depth);
        at_depth.push_back(obj);
    }
    return at_depth.empty() ? nullptr : at_depth[0];
}

const char* bank_slot_name(const bank_slot& slot)
{
    int rnum = slot.objects.empty() ? -1 : real_object(slot.objects[0].item_number);
    return rnum >= 0 ? obj_proto[rnum].short_description : "something";
}

namespace {

std::unordered_map<int, banker_config> g_banker_configs; /* by mob rnum */
std::set<int> g_banker_disabled_logged; /* by mob rnum; cleared on rebuild */

bool is_banker_proto(int rnum) { return is_banker_candidate(&mob_proto[rnum], rnum); }

void add_speech_problem(const char_data& proto, std::vector<vendor_problem>* problems)
{
    if (proto.abilities.intel < 6)
        problems->push_back({ 0, "intelligence below 6 - banker can't speak" });
}

void add_pref_problem(const char_data& proto, std::vector<vendor_problem>* problems)
{
    if (proto.specials2.pref != 0)
        problems->push_back({ 0, "pref set - banker attacks and can be hurt" });
}

struct bank_customer {
    std::string account;
    int side;
    bank_vault* vault;
};

/* Everything a banker checks before any business. Says why when refusing. */
bool banker_admits(struct char_data* host, struct char_data* ch, const banker_config& config, bank_customer* customer)
{
    if (!config.ok) {
        if (g_banker_disabled_logged.insert(host->nr).second)
            vendor_send(vendor_problem_line(host->nr >= 0 ? mob_index[host->nr].virt : -1,
                            { 0, "bad options - banker disabled" }),
                nullptr);
        vendor_say(host, "The bank is closed for now.");
        return false;
    }
    if (!vendor_serves_customer(host, ch))
        return false;
    if (!vendor_hours_open(config.hours, time_info.hours)) {
        vendor_say(host, "I'm closed. Come back later.");
        return false;
    }
    customer->side = IS_NPC(ch) ? BANK_SIDE_NONE : bank_side_for_race(GET_RACE(ch));
    if (customer->side == BANK_SIDE_NONE) {
        vendor_say(host, "I hold nothing for your kind.");
        return false;
    }
    if (!ch->desc || !*ch->desc->account_name) {
        vendor_say(host, "I can't find your account.");
        return false;
    }
    customer->account = ch->desc->account_name;
    std::string error;
    customer->vault = bank_vault_open(customer->account, customer->side, &error);
    if (!customer->vault) {
        vendor_say(host, "I can't open your vault right now.");
        return false;
    }
    return true;
}

long long slot_fee(const banker_config& config, const bank_slot& slot, struct char_data* host, struct char_data* ch)
{
    int days = bank_days_stored(slot.deposited, bank_now(), boot_option(BOOT_BANK_DAY_START_HOUR));
    return bank_fee(config, days, (int)slot.objects.size(), GET_RACE(ch) != GET_RACE(host));
}

void banker_balance(struct char_data* host, struct char_data* ch, const banker_config& config, const bank_customer& customer)
{
    std::vector<bank_balance_row> rows;
    for (const bank_slot& slot : customer.vault->slots) {
        long long fee = slot_fee(config, slot, host, ch);
        rows.push_back({ bank_slot_name(slot), slot.objects.size() > 1 ? (int)slot.objects.size() - 1 : -1,
            fee > 0 ? money_message((int)std::min<long long>(fee, 2000000000LL), 0) : "free" });
    }
    vendor_say(host, "Here is your vault.");
    std::string coins = money_message(customer.vault->coins, 0);
    send_to_char(format_bank_balance(coins, boot_option(BOOT_BANK_COIN_LIMIT_GOLD), (int)customer.vault->slots.size(),
                     boot_option(BOOT_BANK_SLOTS), rows, config.fee > 0)
                     .c_str(),
        ch);
}

void bank_log(const std::string& line)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line.c_str());
    log(buf);
}

std::string bank_log_tail(struct char_data* host, const bank_customer& customer)
{
    return " at mobile #" + std::to_string(host->nr >= 0 ? mob_index[host->nr].virt : -1) + ", account "
        + account::normalize_account_name(customer.account) + ", side " + std::to_string(customer.side);
}

/* "<N> gold|silver|copper|coins|coin" -> copper. *is_coins says whether the
 * argument was a coin request at all; false with *is_coins set means a bad amount. */
bool parse_coins(const char* arg, bool* is_coins, long long* copper)
{
    char first[MAX_INPUT_LENGTH], second[MAX_INPUT_LENGTH], whole[MAX_INPUT_LENGTH];
    strncpy(whole, arg, sizeof(whole) - 1);
    whole[sizeof(whole) - 1] = 0;
    half_chop(whole, first, second);
    long long unit = !str_cmp(second, "gold") ? COPP_IN_GOLD : !str_cmp(second, "silver")       ? COPP_IN_SILV
        : (!str_cmp(second, "copper") || !str_cmp(second, "coins") || !str_cmp(second, "coin")) ? 1
                                                                                                : 0;
    const char* digits = *first == '-' ? first + 1 : first;
    *is_coins = unit != 0 && *digits && strspn(digits, "0123456789") == strlen(digits);
    if (!*is_coins)
        return false;
    if (*first == '-' || strlen(first) > 9)
        return false;
    *copper = atoll(first) * unit;
    return *copper > 0 && *copper <= 2000000000LL;
}

void bank_deposit_coins(struct char_data* host, struct char_data* ch, const bank_customer& customer, long long copper)
{
    char buf[256];
    if (copper > GET_GOLD(ch)) {
        send_to_char("You don't have that much.\n\r", ch);
        return;
    }
    long long room = (long long)boot_option(BOOT_BANK_COIN_LIMIT_GOLD) * COPP_IN_GOLD - customer.vault->coins;
    if (room <= 0) {
        vendor_say(host, "Your vault can hold no more coins.");
        return;
    }
    long long take = std::min(copper, room);
    customer.vault->coins += (int)take;
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error)) { /* vault first */
        customer.vault->coins -= (int)take;
        vendor_say(host, "I can't reach the vault right now.");
        return;
    }
    GET_GOLD(ch) -= (int)take;
    bank_save_character(ch);
    snprintf(buf, sizeof(buf), "You deposit %s.\n\r", money_message((int)take, 0));
    send_to_char(buf, ch);
    if (take < copper) {
        snprintf(buf, sizeof(buf), "%s was refused: your vault is full.\n\r", money_message((int)(copper - take), 0));
        CAP(buf);
        send_to_char(buf, ch);
    }
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " deposits " + std::to_string(take) + " copper"
        + bank_log_tail(host, customer));
}

/* command_interpreter parses the command's targets first and, after this
 * program returns, still runs the SPECIAL_TARGET specials on them. An object
 * this program destroys must not be left in those targets. */
void forget_target(struct waiting_type* wtl, struct obj_data* obj)
{
    if (!wtl)
        return;
    for (struct target_data* target : { &wtl->targ1, &wtl->targ2 })
        if (target->type == TARGET_OBJ && target->ptr.obj == obj) {
            target->type = TARGET_NONE;
            target->ptr.other = 0;
        }
}

void bank_deposit(struct char_data* host, struct char_data* ch, char* arg, const bank_customer& customer,
    struct waiting_type* wtl)
{
    char name[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    bool is_coins = false;
    long long copper = 0;
    if (parse_coins(arg, &is_coins, &copper)) {
        bank_deposit_coins(host, ch, customer, copper);
        return;
    }
    if (is_coins) {
        vendor_say(host, "How much?");
        return;
    }
    one_argument(arg, name);
    if (!*name) {
        vendor_say(host, "What would you like to deposit?");
        return;
    }
    struct obj_data* obj = get_obj_in_list_vis(ch, name, ch->carrying, 9999); /* loose inventory only */
    if (!obj) {
        send_to_char("You don't have that.\n\r", ch);
        return;
    }
    if (!bank_obj_storable(obj)) {
        vendor_say(host, "I can't keep that for you.");
        return;
    }
    if ((int)customer.vault->slots.size() >= boot_option(BOOT_BANK_SLOTS)) {
        vendor_say(host, "Your vault is full.");
        return;
    }
    bank_slot slot;
    slot.deposited = (long)bank_now();
    bank_records_from_obj(obj, &slot.objects);
    customer.vault->slots.push_back(slot);
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error)) { /* vault first */
        customer.vault->slots.pop_back();
        vendor_say(host, "I can't reach the vault right now.");
        return;
    }
    snprintf(buf, sizeof(buf), "You hand %s to %s.\n\r", obj->short_description, GET_NAME(host));
    send_to_char(buf, ch);
    act("$n deposits $p.", FALSE, ch, obj, 0, TO_ROOM);
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " deposits " + obj->short_description + " ("
        + std::to_string(slot.objects[0].item_number) + ")" + bank_log_tail(host, customer));
    forget_target(wtl, obj);
    obj_from_char(obj);
    extract_obj(obj); /* takes its contents with it */
    bank_save_character(ch);
}

void bank_withdraw_coins(struct char_data* host, struct char_data* ch, const bank_customer& customer, long long copper)
{
    char buf[256];
    if (copper > customer.vault->coins) {
        vendor_say(host, "You don't have that much with me.");
        return;
    }
    if ((long long)GET_GOLD(ch) + copper > 2000000000LL) {
        send_to_char("You can't carry that much money.\n\r", ch);
        return;
    }
    GET_GOLD(ch) += (int)copper;
    bank_save_character(ch); /* character first */
    customer.vault->coins -= (int)copper;
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error))
        bank_log("SYSERR: bank: vault write failed after a coin withdrawal: " + error);
    snprintf(buf, sizeof(buf), "You withdraw %s.\n\r", money_message((int)copper, 0));
    send_to_char(buf, ch);
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " withdraws " + std::to_string(copper) + " copper"
        + bank_log_tail(host, customer));
}

void bank_withdraw(struct char_data* host, struct char_data* ch, char* arg, const banker_config& config,
    const bank_customer& customer)
{
    char want[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    bool is_coins = false;
    long long copper = 0;
    if (parse_coins(arg, &is_coins, &copper)) {
        bank_withdraw_coins(host, ch, customer, copper);
        return;
    }
    if (is_coins) {
        vendor_say(host, "How much?");
        return;
    }
    one_argument(arg, want);
    if (!*want) {
        vendor_say(host, "What would you like to withdraw?");
        return;
    }
    std::vector<bank_slot>& slots = customer.vault->slots;
    int pick = -1;
    if (strspn(want, "0123456789") == strlen(want)) { /* a balance number */
        int n = strlen(want) <= 4 ? atoi(want) : 0;
        if (n >= 1 && n <= (int)slots.size())
            pick = n - 1;
    } else { /* a keyword; "2.sword" = the second matching slot */
        char* name = want;
        int nth = get_number(&name);
        for (size_t i = 0; i < slots.size() && pick < 0; ++i) {
            int rnum = real_object(slots[i].objects[0].item_number);
            if (rnum >= 0 && isname(name, obj_proto[rnum].name) && --nth == 0)
                pick = (int)i;
        }
    }
    if (pick < 0) {
        vendor_say(host, "I hold nothing like that for you.");
        return;
    }
    struct obj_data* obj = bank_obj_from_records(slots[pick].objects);
    if (!obj) {
        vendor_say(host, "I can't get that out right now.");
        snprintf(buf, sizeof(buf), "SYSERR: bank: stored object #%d can't be rebuilt",
            slots[pick].objects[0].item_number);
        bank_log(buf);
        return;
    }
    if (IS_CARRYING_N(ch) + 1 > CAN_CARRY_N(ch)) {
        send_to_char("You can't carry that many items.\n\r", ch);
        extract_obj(obj);
        return;
    }
    if (IS_CARRYING_W(ch) + GET_OBJ_WEIGHT(obj) > CAN_CARRY_W(ch)) {
        send_to_char("You can't carry that much weight.\n\r", ch);
        extract_obj(obj);
        return;
    }
    long long fee = slot_fee(config, slots[pick], host, ch);
    if (fee > (long long)GET_GOLD(ch) + customer.vault->coins) {
        snprintf(buf, sizeof(buf), "That costs %s. You don't have it.",
            money_message((int)std::min<long long>(fee, 2000000000LL), 0));
        vendor_say(host, buf);
        extract_obj(obj);
        return;
    }
    int from_purse = (int)std::min<long long>(fee, GET_GOLD(ch));
    int from_vault = (int)(fee - from_purse);
    int vnum = slots[pick].objects[0].item_number;

    GET_GOLD(ch) -= from_purse;
    obj_to_char(obj, ch);
    bank_save_character(ch); /* character first */
    customer.vault->coins -= from_vault;
    slots.erase(slots.begin() + pick);
    std::string error;
    if (!bank_vault_write(customer.account, customer.side, &error))
        bank_log("SYSERR: bank: vault write failed after a withdrawal: " + error);

    snprintf(buf, sizeof(buf), "%s hands you %s.\n\r", GET_NAME(host), obj->short_description);
    CAP(buf);
    send_to_char(buf, ch);
    if (from_purse > 0) {
        snprintf(buf, sizeof(buf), "You pay %s from your purse.\n\r", money_message(from_purse, 0));
        send_to_char(buf, ch);
    }
    if (from_vault > 0) {
        snprintf(buf, sizeof(buf), "%s comes out of your vault.\n\r", money_message(from_vault, 0));
        CAP(buf);
        send_to_char(buf, ch);
    }
    act("$n withdraws $p.", FALSE, ch, obj, 0, TO_ROOM);
    bank_log(std::string("BANK: ") + GET_NAME(ch) + " withdraws " + obj->short_description + " ("
        + std::to_string(vnum) + ")" + bank_log_tail(host, customer) + ", fee " + std::to_string(fee));
}

} // namespace

bool is_banker_candidate(const struct char_data* proto, int rnum)
{
    if (no_specials)
        return false;
    if (!IS_SET(proto->specials2.act, MOB_SPEC) || proto->specials.store_prog_number != PROG_BANKER)
        return false;
    return rnum < 0 || !mob_index[rnum].func || mob_index[rnum].func == (special_func)banker;
}

void banker_config_check(const struct char_data* proto, int mob_vnum, struct char_data* builder)
{
    std::vector<vendor_problem> problems;
    parse_banker_options(proto->specials.mob_options, &problems);
    add_speech_problem(*proto, &problems);
    add_pref_problem(*proto, &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_vnum, problem), builder);
}

/* Soft checks, on /imp only, to the builder alone (not at boot, not logged). */
void banker_implement_check(int mob_rnum, struct char_data* builder)
{
    if (!builder || mob_rnum < 0 || mob_rnum > top_of_mobt || !is_banker_proto(mob_rnum))
        return;
    char buf[128];
    if (!IS_SET(mob_proto[mob_rnum].specials2.act, MOB_NOBASH)) {
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: nobash not set\n\r", mob_index[mob_rnum].virt);
        send_to_char(buf, builder);
    }
    banker_config config = parse_banker_options(mob_proto[mob_rnum].specials.mob_options, nullptr);
    if (config.markup > 0 && config.fee == 0) {
        snprintf(buf, sizeof(buf), "MOB WARNING: mobile #%d: racial_markup without fee\n\r", mob_index[mob_rnum].virt);
        send_to_char(buf, builder);
    }
}

void banker_config_rebuild(int mob_rnum, struct char_data* builder, bool report)
{
    g_banker_disabled_logged.erase(mob_rnum);
    if (mob_rnum < 0 || mob_rnum > top_of_mobt || !is_banker_proto(mob_rnum)) {
        g_banker_configs.erase(mob_rnum);
        return;
    }
    std::vector<vendor_problem> problems;
    g_banker_configs[mob_rnum] = parse_banker_options(mob_proto[mob_rnum].specials.mob_options, &problems);
    if (!report)
        return;
    add_speech_problem(mob_proto[mob_rnum], &problems);
    add_pref_problem(mob_proto[mob_rnum], &problems);
    for (const vendor_problem& problem : problems)
        vendor_send(vendor_problem_line(mob_index[mob_rnum].virt, problem), builder);
}

const banker_config* banker_config_for(int mob_rnum)
{
    auto it = g_banker_configs.find(mob_rnum);
    return it == g_banker_configs.end() ? nullptr : &it->second;
}

void banker_config_boot()
{
    g_banker_configs.clear();
    g_banker_disabled_logged.clear();
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum)
        if (is_banker_proto(rnum))
            banker_config_rebuild(rnum, nullptr);
}

SPECIAL(banker)
{
    /* Only a registered banker mob is ever a banker (see barter_vendor). */
    if (!host || !IS_NPC(host))
        return FALSE;
    const banker_config* config = banker_config_for(host->nr);
    if (!config)
        return FALSE;
    if (callflag == SPECIAL_DAMAGE) { /* before ch == host: poison ticks are self-damage */
        if (ch && ch != host)
            vendor_say(host, "Don't even think about it.");
        return TRUE;
    }
    if (!ch || ch == host)
        return FALSE;
    if (callflag == SPECIAL_TARGET) { /* dust blinds even with its damage cancelled */
        if (cmd != CMD_BLINDING || !wtl || wtl->targ1.type != TARGET_CHAR || wtl->targ1.ptr.ch != host)
            return FALSE;
        vendor_say(host, "Don't even think about it.");
        return TRUE;
    }
    if (callflag != SPECIAL_COMMAND)
        return FALSE;
    if (cmd == CMD_GIVE) {
        if (!arg || !give_targets(host, ch, arg))
            return FALSE;
        vendor_say(host, "I don't take gifts.");
        return TRUE;
    }
    if (cmd != CMD_BALANCE && cmd != CMD_DEPOSIT && cmd != CMD_WITHDRAW)
        return FALSE;

    bank_customer customer;
    if (!banker_admits(host, ch, *config, &customer))
        return TRUE;
    if (cmd == CMD_BALANCE)
        banker_balance(host, ch, *config, customer);
    else if (cmd == CMD_DEPOSIT)
        bank_deposit(host, ch, arg ? arg : (char*)"", customer, wtl);
    else
        bank_withdraw(host, ch, arg ? arg : (char*)"", *config, customer);
    return TRUE;
}

namespace {

std::function<bool(const std::string&, bank_account_ref*)> g_lookup_identifier;
std::function<bool(const std::string&, bank_account_ref*)> g_lookup_name;
std::function<bool(const std::string&, bank_account_ref*, int*)> g_lookup_character;

bool game_lookup_identifier(const std::string& identifier, bank_account_ref* out)
{
    account::AccountData data;
    if (!account::read_account_file_by_identifier(".", identifier, &data, nullptr))
        return false;
    *out = { data.account_name, data.normalized_email };
    return true;
}

bool game_lookup_name(const std::string& name, bank_account_ref* out)
{
    account::AccountData data;
    if (name.find('@') != std::string::npos || !account::read_account_file(".", name, &data, nullptr))
        return false;
    *out = { data.account_name, data.normalized_email };
    return true;
}

const char* side_title(int side)
{
    return side == BANK_SIDE_LIGHT ? "Light" : side == BANK_SIDE_DARK ? "Dark"
                                                                      : "Third";
}

std::string stored_name(const objects_json::ObjectRecord& record)
{
    int rnum = real_object(record.item_number);
    return rnum >= 0 ? obj_proto[rnum].short_description : "something";
}

} // namespace

void bank_set_account_lookups(std::function<bool(const std::string&, bank_account_ref*)> by_email_or_name,
    std::function<bool(const std::string&, bank_account_ref*)> by_name,
    std::function<bool(const std::string&, bank_account_ref*, int*)> by_character)
{
    g_lookup_identifier = by_email_or_name;
    g_lookup_name = by_name;
    g_lookup_character = by_character;
}

std::string format_vault_view(const bank_account_ref&, const std::string&, int side, const bank_vault& vault)
{
    char line[160];
    std::string coins = money_message(vault.coins, 0);
    snprintf(line, sizeof(line), "%s vault: %s, %d of %d slots\n\r", side_title(side), coins.c_str(),
        (int)vault.slots.size(), boot_option(BOOT_BANK_SLOTS));
    std::string out = line;
    if (vault.slots.empty())
        return out + "  (empty)\n\r";
    for (size_t i = 0; i < vault.slots.size(); ++i) {
        const bank_slot& slot = vault.slots[i];
        int days = bank_days_stored(slot.deposited, bank_now(), boot_option(BOOT_BANK_DAY_START_HOUR));
        snprintf(line, sizeof(line), "%2d  %-40.40s stored %d day%s\n\r", (int)(i + 1), stored_name(slot.objects[0]).c_str(),
            days, days == 1 ? "" : "s");
        out += line;
        for (size_t o = 1; o < slot.objects.size(); ++o) {
            int indent = std::min(4 + 2 * slot.objects[o].wear_pos, 30);
            snprintf(line, sizeof(line), "%*s%.*s\n\r", indent, "", 78 - indent, stored_name(slot.objects[o]).c_str());
            out += line;
        }
    }
    return out;
}

namespace {

bool game_lookup_character(const std::string& character, bank_account_ref* out, int* race)
{
    std::string owner;
    if (!account::find_linked_character_owner_account(".", character, &owner, nullptr) || owner.empty())
        return false;
    account::AccountData data;
    if (!account::read_account_file(".", owner, &data, nullptr))
        return false;
    roster_cache::RosterSummary summary;
    if (!roster_cache::get(".", data.account_name, character, &summary) || !summary.readable)
        return false;
    *out = { data.account_name, data.normalized_email };
    *race = summary.race;
    return true;
}

bool lookup_identifier(const std::string& text, bank_account_ref* out)
{
    return g_lookup_identifier ? g_lookup_identifier(text, out) : game_lookup_identifier(text, out);
}

bool lookup_name(const std::string& text, bank_account_ref* out)
{
    return g_lookup_name ? g_lookup_name(text, out) : game_lookup_name(text, out);
}

bool lookup_character(const std::string& text, bank_account_ref* out, int* race)
{
    std::string name = text;
    for (char& c : name)
        c = (char)tolower((unsigned char)c);
    return g_lookup_character ? g_lookup_character(name, out, race) : game_lookup_character(name, out, race);
}

void vault_usage(struct char_data* ch)
{
    send_to_char("Usage: vault <character | email | account> [1|2|3]\n\r"
                 "       vault take <account> <1|2|3> <slot>\n\r"
                 "       vault take <account> <1|2|3> coins <amount> [gold|silver|copper]\n\r"
                 "       vault put <account> <1|2|3> <item>\n\r"
                 "       vault put <account> <1|2|3> coins <amount> [gold|silver|copper]\n\r",
        ch);
}

int vault_side_number(const char* text)
{
    return strlen(text) == 1 && *text >= '1' && *text <= '3' ? *text - '0' : BANK_SIDE_NONE;
}

void vault_show_side(struct char_data* ch, const bank_account_ref& account, const std::string& character, int side)
{
    std::string error;
    bank_vault* vault = bank_vault_open(account.name, side, &error);
    if (!vault) {
        std::string line = std::string(side_title(side)) + " vault: "
            + (error == "That vault's file can't be read." ? "FILE UNREADABLE" : error) + "\n\r";
        send_to_char(line.c_str(), ch);
        return;
    }
    std::string view = format_vault_view(account, character, side, *vault);
    if (view.size() >= MAX_STRING_LENGTH - 1 && ch->desc) {
        std::vector<char> text(view.begin(), view.end());
        text.push_back('\0');
        page_string(ch->desc, text.data(), 1);
    } else
        send_to_char(view.c_str(), ch);
}

void vault_view(struct char_data* ch, const char* identifier, const char* side_text)
{
    int side = BANK_SIDE_NONE;
    if (*side_text && (side = vault_side_number(side_text)) == BANK_SIDE_NONE) {
        vault_usage(ch);
        return;
    }
    bank_account_ref account;
    std::string character;
    int race = RACE_GOD;
    if (!lookup_identifier(identifier, &account)) {
        if (!lookup_character(identifier, &account, &race)) {
            send_to_char("No account or character by that name.\n\r", ch);
            return;
        }
        character = identifier;
        for (char& c : character)
            c = (char)tolower((unsigned char)c);
        character[0] = (char)toupper((unsigned char)character[0]);
    }
    std::string header = "Account: " + account.name + " (" + account.email + ")";
    if (!character.empty())
        header += "   Character: " + character;
    send_to_char((header + "\n\r").c_str(), ch);

    if (!character.empty()) {
        int own = bank_side_for_race(race);
        if (own == BANK_SIDE_NONE) {
            send_to_char("That character has no vault.\n\r", ch);
            return;
        }
        vault_show_side(ch, account, character, own);
        return;
    }
    for (int s = BANK_SIDE_LIGHT; s <= BANK_SIDE_THIRD; ++s)
        if (side == BANK_SIDE_NONE || side == s)
            vault_show_side(ch, account, character, s);
}

/* "coins <amount> [gold|silver|copper]"; a bare amount is copper. */
bool vault_parse_coins(const char* text, long long* copper)
{
    std::string amount = text;
    if (!amount.empty() && amount.find_first_not_of("0123456789") == std::string::npos)
        amount += " copper";
    bool is_coins = false;
    return parse_coins(amount.c_str(), &is_coins, copper);
}

void vault_change(struct char_data* ch, bool take, char* text)
{
    char account_word[MAX_INPUT_LENGTH], side_word[MAX_INPUT_LENGTH], rest[MAX_INPUT_LENGTH], what[MAX_INPUT_LENGTH];
    char first[MAX_INPUT_LENGTH], amount[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    half_chop(text, account_word, rest);
    half_chop(rest, side_word, what);
    if (!*account_word || !*side_word || !*what) {
        vault_usage(ch);
        return;
    }
    bank_account_ref account;
    if (!lookup_name(account_word, &account)) {
        send_to_char("Use the account name shown by 'vault <name>'.\n\r", ch);
        return;
    }
    int side = vault_side_number(side_word);
    if (side == BANK_SIDE_NONE) {
        vault_usage(ch);
        return;
    }
    std::string error;
    bank_vault* vault = bank_vault_open(account.name, side, &error);
    if (!vault) {
        send_to_char((error + "\n\r").c_str(), ch);
        return;
    }

    half_chop(what, first, amount);
    if (!str_cmp(first, "coins")) {
        long long copper = 0;
        if (!vault_parse_coins(amount, &copper)) {
            send_to_char("How much?\n\r", ch);
            return;
        }
        if (take) {
            if (copper > vault->coins) {
                send_to_char("The vault doesn't hold that much.\n\r", ch);
                return;
            }
            if ((long long)GET_GOLD(ch) + copper > 2000000000LL) {
                send_to_char("You can't carry that much money.\n\r", ch);
                return;
            }
            GET_GOLD(ch) += (int)copper;
            bank_save_character(ch); /* the immortal first */
            vault->coins -= (int)copper;
            if (!bank_vault_write(account.name, side, &error))
                bank_log("SYSERR: bank: vault write failed after vault take: " + error);
            snprintf(buf, sizeof(buf), "You take %s from the vault.\n\r", money_message((int)copper, 0));
        } else {
            if (copper > GET_GOLD(ch)) {
                send_to_char("You don't have that much.\n\r", ch);
                return;
            }
            if (vault->coins + copper > (long long)boot_option(BOOT_BANK_COIN_LIMIT_GOLD) * COPP_IN_GOLD) {
                send_to_char("That would pass the vault's coin limit.\n\r", ch);
                return;
            }
            vault->coins += (int)copper;
            if (!bank_vault_write(account.name, side, &error)) { /* the vault first */
                vault->coins -= (int)copper;
                send_to_char((error + "\n\r").c_str(), ch);
                return;
            }
            GET_GOLD(ch) -= (int)copper;
            bank_save_character(ch);
            snprintf(buf, sizeof(buf), "You put %s into the vault.\n\r", money_message((int)copper, 0));
        }
        send_to_char(buf, ch);
        return;
    }

    if (take) {
        int n = strspn(what, "0123456789") == strlen(what) && strlen(what) <= 4 ? atoi(what) : 0;
        if (n < 1 || n > (int)vault->slots.size()) {
            send_to_char("No such slot.\n\r", ch);
            return;
        }
        struct obj_data* obj = bank_obj_from_records(vault->slots[n - 1].objects);
        if (!obj) {
            send_to_char("That item can't be rebuilt (missing prototype).\n\r", ch);
            return;
        }
        obj_to_char(obj, ch);
        bank_save_character(ch); /* the immortal first */
        vault->slots.erase(vault->slots.begin() + (n - 1));
        if (!bank_vault_write(account.name, side, &error))
            bank_log("SYSERR: bank: vault write failed after vault take: " + error);
        snprintf(buf, sizeof(buf), "You take %s from the vault.\n\r", obj->short_description);
        send_to_char(buf, ch);
        return;
    }

    struct obj_data* obj = get_obj_in_list_vis(ch, first, ch->carrying, 9999);
    if (!obj) {
        send_to_char("You don't have that.\n\r", ch);
        return;
    }
    if (!bank_obj_storable(obj)) {
        send_to_char("The bank can't hold that.\n\r", ch);
        return;
    }
    if ((int)vault->slots.size() >= boot_option(BOOT_BANK_SLOTS)) {
        send_to_char("That vault is full.\n\r", ch);
        return;
    }
    bank_slot slot;
    slot.deposited = (long)bank_now();
    bank_records_from_obj(obj, &slot.objects);
    vault->slots.push_back(slot);
    if (!bank_vault_write(account.name, side, &error)) { /* the vault first */
        vault->slots.pop_back();
        send_to_char((error + "\n\r").c_str(), ch);
        return;
    }
    snprintf(buf, sizeof(buf), "You put %s into the vault.\n\r", obj->short_description);
    send_to_char(buf, ch);
    obj_from_char(obj);
    extract_obj(obj);
    bank_save_character(ch);
}

} // namespace

ACMD(do_vault)
{
    char first[MAX_INPUT_LENGTH], rest[MAX_INPUT_LENGTH], second[MAX_INPUT_LENGTH], third[MAX_INPUT_LENGTH];
    char buf[MAX_STRING_LENGTH];
    while (*argument == ' ')
        ++argument;
    half_chop(argument, first, rest);
    if (!*first) {
        vault_usage(ch);
        return;
    }
    /* One line per command, like other immortal commands; what the command
     * shows is never logged. */
    snprintf(buf, sizeof(buf), "(GC) %s: vault %.200s", GET_NAME(ch), argument);
    mudlog(buf, BRF, (sh_int)MAX(LEVEL_GRGOD, GET_INVIS_LEV(ch)), TRUE);

    if (!str_cmp(first, "take") || !str_cmp(first, "put")) {
        vault_change(ch, !str_cmp(first, "take"), rest);
        return;
    }
    half_chop(rest, second, third);
    if (*third) {
        vault_usage(ch);
        return;
    }
    vault_view(ch, first, second);
}
