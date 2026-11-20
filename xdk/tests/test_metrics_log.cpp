#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../../src/stream_metrics.h"
#include "../../src/diagnostic_ring.h"
int main(void)
{
    stream_observations m = {};
    stream_observe(&m, 100, 1000, 41, 100, 2100, 3100);
    assert(m.have_host && m.host_us == 4100 && m.have_timing);
    assert(m.assembly_us == 2000 && m.queue_us == 1000 && m.frame_gaps == 0);
    stream_observe(&m, 103, 2000, 0, 100, 2100, 3100);
    assert(m.frame_gaps == 2 && m.host_us == 4100 && m.payload_bytes == 3000);
    stream_observe(&m, 102, -100, 0, 0, 0, 0);
    stream_observe(&m, 104, 0, 0, 0, 0, 0);
    assert(m.frame_gaps == 2 && m.payload_bytes == 3000);
    m.last_frame = UINT32_MAX;
    stream_observe(&m, 1, 0, 0, 0, 0, 0); assert(m.frame_gaps == 3);
    memset(&m,0,sizeof(m));
    stream_observe(&m, 1, 0, 0, 0, 0, 500000);
    assert(!m.have_host && !m.have_timing);
    stream_observe(&m, 2, 0, 1, 3000, 2000, 1000);
    assert(m.host_us == 100 && !m.have_timing);
    stream_counters a = {}, b = {40,40,38,1000000};
    stream_rates r = stream_rates_between(&a,&b,1000);
    assert(r.received == 40 && r.decoded == 40 && r.shown == 38 && r.payload_mbps == 8.0);
    r = stream_rates_between(&a,&b,0); assert(r.received == 0 && r.payload_mbps == 0);
    a.received = UINT32_MAX - 9; b.received = 30;
    a.payload_bytes = UINT32_MAX - 499999; b.payload_bytes = 500000;
    r = stream_rates_between(&a,&b,1000);
    assert(r.received == 40 && r.payload_mbps == 8.0);

    diagnostic_ring ring = {};
    char text[DIAG_SNAPSHOT_BYTES], guard[8];
    diagnostic_ring_read(&ring,text,sizeof(text)); assert(text[0] == 0);
    diagnostic_ring_add(&ring,7,"first\nsecond\r\nthird\tvalue");
    diagnostic_ring_read(&ring,text,sizeof(text));
    assert(strcmp(text,"7 first\n7 second\n7 third value\n") == 0);
    memset(&ring,0,sizeof(ring));
    for (int i=0;i<100;i++) {
        char line[32]; snprintf(line,sizeof(line),"entry-%d",i);
        diagnostic_ring_add(&ring,(uint32_t)i,line);
    }
    diagnostic_ring_read(&ring,text,sizeof(text));
    assert(strncmp(text,"36 entry-36\n",12) == 0 && strstr(text,"99 entry-99\n"));
    assert(ring.count == 64);
    memset(guard,'X',sizeof(guard));
    assert(diagnostic_ring_read(&ring,guard,0) == 0 && guard[0] == 'X');
    diagnostic_ring_read(&ring,guard,1); assert(guard[0] == 0 && guard[1] == 'X');
    char long_line[1000]; memset(long_line,'A',999); long_line[999] = 0;
    diagnostic_ring_add(&ring,UINT32_MAX,long_line);
    unsigned last=(ring.next+DIAG_LOG_LINES-1)%DIAG_LOG_LINES;
    assert(strlen(ring.lines[last]) == DIAG_LOG_WIDTH-1);
    puts("PASS: telemetry units/rates/missing values/counter wrap/gaps and bounded log split/wrap/truncation/guards.");
}
