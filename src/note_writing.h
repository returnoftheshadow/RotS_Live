#ifndef NOTE_WRITING_H
#define NOTE_WRITING_H

struct descriptor_data;
struct obj_data;

/* A player writing on a note with `write`. The text editor (string_add) edits a buffer the
 * writer's descriptor owns, and each line typed is copied onto the note, so anyone reading the note
 * meanwhile sees what is written so far and the note never holds the editor's buffer. */

/* Starts `writer`'s editor on a new, empty text for `note`, limited to MAX_NOTE_LENGTH characters.
 * The note keeps the text it has until the first line is typed. */
void start_writing_note(descriptor_data& writer, obj_data& note);

/* Copies the editor's text onto the note `writer` is writing on, if any: none at all once the
 * writer aborts with %q. string_add calls it after each line. */
void save_written_note(descriptor_data& writer);

/* Saves the note `writer` is writing on and frees the editor's buffer. Does nothing when `writer`
 * is writing no note. For the end of the edit and for a descriptor that is going away. */
void finish_writing_note(descriptor_data& writer);

/* Ends every edit of `note`, which is about to be freed: each writer loses what it was writing and
 * is told so. */
void stop_writing_on_note(const obj_data& note);

#endif
