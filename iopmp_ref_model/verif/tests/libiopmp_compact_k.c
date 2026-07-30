/***************************************************************************
// libiopmp tests for the Compact-k Model: SRCMD_FMT=1 & MDCFG_FMT=1.
//
// RRID s is hardwired to MD s and there is no MDCFG table either: every MD
// owns a fixed HWCFG3.md_entry_num+1 entries.
***************************************************************************/

#include "iopmp.h"
#include "config.h"
#include "test_utils.h"

#include "libiopmp.h"

// Create IOPMP instance
iopmp_dev_t iopmp_dev = {0};
iopmp_cfg_t cfg = {0};

/* Override libiopmp IO functions */
uint32_t io_read32(uintptr_t addr)
{
    return read_register(&iopmp_dev, addr, 4);
}

void io_write32(uintptr_t addr, uint32_t val)
{
    return write_register(&iopmp_dev, addr, val, 4);
}

/* Reset the reference model with @cfg, then initialize @iopmp on top of it.
 * Returns IOPMP_ERR_NOT_AVAILABLE if @cfg is an illegal configuration, so that
 * one never leaves the model silently holding its previous state. */
static enum iopmp_error libiopmp_setup(IOPMP_t *iopmp, iopmp_cfg_t *cfg)
{
    if (reset_iopmp(&iopmp_dev, cfg) != 0) {
        return IOPMP_ERR_NOT_AVAILABLE;
    }

    memset(iopmp, 0, sizeof(*iopmp));
    return iopmp_init(iopmp, 0, cfg->srcmd_fmt, cfg->mdcfg_fmt,
                      IOPMP_IMPID_NOT_SPECIFIED);
}

#define CFG_MD_NUM      63
#define CFG_ENTRY_NUM   512
#define CFG_RRID_NUM    63

/* SRCMD_FMT=1 has no SRCMD table, so every operation that reaches one is left
 * NULL and libiopmp has to refuse the API before it dispatches. */
static int check_srcmd_apis_refused(IOPMP_t *iopmp)
{
    IOPMP_SRCMD_PERM_CFG_t perm_cfg;
    uint64_t mds = 0x1;
    bool r = true, w = true;

    /* SRCMD_EN(H) and its lock bit only exist in SRCMD_FMT=0 */
    if (iopmp_set_rrid_md_association(iopmp, 0, 0x1, 0, &mds, false) !=
        IOPMP_ERR_NOT_SUPPORTED) {
        return -1;
    }
    if (iopmp_lock_srcmd_table_fmt_0(iopmp, 0) != IOPMP_ERR_NOT_SUPPORTED) {
        return -1;
    }

    /* SRCMD_PERM(H) only exists in SRCMD_FMT=2 */
    if (iopmp_set_md_permission(iopmp, 0, 0, &r, &w) !=
        IOPMP_ERR_NOT_SUPPORTED) {
        return -1;
    }
    IOPMP_SRCMD_PERM_CFG_SET_DIRECT(&perm_cfg, UINT64_MAX, 0x3);
    if (iopmp_set_md_permission_multi(iopmp, 0, &perm_cfg) !=
        IOPMP_ERR_NOT_SUPPORTED) {
        return -1;
    }
    if (iopmp_lock_srcmd_table_fmt_2(iopmp, 0) != IOPMP_ERR_NOT_SUPPORTED) {
        return -1;
    }

    return 0;
}

int main(void)
{
    IOPMP_t iopmp = {0};
    struct iopmp_entry entry = {0};
    struct iopmp_entry rb_entry = {0};
    uint32_t val_u32, val_u32_2;
    uint64_t val_u64;
    bool val_bool;

    // Configure your IOPMP when reset
    cfg.vendor = 1;
    cfg.specver = 1;
    cfg.impid = 0;
    cfg.no_err_rec = false;
    cfg.md_num = CFG_MD_NUM;
    cfg.addrh_en = true;
    cfg.tor_en = true;
    cfg.rrid_num = CFG_RRID_NUM;
    cfg.entry_num = CFG_ENTRY_NUM;
    cfg.prio_entry = 16;
    cfg.prio_ent_prog = false;
    cfg.non_prio_en = true;
    cfg.msi_en = true;
    cfg.peis = true;
    cfg.pees = true;
    /* SRCMD_FMT=1 has no SRCMD table for the SPS registers to extend */
    cfg.sps_en = false;
    cfg.stall_en = true;
    cfg.mfr_en = true;
    cfg.mdcfg_fmt = 1;
    cfg.srcmd_fmt = 1;
    cfg.md_entry_num = 3;
    cfg.xinr = false;
    cfg.no_x = false;
    cfg.no_w = false;
    cfg.rrid_transl_en = true;
    cfg.rrid_transl_prog = false;
    cfg.rrid_transl = 48;
    cfg.entryoffset = 0x2000;
    cfg.granularity = MIN_GRANULARITY;
    cfg.imp_mdlck = true;
    cfg.imp_err_reqid_eid = true;
    cfg.imp_rridscp = true;
    cfg.imp_stall_buffer = true;
    cfg.stall_buffer_size = 32;

    /* Start unit tests */

#if (SRC_ENFORCEMENT_EN == 0)
    START_TEST("Initialize the Compact-k Model");
    FAIL_IF(libiopmp_setup(&iopmp, &cfg) != IOPMP_OK);
    FAIL_IF(iopmp_get_srcmd_fmt(&iopmp) != IOPMP_SRCMD_FMT_1);
    FAIL_IF(iopmp_get_mdcfg_fmt(&iopmp) != IOPMP_MDCFG_FMT_1);
    END_TEST();

    START_TEST("SRCMD_FMT=1 hardwires RRID s to MD s");
    for (uint32_t s = 0; s < 4; s++) {
        FAIL_IF(iopmp_get_rrid_md_association(&iopmp, s, &val_u64,
                                              &val_bool) != IOPMP_OK);
        FAIL_IF(val_u64 != ((uint64_t)0x1 << s));
        FAIL_IF(val_bool != true);
    }
    /* A RRID beyond HWCFG1.rrid_num has no MD */
    FAIL_IF(iopmp_get_rrid_md_association(&iopmp, CFG_RRID_NUM, &val_u64,
                                          &val_bool) !=
            IOPMP_ERR_OUT_OF_BOUNDS);
    END_TEST();

    START_TEST("MDLCK reads back locked and never reaches set_md_lock");
    /* iopmp_drv_init_common() presets MDLCK for SRCMD_FMT=1, because the
     * associations are hardwired. That preset is the only thing keeping
     * iopmp_lock_md() away from the NULL set_md_lock operation. */
    val_u64 = (uint64_t)0x1;
    FAIL_IF(iopmp_lock_md(&iopmp, &val_u64, true) != IOPMP_OK);
    val_u64 = (uint64_t)0x1;
    FAIL_IF(iopmp_lock_md(&iopmp, &val_u64, false) != IOPMP_ERR_REG_IS_LOCKED);
    /* Asking for every implemented MD is still a no-op, not a write */
    val_u64 = ((uint64_t)0x1 << CFG_MD_NUM) - 1;
    FAIL_IF(iopmp_lock_md(&iopmp, &val_u64, true) != IOPMP_OK);
    /* A MD the instance does not implement is still out of bounds */
    val_u64 = (uint64_t)0x1 << CFG_MD_NUM;
    FAIL_IF(iopmp_lock_md(&iopmp, &val_u64, true) != IOPMP_ERR_NOT_SUPPORTED);
    END_TEST();

    START_TEST("SRCMD table APIs are refused");
    FAIL_IF(check_srcmd_apis_refused(&iopmp) != 0);
    END_TEST();

    START_TEST("Each MD owns md_entry_num+1 entries");
    /* MDCFG_FMT=1 has no MDCFG table: MD m owns entries [m*K, m*K+K-1] */
    for (uint32_t m = 0; m < 4; m++) {
        FAIL_IF(iopmp_get_md_entry_association(&iopmp, m, &val_u32,
                                               &val_u32_2) != IOPMP_OK);
        FAIL_IF(val_u32 != m * 4);
        FAIL_IF(val_u32_2 != 4);
    }
    END_TEST();

    START_TEST("There is no MDCFG table to program or lock");
    val_u32 = 1;
    FAIL_IF(iopmp_lock_mdcfg(&iopmp, &val_u32, false) !=
            IOPMP_ERR_NOT_SUPPORTED);
    FAIL_IF(iopmp_set_md_entry_association(&iopmp, 0, &val_u32) !=
            IOPMP_ERR_NOT_ALLOWED);
    END_TEST();

    START_TEST("HWCFG3.md_entry_num is fixed outside MDCFG_FMT=2");
    val_u32 = 7;
    FAIL_IF(iopmp_set_md_entry_num(&iopmp, &val_u32) != IOPMP_ERR_NOT_ALLOWED);
    END_TEST();

    START_TEST("Write and read back an entry");
    entry.addr = 0x80000000 >> 2;
    entry.cfg = IOPMP_ENTRY_A_NAPOT | IOPMP_ENTRY_R | IOPMP_ENTRY_W;
    FAIL_IF(iopmp_set_entry(&iopmp, &entry, 1) != IOPMP_OK);
    FAIL_IF(iopmp_get_entry(&iopmp, &rb_entry, 1) != IOPMP_OK);
    FAIL_IF(rb_entry.addr != entry.addr || rb_entry.cfg != entry.cfg);
    END_TEST();
#endif

    return 0;
}
