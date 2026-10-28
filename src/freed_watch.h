#ifndef FREED_WATCH_H
#define FREED_WATCH_H

#include <vector>

struct affected_type;
struct char_data;
struct obj_data;
struct universal_list;

/* Watches one object or character while code that can free it runs (a trigger script, a death), so
 * that the caller can tell afterwards whether it still exists. free_obj and free_char report each
 * object and character they free, and every watch on it is marked.
 *
 * Watch only something that exists when the watch is made. A watch must be a local variable:
 * watches nest, and each one stops watching when it goes out of scope. The watched object or
 * character is compared by address and never read, so it is safe to ask a watch after what it
 * watched has been freed. */
class freed_watch {
public:
    /* Watches <object>. A null one is never reported freed. */
    explicit freed_watch(const obj_data* object);
    /* Watches <character>. A null one is never reported freed. */
    explicit freed_watch(const char_data* character);
    ~freed_watch();

    freed_watch(const freed_watch&) = delete;
    freed_watch& operator=(const freed_watch&) = delete;

    /* True once the watched object or character has been freed: it must not be touched again. */
    bool freed() const { return m_freed; }

    /* Marks every watch on <object> as freed; for free_obj, which calls it for every object it
     * frees (through note_object_freed). */
    static void note_freed(const obj_data* object);
    /* Marks every watch on <character> as freed; for free_char, as above. */
    static void note_freed(const char_data* character);

private:
    explicit freed_watch(const void* watched);

    static void note_freed_address(const void* address);

    // The newest watch still in scope; each points on to the one made before it. Null when nothing
    // is watched, which is nearly always, so a free costs one test.
    static freed_watch* newest;

    // The watched object or character, compared by address and never read. A later one at the
    // same address cannot be mistaken for it: while it exists no other has its address, and once
    // it is freed the watch is already marked.
    const void* m_watched;
    // Set by note_freed when the watched object or character is freed.
    bool m_freed = false;
    // The watch made before this one, which is the newest again once this one goes.
    freed_watch* m_older;
};

/* Records everything freed while it is in scope: the objects and characters free_obj and free_char
 * report, the affects put_to_affected_type_pool frees and the list entries from_list_to_pool
 * frees. For a walk over a copy of a list whose steps can free entries the copy still names: the
 * walk skips an entry the log holds.
 *
 * A log answers only for things that existed when it was made: an address freed and then given to
 * something new stays in the log. A log must be a local variable: logs nest, and each one stops
 * recording when it goes out of scope. Addresses are compared and never read. */
class freed_log {
public:
    freed_log();
    ~freed_log();

    freed_log(const freed_log&) = delete;
    freed_log& operator=(const freed_log&) = delete;

    /* True if what was at <address> has been freed since this log was made. */
    bool contains(const void* address) const;

    /* Records <affect> as freed in every log in scope; for put_to_affected_type_pool. */
    static void note_freed(const affected_type* affect);
    /* Records <entry> as freed in every log in scope; for from_list_to_pool. */
    static void note_freed(const universal_list* entry);

private:
    /* freed_watch passes on the objects and characters it is told of. */
    friend class freed_watch;

    static void note_freed_address(const void* address);

    // The newest log still in scope; each points on to the one made before it. Null when nothing
    // is recording, which is nearly always, so a free costs one test.
    static freed_log* newest;

    // The addresses freed since this log was made, in the order they were freed.
    std::vector<const void*> m_freed;
    // The log made before this one, which is the newest again once this one goes.
    freed_log* m_older;
};

#endif /* FREED_WATCH_H */
