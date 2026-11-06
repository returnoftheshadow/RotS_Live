#ifndef __PKILL_H__
#define __PKILL_H__

/*
 * XXX: kill_time should be time_t, killer and victim should be
 * longs.  These cannot be changed without updating the binary
 * pkill file, but this SHOULD be done.
 *
 * These values are stored in the file as idnums, but are
 * converted to player table indexes when the file is read.
 */
typedef struct {
    int kill_time;
    int killer;
    int victim;
    unsigned char killer_level;
    unsigned char victim_level;
    int killer_points;
    int victim_points; /* Victim points are stored as negative values */
} PKILL;

void boot_pkills();
void pkill_create(const struct char_data*);

void pkill_unref_character(const struct char_data* c);
void pkill_unref_character_by_index(int);

int pkill_get_total();

inline constexpr int PKILL_STRING_KILLED = 0;
inline constexpr int PKILL_STRING_SLAIN = 1;
char* pkill_get_string(const PKILL*, int);

PKILL* pkill_get_new_kills(int*);

/* XXX: Should eventually be removed */
PKILL* pkill_get_all(int*);

int pkill_get_good_fame();
int pkill_get_evil_fame();

typedef struct {
    char* name;
    int player_idx;
    int rank;
    int fame;
    int race;
    int side;
    int invalid; /* Non-zero means this leader was invalid */
} LEADER;

inline constexpr int PKILL_UNRANKED = -1;
LEADER* pkill_get_leader_by_rank(int, int);
void pkill_free_leader(LEADER*);

int pkill_get_rank_by_character(const struct char_data* c, bool totalRank);
int pkill_get_totalrank_by_character_id(int idx, bool totalRank);

#endif /* __PKILL_H__ */
