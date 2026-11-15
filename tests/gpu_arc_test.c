#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RH_ACTIVE_N 2u
#define RH_TCS_STRIDE 0x2a0u
#define RH_CMD_STRIDE 0x14u
#define RH_IRQ_EN 0u
#define RH_IRQ_STATUS 4u
#define RH_IRQ_CLEAR 8u
#define RH_WAIT 0x10u
#define RH_CONTROL 0x14u
#define RH_STATUS 0x18u
#define RH_ENABLE 0x1cu
#define RH_MSGID 0x30u
#define RH_ADDR 0x34u
#define RH_DATA 0x38u
#define RH_CMD_STATUS 0x3cu
#define RH_AMC_EN (1u << 16)
#define RH_TRIGGER (1u << 24)
#define RH_ISSUED (1u << 8)
#define RH_COMPLETE (1u << 16)
#define RH_DB_HEADER 144u
#define RH_DB_ENTRY 24u
#define RH_TIMEOUT_US 100u
typedef uintptr_t vm_offset_t;
static uint32_t gic_enabled, gic_active, response_value;
static bool map_fail;
static vm_offset_t ml_io_map(vm_offset_t pa, size_t size)
{
    assert(pa == 0x17a00000 && size == 0x1000);
    return map_fail ? 0 : 3;
}
static uint32_t regs[0x3000/4], db[1024/4];
static unsigned rh_ncpt = 16, rh_lock, ticks, writes, scenario;
static bool rh_poisoned, rh_touch_ready;
static uintptr_t rh_tcs = 1, rh_db = 2;
static uint32_t rh_read(uintptr_t base, uint32_t offset)
{
    if (base == 3) {
        assert(offset == 0x104 || offset == 0x304);
        return offset == 0x104 ? gic_enabled : gic_active;
    }
    return base == rh_db ? db[offset/4] : regs[offset/4];
}
static void rh_write(uintptr_t base, uint32_t offset, uint32_t value)
{
    assert(base == rh_tcs);
    writes++;
    if (offset == RH_IRQ_CLEAR) {
        regs[RH_IRQ_STATUS/4] &= ~value;
        return;
    }
    if (offset % RH_TCS_STRIDE == RH_MSGID) {
        assert(!(value & (1u << 16))); /* No resource WRITE command. */
    }
    regs[offset/4] = value;
    if (offset == RH_CONTROL && value == (RH_AMC_EN | RH_TRIGGER)) {
        regs[RH_CMD_STATUS/4] = RH_ISSUED | RH_COMPLETE;
        regs[0x40/4] = response_value; /* Preserve raw responses, don't invent units. */
        if (scenario == 0 && (regs[RH_IRQ_EN/4] & 1)) {
            regs[RH_IRQ_STATUS/4] |= 1;
        }
    }
}
static bool rh_sync(uint32_t off, uint32_t value)
{
    rh_write(rh_tcs, off, value);
    return rh_read(rh_tcs, off) == value;
}
static void delay(unsigned us) { ticks += us; }
static int rh_map(void) { return 0; }
static uint16_t rh_db16(uint32_t off)
{
    return (uint16_t)(db[off/4] >> (8u * (off & 3u)));
}
static bool rh_range(uint32_t off, uint32_t size)
{
    return off <= sizeof(db) && size <= sizeof(db) - off;
}
static bool rh_free(unsigned t)
{
    unsigned b = t * RH_TCS_STRIDE;
    return regs[(b+RH_STATUS)/4] != 0 && regs[(b+RH_STATUS)/4] != UINT32_MAX &&
        !regs[(b+RH_CONTROL)/4] && !regs[(b+RH_ENABLE)/4] &&
        !regs[(b+RH_WAIT)/4] && !(regs[RH_IRQ_STATUS/4] & (1u<<t));
}
#include "sweet_rpmh_arc.inc"

static void reset(void)
{
    memset(regs, 0, sizeof(regs)); memset(db, 0, sizeof(db));
    regs[RH_STATUS/4] = regs[(RH_TCS_STRIDE+RH_STATUS)/4] = 1;
    regs[RH_IRQ_EN/4] = 0x55;
    regs[RH_IRQ_STATUS/4] = 0x80; /* Unrelated interrupt must survive. */
    ticks = writes = scenario = rh_lock = 0;
    response_value = 0x76543210;
    gic_enabled = gic_active = 0; map_fail = false; rh_arc_gicd = 0;
    rh_poisoned = false; rh_touch_ready = true;
}
static void put16(unsigned off, uint16_t v)
{
    unsigned shift = (off & 3u) * 8u;
    db[off/4] = (db[off/4] & ~(0xffffu << shift)) | (uint32_t)v << shift;
}
static void dictionary(void)
{
    /* Header at 8, one ARC entry at 144, aux at 168. */
    put16(8, 3); put16(10, 0); put16(12, 24); put16(14, 1);
    memcpy((char *)db+144, "cx.lvl", 6);
    db[160/4] = 0x30000; put16(164, 8); put16(166, 0);
    put16(168, 0); put16(170, 16); put16(172, 64); put16(174, 128);
}
int main(void)
{
    uint16_t good[] = {0,16,48,64,128,256,0,0};
    uint16_t sweet_cx[16] = {0,16,64,128,192,256,384};
    uint16_t sweet_mx[16] = {0,16,192,320,384};
    uint16_t sweet_gfx[16] = {0,16,64,128,192,256,320,384,416};
    uint16_t descending[] = {0,128,64}, hole[] = {0,16,0,64};
    assert(sweet_arc_level_count(good, 8) == 6);
    assert(sweet_arc_level_index(good, 8, 64) == 3);
    assert(sweet_arc_level_index(good, 8, 65) == 4);
    assert(sweet_arc_level_index(good, 8, 257) == -1);
    assert(sweet_arc_level_count(descending, 3) == -1);
    assert(sweet_arc_level_count(hole, 4) == -1);
    assert(sweet_arc_level_count(good, 17) == -1);
    assert(sweet_arc_level_count(NULL, 0) == -1);
    assert(sweet_arc_level_count(sweet_cx, 16) == 7);
    assert(sweet_arc_level_count(sweet_mx, 16) == 5);
    assert(sweet_arc_level_count(sweet_gfx, 16) == 9);
    assert(sweet_arc_level_index(sweet_mx, 16, 64) == 2);
    assert(sweet_mx[sweet_arc_level_index(sweet_mx, 16, 64)] == 192);
    reset(); dictionary();
    uint16_t levels[16]; unsigned count = 0; uint32_t address = 0, response = 0;
    assert(!rh_arc_lookup("cx.lvl", &address, levels, &count));
    assert(address == 0x30000 && count == 4 && levels[2] == 64);
    put16(166, 0xffff); assert(rh_arc_lookup("cx.lvl", &address, levels, &count) == EINVAL);
    reset(); dictionary(); put16(164, 33);
    assert(rh_arc_lookup("cx.lvl", &address, levels, &count) == EINVAL);
    reset();
    assert(!rh_arc_read(0x30000, &response));
    assert(response == 0x76543210 && !rh_poisoned && ticks == 0);
    assert(rh_free(0) && regs[RH_IRQ_EN/4] == 0x55 && regs[RH_IRQ_STATUS/4] == 0x80);
    reset(); regs[RH_IRQ_EN/4] = 0x54;
    assert(!rh_arc_read(0x30000, &response));
    assert(regs[RH_IRQ_EN/4] == 0x54 && rh_free(0));
    reset(); gic_enabled = 1u << 5;
    assert(rh_arc_read(0x30000, &response) == EBUSY && writes == 0);
    reset(); gic_active = 1u << 5;
    assert(rh_arc_read(0x30000, &response) == EBUSY && writes == 0);
    reset(); gic_enabled = UINT32_MAX;
    assert(rh_arc_read(0x30000, &response) == EBUSY && writes == 0);
    reset(); gic_active = UINT32_MAX;
    assert(rh_arc_read(0x30000, &response) == EBUSY && writes == 0);
    reset(); map_fail = true;
    assert(rh_arc_read(0x30000, &response) == ENOMEM && writes == 0);
    reset(); scenario = 1;
    assert(rh_arc_read(0x30000, &response) == ETIMEDOUT);
    assert(rh_poisoned && ticks == RH_TIMEOUT_US && !(regs[RH_IRQ_EN/4] & 1));
    assert(regs[RH_IRQ_STATUS/4] == 0x80); /* Stale completed status is insufficient. */
    reset(); regs[RH_ENABLE/4] = regs[(RH_TCS_STRIDE+RH_ENABLE)/4] = 1;
    assert(rh_arc_read(0x30000, &response) == EBUSY && writes == 0);
    reset(); rh_touch_ready = false;
    assert(sweet_rpmh_gpu_arc_read("cx.lvl") == EAGAIN && writes == 0 && !rh_lock);
    reset(); rh_poisoned = true;
    assert(sweet_rpmh_gpu_arc_read("cx.lvl") == EIO && writes == 0 && !rh_lock);
    assert(sweet_rpmh_gpu_arc_read("ldoc11") == EINVAL && writes == 0);
    reset(); dictionary(); response_value = 2;
    unsigned level = 999;
    assert(!sweet_rpmh_gpu_arc_level("cx.lvl", &level) && level == 64);
    reset(); dictionary(); level = 999;
    assert(sweet_rpmh_gpu_arc_level("cx.lvl", &level) == EINVAL && level == 999);
    assert(sweet_rpmh_gpu_arc_level("cx.lvl", NULL) == EINVAL);
    puts("ARC policy and transaction tests PASS");
    return 0;
}
