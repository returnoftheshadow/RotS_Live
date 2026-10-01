#include "game_boot_options.h"

#include "comm.h"
#include "json_utils.h"
#include "structs.h"
#include "utils.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

const boot_option_def BOOT_OPTION_DEFS[BOOT_OPTION_COUNT] = {
    { "bank_slots", 10, 1, 100, "item slots in each bank vault" },
    { "bank_coin_limit_gold", 1000, 0, 100000, "most gold a bank vault holds" },
    { "bank_day_start_hour", 5, 0, 23, "hour the bank's fee day starts" },
};

namespace {
boot_options_values g_running = boot_options_defaults();
boot_options_values g_pending = boot_options_defaults();

bool in_range(int index, long value)
{
    return value >= BOOT_OPTION_DEFS[index].min && value <= BOOT_OPTION_DEFS[index].max;
}
} // namespace

boot_options_values boot_options_defaults()
{
    boot_options_values values;
    for (int i = 0; i < BOOT_OPTION_COUNT; ++i)
        values.v[i] = BOOT_OPTION_DEFS[i].def;
    return values;
}

int boot_option_index(const std::string& name)
{
    for (int i = 0; i < BOOT_OPTION_COUNT; ++i)
        if (name == BOOT_OPTION_DEFS[i].name)
            return i;
    return -1;
}

boot_options_values boot_options_parse(const std::string& json, std::vector<std::string>* warnings)
{
    boot_options_values values = boot_options_defaults();
    if (json.find_first_not_of(" \t\r\n") == std::string::npos)
        return values;

    std::vector<std::string> found;
    std::string error;
    json_utils::JsonReader reader(json);
    bool ok = reader.parse_root_object(
        [&values, &found](const std::string& key, json_utils::JsonReader* nested, std::string* nested_error) {
            int index = boot_option_index(key);
            if (index < 0) {
                found.push_back("BOOT OPTIONS: " + key + ": unknown setting - ignored");
                return nested->skip_value(nested_error);
            }
            long value = 0;
            if (!nested->parse_long(&value, nested_error))
                return false;
            if (!in_range(index, value)) {
                const boot_option_def& def = BOOT_OPTION_DEFS[index];
                found.push_back("BOOT OPTIONS: " + key + ": " + std::to_string(value) + " out of range "
                    + std::to_string(def.min) + "-" + std::to_string(def.max) + " - default "
                    + std::to_string(def.def) + " used");
                return true;
            }
            values.v[index] = (int)value;
            return true;
        },
        &error);
    if (!ok) {
        if (warnings)
            warnings->push_back("BOOT OPTIONS: file unreadable - defaults used");
        return boot_options_defaults();
    }
    if (warnings)
        warnings->insert(warnings->end(), found.begin(), found.end());
    return values;
}

std::string boot_options_serialize(const boot_options_values& values)
{
    std::ostringstream out;
    out << "{\n";
    for (int i = 0; i < BOOT_OPTION_COUNT; ++i)
        out << "  \"" << BOOT_OPTION_DEFS[i].name << "\": " << values.v[i] << (i + 1 < BOOT_OPTION_COUNT ? ",\n" : "\n");
    out << "}\n";
    return out.str();
}

void boot_options_load(const char* path)
{
    std::string text;
    std::ifstream in(path, std::ios::binary);
    if (in.good()) {
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }
    std::vector<std::string> warnings;
    g_running = boot_options_parse(text, &warnings);
    g_pending = g_running;
    for (const std::string& warning : warnings) {
        char line[512];
        snprintf(line, sizeof(line), "%s", warning.c_str());
        log(line);
    }
}

int boot_option(int index) { return g_running.v[index]; }
int boot_option_pending(int index) { return g_pending.v[index]; }
void boot_options_set_running_for_tests(int index, int value) { g_running.v[index] = value; }

bool boot_option_set(int index, int value, std::string* error, const char* path)
{
    const boot_option_def& def = BOOT_OPTION_DEFS[index];
    if (!in_range(index, value)) {
        *error = std::string(def.name) + " must be " + std::to_string(def.min) + "-" + std::to_string(def.max) + ".";
        return false;
    }
    boot_options_values next = g_pending;
    next.v[index] = value;
    std::string temp = std::string(path) + ".tmp";
    FILE* file = fopen(temp.c_str(), "w");
    if (!file) {
        *error = std::string("Can't write the settings file: ") + strerror(errno);
        return false;
    }
    std::string json = boot_options_serialize(next);
    size_t written = fwrite(json.data(), 1, json.size(), file);
    if (fclose(file) != 0 || written != json.size() || rename(temp.c_str(), path) != 0) {
        *error = std::string("Can't write the settings file: ") + strerror(errno);
        remove(temp.c_str());
        return false;
    }
    g_pending = next;
    return true;
}

ACMD(do_gameoptions)
{
    char name[MAX_INPUT_LENGTH], value[MAX_INPUT_LENGTH], line[256];
    half_chop(argument, name, value);

    if (!*name) {
        send_to_char("Setting                   Now  After reboot  Default  Range\n\r", ch);
        for (int i = 0; i < BOOT_OPTION_COUNT; ++i) {
            const boot_option_def& def = BOOT_OPTION_DEFS[i];
            char pending[16] = "";
            if (boot_option_pending(i) != boot_option(i))
                snprintf(pending, sizeof(pending), "%d", boot_option_pending(i));
            snprintf(line, sizeof(line), "%-22s %6d  %12s  %7d  %d-%d\n\r", def.name, boot_option(i), pending,
                def.def, def.min, def.max);
            send_to_char(line, ch);
            snprintf(line, sizeof(line), "  %s\n\r", def.what);
            send_to_char(line, ch);
        }
        return;
    }
    int index = boot_option_index(name);
    if (index < 0) {
        send_to_char("No such setting. Type 'gameoptions' for the list.\n\r", ch);
        return;
    }
    char* end = nullptr;
    long number = strtol(value, &end, 10);
    if (!*value || *end || number < -1000000 || number > 1000000) {
        send_to_char("Usage: gameoptions <name> <number>\n\r", ch);
        return;
    }
    std::string error;
    if (!boot_option_set(index, (int)number, &error)) {
        send_to_char((error + "\n\r").c_str(), ch);
        return;
    }
    snprintf(line, sizeof(line), "%s set to %ld. It takes effect at the next reboot.\n\r", name, number);
    send_to_char(line, ch);
    snprintf(line, sizeof(line), "(GC) %s set boot option %s to %ld.", GET_NAME(ch), name, number);
    mudlog(line, BRF, LEVEL_IMPL, TRUE);
}
