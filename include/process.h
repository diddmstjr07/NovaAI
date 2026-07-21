#ifndef NOVA_PROCESS_H
#define NOVA_PROCESS_H

#include "types.h"

bool process_init(void);
bool process_install_builtin(void);
bool process_load(const char *name);
int process_audit_elf_dependencies(const char *name);
void process_schedule(void);
void process_enable_preemption(void);
bool process_is_running(void);
int process_count(void);
int process_foreground_pid(void);
const char *process_name(void);
uint64_t process_syscall_count(void);
const char *process_output(void);
int process_exit_code(void);

extern uint64_t process_kernel_rsp;
extern uint64_t process_syscall_rsp;
extern uint64_t process_user_rsp;
extern uint64_t process_context[18];
extern volatile uint64_t scheduler_ticks;

uint64_t process_handle_syscall(uint64_t number, uint64_t argument1,
                                uint64_t argument2, uint64_t argument3);
uint64_t process_handle_linux_syscall(uint64_t number, uint64_t argument1,
                                      uint64_t argument2, uint64_t argument3,
                                      uint64_t argument4, uint64_t argument5,
                                      uint64_t argument6);
bool process_handle_page_fault(uint64_t error_code, uintptr_t address);

#endif
