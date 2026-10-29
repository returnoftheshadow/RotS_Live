#ifndef OBJ2HTML_H
#define OBJ2HTML_H

struct obj_data;

/* A table type for obj2html; see the type tables in obj2html.cpp. */
struct obj2html_type {
    char* title; /* Title for a table of this type */
    char** keywords;
    unsigned long typemask;
    unsigned long (*get_typemask)(struct obj_data*);
    struct obj2html_type* subtypes;
    struct obj_data** to_output; /* For runtime use */
    int num_to_output; /* For runtime use */
    unsigned long valid_types; /* For runtime use */
};

/* The top-level types; [ITEM_x - 1] is type ITEM_x. */
extern struct obj2html_type obj2html_types[];

/* Clears the run-time selection and output lists. */
void obj2html_clear(struct obj2html_type* list);

/* Marks every type or subtype with keyword arg; returns how many matched. */
int check_keywords(struct obj2html_type* list, const char* arg);

/* Whether list[i] is taken by the keywords given (see obj2html.cpp). */
int obj2html_taken(const struct obj2html_type* list, int i);

#endif
