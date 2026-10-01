#include "banker.h"

#include "../account_management_identity.h"
#include "../account_management_storage.h"
#include "../db.h"
#include "../handler.h"
#include "../json_utils.h"
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
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

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
