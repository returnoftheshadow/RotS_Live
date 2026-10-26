#ifndef OBJECT_TEXT_H
#define OBJECT_TEXT_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "structs.h"

/* The words an object shows: its keywords, its short and room descriptions, its action text and
 * its extra descriptions. It never changes once made, so a prototype and every copy made from it
 * share one, and an object's char* text fields point into the one it shows (obj_data::set_text).
 * Changing an object's text gives that object a new one; each is freed when the last object
 * showing it lets go of it. */
class object_text {
public:
    /* One extra description: the keywords that find it and the text they show. */
    struct extra_description {
        // The keywords, separated by spaces, that look and examine match.
        std::string keyword;
        // What looking at one of the keywords shows.
        std::string description;
    };

    /* The text as plain values, to build an object_text from or to change before building one.
     * A field with no value shows as a null pointer. */
    struct fields {
        // The keywords that find the object (obj_data::name).
        std::optional<std::string> name;
        // What it is called when carried, worn or held.
        std::optional<std::string> short_description;
        // Its line in a room.
        std::optional<std::string> description;
        // What is written on it, or what using it shows.
        std::optional<std::string> action_description;
        // Its extra descriptions, in the order its list shows them.
        std::vector<extra_description> extra_descriptions;
    };

    /* Holds `text`; the pointers it hands out stay valid for as long as it exists. */
    explicit object_text(fields text);

    object_text(const object_text&) = delete;
    object_text& operator=(const object_text&) = delete;

    /* The text as values. */
    const fields& values() const { return m_fields; }

    /* Each field as obj_data shows it, null for a field with no value. Read only through these:
     * the text is shared and never changes. */
    char* name() const;
    char* short_description() const;
    char* description() const;
    char* action_description() const;

    /* The first extra description, linked to the rest in order; null when there are none. Read
     * only, like the fields. */
    extra_descr_data* extra_descriptions() const;

private:
    // The text itself, never changed after construction so the pointers handed out stay valid.
    const fields m_fields;
    // One node per extra description, pointing at its strings in m_fields and linked to the next.
    std::vector<extra_descr_data> m_extra_description_nodes;
};

/* A new text holding `text`, for obj_data::set_text. */
[[nodiscard]] std::shared_ptr<const object_text> create_object_text(object_text::fields text);

/* The text `object` shows now, read from its char* fields: a null field has no value, and a null
 * extra-description keyword or description is empty. To change one field of an object's text,
 * read the fields, change them and give the object a new text built from them. */
[[nodiscard]] object_text::fields object_text_fields_of(const obj_data& object);

/* The text `object` shows, for another object to share: the one it holds, or a new one built
 * from its char* fields when it holds none (an object whose fields were set one by one). */
[[nodiscard]] std::shared_ptr<const object_text> shared_text_of(const obj_data& object);

#endif
