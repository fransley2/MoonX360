#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../../src/timing.h"

int main(void)
{
    assert(moonx_ticks_to_us(0,50000000)==0);
    assert(moonx_ticks_to_us(50000000,50000000)==1000000);
    assert(moonx_ticks_to_us(50000001,50000000)==1000000);
    assert(moonx_ticks_to_us(0,0)==0);
    const uint64_t four_days=4ULL*24*60*60;
    assert(moonx_ticks_to_us(four_days*50000000ULL,50000000)==four_days*1000000ULL);
    assert(moonx_ticks_to_us((four_days+1)*50000000ULL,50000000)==(four_days+1)*1000000ULL);
    puts("PASS: QPC conversion handles long uptime and zero frequency");
}
