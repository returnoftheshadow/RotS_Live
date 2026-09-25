#pragma once

struct char_data;

// fight.cpp's raw_kill(), declared here because no production header declares it; the tests that
// kill a character the way a death does include this rather than repeating the declaration.
void raw_kill(char_data* dead_man, char_data* killer, int attack_type);
