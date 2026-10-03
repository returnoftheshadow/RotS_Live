#ifndef ORDINAL_H
#define ORDINAL_H

#include <string>
#include <string_view>

// The suffix that makes a number an ordinal: "st", "nd", "rd" or "th". Only 11, 12 and 13
// themselves take "th", so 111 is "111st"; this matches nth(), which the game's texts use.
// The view refers to a string literal and never allocates.
std::string_view ordinal_suffix(int number);

// Appends the number and its ordinal_suffix(), such as "5th", to the end of out_text. Allocates
// only if out_text lacks the capacity.
void append_ordinal(std::string& out_text, int number);

#endif // ORDINAL_H
