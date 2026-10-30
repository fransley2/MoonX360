#pragma once
#include <stdint.h>
/* Axial transfer function; preserves signed endpoints and avoids the -32768 abs overflow. */
static int16_t stick_calibrate(int16_t value,int minimum,int maximum)
{
    int limit=value<0?32768:32767, magnitude=value<0?-(int)value:(int)value;
    int low,high,result;
    minimum=minimum<0?0:minimum>30?30:minimum;
    maximum=maximum<50?50:maximum>100?100:maximum;
    if(!minimum && maximum==100)return value;
    low=limit*minimum/100;high=limit*maximum/100;
    if(magnitude<=low)return 0;
    if(magnitude>=high)return (int16_t)(value<0?-limit:limit);
    result=(int)(((int64_t)(magnitude-low)*limit)/(high-low));
    return (int16_t)(value<0?-result:result);
}
