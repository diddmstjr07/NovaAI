#include "types.h"

typedef uint64_t UnwindWord;
typedef int UnwindReason;

enum {
    UNWIND_NO_REASON = 0,
    UNWIND_FATAL_PHASE1_ERROR = 3,
    UNWIND_END_OF_STACK = 5,
    UNWIND_CONTINUE = 8
};

typedef struct {
    uintptr_t instruction_pointer;
    uintptr_t canonical_frame_address;
} NovaUnwindContext;

typedef UnwindReason (*UnwindTrace)(NovaUnwindContext *, void *);

UnwindReason _Unwind_Backtrace(UnwindTrace trace, void *argument) {
    uintptr_t *frame;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(frame));
    for (int depth = 0; frame && depth < 64; ++depth) {
        uintptr_t next = frame[0];
        uintptr_t return_address = frame[1];
        if (!return_address) break;
        NovaUnwindContext context = {return_address, (uintptr_t)(frame + 2)};
        UnwindReason reason = trace(&context, argument);
        if (reason != UNWIND_NO_REASON) return reason;
        if (next <= (uintptr_t)frame || next - (uintptr_t)frame > 16 * 1024 * 1024 ||
            (next & (sizeof(uintptr_t) - 1))) break;
        frame = (uintptr_t *)next;
    }
    return UNWIND_END_OF_STACK;
}

uintptr_t _Unwind_GetIP(NovaUnwindContext *context) {
    return context ? context->instruction_pointer : 0;
}

uintptr_t _Unwind_GetIPInfo(NovaUnwindContext *context, int *before_instruction) {
    if (before_instruction) *before_instruction = 0;
    return _Unwind_GetIP(context);
}

uintptr_t _Unwind_GetCFA(NovaUnwindContext *context) {
    return context ? context->canonical_frame_address : 0;
}

UnwindWord _Unwind_GetGR(NovaUnwindContext *context, int index) {
    (void)context;
    (void)index;
    return 0;
}

void _Unwind_SetGR(NovaUnwindContext *context, int index, UnwindWord value) {
    (void)context;
    (void)index;
    (void)value;
}

void _Unwind_SetIP(NovaUnwindContext *context, uintptr_t value) {
    if (context) context->instruction_pointer = value;
}

uintptr_t _Unwind_GetLanguageSpecificData(NovaUnwindContext *context) {
    (void)context;
    return 0;
}
uintptr_t _Unwind_GetRegionStart(NovaUnwindContext *context) {
    return _Unwind_GetIP(context);
}
uintptr_t _Unwind_GetDataRelBase(NovaUnwindContext *context) {
    (void)context;
    return 0;
}
uintptr_t _Unwind_GetTextRelBase(NovaUnwindContext *context) {
    (void)context;
    return 0;
}
void *_Unwind_FindEnclosingFunction(void *address) { return address; }

UnwindReason _Unwind_RaiseException(void *exception) {
    (void)exception;
    return UNWIND_END_OF_STACK;
}
UnwindReason _Unwind_ForcedUnwind(void *exception, void *stop, void *argument) {
    (void)exception;
    (void)stop;
    (void)argument;
    return UNWIND_END_OF_STACK;
}
UnwindReason _Unwind_Resume_or_Rethrow(void *exception) {
    (void)exception;
    return UNWIND_END_OF_STACK;
}
void _Unwind_Resume(void *exception) { (void)exception; }
void _Unwind_DeleteException(void *exception) { (void)exception; }

UnwindReason __gcc_personality_v0(int version, int actions, uint64_t exception_class,
                                 void *exception, NovaUnwindContext *context) {
    (void)version;
    (void)actions;
    (void)exception_class;
    (void)exception;
    (void)context;
    return UNWIND_CONTINUE;
}

void __register_frame(const void *frame) { (void)frame; }
void __register_frame_info(const void *frame, void *object) {
    (void)frame;
    (void)object;
}
void __deregister_frame(const void *frame) { (void)frame; }
