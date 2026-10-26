#include "note_writing.h"

#include "comm.h"
#include "object_text.h"
#include "structs.h"
#include "utils.h"

#include <utility>

extern struct descriptor_data* descriptor_list;

void start_writing_note(descriptor_data& writer, obj_data& note)
{
    finish_writing_note(writer);
    writer.written_note = &note;
    writer.written_note_text = nullptr;
    writer.str = &writer.written_note_text;
    writer.str_limit = MAX_NOTE_LENGTH;
}

void save_written_note(descriptor_data& writer)
{
    if (!writer.written_note) {
        return;
    }
    obj_data& note = *writer.written_note;
    object_text::fields text = object_text_fields_of(note);
    if (writer.written_note_text) {
        text.action_description = writer.written_note_text;
    } else {
        text.action_description.reset();
    }
    note.set_text(create_object_text(std::move(text)));
}

void finish_writing_note(descriptor_data& writer)
{
    if (!writer.written_note) {
        return;
    }
    save_written_note(writer);
    if (writer.str == &writer.written_note_text) {
        writer.str = nullptr;
    }
    RELEASE(writer.written_note_text);
    writer.written_note = nullptr;
}

void stop_writing_on_note(const obj_data& note)
{
    for (descriptor_data* descriptor = descriptor_list; descriptor; descriptor = descriptor->next) {
        if (descriptor->written_note != &note) {
            continue;
        }
        /* The note is going, so nothing is saved onto it. */
        descriptor->written_note = nullptr;
        RELEASE(descriptor->written_note_text);
        descriptor->str = nullptr;
        descriptor->str_limit = 0;
        char_data* const writer = descriptor->character;
        if (writer) {
            if (!descriptor->connected && !IS_NPC(writer)) {
                REMOVE_BIT(PLR_FLAGS(writer), PLR_WRITING);
            }
            send_to_char("What you were writing is gone.\n\r", writer);
        }
    }
}
