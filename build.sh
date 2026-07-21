#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$PROJECT_DIR"

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing tool: %s\n' "$1" >&2
        printf 'Install the toolchain with:\n  brew install nasm qemu x86_64-elf-gcc\n' >&2
        exit 1
    fi
}

check_build_tools() {
    require_tool make
    require_tool nasm
    require_tool x86_64-elf-gcc
    require_tool x86_64-elf-ld
    require_tool x86_64-elf-objcopy
}

# QEMU user-mode networking lives in the Makefile (QEMU_NET / QEMU_PCAP).
# Every frame crossing netdev n0 is captured here for Wireshark inspection.
announce_pcap() {
    printf 'Packet capture: %s/net.pcap\n' "$PROJECT_DIR"
}

start_bridge() {
    require_tool python3
    python3 tools/ai_bridge.py &
    BRIDGE_PID=$!
    trap 'kill "$BRIDGE_PID" 2>/dev/null || true' EXIT INT TERM
}

case "${1:-run}" in
    build)
        check_build_tools
        make build
        printf 'Ready: %s/build/novaos.img\n' "$PROJECT_DIR"
        ;;
    run)
        check_build_tools
        require_tool qemu-system-x86_64
        start_bridge
        announce_pcap
        make run
        ;;
    window)
        check_build_tools
        require_tool qemu-system-x86_64
        start_bridge
        announce_pcap
        make QEMU_DISPLAY=cocoa,zoom-to-fit=on run
        ;;
    ai|internet)
        check_build_tools
        require_tool qemu-system-x86_64
        start_bridge
        make run
        ;;
    all)
        check_build_tools
        require_tool qemu-system-x86_64
        make clean
        start_bridge
        announce_pcap
        make run
        ;;
    debug)
        check_build_tools
        require_tool qemu-system-x86_64
        announce_pcap
        make debug
        ;;
    clean)
        require_tool make
        make clean
        ;;
    *)
        printf 'Usage: %s [run|window|internet|ai|build|all|debug|clean]\n' "$0" >&2
        exit 2
        ;;
esac
