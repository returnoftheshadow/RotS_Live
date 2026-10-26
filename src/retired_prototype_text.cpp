#include "retired_prototype_text.h"

#include "structs.h"
#include "utils.h"

#include <algorithm>
#include <string>
#include <vector>

extern struct obj_data* object_list;

/* Copies of a prototype share its text and extra-description list, and every copy is on
 * object_list: read_object links each new copy there, restyle_object points a copy already on it
 * at its new prototype's text, and extract_obj unlinks a copy before freeing it. Shape sessions
 * read their own text from the object files. So object_list holds every object that can show
 * retired text. */

namespace {

/* An extra-description list a prototype has replaced. */
struct retired_extra_descriptions {
    // The list's first node; freeing the list walks it from here.
    extra_descr_data* first_node;
    // The list's last node. A list sharing any node of it reaches this one too, so holding the
    // list is checked against it alone.
    const extra_descr_data* last_node;
};

// Retired text some object still held when last checked. Each entry is freed once, as it is
// removed.
std::vector<char*> retired_texts;
// Retired extra-description lists some object still reached when last checked, freed the same way.
std::vector<retired_extra_descriptions> retired_lists;

bool object_holds_text(const obj_data& object, const char* text)
{
    return object.name == text || object.short_description == text
        || object.description == text || object.action_description == text;
}

bool object_reaches_node(const obj_data& object, const extra_descr_data* node)
{
    for (const extra_descr_data* own_node = object.ex_description; own_node;
         own_node = own_node->next) {
        if (own_node == node) {
            return true;
        }
    }
    return false;
}

/* True when an object on object_list other than `ignored` (null ignores none) holds `text`. */
bool another_object_holds_text(const char* text, const obj_data* ignored)
{
    for (const obj_data* object = object_list; object; object = object->next) {
        if (object != ignored && object_holds_text(*object, text)) {
            return true;
        }
    }
    return false;
}

/* True when an object on object_list other than `ignored` (null ignores none) reaches `node`. */
bool another_object_reaches_node(const extra_descr_data* node, const obj_data* ignored)
{
    for (const obj_data* object = object_list; object; object = object->next) {
        if (object != ignored && object_reaches_node(*object, node)) {
            return true;
        }
    }
    return false;
}

void free_extra_descriptions(extra_descr_data* list)
{
    while (list) {
        extra_descr_data* next_node = list->next;
        RELEASE(list->keyword);
        RELEASE(list->description);
        RELEASE(list);
        list = next_node;
    }
}

/* Frees and forgets each retired text that `must_hold` holds (null: every retired text) and no
 * object other than `ignored` holds. */
void release_unheld_texts(const obj_data* must_hold, const obj_data* ignored)
{
    std::size_t kept_count = 0;
    for (std::size_t index = 0; index < retired_texts.size(); ++index) {
        char* text = retired_texts[index];
        const bool candidate = !must_hold || object_holds_text(*must_hold, text);
        if (candidate && !another_object_holds_text(text, ignored)) {
            RELEASE(text);
        } else {
            retired_texts[kept_count] = text;
            ++kept_count;
        }
    }
    retired_texts.resize(kept_count);
}

/* As release_unheld_texts, for the retired extra-description lists. */
void release_unheld_lists(const obj_data* must_hold, const obj_data* ignored)
{
    std::size_t kept_count = 0;
    for (std::size_t index = 0; index < retired_lists.size(); ++index) {
        const retired_extra_descriptions list = retired_lists[index];
        const bool candidate = !must_hold || object_reaches_node(*must_hold, list.last_node);
        if (candidate && !another_object_reaches_node(list.last_node, ignored)) {
            free_extra_descriptions(list.first_node);
        } else {
            retired_lists[kept_count] = list;
            ++kept_count;
        }
    }
    retired_lists.resize(kept_count);
}

void log_retired_twice(const char* function_name)
{
    const std::string message = std::string("SYSERR: ") + function_name
        + ": this was retired already; it is kept once";
    log(message.c_str());
}

} // namespace

/* Implement is a rare builder command, so each retirement checks every retired piece against the
 * whole object list. That also frees what a copy stopped showing while it lived. */
void retire_prototype_text(char* text)
{
    if (!text) {
        return;
    }
    const auto found = std::find(retired_texts.begin(), retired_texts.end(), text);
    if (found != retired_texts.end()) {
        log_retired_twice(__func__);
    } else {
        retired_texts.push_back(text);
    }
    release_unheld_texts(nullptr, nullptr);
}

void retire_prototype_extra_descriptions(extra_descr_data* list)
{
    if (!list) {
        return;
    }
    const auto same_list = [list](const retired_extra_descriptions& retired) -> bool {
        return retired.first_node == list;
    };
    const auto found = std::find_if(retired_lists.begin(), retired_lists.end(), same_list);
    if (found != retired_lists.end()) {
        log_retired_twice(__func__);
    } else {
        const extra_descr_data* last_node = list;
        while (last_node->next) {
            last_node = last_node->next;
        }
        retired_lists.push_back({ list, last_node });
    }
    release_unheld_lists(nullptr, nullptr);
}

/* Objects are freed often, so the object list is scanned only for retired pieces `leaving`
 * holds, which is rare: usually nothing is retired at all. */
void release_retired_text_left_by(const obj_data& leaving)
{
    if (!retired_texts.empty()) {
        release_unheld_texts(&leaving, &leaving);
    }
    if (!retired_lists.empty()) {
        release_unheld_lists(&leaving, &leaving);
    }
}

std::size_t retired_prototype_text_count()
{
    return retired_texts.size() + retired_lists.size();
}
