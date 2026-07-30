/***************************************************************************
// libiopmp tests for SRCMD_FMT=2 & MDCFG_FMT=0.
//
// SRCMD_EN(H) is replaced by the MD-indexed SRCMD_PERM(H), while the
// MDCFG table stays the baseline one.
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
#define CFG_RRID_NUM    32

/* SRCMD_PERM(m) and SRCMD_PERMH(m) as the reference model lays them out */
#define SRCMD_PERM_ADDR(m)  (SRCMD_TABLE_BASE_OFFSET + (m) * SRCMD_REG_STRIDE)
#define SRCMD_PERMH_ADDR(m) (SRCMD_PERM_ADDR(m) + 4)

static uint64_t read_srcmd_perm_64(uint32_t mdidx)
{
    uint64_t lo = read_register(&iopmp_dev, SRCMD_PERM_ADDR(mdidx), 4);
    uint64_t hi = read_register(&iopmp_dev, SRCMD_PERMH_ADDR(mdidx), 4);

    return (hi << 32) | lo;
}

/* SRCMD_EN(H) belongs to SRCMD_FMT=0, so libiopmp has to refuse these before
 * it reaches the NULL operations this model leaves behind. */
static int check_srcmd_fmt_0_apis_refused(IOPMP_t *iopmp)
{
    uint64_t mds = 0x1;

    if (iopmp_set_rrid_md_association(iopmp, 0, 0x1, 0, &mds, false) !=
        IOPMP_ERR_NOT_SUPPORTED) {
        return -1;
    }
    if (iopmp_lock_srcmd_table_fmt_0(iopmp, 0) != IOPMP_ERR_NOT_SUPPORTED) {
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
    cfg.sps_en = false;
    cfg.stall_en = true;
    cfg.mfr_en = true;
    cfg.mdcfg_fmt = 0;
    cfg.srcmd_fmt = 2;
    cfg.md_entry_num = 0;
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
    START_TEST("Initialize SRCMD_FMT=2 & MDCFG_FMT=0");
    FAIL_IF(libiopmp_setup(&iopmp, &cfg) != IOPMP_OK);
    FAIL_IF(iopmp_get_srcmd_fmt(&iopmp) != IOPMP_SRCMD_FMT_2);
    FAIL_IF(iopmp_get_mdcfg_fmt(&iopmp) != IOPMP_MDCFG_FMT_0);
    END_TEST();

    START_TEST("SRCMD_FMT=2 implicitly associates every MD with every RRID");
    FAIL_IF(iopmp_get_rrid_md_association(&iopmp, 1, &val_u64,
                                          &val_bool) != IOPMP_OK);
    FAIL_IF(val_u64 != (((uint64_t)0x1 << CFG_MD_NUM) - 1));
    FAIL_IF(val_bool != true);
    END_TEST();

    START_TEST("Set and get a single RRID's permission to a MD");
    val_bool = true;    /* r */
    {
        bool w = false;

        FAIL_IF(iopmp_set_md_permission(&iopmp, 3, 1, &val_bool,
                                        &w) != IOPMP_OK);
        FAIL_IF(val_bool != true || w != false);
    }
    /* RRID 3 occupies SRCMD_PERM(1) bits 7:6 */
    FAIL_IF(read_srcmd_perm_64(1) != ((uint64_t)0x1 << 6));
    END_TEST();

    START_TEST("Set every RRID's permission to a MD at once");
    {
        IOPMP_SRCMD_PERM_CFG_t perm_cfg;

        IOPMP_SRCMD_PERM_CFG_SET_DIRECT(&perm_cfg, UINT64_MAX, 0x3);
        FAIL_IF(iopmp_set_md_permission_multi(&iopmp, 2,
                                              &perm_cfg) != IOPMP_OK);
    }
    FAIL_IF(read_srcmd_perm_64(2) != 0x3);
    END_TEST();

    START_TEST("Locking a MD's SRCMD table sets its MDLCK bit");
    FAIL_IF(iopmp_is_srcmd_table_fmt_2_locked(&iopmp, 4,
                                              &val_bool) != IOPMP_OK);
    FAIL_IF(val_bool != false);
    FAIL_IF(iopmp_lock_srcmd_table_fmt_2(&iopmp, 4) != IOPMP_OK);
    FAIL_IF(iopmp_is_srcmd_table_fmt_2_locked(&iopmp, 4,
                                              &val_bool) != IOPMP_OK);
    FAIL_IF(val_bool != true);
    /* SRCMD_FMT=2 has no lock of its own: MDLCK.md is the lock */
    val_u64 = (uint64_t)0x1 << 4;
    FAIL_IF(iopmp_lock_md(&iopmp, &val_u64, false) != IOPMP_OK);
    {
        bool r = true, w = true;

        FAIL_IF(iopmp_set_md_permission(&iopmp, 0, 4, &r, &w) !=
                IOPMP_ERR_REG_IS_LOCKED);
    }
    END_TEST();

    START_TEST("SRCMD_EN(H) APIs are refused");
    FAIL_IF(check_srcmd_fmt_0_apis_refused(&iopmp) != 0);
    END_TEST();

    START_TEST("MDCFG_FMT=0 programs and locks the MDCFG table");
    val_u32 = 4;
    FAIL_IF(iopmp_set_md_entry_association(&iopmp, 0, &val_u32) != IOPMP_OK);
    FAIL_IF(val_u32 != 4);
    FAIL_IF(iopmp_get_md_entry_association(&iopmp, 0, &val_u32,
                                           &val_u32_2) != IOPMP_OK);
    FAIL_IF(val_u32 != 0 || val_u32_2 != 4);
    val_u32 = 1;
    FAIL_IF(iopmp_lock_mdcfg(&iopmp, &val_u32, false) != IOPMP_OK);
    FAIL_IF(val_u32 != 1);
    /* MDCFG(0) is below MDCFGLCK.f now, so it no longer takes a write */
    val_u32 = 8;
    FAIL_IF(iopmp_set_md_entry_association(&iopmp, 0, &val_u32) !=
            IOPMP_ERR_REG_IS_LOCKED);
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
