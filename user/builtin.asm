[bits 64]

section .rodata
global builtin_init_start
global builtin_init_end
global builtin_libnova_start
global builtin_libnova_end
global builtin_nova_ld_start
global builtin_nova_ld_end
global builtin_libc_start
global builtin_libc_end
global builtin_libpthread_start
global builtin_libpthread_end
global builtin_libdl_start
global builtin_libdl_end
global builtin_libm_start
global builtin_libm_end
global builtin_libgcc_start
global builtin_libgcc_end
global builtin_libtls_start
global builtin_libtls_end
global builtin_libtlsdesc_start
global builtin_libtlsdesc_end
global builtin_libctor_start
global builtin_libctor_end
global builtin_test_deb_start
global builtin_test_deb_end

builtin_init_start:
    incbin "build/user/init.elf"
builtin_init_end:

align 16
builtin_libnova_start:
    incbin "build/user/libnova.so"
builtin_libnova_end:

align 16
builtin_nova_ld_start:
    incbin "build/user/nova-ld.so"
builtin_nova_ld_end:

align 16
builtin_libc_start:
    incbin "build/user/libc.so.6"
builtin_libc_end:

align 16
builtin_libpthread_start:
    incbin "build/user/libpthread.so.0"
builtin_libpthread_end:

align 16
builtin_libdl_start:
    incbin "build/user/libdl.so.2"
builtin_libdl_end:

align 16
builtin_libm_start:
    incbin "build/user/libm.so.6"
builtin_libm_end:

align 16
builtin_libgcc_start:
    incbin "build/user/libgcc_s.so.1"
builtin_libgcc_end:

align 16
builtin_libtls_start:
    incbin "build/user/libtls.so"
builtin_libtls_end:

align 16
builtin_libtlsdesc_start:
    incbin "build/user/libtlsdesc.so"
builtin_libtlsdesc_end:

align 16
builtin_libctor_start:
    incbin "build/user/libctor.so"
builtin_libctor_end:

align 16
builtin_test_deb_start:
    incbin "build/user/nova-test.deb"
builtin_test_deb_end:
