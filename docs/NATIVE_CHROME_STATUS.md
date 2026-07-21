# NovaOS 네이티브 Chrome 호환 계층

이 프로젝트는 Linux 커널이나 Debian/Ubuntu 사용자 공간을 내장하지 않는다. Chrome이
필요로 하는 x86-64 실행 환경을 NovaOS 커널과 자체 사용자 공간에 직접 구현한다.
“검증됨”은 `user/init.asm`이 부팅 중 해당 경로를 실제 실행하고 직렬 로그에
`verified` 메시지를 남긴다는 뜻이다.

## 현재 Chrome 패키지 상태

- 공식 `google-chrome-stable_current_amd64.deb` 150.0.7871.128-1이
  `build/novaos.img`의 `opt/google/chrome/`에 설치되어 있다.
- 실제 `chrome` ELF는 282,369,272 bytes이며, 커널이 1 MiB 제한 없이 512-byte 단위로
  `PT_LOAD`를 스트리밍한다.
- Terminal의 `chrome` 명령은 이 바이너리의 `PT_INTERP`와 직접 `DT_NEEDED`를 읽고,
  누락된 라이브러리를 직렬 로그에 정확히 출력한다.
- 현재 Nova 내장 `ld-linux-x86-64.so.2`, `libc.so.6`, `libpthread.so.0`,
  `libdl.so.2`, `libm.so.6`, `libgcc_s.so.1`은 발견된다.
- 아직 누락된 직접 라이브러리는 26개다.

```text
libglib-2.0.so.0       libgobject-2.0.so.0   libnspr4.so
libnss3.so             libnssutil3.so         libsmime3.so
libgio-2.0.so.0        libatk-1.0.so.0        libatk-bridge-2.0.so.0
libdbus-1.so.3         libcups.so.2           libexpat.so.1
libxcb.so.1            libxkbcommon.so.0      libasound.so.2
libgbm.so.1            libX11.so.6            libXext.so.6
libcairo.so.2          libpango-1.0.so.0      libudev.so.1
libXcomposite.so.1     libXdamage.so.1        libXfixes.so.3
libXrandr.so.2         libatspi.so.0
```

따라서 패키지는 실제로 설치되어 있지만 Chrome 프로세스 실행은 아직 시작하지 않는다.
이 목록은 직접 의존성만 센 것이며, 위 라이브러리의 재귀 의존성은 추가로 필요하다.

공식 Chrome 본체의 동적 재배치를 직접 감사한 결과는 `RELATIVE` 1,059,668개,
`GLOB_DAT` 112개, `JUMP_SLOT` 1,334개, `R_X86_64_64` 2개다. 이 네 종류는 현재
로더가 모두 처리한다. 본체 자체에는 COPY/IFUNC 재배치가 없지만 재귀 DSO에는 추가 형식이
나타날 수 있다.

## 부팅 이미지에서 검증된 구현

| 영역 | 현재 구현 |
|---|---|
| 주소 공간 | 프로세스별 sparse 4단계 페이지 테이블, CR3 전환, canonical 상단 스택 |
| VM | 48-bit, demand stack, `brk`, anon/file/memfd `mmap`, COW, `MAP_SHARED`, NX/W^X |
| VM 인덱스 | 최대 131,072 pages, 262,144-slot O(1) page hash, 최대 1,024 page-table pages |
| JIT | 1 TiB `MAP_FIXED`, RW 코드 생성 후 RX 전환·실행, RWX 거부 |
| 스케줄러 | 최대 64 프로세스/스레드, 100 Hz 선점형 라운드로빈 |
| SIMD | CR0/CR4 x87/SSE 활성화, 스레드별 512-byte FXSAVE/FXRSTOR 상태 보존 |
| 프로세스 | `fork`, `vfork`, COW, `execve`, `wait4`, PID/TID, `exit_group` |
| 스레드 | `clone(CLONE_VM/SETTLS)`, 스레드별 PT_TLS block/FS base, clear-child-tid, futex |
| ELF | streaming ELF64 `ET_EXEC`/`ET_DYN`, PIE, `PT_INTERP`, recursive `DT_NEEDED` |
| 동적 심볼 | SysV/GNU hash, Bloom/bucket/chain lookup, VERSYM/VERNEED/VERDEF, global/weak |
| 재배치 | `RELATIVE`, `64`, `GLOB_DAT`, `JUMP_SLOT`, `DTPMOD64`, `DTPOFF64/32`, `TPOFF64/32`, `TLSDESC` |
| TLS | x86-64 variant-II layout, TLS image/TCB/DTV, 스레드별 템플릿, `__tls_get_addr`, GNU2 resolver |
| 초기화 | `DT_PREINIT_ARRAY`/`DT_INIT`/`DT_INIT_ARRAY`를 Ring 3 `nova-ld.so`에서 의존성 우선 실행 |
| 객체 규모 | 커널 스택 배열 제거, 힙 기반 최대 128 ELF objects |
| libc | GLIBC_2.2.5 버전 노드, 문자열/메모리, `brk` allocator, 파일·VM·시간·프로세스 wrapper |
| pthread | create/join/exit, mutex/cond/once/rwlock/spin/attr의 futex 기반 초기 ABI |
| 추가 DSO | 제한적 `libdl`, x87/SSE `libm`, 프레임 기반 `libgcc_s` unwind ABI |
| IPC | pipe/event/timer fd, socketpair, epoll/poll, 참조 카운팅 memfd shared frames |
| FD ABI | 최대 256 FD, `pread/pwrite/readv/writev/dup`, `ioctl`, access, rlimit, affinity |
| 파일 ABI | `openat/read/write/close/lseek/stat/getdents64`, mkdir/unlink/readlink/symlink |
| NovaFS | 512 MiB, 1024 entries, 95-byte paths, 64-bit size, streaming I/O, v1→v2 migration |
| 샌드박스 | `no_new_privs`, strict seccomp child, 별도 주소 공간에서 금지 syscall 거부 |
| `.deb` | 커널의 ar + 비압축 USTAR 설치기, 파일/디렉터리/symlink 처리 |
| 그래픽 | 1024×768 VBE, CPU alpha/rounded rect/shadow, 안티앨리어스 글꼴, 창 이동/최소화 |

`PT_INTERP`는 일반 Linux `ld.so`가 아니라 Nova 전용 하이브리드 구조다. 커널이
의존성·심볼·재배치를 eager 처리하고 읽기 전용 startup table을 만든 뒤, Ring 3
`nova-ld.so` bootstrap이 공유 객체 생성자를 호출하고 main entry로 넘긴다.

## 부팅 테스트 순서

1. Ring 3 인터프리터가 `DT_INIT_ARRAY` DSO 생성자를 main보다 먼저 실행
2. Linux x86-64 `syscall`, 높은 스택 demand paging
3. 스케줄러 왕복 뒤 XMM register 보존
4. GNU hash + 버전 심볼로 `libm@GLIBC_2.2.5`, `libgcc_s@GCC_3.0` 호출
5. `PT_INTERP`/`DT_NEEDED`/PLT를 통한 `libnova`, libc, pthread 호출
6. GLIBC 문자열과 `malloc/free`, pthread create/join 및 mutex
7. TLS DSO의 IE `TPOFF64`, GD `DTPMOD64/DTPOFF64`, GNU2 `TLSDESC`, 스레드 격리
8. pipe/event/timer fd + epoll/poll, socketpair, memfd 이중·프로세스 간 매핑
9. 중첩 디렉터리, 절대 symlink, `getdents64`
10. 1 TiB VM과 W^X JIT 기계어 실행
11. seccomp child, clone/futex, file-backed mmap
12. fork COW와 anonymous `MAP_SHARED`

커널 부팅 과정에서는 테스트 `.deb` 설치, 1 MiB 초과 파일 I/O, NovaFS v2 형식도
별도로 검증한다.

## 설치 경로 두 가지

`deb install <NovaFS 파일>`은 게스트 커널에서 실행되며 현재 비압축 `data.tar`만 읽는다.
반면 공식 Chrome의 `data.tar.xz`는 빌드 호스트의
`tools/import_deb_to_novafs.py`가 Python 표준 `lzma/tarfile`로 스트리밍해 NovaFS에 넣었다.
즉, Chrome XZ 설치는 이미지 생성 경로에서 동작하지만 게스트 내부 XZ decoder와 dependency
resolver는 아직 없다. `downloads/google-chrome-stable_current_amd64.deb`가 있으면 Make가
SHA-256 stamp를 확인해 깨끗한 이미지 빌드에도 이 import를 자동 실행한다.

## Chrome 실행 전 남은 핵심 작업

### 동적 로더와 ABI

- local-dynamic 최적화, `dlopen` 후 DTV generation과 동적 TLS block 확장
- GNU IFUNC/IRELATIVE, COPY relocation, lazy PLT, 실제 late `dlopen/dlsym`
- `DT_FINI`/`DT_FINI_ARRAY`의 프로세스 종료 순서와 `atexit`
- glibc의 locale/NSS/DNS resolver/stdio/regex/iconv와 pthread cancellation
- DWARF CFI 기반 완전한 unwinder 및 C++ exception propagation
- 위 26개 직접 DSO와 재귀 의존성을 Nova ABI로 포팅

### IPC·샌드박스·VM

- blocking pipe/socket, 전체 FD 객체 refcount, pathname Unix socket, SCM_RIGHTS/credentials
- blocking epoll/poll/ppoll timeout·wake와 NovaFS file-backed shared write-back
- memfd seal, truncate 후 매핑 SIGBUS, huge-page flag와 `mremap`
- VMA tree, lazy commit, page cache/reclaim, sparse file, swap
- seccomp-BPF, namespace/capability, setuid broker, ASLR, SMEP/SMAP

### Skia·V8·창 시스템

- 현재 렌더러는 Nova 전용 CPU rasterizer이며 Skia 자체가 아니다.
- FreeType/HarfBuzz, fontconfig, shaping, shared surface와 compositor IPC가 필요하다.
- V8 source/toolchain, pointer-compression cage, GC, signal handler, JIT code cache가 필요하다.
- Chromium browser/renderer/GPU/network/utility 프로세스용 Nova platform backend이 필요하다.

### 파일시스템·패키지

- NovaFS는 단일 연속 extent를 사용하며 free-space 회수, journal, hard link, atomic rename이 없다.
- 게스트 `.deb` 설치기에 XZ/Zstd/Gzip, dependency/version DB, signature, maintainer script,
  transaction rollback이 필요하다.

## 확인 방법

```sh
./build.sh build
./build.sh window
```

정상 부팅 로그의 핵심 문장:

```text
NovaOS: Chrome package installed; unresolved direct libraries: 26
NovaOS: GNU_HASH and versioned ELF symbols verified
NovaOS: PT_TLS static module layout ready
init.elf: DT_INIT_ARRAY user-mode constructor bootstrap verified
init.elf: per-thread x87/SSE context switching verified
init.elf: versioned libm.so.6 SSE/x87 ABI verified
init.elf: versioned libgcc_s.so.1 unwind ABI verified
init.elf: GLIBC string and allocator ABI verified
init.elf: PT_TLS IE/GD/TLSDESC thread isolation verified
init.elf: memfd shared frames and fd event IPC verified
init.elf: wide virtual address and W^X JIT transition verified
init.elf: fork COW isolation and MAP_SHARED verified
```

이 상태를 “Chrome 실행 완료”라고 부르지 않는다. 설치, 의존성 발견, ELF 로딩,
프로세스가 실제로 실행되는 단계를 계속 구분한다.
