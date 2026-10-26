#ifndef FREED_WATCH_H
#define FREED_WATCH_H

struct char_data;
struct obj_data;

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

#endif /* FREED_WATCH_H */
