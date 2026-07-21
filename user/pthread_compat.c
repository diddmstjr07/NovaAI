#include "types.h"

static long linux_call6(long number, long a1, long a2, long a3,
                        long a4, long a5, long a6) {
    register long fourth __asm__("r10") = a4;
    register long fifth __asm__("r8") = a5;
    register long sixth __asm__("r9") = a6;
    long result;
    __asm__ volatile ("syscall" : "=a"(result) : "a"(number), "D"(a1), "S"(a2),
                      "d"(a3), "r"(fourth), "r"(fifth), "r"(sixth)
                      : "rcx", "r11", "memory");
    return result;
}

static void futex_wait(volatile uint32_t *address, uint32_t expected) {
    linux_call6(202, (long)address, 128, expected, 0, 0, 0);
}

static void futex_wake(volatile uint32_t *address, int count) {
    linux_call6(202, (long)address, 129, count, 0, 0, 0);
}

uint64_t pthread_self(void) { return (uint64_t)linux_call6(186, 0, 0, 0, 0, 0, 0); }
int pthread_equal(uint64_t left, uint64_t right) { return left == right; }
int pthread_detach(uint64_t thread) { (void)thread; return 0; }

int pthread_mutex_init(void *mutex, const void *attributes) {
    (void)attributes;
    __atomic_store_n((uint32_t *)mutex, 0, __ATOMIC_RELAXED);
    return 0;
}
int pthread_mutex_destroy(void *mutex) { (void)mutex; return 0; }

int pthread_mutex_trylock(void *mutex) {
    uint32_t expected = 0;
    return __atomic_compare_exchange_n((uint32_t *)mutex, &expected, 1, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 0 : 16;
}

int pthread_mutex_lock(void *mutex) {
    volatile uint32_t *state = mutex;
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n((uint32_t *)state, &expected, 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 0;
    for (;;) {
        if (__atomic_exchange_n((uint32_t *)state, 2, __ATOMIC_ACQUIRE) == 0) return 0;
        futex_wait(state, 2);
    }
}

int pthread_mutex_unlock(void *mutex) {
    volatile uint32_t *state = mutex;
    uint32_t previous = __atomic_exchange_n((uint32_t *)state, 0, __ATOMIC_RELEASE);
    if (previous > 1) futex_wake(state, 1);
    return 0;
}

int pthread_cond_init(void *condition, const void *attributes) {
    (void)attributes;
    __atomic_store_n((uint32_t *)condition, 0, __ATOMIC_RELAXED);
    return 0;
}
int pthread_cond_destroy(void *condition) { (void)condition; return 0; }

int pthread_cond_wait(void *condition, void *mutex) {
    volatile uint32_t *sequence = condition;
    uint32_t expected = __atomic_load_n((uint32_t *)sequence, __ATOMIC_ACQUIRE);
    pthread_mutex_unlock(mutex);
    futex_wait(sequence, expected);
    return pthread_mutex_lock(mutex);
}

int pthread_cond_timedwait(void *condition, void *mutex, const void *absolute_time) {
    (void)absolute_time;
    return pthread_cond_wait(condition, mutex);
}

int pthread_cond_signal(void *condition) {
    volatile uint32_t *sequence = condition;
    __atomic_add_fetch((uint32_t *)sequence, 1, __ATOMIC_RELEASE);
    futex_wake(sequence, 1);
    return 0;
}

int pthread_cond_broadcast(void *condition) {
    volatile uint32_t *sequence = condition;
    __atomic_add_fetch((uint32_t *)sequence, 1, __ATOMIC_RELEASE);
    futex_wake(sequence, 0x7FFFFFFF);
    return 0;
}

int pthread_once(void *once_control, void (*initializer)(void)) {
    volatile uint32_t *state = once_control;
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n((uint32_t *)state, &expected, 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        initializer();
        __atomic_store_n((uint32_t *)state, 2, __ATOMIC_RELEASE);
        futex_wake(state, 0x7FFFFFFF);
        return 0;
    }
    while (__atomic_load_n((uint32_t *)state, __ATOMIC_ACQUIRE) != 2) {
        futex_wait(state, 1);
    }
    return 0;
}

int pthread_rwlock_init(void *lock, const void *attributes) {
    return pthread_mutex_init(lock, attributes);
}
int pthread_rwlock_destroy(void *lock) { return pthread_mutex_destroy(lock); }
int pthread_rwlock_rdlock(void *lock) { return pthread_mutex_lock(lock); }
int pthread_rwlock_wrlock(void *lock) { return pthread_mutex_lock(lock); }
int pthread_rwlock_tryrdlock(void *lock) { return pthread_mutex_trylock(lock); }
int pthread_rwlock_trywrlock(void *lock) { return pthread_mutex_trylock(lock); }
int pthread_rwlock_unlock(void *lock) { return pthread_mutex_unlock(lock); }

int pthread_spin_init(void *lock, int shared) {
    (void)shared;
    *(volatile uint32_t *)lock = 0;
    return 0;
}
int pthread_spin_destroy(void *lock) { (void)lock; return 0; }
int pthread_spin_trylock(void *lock) { return pthread_mutex_trylock(lock); }
int pthread_spin_lock(void *lock) {
    while (pthread_mutex_trylock(lock)) linux_call6(24, 0, 0, 0, 0, 0, 0);
    return 0;
}
int pthread_spin_unlock(void *lock) { return pthread_mutex_unlock(lock); }

int pthread_attr_init(void *attributes) {
    uint8_t *bytes = attributes;
    for (int index = 0; index < 64; ++index) bytes[index] = 0;
    return 0;
}
int pthread_attr_destroy(void *attributes) { (void)attributes; return 0; }
int pthread_attr_setdetachstate(void *attributes, int state) {
    (void)attributes;
    (void)state;
    return 0;
}
int pthread_attr_setstacksize(void *attributes, size_t size) {
    (void)attributes;
    (void)size;
    return 0;
}
int pthread_setname_np(uint64_t thread, const char *name) {
    (void)thread;
    (void)name;
    return 0;
}
