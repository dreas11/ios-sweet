#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sweet_gpu_clock.h"
static uint32_t cc[0x2000/4], gcc[0x53000/4];
static unsigned writes, ticks, scenario, updates, configs;
static uint32_t rd(void *cookie, bool is_gcc, unsigned off)
{ assert(cookie==cc); return is_gcc ? gcc[off/4] : cc[off/4]; }
static void wr(void *cookie, bool is_gcc, unsigned off, uint32_t value)
{
    assert(cookie==cc); writes++;
    if (is_gcc) { assert(off==SGR_GATE); gcc[off/4]=value; return; }
    assert(off==SGR_CFG || off==SGR_CMD); /* Never PLL or child-branch writes. */
    if (off==SGR_CFG) {
        configs++;
        if (scenario==5 && configs==2) return;
        cc[off/4]=value; return;
    }
    cc[off/4]=value;
    if (value & SGR_UPDATE) {
        updates++;
        if (scenario!=1) cc[off/4]&=~SGR_UPDATE;
    }
    if (value & SGR_FORCE) {
        if (scenario!=2) cc[off/4]&=~SGR_OFF;
        gcc[SGR_GATE/4]|=0x80; /* Concurrent unrelated voter must survive. */
    } else if (scenario!=4 || writes<5) cc[off/4]|=SGR_OFF;
    else cc[off/4]&=~SGR_OFF;
}
static void pause_us(unsigned us) { ticks+=us; }
static void reset(void)
{
    memset(cc,0,sizeof(cc));memset(gcc,0,sizeof(gcc));writes=ticks=scenario=updates=configs=0;
    gcc[0]=SGR_OFF|5;gcc[4/4]=31;gcc[0x38/4]=0x4000;gcc[0xc/4]=7;gcc[0x2c/4]=1;
    gcc[SGR_GATE/4]=0x40;
    cc[SGR_CMD/4]=SGR_OFF;cc[SGR_CFG/4]=1;
    cc[SGR_CX/4]=0x80004220;cc[SGR_GX/4]=SGR_OFF;
}
int main(void)
{
    struct sweet_gpu_clock_io io={cc,rd,wr,pause_us};struct sweet_gpu_clock_result r;
    reset();assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_OK);
    assert(r.running && r.changed && r.safe_to_collapse && r.cfg_running==0x602);
    assert(r.cmd_running==SGR_FORCE && r.cmd_after==SGR_OFF && r.cfg_after==1);
    assert(gcc[SGR_GATE/4]==0xc0 && writes==8);
    reset();gcc[SGR_GATE/4]|=SGR_GATE_BIT;cc[SGR_CFG/4]=0x40000602;
    assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_OK && r.cfg_after==0x40000602);
    assert(gcc[SGR_GATE/4] & SGR_GATE_BIT);
    const unsigned go[]={0,4,0x38,0xc,0x2c,SGR_GATE};
    const unsigned co[]={SGR_CMD,SGR_CFG,SGR_CX,SGR_GX};
    for(unsigned i=0;i<6;i++){reset();gcc[go[i]/4]=UINT32_MAX;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_GUARD && writes==0 && r.safe_to_collapse);}
    for(unsigned i=0;i<4;i++){reset();cc[co[i]/4]=UINT32_MAX;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_GUARD && writes==0);}
    reset();gcc[4/4]=30;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_GUARD && writes==0);
    reset();cc[SGR_CX/4]|=1;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_GUARD && writes==0);
    reset();cc[SGR_GX/4]|=1;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_GUARD && writes==0);
    reset();cc[SGR_CFG/4]=0x501;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_GUARD && writes==0);
    reset();cc[SGR_CMD/4]|=SGR_UPDATE;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_GUARD && writes==0);
    reset();scenario=1;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_RESTORE && !r.safe_to_collapse);
    assert(gcc[SGR_GATE/4]&SGR_GATE_BIT); /* Uncertain update: keep supply. */
    reset();scenario=2;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_TIMEOUT && r.safe_to_collapse);
    reset();scenario=4;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_RESTORE && !r.safe_to_collapse);
    assert(gcc[SGR_GATE/4]&SGR_GATE_BIT);
    reset();scenario=5;assert(sweet_gpu_gmu_root_cycle(&io,&r)==SGR_RESTORE && !r.safe_to_collapse);
    puts("GPU root clock tests PASS");
}
