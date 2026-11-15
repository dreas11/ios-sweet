#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sweet_gpu_power.h"
static uint32_t regs[0x2000/4];
static unsigned writes, ticks, scenario;
static uint32_t read_reg(void *cookie, unsigned off)
{
    assert(cookie == regs);
    if (scenario == 2 && off == SGC_HW && writes == 2) return UINT32_MAX;
    return regs[off/4];
}
static void write_reg(void *cookie, unsigned off, uint32_t value)
{
    assert(cookie == regs && off == SGC_CTRL); /* No clock/GX/core writes. */
    writes++;
    if ((scenario == 3 && writes == 1) || (scenario == 4 && writes == 3)) return;
    regs[off/4] = value;
    if (!(value & SGC_COLLAPSE) && scenario != 1) {
        regs[SGC_HW/4] = SGC_ON;
        regs[SGC_CTRL/4] |= SGC_ON;
        regs[SGC_AHB/4] = 1;
    } else {
        regs[SGC_HW/4] = 0;
        regs[SGC_CTRL/4] &= ~SGC_ON;
        regs[SGC_AHB/4] = SGC_ON | 1;
    }
}
static void pause_us(unsigned us) { ticks += us; }
static bool unsafe_probe(void *cookie) { assert(cookie==regs); return false; }
static void reset(void)
{
    memset(regs, 0, sizeof(regs)); writes = ticks = scenario = 0;
    regs[SGC_CTRL/4] = regs[SGC_GX/4] = 0x222001;
    regs[SGC_AHB/4] = SGC_ON | 1;
    regs[SGC_GMU/4] = 0x80004220;
}
int main(void)
{
    struct sweet_gpu_power_io io = {regs, read_reg, write_reg, pause_us, NULL};
    struct sweet_gpu_power_result r;
    reset();
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_OK);
    assert(writes == 4 && r.requested && r.restored && r.hw_powered == SGC_ON);
    assert(r.ahb_powered == 1 && r.after == 0x222001 && r.gx_after == 0x222001);
    assert(ticks >= 104);
    reset(); assert(sweet_gpu_cx_cycle(&io,16,320,0,&r) == SGC_GUARD && writes == 0);
    reset(); assert(sweet_gpu_cx_cycle(&io,256,16,0,&r) == SGC_GUARD && writes == 0);
    reset(); assert(sweet_gpu_cx_cycle(&io,256,320,16,&r) == SGC_GUARD && writes == 0);
    const unsigned offsets[] = {SGC_CTRL,SGC_HW,SGC_GX,SGC_AHB,SGC_GMU};
    for (unsigned i=0;i<5;i++) {
        reset(); regs[offsets[i]/4] = UINT32_MAX;
        assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_GUARD && writes == 0);
    }
    reset(); regs[SGC_CTRL/4] |= 2;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_GUARD && writes == 0);
    reset(); regs[SGC_HW/4] = SGC_ON;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_GUARD && writes == 0);
    reset(); regs[SGC_GX/4] = SGC_ON;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_GUARD && writes == 0);
    reset(); regs[SGC_GMU/4] |= 1;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_GUARD && writes == 0);
    reset(); scenario=1;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_TIMEOUT && r.restored);
    assert(ticks == 1102 && regs[SGC_CTRL/4] == 0x222001);
    reset(); scenario=2;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_IO && r.restored);
    reset(); scenario=3;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_IO && r.restored);
    reset(); scenario=4;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r) == SGC_RESTORE && !r.restored);
    reset(); io.powered_probe=unsafe_probe;
    assert(sweet_gpu_cx_cycle(&io,256,320,0,&r)==SGC_PROBE_UNSAFE && !r.restored && r.probe_called);
    assert(writes==2 && (regs[SGC_HW/4]&SGC_ON) && !(regs[SGC_CTRL/4]&SGC_COLLAPSE));
    puts("GPU CX cycle tests PASS");
}
