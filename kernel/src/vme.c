#include "klib.h"
#include "vme.h"
#include "proc.h"

static TSS32 tss;

typedef union free_page{
  union free_page * next;
  char buf[PGSIZE];
} page_t;

page_t * free_page_list;

void init_gdt() {
  static SegDesc gdt[NR_SEG];
  gdt[SEG_KCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_KERN);
  gdt[SEG_KDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_KERN);
  gdt[SEG_UCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_USER);
  gdt[SEG_UDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_USER);
  gdt[SEG_TSS]   = SEG16(STS_T32A,     &tss,  sizeof(tss)-1, DPL_KERN);
  set_gdt(gdt, sizeof(gdt[0]) * NR_SEG);
  set_tr(KSEL(SEG_TSS));
}

void set_tss(uint32_t ss0, uint32_t esp0) {
  tss.ss0 = ss0;
  tss.esp0 = esp0;
}

static PD kpd;
static PT kpt[PHY_MEM / PT_SIZE] __attribute__((used));

void init_page() {
  extern char end;
  panic_on((size_t)(&end) >= KER_MEM - PGSIZE, "Kernel too big (MLE)");
  static_assert(sizeof(PTE) == 4, "PTE must be 4 bytes");
  static_assert(sizeof(PDE) == 4, "PDE must be 4 bytes");
  static_assert(sizeof(PT) == PGSIZE, "PT must be one page");
  static_assert(sizeof(PD) == PGSIZE, "PD must be one page");
  // Lab1-4: init kpd and kpt, identity mapping of [0 (or 4096), PHY_MEM)
  //TODO();
  for(int i = 0 ; i < 32 ; i++){
    kpd.pde[i].val = MAKE_PDE((uint32_t) &kpt[i], 1);
    for(int j = 0; j < 1024 ; j++){
      kpt[i].pte[j].val = MAKE_PTE((uint32_t) (i << DIR_SHIFT | j << TBL_SHIFT), 1);
    }
  }
  kpt[0].pte[0].val = 0;
  set_cr3(&kpd);
  set_cr0(get_cr0() | CR0_PG);
  // Lab1-4: init free memory at [KER_MEM, PHY_MEM), a heap for kernel
  // TODO();
  for(int i = 0; i < 0x7e00; i++){// num of free pages is 0x7e00
    page_t* temp = (void *)PAGE_DOWN((KER_MEM + i * PGSIZE));
    temp->next = free_page_list;
    free_page_list = temp;
  }
}

void *kalloc() {
  // Lab1-4: alloc a page from kernel heap, abort when heap empty
  // TODO();
  assert(free_page_list != NULL);
  
  page_t* temp  = free_page_list; 
  int pd_index = ADDR2DIR(temp);
  PDE* pde = &(kpd.pde[pd_index]);
  PT* pt = PDE2PT(*pde);
  int pt_index = ADDR2TBL(temp);
  PTE* pte = &(pt->pte[pt_index]);
  pte->present = 1;

  free_page_list = free_page_list->next;

  memset(temp, 0, PGSIZE);
  return temp;
}

void kfree(void *ptr) {
  // Lab1-4: free a page to kernel heap
  // you can just do nothing :)
  // TODO();

  page_t* temp = (void*)PAGE_DOWN(ptr);

  memset(temp, 0x1234, PGSIZE);

  temp->next = free_page_list;
  free_page_list = temp;

  int pd_index = ADDR2DIR(temp);
  PDE* pde = &(kpd.pde[pd_index]);
  PT* pt = PDE2PT(*pde);
  int pt_index = ADDR2TBL(temp);
  PTE* pte = &(pt->pte[pt_index]);
  pte->present = 0;
  set_cr3(vm_curr());
}

PD *vm_alloc() {
  // Lab1-4: alloc a new pgdir, map memory under PHY_MEM identityly
  // TODO();
  PD* pgdir = kalloc();
  for(int i = 0 ; i < NR_PDE; i++){
    if(i < 32)
      pgdir->pde[i].val = MAKE_PDE((uint32_t) &kpt[i], 1);
    else
      pgdir->pde[i].val = 0;
  }
  return pgdir;
}

void vm_teardown(PD *pgdir) {
  // Lab1-4: free all pages mapping above PHY_MEM in pgdir, then free itself
  // you can just do nothing :)
  //TODO();
  for(int i = 32; i < NR_PDE; i++){
    PDE* pde = &(pgdir->pde[i]);
    if(pde->present == 0) continue;
    PT* pt = PDE2PT(*pde);
    for(int j = 0; j < NR_PTE; j++){
      PTE* pte = &(pt->pte[j]);
      if(pte->present == 0) continue;
      void* page = PTE2PG(*pte);
      kfree(page);
      pte->present = 0;
    }
    kfree(pt);
    pde->present = 0;
  }
  kfree(pgdir);
}

PD *vm_curr() {
  return (PD*)PAGE_DOWN(get_cr3());
}

PTE *vm_walkpte(PD *pgdir, size_t va, int prot) {
  // Lab1-4: return the pointer of PTE which match va
  // if not exist (PDE of va is empty) and prot&1, alloc PT and fill the PDE
  // if not exist (PDE of va is empty) and !(prot&1), return NULL
  // remember to let pde's prot |= prot, but not pte
  assert((prot & ~7) == 0);
  //TODO();
  int pd_index = ADDR2DIR(va);
  PDE* pde = &(pgdir->pde[pd_index]);
  if(pde->present == 1){
    PT* pt = PDE2PT(*pde);
    int pt_index = ADDR2TBL(va);
    PTE* pte = &(pt->pte[pt_index]);
    return pte;
  }
  else if(prot != 0){
    PT* pt = kalloc();
    pde->val = MAKE_PDE(pt, prot);
    int pt_index = ADDR2TBL(va);
    PTE* pte = &(pt->pte[pt_index]);
    return pte;
  }
  pde->val |= prot;
  return NULL;
}

void *vm_walk(PD *pgdir, size_t va, int prot) {
  // Lab1-4: translate va to pa
  // if prot&1 and prot voilation ((pte->val & prot & 7) != prot), call vm_pgfault
  // if va is not mapped and !(prot&1), return NULL
  //TODO();
  PTE* pte = vm_walkpte(pgdir, va, prot);
  void* page = PTE2PG(*pte);
  void* pa = (void*)((uint32_t) page | ADDR2OFF(va));
  return pa;
}

void vm_map(PD *pgdir, size_t va, size_t len, int prot) {
  // Lab1-4: map [PAGE_DOWN(va), PAGE_UP(va+len)) at pgdir, with prot
  // if have already mapped pages, just let pte->prot |= prot
  assert(prot & PTE_P);
  assert((prot & ~7) == 0);
  size_t start = PAGE_DOWN(va);
  size_t end = PAGE_UP(va + len);
  assert(start >= PHY_MEM);
  assert(end >= start);
  //TODO();
  for(; start <= end; start += PGSIZE){
    PTE* pte = vm_walkpte(pgdir, start, prot);
    if(pte->present == 0){
      void* pa = kalloc();
      pte->val = MAKE_PTE(pa, prot);
    }
    else{
      pte->val |= prot;
    }
  }
}

void vm_unmap(PD *pgdir, size_t va, size_t len) {
  // Lab1-4: unmap and free [va, va+len) at pgdir
  // you can just do nothing :)
  assert(ADDR2OFF(va) == 0);
  assert(ADDR2OFF(len) == 0);
  //TODO();
  size_t start = va;
  size_t end = va + len;
  for(; start <= end; start += PGSIZE){
    int pd_index = ADDR2DIR(start);
    PDE* pde = &(pgdir->pde[pd_index]);
    PT* pt = PDE2PT(*pde);
    int pt_index = ADDR2TBL(start);
    PTE* pte = &(pt->pte[pt_index]);
    if(pte->present == 1){
      page_t* page = PTE2PG(*pte);
      kfree(page);
      pte->present = 0;
      set_cr3(vm_curr());
    }
    else
      continue;
  }
  if(pgdir == vm_curr())
    set_cr3(vm_curr());
}

void vm_copycurr(PD *pgdir) {
  // Lab2-2: copy memory mapped in curr pd to pgdir
  //TODO();
  for(uint32_t va = PHY_MEM; va < USR_MEM; va += PGSIZE){
    // note the prot ! ! !
    PTE* pte = vm_walkpte(vm_curr(), va, 7);
    if(pte->present == 1){
      vm_map(pgdir, va, PGSIZE, 7);
      void* page = vm_walk(pgdir, va, 7);
      memcpy(page, (void *)va, PGSIZE);
    }
  }
}

void vm_pgfault(size_t va, int errcode) {
  printf("pagefault @ 0x%p, errcode = %d\n", va, errcode);
  panic("pgfault");
}
