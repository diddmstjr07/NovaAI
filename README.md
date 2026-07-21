# NovaOS Native Compatibility Desktop

macOS에서 빌드하고 QEMU로 바로 실행할 수 있는 64-bit x86-64 학습용 운영체제입니다.
2단계 BIOS 부트로더, IA-32e Long Mode C 커널, VBE 그래픽, PS/2 키보드/마우스와
Windows 11에서 영감을 받은 데스크톱 셸을 모두 이 프로젝트 안에서 구현합니다.
이 폴더는 Linux 배포판을 포함하지 않으며, Chrome 실행에 필요한 ABI를 NovaOS 커널과
사용자 공간에 직접 구현하는 개발판입니다.

## 네이티브 호환 계층 진행 상태

현재 개발판에는 다음 기반이 실제 코드로 구현되어 있습니다.

- 프로세스별 독립 sparse 4단계 페이지 테이블과 CR3 전환
- canonical 48-bit 상단 사용자 스택과 demand paging
- 참조 카운팅 공유 프레임, 페이지 폴트 기반 copy-on-write `fork`
- NX와 ELF R/W/X 권한, 4 KiB 단위 `mmap`, `munmap`, `mprotect`, `brk`
- 1 TiB 상단 매핑, RW→RX JIT 실행, RWX 거부, anonymous `MAP_SHARED`
- `memfd_create`/`ftruncate`, 참조 카운팅 backing frame과 프로세스 간 반복 `MAP_SHARED`
- NovaFS `MAP_PRIVATE` 파일 기반 `mmap`
- 최대 64개 프로세스/스레드 슬롯, 256 FD와 100 Hz 라운드로빈 선점
- 스레드별 x87/SSE `FXSAVE/FXRSTOR` 상태 보존
- x86-64 `SYSCALL` MSR 진입, 스케줄러 `iretq` 복귀와 Linux 번호 기반 syscall 디스패치
- 주소 공간을 복제하는 `fork`, 이미지를 교체하는 `execve`
- `CLONE_VM` 공유 주소 공간 스레드와 대기/깨우기 가능한 `futex`
- 최소 신호 등록·전달·`rt_sigreturn`, `kill`, `exit_group`, `wait4`
- `pipe2`, `eventfd2`, `timerfd`, `epoll`, `poll/ppoll`, Unix `socketpair`, 공유 메모리 IPC
- `openat/read/write/close/lseek/stat/getdents64` 및 디렉터리·symlink syscall
- `arch_prctl` FS/GS TLS 베이스, `clock_gettime`, `nanosleep`, `getrandom`
- streaming `ET_EXEC`/`ET_DYN`, `PT_INTERP`, recursive `DT_NEEDED`, 최대 128 ELF 객체
- `DT_PREINIT_ARRAY`/`DT_INIT`/`DT_INIT_ARRAY`를 실행하는 Ring 3 `nova-ld.so` bootstrap
- x86-64 variant-II `PT_TLS` 정적 레이아웃, 스레드별 TLS 템플릿과 TCB/DTV/FS base
- `DTPMOD64`, `DTPOFF64/32`, `TPOFF64/32`, `TLSDESC`, `__tls_get_addr` TLS ABI
- SysV/GNU hash와 Bloom lookup, VERSYM/VERNEED/VERDEF, PLT/GOT RELA
- GLIBC_2.2.5 버전의 `libc.so.6`/`libpthread.so.0` 초기 호환 계층
- 제한적 `libdl.so.2`, x87/SSE `libm.so.6`, frame unwinder `libgcc_s.so.1`
- `no_new_privs` + seccomp strict child sandbox
- `ar` + 비압축 `data.tar` 네이티브 `.deb` 설치기
- NovaCC의 기존 `int 0x80` ABI와 새 Linux x86-64 ABI 동시 지원

부팅 시 포함된 PIE 테스트가 공유 링킹, DSO 생성자, IPC, 디렉터리/symlink, W^X JIT,
샌드박스, 스레드별 `PT_TLS`, memfd 공유 프레임, `clone`/`futex`, COW `fork`를
실제로 실행합니다. 직렬 로그에서 각각의 `verified` 메시지를 확인할 수 있습니다.

공식 Chrome 150.0.7871.128-1 패키지와 282 MB 실행 파일은 현재 NovaFS 이미지에
설치되어 있습니다. 다만 직접 공유 라이브러리 26개가 아직 없어 Chrome 프로세스 실행은
시작하지 않습니다. dlopen 동적 TLS/IFUNC, 전체 glibc, lazy VMA, seccomp-BPF/namespace,
V8/Skia와 Chromium 플랫폼 backend도 남아 있습니다.
구현 순서와 현재 제한은 [docs/NATIVE_CHROME_STATUS.md](docs/NATIVE_CHROME_STATUS.md)에
정리되어 있습니다.

## 포함 기능

- 1024 x 768, 32-bit 선형 프레임버퍼 그래픽
- 그라데이션 배경, 중앙 작업 표시줄, 시작 메뉴, 시계
- 마우스 포인터, 클릭, 창 이동, 최소화, 닫기
- Explorer, Nova Browser, Notepad, Terminal, Calculator, Settings, C Studio, Nova AI, About 앱
- 메모장 키보드 입력과 터미널 내장 명령
- 다크/라이트 테마 및 강조색 변경
- NASM 2단계 부트로더와 freestanding x86-64 C 커널
- 4단계 페이지 테이블과 2 MiB 페이지로 초기 4 GiB identity mapping
- 직렬 포트 부팅 로그와 GDB 대기 모드
- 960 MiB 동적 커널 힙 할당기(QEMU RAM 1 GiB 구성)
- ATA PIO 디스크 드라이버와 512 MiB 부팅 디스크
- NovaFS 영구 파일 시스템과 `note.txt` 저장
- NovaFS 중첩 경로, 명시적 디렉터리, symbolic link
- 실제 NovaFS 목록을 표시하는 Explorer와 디스크 저장 Notepad
- Ring 0/Ring 3 분리, TSS/GDT/IDT, `int 0x80` 시스템 호출
- PIT 100 Hz 다중 프로세스 선점형 스케줄러와 사용자 ELF64/PIE 로더
- NovaCC: OS 내부에서 제한된 C 소스를 x86-64 ELF로 컴파일하고 Ring 3에서 실행
- PCI 검색, Intel e1000 드라이버, Ethernet/ARP/IPv4/UDP 및 단일 TCP 클라이언트
- Mac 호스트 브리지를 통한 Nova AI 앱과 OpenAI Responses API 연결
- 공개 HTTP/HTTPS 문서에 접속하는 Nova Browser 텍스트 웹 렌더러
- 영구 `users.db`, UID, 관리자 계정, 파일 소유자와 3자리 읽기/쓰기 권한

## 최초 준비

```sh
brew install nasm qemu x86_64-elf-gcc
```

`x86_64-elf-gcc`는 macOS/Apple Silicon에서 커널을 만드는 크로스 컴파일러입니다.
부팅한 NovaOS 안에는 별도로 NovaCC가 들어 있으며 C Studio의 `Build + Run` 버튼으로
`hello.c`를 `program.elf`로 생성하고 사용자 모드에서 실행합니다.

## 빌드와 실행

```sh
./build.sh          # 인터넷 브리지 시작, 빌드 후 전체 화면 QEMU 실행
./build.sh run      # 기본 실행과 동일
./build.sh window   # 크기 조절 가능한 창 모드로 실행
./build.sh ai       # 로컬 AI 브리지와 NovaOS를 함께 실행
./build.sh internet # ai와 동일하며 API 키 없이 웹 브라우징만 사용
./build.sh build    # 이미지 생성; downloads/의 공식 Chrome .deb도 자동 설치
./build.sh all      # 완전 재빌드 후 전체 화면 실행
./build.sh clean    # 빌드 결과 삭제
```

게스트는 1024 x 768로 렌더링하며 QEMU가 Mac 화면 크기에 맞게 확대합니다.
QEMU가 키보드와 마우스를 잡으면
`Control + Option + G`로 해제할 수 있습니다. macOS 전체 화면은 `Control + Command + F`로
전환할 수 있습니다.

## Windows 11에서 WSL 없이 부팅

Windows에서는 미리 생성된 `build/novaos.img`를 네이티브 Windows QEMU로 직접
부팅합니다. WSL, Ubuntu, Docker 또는 별도 C 컴파일러가 필요하지 않습니다.

`run_windows.bat`을 더블클릭하면 됩니다. QEMU가 설치되어 있지 않으면 Windows의
`winget`으로 `SoftwareFreedomConservancy.QEMU` 패키지만 자동 설치한 뒤 NovaOS를
부팅합니다.

PowerShell에서 실행하려면 다음 명령을 사용합니다.

```powershell
.\build.bat
# 또는
.\run_windows.bat
```

Windows에서 QEMU가 키보드와 마우스를 잡으면 `Ctrl + Alt + G`로 해제할 수 있습니다.
Windows에서 OS 소스를 다시 컴파일하는 과정은 포함하지 않으며, 현재 포함된 부팅 이미지를
실행하는 방식입니다. 소스 재빌드는 macOS의 `build.sh`를 사용합니다.

### Windows 성능 가속

실행 스크립트는 WHPX 하드웨어 가속을 먼저 시도하고 사용할 수 없을 때만 TCG로 자동
전환합니다. 렉이 크다면 `enable_acceleration.bat`을 한 번 실행해 관리자 권한을 허용한 뒤
Windows를 재부팅합니다. 이 작업은 Windows의 `HypervisorPlatform` 기능만 활성화하며
WSL을 설치하지 않습니다.

커널 자체도 마우스 이동 시 전체 화면 대신 커서 주변의 작은 영역만 갱신하고, 화면에
표시되는 시계가 바뀌는 분 단위에서만 전체 화면을 다시 그리도록 최적화되어 있습니다.

## 영구 저장소

NovaFS는 부팅 이미지의 LBA 4096부터 시작합니다. Notepad의 `Save` 버튼을
누르거나 Terminal에서 `save`를 입력하면 `note.txt`가 디스크에 기록됩니다.
일반 소스 재빌드는 파일 볼륨을 보존하지만 `./build.sh clean`은 `build/`를
삭제하므로 저장된 파일도 함께 삭제됩니다.

Terminal에서 `disk`, `ls`, `save`, `mem` 명령으로 저장소와 힙 상태를 확인할 수 있습니다.

## C Studio와 NovaCC

C Studio는 `int main()`, `print("text")`, `return <integer>` 문법을 지원하는 작은
freestanding C 컴파일러입니다. 빌드하면 소스와 ELF가 각각 `hello.c`, `program.elf`로
NovaFS에 영구 저장됩니다. 결과 프로그램은 커널의 ELF64 로더가 격리된 Ring 3 영역에
올려 실행하며 출력과 종료 코드를 C Studio 오른쪽 패널에 표시합니다.

이는 전체 ISO C 표준 라이브러리와 최적화기를 갖춘 GCC/Clang 대체물이 아니라,
OS 내부 컴파일-실행 파이프라인을 실제 기계어까지 연결한 초기 컴파일러입니다.

## 네트워크와 Nova AI

QEMU의 e1000 장치를 직접 구동하며 게스트 주소는 `10.0.2.15`, 게이트웨이는
`10.0.2.2`입니다. ARP, IPv4, UDP와 Nova AI 브리지용 단일 TCP 연결이 구현되어 있습니다.

```sh
OPENAI_API_KEY=... ./build.sh ai
```

API 키는 Mac 환경 변수에만 남고 디스크 이미지에는 기록되지 않습니다. 브리지는 OpenAI의
[Responses API](https://developers.openai.com/api/docs/guides/text)를 사용합니다.
키가 없을 때도 로컬 오프라인 응답으로 TCP 연결 자체를 시험할 수 있습니다.

## Nova Browser와 인터넷 접속

`./build.sh`가 인터넷 브리지를 자동 실행합니다. Nova Browser에서
`https://example.com`처럼 공개 주소를 입력하고 Enter 또는 `Go`를 누르면 NovaOS의
e1000/ARP/IPv4/TCP 경로를 통해 요청이 전달되고, 호스트 브리지가 DNS·TLS·인증서 검증과
HTML 텍스트 추출을 담당합니다. 마지막 본문은 `browser-last.txt`로 NovaFS에 저장됩니다.
Terminal에서는 `web https://example.com` 명령으로도 요청할 수 있습니다.
게스트와 브리지 사이의 전용 TCP 포트는 `7780`입니다.

Windows의 `run_windows.bat`도 네이티브 `py.exe` 또는 `python.exe`를 찾으면 브리지를
자동 실행합니다. Python이 없더라도 NovaOS 부팅은 계속되지만 브라우저는 오프라인이며,
Python 3 for Windows를 설치하면 됩니다. WSL은 사용하지 않습니다.

브리지는 보안을 위해 loopback, link-local, 사설·예약 IP와 사용자 정보가 포함된 URL을
거부합니다. 현재 렌더러는 제목과 읽을 수 있는 본문 텍스트를 표시하며 JavaScript, CSS,
이미지·동영상 렌더링과 내려받은 실행 파일의 실행은 지원하지 않습니다.

외부 파일은 `download https://example.com/file.bin` 명령으로 받을 수 있으며 Nova Browser의
`Get Chrome` 버튼은 Google의 공식 x86-64 Linux `.deb` 패키지를 요청합니다. 브리지는 최대
512MiB까지 스트리밍하고 임시 파일을 완전히 받은 뒤 프로젝트의 `downloads/` 폴더로
원자적으로 이동합니다. 진행률이 브라우저에 표시되고 완료 시 SHA-256과 파일명이
`browser-download.txt`에도 기록됩니다. 저장된 공식 `.deb`는
`tools/import_deb_to_novafs.py`가 `data.tar.xz`를 호스트에서 스트리밍 해제해
`build/novaos.img`에 설치할 수 있습니다. 현재 제공 이미지에는 이미
`opt/google/chrome/chrome`이 설치되어 있습니다.

설치와 실행은 다른 단계입니다. 부팅 시 실제 Chrome ELF의 `PT_INTERP`와 직접
`DT_NEEDED`를 검사하며, Nova가 제공하는 libc/pthread/libdl/libm/libgcc를 제외하고
26개 직접 라이브러리가 아직 누락되어 있습니다. Terminal의 `chrome` 명령은 누락 목록을
직렬 로그에 표시하고, 의존성이 모두 준비되기 전에는 잘못된 실행을 시도하지 않습니다.
Nova Browser는 그동안 실제 인터넷 문서를 확인하기 위한 NovaOS 네이티브 앱입니다.

Terminal의 `deb install <NovaFS 파일>`은 표준 `ar` 컨테이너 안의 비압축
`data.tar` 파일·디렉터리·symlink를 설치합니다. Chrome의 XZ 해제는 현재 위 호스트 도구가
담당하며, 게스트 내부 xz/zstd, dependency resolver, maintainer script는 아직 없습니다.

## 사용자 계정과 파일 권한

기본 데스크톱 사용자는 `eunseokyang`(UID 1000)이며 빈 암호로 자동 로그인합니다.
관리 작업이 필요하면 Terminal에서 `login root nova`로 전환할 수 있습니다. 이 기본 암호는
교육용 이미지의 초기값이므로 실제 보안 경계로 사용하면 안 됩니다.

```text
whoami
users
login root nova
useradd guest guestpass
login guest guestpass
chmod 600 hello.c
```

`users.db`는 root 소유 0600 모드로 저장되며 일반 데스크톱 사용자의 파일 API 접근은
거부됩니다. 새 파일은 현재 UID 소유 0660 모드로 생성됩니다. 현재 권한 모델은 owner/other만
구분하며 Unix 그룹, ACL, 암호화와 강한 암호 해시는 아직 포함하지 않습니다.

## 프로젝트 구조

```text
boot/       BIOS stage 1/2, VBE 설정, 보호 모드 전환
kernel/     힙, NovaFS, 프로세스/스케줄러, ELF, 네트워크, NovaCC
drivers/    ATA, PCI/e1000, PS/2 입력, RTC, 직렬 포트
graphics/   프레임버퍼, 도형, 글꼴, 합성
ui/         데스크톱, 창 관리자, 기본 앱
runtime/    libc 없이 사용하는 메모리/문자열 함수
include/    공용 헤더
```

## 범위

이 결과물은 Windows 11의 시각 언어와 데스크톱 상호작용을 재현한 독립 부팅형
교육용 OS입니다. 현재 부팅 방식은 레거시 BIOS이며 QEMU가 제공하는 IDE, VBE, PS/2,
e1000 장치를 대상으로 합니다. 사용자 프로세스는 최대 64개 슬롯과 각자의 CR3를 사용하며,
스레드는 `CLONE_VM`으로 주소 공간을 공유합니다.

아직 포함되지 않은 범위는 dlopen 동적 TLS/IFUNC를 포함한 완전한 glibc/ld.so 호환,
Chrome이 직접 요구하는 26개 DSO, Chrome V8/Skia/GPU 포팅,
UEFI 부트로더, 실제 메인보드별 ACPI/APIC 초기화,
USB xHCI/HID, 오디오 HDA, Wi-Fi 칩셋/802.11, DHCP/DNS/TLS, Unix 그룹/ACL입니다.
NovaFS v2는 512 MiB 실제 디스크에 지속되고, 1024 entries, 95자 경로,
대용량 스트리밍 read/write, 중첩 디렉터리와 symlink를 제공합니다. v1 볼륨은 부팅 시
파일을 보존하며 v2로 마이그레이션됩니다. 아직 연속 extent 재할당, free-space 회수,
저널링·장애 복구는 없습니다.
따라서 현재 이미지를 일반 PC의 시스템 디스크에 직접 기록하지 말고
QEMU/가상 디스크에서 사용하십시오.
