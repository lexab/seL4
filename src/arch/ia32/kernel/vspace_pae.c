/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <model/statedata.h>
#include <arch/kernel/vspace.h>

#ifdef CONFIG_PAE_PAGING

/* The boot pd is referenced by code that runs before paging, so
 * place it in PHYS_DATA. In PAE mode the top level is actually
 * a PDPTE, but we call it _boot_pd for compatibility */
pdpte_t _boot_pd[BIT(PDPT_BITS)] __attribute__((aligned(BIT(PAGE_BITS)))) PHYS_DATA;
/* Allocate enough page directories to fill every slot in the PDPT */
pde_t _boot_pds[BIT(PD_BITS + PDPT_BITS)] __attribute__((aligned(BIT(PAGE_BITS)))) PHYS_DATA;

BOOT_CODE
pde_t *get_boot_pd()
{
    /* return a pointer to the continus array of boot pds */
    return _boot_pds;
}

/* These functions arefrom what is generated by the bitfield tool. It is
 * required by functions that need to call it before the MMU is turned on.
 * Any changes made to the bitfield generation need to be replicated here.
 */
PHYS_CODE
static inline void
pdpte_ptr_new_phys(pdpte_t *pdpte_ptr, uint32_t pd_base_address, uint32_t avl, uint32_t cache_disabled, uint32_t write_through, uint32_t present)
{
    pdpte_ptr->words[0] = 0;
    pdpte_ptr->words[1] = 0;

    pdpte_ptr->words[0] |= (pd_base_address & 0xfffff000) >> 0;
    pdpte_ptr->words[0] |= (avl & 0x7) << 9;
    pdpte_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pdpte_ptr->words[0] |= (write_through & 0x1) << 3;
    pdpte_ptr->words[0] |= (present & 0x1) << 0;
}

PHYS_CODE
static inline void
pde_pde_large_ptr_new_phys(pde_t *pde_ptr, uint32_t page_base_address,
                           uint32_t pat, uint32_t avl, uint32_t global, uint32_t dirty,
                           uint32_t accessed, uint32_t cache_disabled, uint32_t write_through,
                           uint32_t super_user, uint32_t read_write, uint32_t present)
{
    pde_ptr->words[0] = 0;
    pde_ptr->words[1] = 0;

    pde_ptr->words[0] |= (page_base_address & 0xffe00000) >> 0;
    pde_ptr->words[0] |= (pat & 0x1) << 12;
    pde_ptr->words[0] |= (avl & 0x7) << 9;
    pde_ptr->words[0] |= (global & 0x1) << 8;
    pde_ptr->words[0] |= ((uint32_t)pde_pde_large & 0x1) << 7;
    pde_ptr->words[0] |= (dirty & 0x1) << 6;
    pde_ptr->words[0] |= (accessed & 0x1) << 5;
    pde_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pde_ptr->words[0] |= (write_through & 0x1) << 3;
    pde_ptr->words[0] |= (super_user & 0x1) << 2;
    pde_ptr->words[0] |= (read_write & 0x1) << 1;
    pde_ptr->words[0] |= (present & 0x1) << 0;

}

PHYS_CODE VISIBLE void
init_boot_pd(void)
{
    unsigned int i;

    /* first map in all the pds into the pdpt */
    for (i = 0; i < BIT(PDPT_BITS); i++) {
        uint32_t pd_base = (uint32_t)&_boot_pds[i * BIT(PD_BITS)];
        pdpte_ptr_new_phys(
            _boot_pd + i,
            pd_base,    /* pd_base_address */
            0,          /* avl */
            0,          /* cache_disabled */
            0,          /* write_through */
            1           /* present */
        );
    }

    /* identity mapping from 0 up to PPTR_BASE (virtual address) */
    for (i = 0; (i << IA32_2M_bits) < PPTR_BASE; i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pds + i,
            i << IA32_2M_bits, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }

    /* mapping of PPTR_BASE (virtual address) to PADDR_BASE up to end of virtual address space */
    for (i = 0; (i << IA32_2M_bits) < -PPTR_BASE; i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pds + i + (PPTR_BASE >> IA32_2M_bits),
            (i << IA32_2M_bits) + PADDR_BASE, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }
}

BOOT_CODE void
map_it_pd_cap(cap_t pd_cap)
{
    pdpte_t *pdpt = PDPTE_PTR(cap_page_directory_cap_get_capPDMappedObject(pd_cap));
    pde_t *pd = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(pd_cap));
    uint32_t index = cap_page_directory_cap_get_capPDMappedIndex(pd_cap);

    pdpte_ptr_new(
        pdpt + index,
        pptr_to_paddr(pd),
        0, /* avl */
        0, /* cache_disabled */
        0, /* write_through */
        1  /* present */
    );
    invalidatePageStructureCache();
}

/* ==================== BOOT CODE FINISHES HERE ==================== */

void copyGlobalMappings(void* new_vspace)
{
    unsigned int i;
    pdpte_t *pdpt = (pdpte_t*)new_vspace;

    for (i = PPTR_BASE >> IA32_1G_bits; i < BIT(PDPT_BITS); i++) {
        pdpt[i] = ia32KSkernelPDPT[i];
    }
}

bool_t CONST isVTableRoot(cap_t cap)
{
    return cap_get_capType(cap) == cap_pdpt_cap;
}

bool_t CONST isValidVTableRoot(cap_t cap)
{
    return isVTableRoot(cap);
}

void *getValidVSpaceRoot(cap_t vspace_cap)
{
    if (isValidVTableRoot(vspace_cap)) {
        return PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(vspace_cap));
    }
    return NULL;
}

static inline pdpte_t *lookupPDPTSlot(void *vspace, vptr_t vptr)
{
    pdpte_t *pdpt = PDPT_PTR(vspace);
    return pdpt + (vptr >> IA32_1G_bits);
}

lookupPDSlot_ret_t lookupPDSlot(void *vspace, vptr_t vptr)
{
    pdpte_t *pdptSlot;
    lookupPDSlot_ret_t ret;

    pdptSlot = lookupPDPTSlot(vspace, vptr);

    if (!pdpte_ptr_get_present(pdptSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(PAGE_BITS + PT_BITS + PD_BITS);
        ret.pdSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        pde_t *pd;
        pde_t *pdSlot;
        unsigned int pdIndex;

        pd = paddr_to_pptr(pdpte_ptr_get_pd_base_address(pdptSlot));
        pdIndex = (vptr >> (PAGE_BITS + PT_BITS)) & MASK(PD_BITS);
        pdSlot = pd + pdIndex;

        ret.pdSlot = pdSlot;
        ret.pd = pd;
        ret.pdIndex = pdIndex;
        ret.status = EXCEPTION_NONE;
        return ret;
    }
}

void unmapPageDirectory(pdpte_t *pdpt, uint32_t pdptIndex, pde_t *pd)
{
    cap_t threadRoot;
    pdpt[pdptIndex] = pdpte_new(
                          0, /* pdpt_base_address */
                          0, /* val */
                          0, /* cache_disabled */
                          0, /* write_through */
                          0  /* present */
                      );
    /* according to the intel manual if we modify a pdpt we must
     * reload cr3 */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidVTableRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == (void*)pdpt) {
        write_cr3(read_cr3());
    }
}

void unmapAllPageDirectories(pdpte_t *pdpt)
{
    uint32_t i;

    for (i = 0; i < PPTR_USER_TOP >> IA32_1G_bits; i++) {
        if (pdpte_ptr_get_present(pdpt + i)) {
            pde_t *pd = PD_PTR(paddr_to_pptr(pdpte_ptr_get_pd_base_address(pdpt + i)));
            cte_t *pdCte;
            cap_t pdCap;
            pdCte = cdtFindAtDepth(capSpaceTypedMemory, PD_REF(pd), BIT(PD_SIZE_BITS), 0, (uint32_t)(pdpt + i), pdpte_ptr_get_avl_cte_depth(pdpt + i));
            assert(pdCte);

            pdCap = pdCte->cap;
            pdCap = cap_page_directory_cap_set_capPDMappedObject(pdCap, 0);
            cdtUpdate(pdCte, pdCap);
        }
    }
}

void flushAllPageDirectories(pdpte_t *pdpt)
{
    cap_t threadRoot;
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(threadRoot)) == pdpt) {
        invalidateTLB();
    }
}

void flushPageSmall(pte_t *pt, uint32_t ptIndex)
{
    cap_t threadRoot;
    cte_t *ptCte;
    pde_t *pd;
    uint32_t pdIndex;

    /* We know this pt can only be mapped into one single pd. So
     * lets find a cap with that mapping information */
    ptCte = cdtFindWithExtra(capSpaceTypedMemory, PT_REF(pt), BIT(PT_SIZE_BITS), 0, cte_depth_bits_type(cap_page_table_cap));
    if (ptCte) {
        pd = PD_PTR(cap_page_table_cap_get_capPTMappedObject(ptCte->cap));
        pdIndex = cap_page_table_cap_get_capPTMappedIndex(ptCte->cap);

        if (pd) {
            cte_t *pdCte;
            pdpte_t *pdpt;
            uint32_t pdptIndex;
            pdCte = cdtFindWithExtra(capSpaceTypedMemory, PD_REF(pd), BIT(PD_SIZE_BITS), 0, cte_depth_bits_type(cap_page_directory_cap));
            if (pdCte) {
                pdpt = PDPT_PTR(cap_page_directory_cap_get_capPDMappedObject(pdCte->cap));
                pdptIndex = cap_page_directory_cap_get_capPDMappedIndex(pdCte->cap);

                /* check if page belongs to current address space */
                threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
                if (isValidVTableRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == pdpt) {
                    invalidateTLBentry( (pdptIndex << (PD_BITS + PT_BITS + PAGE_BITS)) | (pdIndex << (PT_BITS + PAGE_BITS)) | (ptIndex << PAGE_BITS));
                    invalidatePageStructureCache();
                }
            }
        }
    }
}

void flushPageLarge(pde_t *pd, uint32_t pdIndex)
{
    cap_t threadRoot;
    cte_t *pdCte;

    pdCte = cdtFindWithExtra(capSpaceTypedMemory, PD_REF(pd), BIT(PD_SIZE_BITS), 0, cte_depth_bits_type(cap_page_directory_cap));
    if (pdCte) {
        pdpte_t *pdpt;
        uint32_t pdptIndex;
        pdpt = PDPT_PTR(cap_page_directory_cap_get_capPDMappedObject(pdCte->cap));
        pdptIndex = cap_page_directory_cap_get_capPDMappedIndex(pdCte->cap);

        /* check if page belongs to current address space */
        threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
        if (cap_get_capType(threadRoot) == cap_pdpt_cap &&
                PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(threadRoot)) == pdpt) {
            invalidateTLBentry( (pdIndex << (PT_BITS + PAGE_BITS)) | (pdptIndex << IA32_1G_bits));
            invalidatePageStructureCache();
        }
    }
}

void flushAllPageTables(pde_t *pd)
{
    cap_t threadRoot;
    cte_t *pdCte;

    pdCte = cdtFindWithExtra(capSpaceTypedMemory, PD_REF(pd), BIT(PD_SIZE_BITS), 0, cte_depth_bits_type(cap_page_directory_cap));
    if (pdCte) {
        pdpte_t *pdpt;
        pdpt = PDPT_PTR(cap_page_directory_cap_get_capPDMappedObject(pdCte->cap));
        /* check if this is the current address space */
        threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
        if (cap_get_capType(threadRoot) == cap_pdpt_cap &&
                PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(threadRoot)) == pdpt) {
            invalidateTLB();
        }
        invalidatePageStructureCache();
    }
}

void flushPageDirectory(pdpte_t *pdpt, uint32_t pdptIndex, pde_t *pd)
{
    flushAllPageDirectories(pdpt);
}

exception_t
decodeIA32PageDirectoryInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    word_t          vaddr;
    vm_attributes_t attr;
    pdpte_t *       pdpt;
    pdpte_t *       pdptSlot;
    unsigned int    pdptIndex;
    cap_t           vspaceCap;
    pdpte_t         pdpte;
    paddr_t         paddr;
    cap_t           threadRoot;

    if (label == IA32PageDirectoryUnmap) {
        setThreadState(ksCurThread, ThreadState_Restart);

        pdpt = PDPTE_PTR(cap_page_directory_cap_get_capPDMappedObject(cap));
        if (pdpt) {
            pde_t *pd = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(cap));
            pdptIndex = cap_page_directory_cap_get_capPDMappedIndex(cap);
            unmapPageDirectory(pdpt, pdptIndex, pd);
            flushPageDirectory(pdpt, pdptIndex, pd);
            clearMemory((void *)pd, cap_get_capSizeBits(cap));
        }
        cdtUpdate(cte, cap_page_directory_cap_set_capPDMappedObject(cap, 0));

        return EXCEPTION_NONE;
    }

    if (label != IA32PageDirectoryMap) {
        userError("IA32PageDirectory: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || extraCaps.excaprefs[0] == NULL) {
        userError("IA32PageDirectory: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cdtCapFindWithExtra(cap)) {
        userError("IA32PageDirectory: Page direcotry is already mapped to a pdpt.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer) & (~MASK(IA32_1G_bits));
    attr = vmAttributesFromWord(getSyscallArg(1, buffer));
    vspaceCap = extraCaps.excaprefs[0]->cap;

    if (!isValidVTableRoot(vspaceCap)) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    pdpt = (void*)pptr_of_cap(vspaceCap);

    if (vaddr >= PPTR_USER_TOP) {
        userError("IA32PageDirectory: Mapping address too high.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    pdptIndex = vaddr >> IA32_1G_bits;
    pdptSlot = pdpt + pdptIndex;

    if (pdpte_ptr_get_present(pdptSlot)) {
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr(PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(cap)));
    pdpte = pdpte_new(
                paddr,                                      /* pd_base_address  */
                mdb_node_get_cdtDepth(cte->cteMDBNode),     /* avl_cte_depth    */
                vm_attributes_get_ia32PCDBit(attr),         /* cache_disabled   */
                vm_attributes_get_ia32PWTBit(attr),         /* write_through    */
                1                                           /* present          */
            );

    cap = cap_page_directory_cap_set_capPDMappedObject(cap, PDPT_REF(pdpt));
    cap = cap_page_directory_cap_set_capPDMappedIndex(cap, pdptIndex);

    cdtUpdate(cte, cap);
    *pdptSlot = pdpte;

    /* according to the intel manual if we modify a pdpt we must
     * reload cr3 */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidVTableRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == (void*)pptr_of_cap(vspaceCap)) {
        write_cr3(read_cr3());
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    invalidatePageStructureCache();
    return EXCEPTION_NONE;
}

#endif
