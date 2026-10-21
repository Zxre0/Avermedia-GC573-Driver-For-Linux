// SPDX-License-Identifier: GPL-2.0-only
/* Pure policy checks, not kernel or hardware capture evidence. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "gc573_modes.h"

int main(void)
{
    assert(gc573_capture_profile_max_fps(2560,1440,16000,0) == 120);
    assert(gc573_capture_profile_max_fps(2560,1440,16000,1) == 60);
    assert(!gc573_capture_profile_max_fps(2560,1440,15999,1));
    assert(gc573_capture_profile_max_fps(1920,1080,8000,1) == 60);
    assert(!gc573_capture_profile_max_fps(3840,2160,32000,1));
    assert(gc573_capture_rate(144,gc573_capture_profile_max_fps(2560,1440,16000,1)) == 60);
    assert(gc573_input_period_supported(1920,1080,1666667,0));
    assert(gc573_input_period_supported(2560,1440,833333,0));
    assert(!gc573_input_period_supported(2560,1440,694444,0));
    assert(gc573_input_period_supported(2560,1440,694444,1));
    assert(gc573_input_period_supported(2560,1440,689656,1));
    assert(gc573_input_period_supported(2560,1440,699300,1));
    assert(gc573_input_period_supported(2560,1440,100000000U / 145,1));
    assert(gc573_input_period_supported(2560,1440,100000000U / 143,1));
    assert(!gc573_input_period_supported(2560,1440,689654,1));
    assert(!gc573_input_period_supported(2560,1440,699301,1));
    assert(!gc573_input_period_supported(1920,1080,694444,1));
    assert(!gc573_input_period_supported(3840,2160,694444,1));
    assert(!gc573_input_period_supported(2560,1440,0,1));
    assert(!gc573_input_period_supported(2560,1440,UINT_MAX,1));
    puts("PASS: opt-in 1440p timing bounds, <=60 capture, legacy defaults unchanged");
    return 0;
}
