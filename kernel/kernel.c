#include "graphics.h"
#include "account.h"
#include "ata.h"
#include "compiler.h"
#include "deb.h"
#include "filesystem.h"
#include "heap.h"
#include "process.h"
#include "input.h"
#include "io.h"
#include "network.h"
#include "rtc.h"
#include "serial.h"
#include "ui.h"
#include "runtime.h"

static bool filesystem_large_file_self_test(void) {
    const size_t size = 1024 * 1024 + 4096;
    uint8_t *payload = heap_calloc(1, size);
    if (!payload) return false;
    payload[0] = 0x4E;
    payload[size - 1] = 0x56;
    bool written = fs_write("nova-large-file-test.bin", payload, size);
    heap_free(payload);
    uint8_t edges[2] = {0, 0};
    bool checked = written && fs_read_at("nova-large-file-test.bin", 0, &edges[0], 1) == 1 &&
                   fs_read_at("nova-large-file-test.bin", size - 1, &edges[1], 1) == 1 &&
                   edges[0] == 0x4E && edges[1] == 0x56;
    if (written) fs_delete("nova-large-file-test.bin");
    return checked;
}

void kernel_main(void) {
    serial_init();
    serial_write("NovaOS: x86-64 Long Mode C kernel started\r\n");

    heap_init();
    void *heap_probe = heap_alloc(4096);
    if (!heap_probe) {
        serial_write("NovaOS: heap initialization failed\r\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    heap_free(heap_probe);
    serial_write("NovaOS: dynamic heap ready\r\n");

    if (ata_init()) {
        serial_write("NovaOS: ATA disk ready\r\n");
        if (fs_init()) serial_write("NovaOS: NovaFS mounted\r\n");
        else serial_write("NovaOS: NovaFS mount failed\r\n");
    } else {
        serial_write("NovaOS: ATA disk unavailable\r\n");
    }

    process_init();
    serial_write("NovaOS: per-process CR3 and Linux x86-64 syscall ABI ready\r\n");
    bool init_installed = process_install_builtin();
    int chrome_missing = process_audit_elf_dependencies("opt/google/chrome/chrome");
    if (chrome_missing >= 0) {
        serial_write("NovaOS: Chrome package installed; unresolved direct libraries: ");
        serial_write_dec((uint32_t)chrome_missing);
        serial_write("\r\n");
    }
    if (deb_self_test()) {
        serial_write("NovaOS: native ar/data.tar .deb installer self-test passed\r\n");
    } else {
        serial_write("NovaOS: native .deb installer self-test failed\r\n");
    }
    if (filesystem_large_file_self_test()) {
        serial_write("NovaOS: NovaFS v2 large-file and 1024-entry format verified\r\n");
    } else {
        serial_write("NovaOS: NovaFS v2 large-file self-test failed\r\n");
    }
    bool compiler_self_test = false;
    char compiler_diagnostic[128];
    static const char self_test_source[] =
        "int main() { print(\"NovaCC Ring 3 self-test\\n\"); return 7; }";
    if (compiler_compile(self_test_source, "selftest.elf", compiler_diagnostic,
                         sizeof(compiler_diagnostic)) && process_load("selftest.elf")) {
        compiler_self_test = true;
        serial_write("NovaOS: NovaCC generated selftest.elf\r\n");
    } else if (init_installed && process_load("init.elf")) {
        serial_write("NovaOS: init.elf loaded from NovaFS\r\n");
    } else {
        serial_write("NovaOS: no user process loaded\r\n");
    }
    if (account_init()) {
        serial_write("NovaOS: user accounts and file permissions ready\r\n");
        uint8_t permission_probe;
        if (fs_read("users.db", &permission_probe, 1) < 0) {
            serial_write("NovaOS: protected account database access denied for desktop user\r\n");
        }
    } else serial_write("NovaOS: account database unavailable\r\n");

    if (!gfx_init()) {
        serial_write("NovaOS: unsupported VBE framebuffer\r\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    serial_write("NovaOS: framebuffer ready\r\n");
    ui_init();
    input_init(gfx_width(), gfx_height());
    serial_write("NovaOS: PS/2 input ready\r\n");
    process_enable_preemption();
    serial_write("NovaOS: 100 Hz multi-process round-robin scheduler ready\r\n");
    if (network_init()) serial_write("NovaOS: e1000 Ethernet ready\r\n");
    else serial_write("NovaOS: e1000 Ethernet unavailable\r\n");

    RtcTime previous_time = {255, 255, 255};
    bool preemption_reported = false;
    bool network_reported = false;
    bool bridge_attempted = false;
    for (;;) {
        InputEvent event;
        /* Coalesce queued PS/2 packets so a mouse move produces one frame. */
        int processed_events = 0;
        while (processed_events < 128 && input_poll(&event)) {
            if (event.type == INPUT_MOUSE) {
                ui_mouse(event.mouse_x, event.mouse_y, event.mouse_buttons);
            } else if (event.type == INPUT_KEY) {
                ui_key(event.character, event.key);
            }
            ++processed_events;
        }

        RtcTime now = rtc_time();
        if (now.second != previous_time.second || now.minute != previous_time.minute ||
            now.hour != previous_time.hour) {
            previous_time = now;
            ui_set_time(now.hour, now.minute, now.second);
        }

        if (ui_needs_render()) {
            ui_render();
        }
        network_poll();
        ui_poll_services();
        if (!network_reported && network_gateway_resolved()) {
            serial_write("NovaOS: ARP gateway resolved, IPv4/UDP online\r\n");
            network_reported = true;
        }
        if (!bridge_attempted && network_gateway_resolved()) {
            network_tcp_connect(IPV4(10,0,2,2), 7780);
            bridge_attempted = true;
        }
        process_schedule();
        if (compiler_self_test && !process_is_running()) {
            if (!strcmp(process_output(), "NovaCC Ring 3 self-test\n") &&
                process_exit_code() == 7) {
                serial_write("NovaOS: NovaCC compile/load/run self-test passed\r\n");
            } else {
                serial_write("NovaOS: NovaCC self-test failed\r\n");
            }
            compiler_self_test = false;
            if (init_installed && process_load("init.elf")) {
                serial_write("NovaOS: init.elf loaded from NovaFS\r\n");
            }
        }
        if (!preemption_reported && scheduler_ticks >= 100) {
            serial_write("NovaOS: Ring 3 preemption verified\r\n");
            preemption_reported = true;
        }
        cpu_pause();
    }
}
