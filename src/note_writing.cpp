#include "note_writing.h"

#include "comm.h"
#include "structs.h"
#include "utils.h"

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
    writer.written_note->set_owned_text(object_text::action_description,
        str_dup(writer.written_note_text));
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
