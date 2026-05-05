#pragma once

struct SearchHudState {
    int current_one_based = 0;
    int total             = 0;
    int hovered_button    = -1;   // -1 none, 0 prev, 1 next
};
