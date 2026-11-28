/* color.h */

#ifndef COLOR_H
#define COLOR_H

/* Number big enough to hold the fields below */
inline constexpr int MAX_COLOR_FIELDS = 16;

inline constexpr int COLOR_NARR = 0;
inline constexpr int COLOR_CHAT = 1;
inline constexpr int COLOR_YELL = 2;
inline constexpr int COLOR_TELL = 3;
inline constexpr int COLOR_SAY = 4;
inline constexpr int COLOR_ROOM = 5;
inline constexpr int COLOR_HIT = 6;
inline constexpr int COLOR_DAMG = 7;
inline constexpr int COLOR_CHAR = 8;
#define COLOR_OBJ 9
#define COLOR_ENMY 10
#define COLOR_DESC 11
#define COLOR_GTELL 12
#define COLOR_MAGIC 13
#define COLOR_WEATHER 14
#define COLOR_MOB 15

inline constexpr int CNRM = 0;
inline constexpr int CRED = 1;
inline constexpr int CGRN = 2;
inline constexpr int CYEL = 3;
inline constexpr int CBLU = 4;
inline constexpr int CMAG = 5;
inline constexpr int CCYN = 6;
inline constexpr int CWHT = 7;
inline constexpr int CBRED = 8;
inline constexpr int CBGRN = 9;
inline constexpr int CBYEL = 10;
inline constexpr int CBBLU = 11;
#define CBMAG 12
#define CBCYN 13
#define CBWHT 14

enum color_value_mode {
    COLOR_VALUE_DEFAULT = 0,
    COLOR_VALUE_ANSI16 = 1,
    COLOR_VALUE_TRUECOLOR = 2,
};

struct color_value_data {
    unsigned char mode = COLOR_VALUE_DEFAULT;
    unsigned char ansi = CNRM;
    unsigned char red = 0;
    unsigned char green = 0;
    unsigned char blue = 0;
};

struct color_slot_data {
    color_value_data foreground;
    color_value_data background;
};

/* TRUE if 'ch' has colors turned on */
#define clr(ch) (PRF_FLAGGED((ch), PRF_COLOR) ? 1 : 0)

#define CC_USE(ch, col) \
    (clr(ch) ? get_color_sequence((ch), (col)) : "")
#define CC_NORM(ch) \
    (clr((ch)) ? color_sequence[0] : "")
#define CC_FIX(ch, col) \
    (clr((ch)) ? color_sequence[col] : "")

extern const char* color_color[];
extern const char* color_fields[];
extern char* color_sequence[];

const char* get_color_sequence(const struct char_data*, int);
void set_truecolor_foreground(const struct char_data*, int, int, int, int);
void set_truecolor_background(const struct char_data*, int, int, int, int);
void set_ansi_background(const struct char_data*, int, int);
void clear_color_background(const struct char_data*, int);
void clear_color_foreground(const struct char_data*, int);
int nearest_ansi_color(int red, int green, int blue);
void set_colors_default(struct char_data*);
int char_color_slot(const struct char_data*);

#endif /* COLOR_H */
