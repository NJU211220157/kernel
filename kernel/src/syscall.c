#include "klib.h"
#include "cte.h"
#include "sysnum.h"
#include "vme.h"
#include "serial.h"
#include "loader.h"
#include "proc.h"
#include "timer.h"
#include "file.h"

typedef int (*syshandle_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

extern void *syscall_handle[NR_SYS];

void do_syscall(Context *ctx) {
  // TODO: Lab1-5 call specific syscall handle and set ctx register
  int sysnum = ctx->eax;
  uint32_t arg1 = ctx->ebx;
  uint32_t arg2 = ctx->ecx;
  uint32_t arg3 = ctx->edx;
  uint32_t arg4 = ctx->esi;
  uint32_t arg5 = ctx->edi;
  int res;
  if (sysnum < 0 || sysnum >= NR_SYS) {
    res = -1;
  } else {
    res = ((syshandle_t)(syscall_handle[sysnum]))(arg1, arg2, arg3, arg4, arg5);
  }
  ctx->eax = res;
}

int sys_write(int fd, const void *buf, size_t count) {
  // TODO: rewrite me at Lab3-1
  file_t* fp = proc_getfile(proc_curr(), fd);
  if(fp == NULL)  return -1;
  return fwrite(fp, buf, count);
  //return serial_write(buf, count);
}

int sys_read(int fd, void *buf, size_t count) {
  // TODO: rewrite me at Lab3-1
  file_t* fp = proc_getfile(proc_curr(), fd);
  if(fp == NULL)  return -1;
  return fread(fp, buf, count);
  //return serial_read(buf, count);
}

int sys_brk(void *addr) {
  // TODO: Lab1-5
  // static size_t brk = 0; // use brk of proc instead of this in Lab2-1
  proc_t* c_pcb = proc_curr();
  size_t new_brk = PAGE_UP(addr);
  if (c_pcb->brk == 0) {
    c_pcb->brk = new_brk;
  } else if (new_brk > c_pcb->brk) {
    //TODO();
    PD* pgdir = vm_curr();
    vm_map(pgdir, c_pcb->brk, new_brk - c_pcb->brk, 7);
    c_pcb->brk = new_brk;
  } else if (new_brk < c_pcb->brk) {
    PD* pgdir = vm_curr();
    vm_unmap(pgdir, new_brk, c_pcb->brk - new_brk);
    c_pcb->brk = new_brk;
  }
  return 0;
}

void sys_sleep(int ticks) {
  //TODO(); // Lab1-7
  int tick = get_tick();
  while((get_tick() - tick) < ticks){
    //sti(); hlt(); cli();
    proc_yield();
  }
}

int sys_exec(const char *path, char *const argv[]) {
  //TODO(); // Lab1-8, Lab2-1
  PD* pgdir = vm_alloc();
  Context ctx;
  int val = load_user(pgdir, &ctx, path, argv);
  if(val != 0){
    kfree(pgdir);
    return -1;
  }
  else{
    PD* temp = vm_curr();
    set_cr3(pgdir);
    proc_curr()->pgdir = vm_curr();
    kfree(temp);
    irq_iret(&ctx);
  }
  return 0;
}

int sys_getpid() {
  //TODO(); // Lab2-1
  return proc_curr()->pid;
}

void sys_yield() {
  proc_yield();
}

int sys_fork() {
  //TODO(); // Lab2-2
  proc_t* proc = proc_alloc();
  if(proc == NULL)  return -1;
  proc_copycurr(proc);
  proc_addready(proc);
  return proc->pid;
}

void sys_exit(int status) {
  //TODO(); // Lab2-3
  proc_makezombie(proc_curr(), status);
  INT(0x81);
  assert(0);
}

int sys_wait(int *status) {
  //TODO(); // Lab2-3, Lab2-4
  if(proc_curr()->child_num == 0) return -1;
  /*
  proc_t* child = proc_findzombie(proc_curr());
  while(child == NULL){
    proc_yield();
    child = proc_findzombie(proc_curr());
  }*/
  sem_p(&proc_curr()->zombie_sem);
  proc_t* child = proc_findzombie(proc_curr());
  if(status != NULL){
    *status = child->exit_code;
  }
  int pid = child->pid;
  proc_free(child);
  proc_curr()->child_num--;
  return pid;
}

int sys_sem_open(int value) {
  //TODO(); // Lab2-5
  int i = proc_allocusem(proc_curr());
  if(i == -1) return -1;
  usem_t* usem =  usem_alloc(value);
  if(usem == NULL)  return -1;
  proc_curr()->usems[i] = usem;
  return i;
}
 
int sys_sem_p(int sem_id) {
  //TODO(); // Lab2-5
  usem_t* usem = proc_getusem(proc_curr(), sem_id);
  if(usem == NULL)  return -1;
  sem_p(&usem->sem);
  return 0;
}

int sys_sem_v(int sem_id) {
  //TODO(); // Lab2-5
  usem_t* usem = proc_getusem(proc_curr(), sem_id);
  if(usem == NULL)  return -1;
  sem_v(&usem->sem);
  return 0;
}

int sys_sem_close(int sem_id) {
  //TODO(); // Lab2-5
  usem_t* usem = proc_getusem(proc_curr(), sem_id);
  if(usem == NULL)  return -1;
  usem_close(usem);
  proc_curr()->usems[sem_id] = NULL;
  return 0;
}

int sys_open(const char *path, int mode) {
  //TODO(); // Lab3-1
  int i = proc_allocfile(proc_curr());
  if(i == -1) return -1;
  file_t * fp = fopen(path, mode);
  if(fp == NULL)  return -1;
  proc_curr()->files[i] = fp;
  return i;
}

int sys_close(int fd) {
  //TODO(); // Lab3-1
  file_t* fp = proc_getfile(proc_curr(), fd);
  if(fp == NULL)  return -1;
  fclose(fp);
  proc_curr()->files[fd] = NULL;
  return 0;
}

int sys_dup(int fd) {
  //TODO(); // Lab3-1
  file_t* fp = proc_getfile(proc_curr(), fd);
  if(fp == NULL)  return -1;
  int i = proc_allocfile(proc_curr());
  if( i == -1)  return -1;
  fdup(fp);
  proc_curr()->files[i] = fp;
  return i;
}

uint32_t sys_lseek(int fd, uint32_t off, int whence) {
  //TODO(); // Lab3-1
  file_t* fp = proc_getfile(proc_curr(), fd);
  if(fp == NULL)  return -1;
  return fseek(fp, off, whence);
}

int sys_fstat(int fd, struct stat *st) {
  //TODO(); // Lab3-1
  file_t* fp = proc_getfile(proc_curr(), fd);
  if(fp == NULL)  return -1;
  if(fp->type == TYPE_FILE){
    st->type = itype(fp->inode);
    st->size = isize(fp->inode);
    st->node = ino(fp->inode);
    return 0;
  }
  else if(fp->type == TYPE_DEV){
    st->type = TYPE_DEV;
    st->node = 0;
    st->size = 0;
    return 0;
  }
  return -1;
}


int sys_chdir(const char *path) {
  //TODO(); // Lab3-2
  inode_t* fp = iopen(path, TYPE_NONE);
  if(fp == NULL){
    return -1;
  }
  if(itype(fp) != TYPE_DIR){
    iclose(fp);
    return -1;
  }
  iclose(proc_curr()->cwd);
  proc_curr()->cwd = fp;
  return 0;
}

int sys_unlink(const char *path) {
  return iremove(path);
}

// optional syscall

void *sys_mmap() {
  TODO();
}

void sys_munmap(void *addr) {
  TODO();
}

int sys_clone(void (*entry)(void*), void *stack, void *arg) {
  TODO();
}

int sys_kill(int pid) {
  TODO();
}

int sys_cv_open() {
  TODO();
}

int sys_cv_wait(int cv_id, int sem_id) {
  TODO();
}

int sys_cv_sig(int cv_id) {
  TODO();
}

int sys_cv_sigall(int cv_id) {
  TODO();
}

int sys_cv_close(int cv_id) {
  TODO();
}

int sys_pipe(int fd[2]) {
  TODO();
}

int sys_link(const char *oldpath, const char *newpath) {
  TODO();
}

int sys_symlink(const char *oldpath, const char *newpath) {
  TODO();
}

void *syscall_handle[NR_SYS] = {
  [SYS_write] = sys_write,
  [SYS_read] = sys_read,
  [SYS_brk] = sys_brk,
  [SYS_sleep] = sys_sleep,
  [SYS_exec] = sys_exec,
  [SYS_getpid] = sys_getpid,
  [SYS_yield] = sys_yield,
  [SYS_fork] = sys_fork,
  [SYS_exit] = sys_exit,
  [SYS_wait] = sys_wait,
  [SYS_sem_open] = sys_sem_open,
  [SYS_sem_p] = sys_sem_p,
  [SYS_sem_v] = sys_sem_v,
  [SYS_sem_close] = sys_sem_close,
  [SYS_open] = sys_open,
  [SYS_close] = sys_close,
  [SYS_dup] = sys_dup,
  [SYS_lseek] = sys_lseek,
  [SYS_fstat] = sys_fstat,
  [SYS_chdir] = sys_chdir,
  [SYS_unlink] = sys_unlink,
  [SYS_mmap] = sys_mmap,
  [SYS_munmap] = sys_munmap,
  [SYS_clone] = sys_clone,
  [SYS_kill] = sys_kill,
  [SYS_cv_open] = sys_cv_open,
  [SYS_cv_wait] = sys_cv_wait,
  [SYS_cv_sig] = sys_cv_sig,
  [SYS_cv_sigall] = sys_cv_sigall,
  [SYS_cv_close] = sys_cv_close,
  [SYS_pipe] = sys_pipe,
  [SYS_link] = sys_link,
  [SYS_symlink] = sys_symlink};
