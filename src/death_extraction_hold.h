#ifndef DEATH_EXTRACTION_HOLD_H
#define DEATH_EXTRACTION_HOLD_H

struct char_data;

/* Holds back a script's EXTRACT_CHAR of one dying character while die() runs its ON_DIE trigger,
 * so that die() can finish the death (experience, the kill log) before the character goes. The
 * extraction is only recorded: die() then extracts the character itself, in place of the corpse
 * an ordinary death leaves.
 *
 * A hold must be a local variable: holds nest (a death script that kills another), and each one
 * stops holding when it goes out of scope. The dying character is compared by address only. */
class death_extraction_hold {
public:
    /* Holds back extractions of <dying>. */
    explicit death_extraction_hold(const char_data* dying);
    ~death_extraction_hold();

    death_extraction_hold(const death_extraction_hold&) = delete;
    death_extraction_hold& operator=(const death_extraction_hold&) = delete;

    /* True once a script asked to extract the dying character. */
    bool extraction_requested() const { return m_requested; }

    /* For EXTRACT_CHAR: true if <character> is held back, with the request recorded; it must then
     * not be extracted now. False, with nothing recorded, otherwise. */
    static bool hold_extraction(const char_data* character);

private:
    // The newest hold in scope; each points on to the one made before it. Null when no death is
    // running its ON_DIE trigger, which is nearly always.
    static death_extraction_hold* newest;

    // The dying character, compared by address.
    const char_data* m_dying;
    // Set by hold_extraction when a script asks to extract the dying character.
    bool m_requested = false;
    // The hold made before this one, the newest again once this one goes.
    death_extraction_hold* m_older;
};

#endif /* DEATH_EXTRACTION_HOLD_H */
