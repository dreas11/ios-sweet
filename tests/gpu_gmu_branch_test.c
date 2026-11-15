#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sweet_gpu_gmu_branch.h"
static uint32_t cc[0x2000/4], fence, reset_value;
static unsigned writes, reads_gmu, reads_cm3, scenario, ticks;
static uint32_t rd(void *cookie, unsigned off)
{
    assert(cookie==cc);
    if (scenario==6 && writes>=2 && off==SGB_CX) return UINT32_MAX;
    return cc[off/4];
}
static uint32_t gmurd(void *cookie, bool ao, unsigned off)
{
    assert(cookie==cc);reads_gmu++;
    if (ao) { assert(off==0x2c40); return fence; }
    reads_cm3++;
    if (off==0x2000) return scenario==3 && writes>=2 ? 0 : reset_value;
    if (off==0x2070) return 0xbabeface;
    assert(off==0x20b4);return 0x4052;
}
static void wr(void *cookie, unsigned off, uint32_t value)
{
    assert(cookie==cc && off==SGB_CX); /* No GMU/reset/firmware writes possible. */
    writes++;
    if (scenario==5 && writes==1) return;
    if (scenario==4 && writes==4) return;
    cc[off/4]=value;
    if (value&1) {
        if (scenario==1) cc[off/4]|=SGB_OFF;
        else cc[off/4]&=~SGB_OFF;
    } else {
        if (scenario==2 && writes>=3) cc[off/4]&=~SGB_OFF;
        else cc[off/4]|=SGB_OFF;
    }
}
static void pause_us(unsigned us) { ticks+=us; }
static void setup(void)
{
    memset(cc,0,sizeof(cc));fence=0;reset_value=1;
    writes=reads_gmu=reads_cm3=scenario=ticks=0;
    cc[0x1078/4]=1;cc[0x1540/4]=0x80001fe0;
    cc[0x1120/4]=2;cc[0x1124/4]=0x602;cc[SGB_CX/4]=0x80004220;
}
int main(void)
{
    struct sweet_gpu_gmu_io io={cc,rd,wr,gmurd,pause_us};
    struct sweet_gpu_gmu_result r;
    setup();assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_OK);
    assert(r.registers_read && r.changed && r.clock_running && r.restored && r.safe_to_stop_root);
    assert(r.running==0x4ff1 && r.after==r.before && r.after==0x80004220 && writes==4);
    assert(r.reset_before==1 && r.reset_after==1 && r.fw_result==0xbabeface);
    const unsigned regs[]={0x1078,0x1540,0x1120,0x1124,SGB_CX};
    for(unsigned i=0;i<5;i++){
        setup();cc[regs[i]/4]=UINT32_MAX;
        assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_GUARD && writes==0 && reads_gmu==0);
    }
    setup();cc[0x1078/4]|=SGB_OFF;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_GUARD && reads_gmu==0);
    setup();cc[SGB_CX/4]=0x4221;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_GUARD && writes==0);
    setup();fence=1;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_GUARD && reads_cm3==0 && writes==0);
    setup();fence=UINT32_MAX;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_GUARD && reads_cm3==0);
    setup();reset_value=0;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_GUARD && reads_cm3==1 && writes==0);
    setup();reset_value=UINT32_MAX;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_GUARD && writes==0);
    setup();scenario=1;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_TIMEOUT && r.restored && r.safe_to_stop_root && ticks==500);
    setup();scenario=2;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_RESTORE && !r.safe_to_stop_root && writes==3);
    setup();scenario=3;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_RESET_UNSAFE && !r.safe_to_stop_root && writes==2);
    assert(cc[SGB_CX/4]&1); /* Keep possibly running CM3 clocked. */
    setup();scenario=4;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_RESTORE && !r.safe_to_stop_root);
    setup();scenario=5;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_IO && r.safe_to_stop_root);
    setup();scenario=6;assert(sweet_gpu_gmu_branch_cycle(&io,&r)==SGB_RESTORE && !r.safe_to_stop_root && writes==2);
    puts("GMU reset-held branch tests PASS");
}
