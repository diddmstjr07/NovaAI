#include "ui.h"
#include "account.h"
#include "compiler.h"
#include "deb.h"
#include "filesystem.h"
#include "graphics.h"
#include "heap.h"
#include "io.h"
#include "network.h"
#include "process.h"
#include "runtime.h"

typedef enum {
    APP_EXPLORER,
    APP_BROWSER,
    APP_NOTEPAD,
    APP_TERMINAL,
    APP_CALCULATOR,
    APP_SETTINGS,
    APP_COMPILER,
    APP_AI,
    APP_ABOUT,
    APP_COUNT
} AppId;

typedef struct {
    AppId app;
    int x;
    int y;
    int width;
    int height;
    bool visible;
    bool minimized;
} DesktopWindow;

static const uint32_t accent_colors[] = {
    0x4C8DFF, 0xA970FF, 0x18A999, 0xF05D7B, 0xF59E42
};

static const char *app_names[APP_COUNT] = {
    "File Explorer", "Nova Browser", "Notepad", "Terminal", "Calculator", "Settings",
    "C Studio", "Nova AI", "About NovaOS"
};

static DesktopWindow window;
static bool dark_mode = true;
static int accent_index;
static bool start_open;
static bool dirty;
static bool window_dirty;
static bool cursor_dirty;
static int mouse_x;
static int mouse_y;
static int rendered_mouse_x;
static int rendered_mouse_y;
static uint8_t old_buttons;
static bool dragging;
static int drag_offset_x;
static int drag_offset_y;
static uint8_t clock_hour;
static uint8_t clock_minute;
static uint8_t clock_second;

static char note_text[2048];
static int note_length;
static bool note_dirty;
static bool note_save_failed;

#define TERMINAL_ROWS 10
#define TERMINAL_COLUMNS 78
static char terminal_lines[TERMINAL_ROWS][TERMINAL_COLUMNS];
static int terminal_count;
static char terminal_command[TERMINAL_COLUMNS];
static int terminal_length;

static int calculator_value;
static int calculator_accumulator;
static char calculator_operator;
static bool calculator_new_value = true;
static bool calculator_error;

static char compiler_source[1024];
static int compiler_source_length;
static char compiler_diagnostic[256];
static char compiler_output[512];
static bool compiler_waiting;

#define BROWSER_BODY_CAPACITY 8192
#define BROWSER_MAX_LINKS 24
#define BROWSER_HISTORY_CAPACITY 32

typedef struct {
    char label[64];
    char url[224];
} BrowserLink;

static char browser_url[256];
static int browser_url_length;
static char browser_title[128];
static char browser_body[BROWSER_BODY_CAPACITY];
static char browser_status[64];
static bool browser_loading;
static bool browser_address_selected;
static BrowserLink browser_links[BROWSER_MAX_LINKS];
static int browser_link_count;
static int browser_scroll;
static int browser_max_scroll;

/* Hit-test data for the link rows currently on screen, refreshed by
   draw_browser and consumed by the click handler. */
static int browser_link_row_y[BROWSER_MAX_LINKS];
static int browser_link_row_target[BROWSER_MAX_LINKS];
static int browser_link_row_count;
static int browser_link_row_x;
static int browser_link_row_width;

/* A page arrives as PAGE / BODY* / LINK* / PAGEEND. Chunks land in these
   staging buffers and only replace the visible page once PAGEEND arrives, so a
   half-delivered page never overwrites the one on screen. */
static char browser_pending_title[128];
static char browser_pending_url[256];
static char browser_pending_status[64];
static char browser_pending_body[BROWSER_BODY_CAPACITY];
static int browser_pending_body_length;
static BrowserLink browser_pending_links[BROWSER_MAX_LINKS];
static int browser_pending_link_count;
static bool browser_pending_active;

/* Visited URLs. Navigating from anywhere but the history buttons truncates the
   forward entries, exactly like a normal browser. */
static char browser_history[BROWSER_HISTORY_CAPACITY][256];
static int browser_history_count;
static int browser_history_index;
static bool browser_navigating_history;

static char download_status[160];
static int download_percent;
static bool download_active;

static char ai_prompt[256];
static int ai_prompt_length;
static char ai_last_prompt[256];
static char ai_response[1024];
/* Must exceed the bridge's longest wire line (880) plus one 256-byte read so a
   partial line is never truncated while waiting for its newline. */
static char ai_wire_buffer[2048];
static int ai_wire_length;
static bool ai_waiting;

static uint32_t accent(void) {
    return accent_colors[accent_index];
}

static uint32_t surface(void) {
    return dark_mode ? 0x20242C : 0xF7F9FC;
}

static uint32_t surface_alt(void) {
    return dark_mode ? 0x2B303A : 0xE9EDF4;
}

static uint32_t text_primary(void) {
    return dark_mode ? 0xF5F7FA : 0x18202C;
}

static uint32_t text_secondary(void) {
    return dark_mode ? 0xAEB7C5 : 0x596477;
}

static bool contains(int x, int y, int width, int height, int px, int py) {
    return px >= x && py >= y && px < x + width && py < y + height;
}

static bool split_arguments(const char *input, char *first, size_t first_capacity,
                            char *second, size_t second_capacity) {
    while (*input == ' ') input++;
    size_t first_length = 0;
    while (*input && *input != ' ') {
        if (first_length + 1 >= first_capacity) return false;
        first[first_length++] = *input++;
    }
    first[first_length] = 0;
    while (*input == ' ') input++;
    size_t second_length = 0;
    while (*input) {
        if (second_length + 1 >= second_capacity) return false;
        second[second_length++] = *input++;
    }
    second[second_length] = 0;
    return first_length != 0;
}

static bool parse_file_mode(const char *text, uint16_t *mode, const char **name) {
    if (text[0] < '0' || text[0] > '7' || text[1] < '0' || text[1] > '7' ||
        text[2] < '0' || text[2] > '7' || text[3] != ' ') return false;
    *mode = (uint16_t)(((text[0] - '0') << 6) | ((text[1] - '0') << 3) |
                       (text[2] - '0'));
    *name = text + 4;
    while (**name == ' ') (*name)++;
    return **name != 0;
}

static char *next_wire_field(char *text) {
    while (*text && *text != '\t') text++;
    if (*text != '\t') return NULL;
    *text = 0;
    return text + 1;
}

static int parse_decimal(const char *text) {
    int value = 0;
    while (*text >= '0' && *text <= '9') {
        if (value < 100000000) value = value * 10 + (*text - '0');
        text++;
    }
    return value;
}

static void two_digits(uint8_t value, char *output) {
    output[0] = (char)('0' + value / 10);
    output[1] = (char)('0' + value % 10);
    output[2] = 0;
}

static void clock_text(char *output, bool seconds) {
    two_digits(clock_hour, output);
    output[2] = ':';
    two_digits(clock_minute, output + 3);
    if (seconds) {
        output[5] = ':';
        two_digits(clock_second, output + 6);
    }
}

static void draw_centered_text(int x, int y, int width, int height,
                               const char *text, uint32_t color, int scale) {
    int text_width = gfx_text_width(text, scale);
    int text_height = GFX_FONT_HEIGHT * scale;
    gfx_text(x + (width - text_width) / 2, y + (height - text_height) / 2,
             text, color, scale);
}

static void draw_nova_logo(int x, int y, int size) {
    int gap = size / 10;
    int cell = (size - gap) / 2;
    uint32_t color = accent();
    gfx_round_rect(x, y, cell, cell, size / 10, color);
    gfx_round_rect(x + cell + gap, y, cell, cell, size / 10, gfx_mix(color, 0xFFFFFF, 30));
    gfx_round_rect(x, y + cell + gap, cell, cell, size / 10, gfx_mix(color, 0xFFFFFF, 48));
    gfx_round_rect(x + cell + gap, y + cell + gap, cell, cell, size / 10,
                   gfx_mix(color, 0xFFFFFF, 68));
}

static void draw_app_icon(AppId app, int x, int y, int size, bool selected) {
    if (selected) {
        gfx_round_rect_alpha(x - 5, y - 5, size + 10, size + 10, 11, accent(), 42);
    }
    switch (app) {
        case APP_EXPLORER:
            gfx_round_rect(x, y + size / 4, size, size * 3 / 4, size / 7, 0xF4B83F);
            gfx_round_rect(x + size / 12, y + size / 8, size / 2, size / 3,
                           size / 9, 0xFFD875);
            gfx_round_rect(x + 2, y + size / 3, size - 4, size * 2 / 3,
                           size / 7, 0xFFC94F);
            gfx_fill_rect_alpha(x + 5, y + size / 3 + 3, size - 10, 2,
                                0xFFFFFF, 110);
            break;
        case APP_BROWSER:
            gfx_round_rect(x, y, size, size, size / 4, 0x2788F5);
            gfx_circle(x + size / 2, y + size / 2, size * 3 / 8, 0xE8F4FF);
            gfx_line(x + size / 7, y + size / 2, x + size * 6 / 7, y + size / 2, 0x2788F5);
            gfx_line(x + size / 2, y + size / 7, x + size / 2, y + size * 6 / 7, 0x2788F5);
            gfx_circle(x + size / 2, y + size / 2, size / 5, 0x2788F5);
            break;
        case APP_NOTEPAD:
            gfx_round_rect(x, y, size, size, size / 5, 0x477EEA);
            gfx_round_rect(x + size / 5, y + size / 9, size * 3 / 5,
                           size * 7 / 9, size / 12, 0xF7FAFF);
            gfx_round_rect(x + size / 5, y + size / 9, size * 3 / 5,
                           size / 6, size / 14, 0xBFD5FF);
            for (int line = 0; line < 3; ++line)
                gfx_round_rect(x + size / 3, y + size / 3 + line * size / 7,
                               size / 3, 2, 1, 0x6E82A5);
            break;
        case APP_TERMINAL:
            gfx_round_rect(x, y, size, size, size / 5, 0x101820);
            gfx_round_rect_alpha(x + 2, y + 2, size - 4, size / 3,
                                 size / 7, 0x5E7187, 50);
            gfx_line(x + size / 4, y + size / 3, x + size * 2 / 5, y + size / 2,
                     0x6CE6A3);
            gfx_line(x + size * 2 / 5, y + size / 2, x + size / 4,
                     y + size * 2 / 3, 0x6CE6A3);
            gfx_round_rect(x + size / 2, y + size * 2 / 3, size / 4, 2, 1, 0x6CE6A3);
            break;
        case APP_CALCULATOR:
            gfx_round_rect(x, y, size, size, size / 5, 0x356FD1);
            gfx_round_rect_alpha(x + size / 6, y + size / 8, size * 2 / 3,
                                 size / 4, size / 12, 0xFFFFFF, 215);
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 3; ++col)
                    gfx_circle(x + size / 4 + col * size / 4,
                                   y + size / 2 + row * size / 5,
                                   size / 16, 0xFFFFFF);
            break;
        case APP_SETTINGS:
            gfx_round_rect(x, y, size, size, size / 5, 0x667386);
            gfx_circle(x + size / 2, y + size / 2, size / 3, 0xDCE3EC);
            gfx_circle(x + size / 2, y + size / 2, size / 5, 0x667386);
            gfx_circle(x + size / 2, y + size / 2, size / 10, 0xDCE3EC);
            break;
        case APP_COMPILER:
            gfx_round_rect(x, y, size, size, size / 5, 0x16897B);
            gfx_round_rect_alpha(x + size / 8, y + size / 7, size * 3 / 4,
                                 size * 5 / 7, size / 9, 0x0C363B, 210);
            gfx_text(x + size / 4, y + size / 2 - 4, "<C>", 0xB9FFF2, 1);
            break;
        case APP_AI:
            gfx_round_rect(x, y, size, size, size / 4, 0x7557E8);
            gfx_circle(x + size / 2, y + size / 2, size / 3, 0xD9D2FF);
            gfx_circle(x + size / 2, y + size / 2, size / 5, 0x7557E8);
            gfx_line(x + size / 2, y + size / 6, x + size / 2, y + size / 3, 0xFFFFFF);
            gfx_line(x + size / 2, y + size * 2 / 3, x + size / 2, y + size * 5 / 6, 0xFFFFFF);
            gfx_line(x + size / 6, y + size / 2, x + size / 3, y + size / 2, 0xFFFFFF);
            gfx_line(x + size * 2 / 3, y + size / 2, x + size * 5 / 6, y + size / 2, 0xFFFFFF);
            break;
        case APP_ABOUT:
            gfx_round_rect(x, y, size, size, size / 5, accent());
            gfx_circle(x + size / 2, y + size / 3, size / 12, 0xFFFFFF);
            gfx_round_rect(x + size / 2 - size / 14, y + size / 2,
                           size / 7, size / 3, size / 14, 0xFFFFFF);
            break;
        default:
            break;
    }
}

static void terminal_add(const char *text) {
    if (terminal_count == TERMINAL_ROWS) {
        for (int row = 1; row < TERMINAL_ROWS; ++row) {
            strcpy(terminal_lines[row - 1], terminal_lines[row]);
        }
        terminal_count--;
    }
    strncpy(terminal_lines[terminal_count], text, TERMINAL_COLUMNS - 1);
    terminal_lines[terminal_count][TERMINAL_COLUMNS - 1] = 0;
    terminal_count++;
}

static void terminal_initialize(void) {
    terminal_count = 0;
    terminal_length = 0;
    terminal_command[0] = 0;
    terminal_add("NovaOS Terminal 1.0 - freestanding x86-64 C kernel");
    terminal_add("Type 'help' to list commands.");
}

static void rebuild_wallpaper(void) {
    gfx_generate_wallpaper(accent(), dark_mode);
    dirty = true;
}

static bool notepad_save(void) {
    if (!fs_is_ready()) {
        note_save_failed = true;
        return false;
    }
    bool saved = fs_write("note.txt", note_text, (size_t)note_length);
    note_dirty = !saved;
    note_save_failed = !saved;
    window_dirty = true;
    return saved;
}

static void compiler_build_and_run(void) {
    compiler_output[0] = 0;
    compiler_waiting = false;
    if (!fs_is_ready() ||
        !fs_write("hello.c", compiler_source, (size_t)compiler_source_length)) {
        strcpy(compiler_diagnostic, "Save failed: NovaFS is unavailable.");
        window_dirty = true;
        return;
    }
    if (!compiler_compile(compiler_source, "program.elf", compiler_diagnostic,
                          sizeof(compiler_diagnostic))) {
        window_dirty = true;
        return;
    }
    if (!process_load("program.elf")) {
        strcpy(compiler_diagnostic, "Build succeeded, but the ELF loader rejected the program.");
        window_dirty = true;
        return;
    }
    strcpy(compiler_diagnostic, "Build succeeded. Running program.elf in Ring 3...");
    compiler_waiting = true;
    window_dirty = true;
}

/* Record browser_url as the newest history entry, discarding any forward
   entries. Skipped while the Back/Forward buttons drive the navigation. */
static void browser_history_record(void) {
    if (browser_navigating_history) return;
    if (browser_history_count &&
        !strcmp(browser_history[browser_history_index], browser_url)) return;

    if (browser_history_index + 1 >= BROWSER_HISTORY_CAPACITY) {
        /* Full: drop the oldest entry so the newest always fits. */
        for (int index = 1; index < BROWSER_HISTORY_CAPACITY; ++index) {
            strcpy(browser_history[index - 1], browser_history[index]);
        }
        browser_history_index = BROWSER_HISTORY_CAPACITY - 2;
    }
    browser_history_index = browser_history_count ? browser_history_index + 1 : 0;
    strncpy(browser_history[browser_history_index], browser_url,
            sizeof(browser_history[0]) - 1);
    browser_history[browser_history_index][sizeof(browser_history[0]) - 1] = 0;
    browser_history_count = browser_history_index + 1;
}

static void browser_navigate(void) {
    if (!browser_url_length || browser_loading) return;
    browser_address_selected = false;
    browser_scroll = 0;
    char normalized[256];
    if (strncmp(browser_url, "http://", 7) && strncmp(browser_url, "https://", 8)) {
        strcpy(normalized, "https://");
        strncpy(normalized + 8, browser_url, sizeof(normalized) - 9);
        normalized[sizeof(normalized) - 1] = 0;
        strcpy(browser_url, normalized);
        browser_url_length = (int)strlen(browser_url);
    }
    char message[272];
    strcpy(message, "GET ");
    strncpy(message + 4, browser_url, sizeof(message) - 6);
    message[sizeof(message) - 2] = 0;
    size_t length = strlen(message);
    message[length++] = '\n';
    message[length] = 0;
    if (network_tcp_send(message, (uint16_t)length)) {
        browser_history_record();
        strcpy(browser_status, "Loading through the Nova HTTPS bridge...");
        strcpy(browser_title, "Loading...");
        strcpy(browser_body, "Connecting, downloading and extracting readable page text.");
        browser_link_count = 0;
        browser_loading = true;
    } else {
        strcpy(browser_status, "Internet bridge offline - restart with ./build.sh");
        strcpy(browser_title, "Unable to connect");
        strcpy(browser_body, "The Ethernet stack is running, but the host TLS bridge is unavailable.");
        browser_link_count = 0;
    }
    browser_navigating_history = false;
    window_dirty = true;
}

/* Load an absolute URL that the user clicked rather than typed. */
static void browser_open_url(const char *url) {
    if (!url || !*url || browser_loading) return;
    strncpy(browser_url, url, sizeof(browser_url) - 1);
    browser_url[sizeof(browser_url) - 1] = 0;
    browser_url_length = (int)strlen(browser_url);
    browser_navigate();
}

static void browser_go_back(void) {
    if (browser_loading || browser_history_index <= 0) return;
    browser_navigating_history = true;
    browser_open_url(browser_history[--browser_history_index]);
}

static void browser_go_forward(void) {
    if (browser_loading || browser_history_index + 1 >= browser_history_count) return;
    browser_navigating_history = true;
    browser_open_url(browser_history[++browser_history_index]);
}

/* Follow a link row if the click landed on one. Rows come from the last
   draw_browser pass, so coordinates are already absolute. */
static bool browser_link_click(int x, int y) {
    if (browser_loading) return false;
    for (int index = 0; index < browser_link_row_count; ++index) {
        if (!contains(browser_link_row_x, browser_link_row_y[index] - 2,
                      browser_link_row_width, GFX_FONT_HEIGHT + 5, x, y)) continue;
        int target = browser_link_row_target[index];
        if (target < 0 || target >= browser_link_count) return false;
        browser_open_url(browser_links[target].url);
        return true;
    }
    return false;
}

static void browser_scroll_by(int lines) {
    browser_scroll += lines;
    if (browser_scroll > browser_max_scroll) browser_scroll = browser_max_scroll;
    if (browser_scroll < 0) browser_scroll = 0;
    window_dirty = true;
}

static void browser_download(const char *url) {
    if (download_active || !url || !*url) return;
    char message[272];
    if (!strcmp(url, "chrome")) {
        strcpy(message, "DOWNLOAD_CHROME\n");
    } else {
        strcpy(message, "DOWNLOAD ");
        strncpy(message + 9, url, sizeof(message) - 11);
        message[sizeof(message) - 2] = 0;
        size_t length = strlen(message);
        message[length++] = '\n';
        message[length] = 0;
    }
    if (network_tcp_send(message, (uint16_t)strlen(message))) {
        strcpy(download_status, "Download request sent to the host cache");
        download_percent = 0;
        download_active = true;
    } else {
        strcpy(download_status, "Download failed: internet bridge offline");
    }
    window_dirty = true;
}

static void terminal_execute(void) {
    char prompt[TERMINAL_COLUMNS];
    strcpy(prompt, "C:\\NovaOS> ");
    strncpy(prompt + strlen(prompt), terminal_command,
            TERMINAL_COLUMNS - strlen(prompt) - 1);
    prompt[TERMINAL_COLUMNS - 1] = 0;
    terminal_add(prompt);

    if (!terminal_command[0]) {
    } else if (!strcmp(terminal_command, "help")) {
        terminal_add("help clear about date mem disk ls save ps run cc deb chrome net web download");
        terminal_add("whoami users login useradd chmod apps theme reboot shutdown");
    } else if (!strcmp(terminal_command, "clear")) {
        terminal_count = 0;
    } else if (!strcmp(terminal_command, "about")) {
        terminal_add("NovaOS: BIOS + x86-64 Long Mode + C + VBE desktop");
    } else if (!strcmp(terminal_command, "date")) {
        char value[16];
        clock_text(value, true);
        terminal_add(value);
    } else if (!strcmp(terminal_command, "mem")) {
        char amount[16];
        int_to_string((int)(heap_free_bytes() / (1024 * 1024)), amount);
        terminal_add("Dynamic kernel heap free (MiB):");
        terminal_add(amount);
    } else if (!strcmp(terminal_command, "disk")) {
        terminal_add(fs_is_ready() ? "NovaFS mounted: persistent ATA storage" :
                     "NovaFS unavailable");
    } else if (!strcmp(terminal_command, "ls")) {
        int count = fs_file_count();
        if (!count) terminal_add("No files");
        for (int index = 0; index < count; ++index) {
            FsFileInfo info;
            if (fs_file_info(index, &info)) terminal_add(info.name);
        }
    } else if (!strcmp(terminal_command, "save")) {
        terminal_add(notepad_save() ? "note.txt saved" : "Save failed");
    } else if (!strncmp(terminal_command, "deb install ", 12)) {
        DebInstallResult result;
        terminal_add(deb_install_file(terminal_command + 12, &result) ?
                     "Package installed from ar/data.tar into NovaFS" :
                     "Install failed (requires an uncompressed data.tar member)");
    } else if (!strcmp(terminal_command, "chrome")) {
        int missing = process_audit_elf_dependencies("opt/google/chrome/chrome");
        if (missing < 0) {
            terminal_add("Chrome is not installed or its ELF metadata is invalid");
        } else if (missing) {
            char status[64] = "Chrome installed; missing shared libraries: ";
            int_to_string(missing, status + strlen(status));
            terminal_add(status);
            terminal_add("See the serial log for exact DT_NEEDED names");
        } else {
            terminal_add(process_load("opt/google/chrome/chrome") ?
                         "Chrome process scheduled" : "Chrome ELF load failed");
        }
    } else if (!strcmp(terminal_command, "whoami")) {
        char identity[48];
        strcpy(identity, account_current_name());
        strcpy(identity + strlen(identity), "  uid=");
        int_to_string((int)account_current_uid(), identity + strlen(identity));
        terminal_add(identity);
    } else if (!strcmp(terminal_command, "users")) {
        for (int index = 0; index < account_count(); ++index) {
            AccountInfo info;
            if (!account_info(index, &info)) continue;
            char line[TERMINAL_COLUMNS];
            strcpy(line, info.administrator ? "ADMIN  " : "USER   ");
            strcpy(line + strlen(line), info.name);
            strcpy(line + strlen(line), "  uid=");
            int_to_string((int)info.uid, line + strlen(line));
            terminal_add(line);
        }
    } else if (!strncmp(terminal_command, "login ", 6)) {
        char name[24];
        char password[32];
        bool parsed = split_arguments(terminal_command + 6, name, sizeof(name),
                                      password, sizeof(password));
        terminal_add(parsed && account_login(name, password) ? "Login succeeded" :
                     "Login failed");
        dirty = true;
    } else if (!strncmp(terminal_command, "useradd ", 8)) {
        char name[24];
        char password[32];
        bool parsed = split_arguments(terminal_command + 8, name, sizeof(name),
                                      password, sizeof(password));
        terminal_add(parsed && account_create(name, password, false) ? "User created" :
                     "useradd failed (login as an administrator)");
    } else if (!strncmp(terminal_command, "chmod ", 6)) {
        uint16_t mode;
        const char *name;
        terminal_add(parse_file_mode(terminal_command + 6, &mode, &name) &&
                     fs_chmod(name, mode) ? "File mode updated" : "chmod failed");
    } else if (!strcmp(terminal_command, "ps")) {
        char process_line[TERMINAL_COLUMNS];
        if (process_count()) {
            strcpy(process_line, "PID ");
            int_to_string(process_foreground_pid(), process_line + strlen(process_line));
            strcpy(process_line + strlen(process_line), "  Ring 3  RUNNABLE  tasks=");
            int_to_string(process_count(), process_line + strlen(process_line));
            terminal_add(process_line);
        } else terminal_add("No runnable user process");
        terminal_add(process_name());
    } else if (!strcmp(terminal_command, "run") ||
               !strcmp(terminal_command, "run init.elf")) {
        terminal_add(process_load("init.elf") ? "init.elf scheduled" : "ELF load failed");
    } else if (!strcmp(terminal_command, "run program.elf")) {
        terminal_add(process_load("program.elf") ? "program.elf scheduled" : "ELF load failed");
    } else if (!strcmp(terminal_command, "cc")) {
        compiler_build_and_run();
        terminal_add(compiler_diagnostic);
    } else if (!strcmp(terminal_command, "net")) {
        terminal_add(network_status());
        char counts[32];
        strcpy(counts, "TX packets: ");
        int_to_string((int)network_packets_sent(), counts + strlen(counts));
        terminal_add(counts);
        strcpy(counts, "RX packets: ");
        int_to_string((int)network_packets_received(), counts + strlen(counts));
        terminal_add(counts);
    } else if (!strcmp(terminal_command, "udp")) {
        static const char probe[] = "NovaOS UDP probe";
        terminal_add(network_send_udp(IPV4(10,0,2,2), 7777, probe, sizeof(probe) - 1) ?
                     "UDP probe sent to 10.0.2.2:7777" : "UDP send failed; try net");
    } else if (!strncmp(terminal_command, "web ", 4)) {
        strncpy(browser_url, terminal_command + 4, sizeof(browser_url) - 1);
        browser_url[sizeof(browser_url) - 1] = 0;
        browser_url_length = (int)strlen(browser_url);
        browser_navigate();
        terminal_add(browser_loading ? "Web request sent; open Nova Browser" : browser_status);
    } else if (!strncmp(terminal_command, "download ", 9)) {
        browser_download(!strcmp(terminal_command + 9, "chrome") ? "chrome" :
                         terminal_command + 9);
        terminal_add(download_status);
    } else if (!strcmp(terminal_command, "apps")) {
        terminal_add("Explorer Browser Notepad Terminal Calculator C Studio Nova AI");
    } else if (!strcmp(terminal_command, "theme")) {
        dark_mode = !dark_mode;
        rebuild_wallpaper();
        terminal_add(dark_mode ? "Dark theme enabled" : "Light theme enabled");
    } else if (!strcmp(terminal_command, "reboot")) {
        terminal_add("Rebooting...");
        outb(0x64, 0xFE);
    } else if (!strcmp(terminal_command, "shutdown")) {
        terminal_add("Powering off QEMU...");
        outw(0x604, 0x2000);
    } else if (!strncmp(terminal_command, "echo ", 5)) {
        terminal_add(terminal_command + 5);
    } else {
        terminal_add("Command not found. Type 'help'.");
    }
    terminal_length = 0;
    terminal_command[0] = 0;
}

static void calculator_apply(void) {
    if (!calculator_operator) return;
    switch (calculator_operator) {
        case '+': calculator_accumulator += calculator_value; break;
        case '-': calculator_accumulator -= calculator_value; break;
        case '*': calculator_accumulator *= calculator_value; break;
        case '/':
            if (!calculator_value) calculator_error = true;
            else calculator_accumulator /= calculator_value;
            break;
        default: break;
    }
    calculator_value = calculator_accumulator;
    calculator_operator = 0;
    calculator_new_value = true;
}

static void calculator_digit(int digit) {
    if (calculator_error) return;
    if (calculator_new_value) {
        calculator_value = 0;
        calculator_new_value = false;
    }
    if (calculator_value < 100000000 && calculator_value > -100000000) {
        calculator_value = calculator_value * 10 + digit;
    }
}

static void calculator_press(char key) {
    if (key >= '0' && key <= '9') {
        calculator_digit(key - '0');
    } else if (key == 'C') {
        calculator_value = 0;
        calculator_accumulator = 0;
        calculator_operator = 0;
        calculator_error = false;
        calculator_new_value = true;
    } else if (key == '=') {
        calculator_apply();
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (calculator_operator && !calculator_new_value) calculator_apply();
        calculator_accumulator = calculator_value;
        calculator_operator = key;
        calculator_new_value = true;
    }
}

static void open_app(AppId app) {
    static const int widths[APP_COUNT] = {900, 920, 880, 900, 470, 820, 900, 820, 720};
    static const int heights[APP_COUNT] = {610, 620, 620, 580, 580, 600, 620, 600, 480};
    if (window.visible && window.app == APP_NOTEPAD && note_dirty) notepad_save();
    window.app = app;
    if (app == APP_BROWSER) browser_address_selected = true;
    window.width = widths[app];
    window.height = heights[app];
    if (window.width > gfx_width() - 60) window.width = gfx_width() - 60;
    if (window.height > gfx_height() - 100) window.height = gfx_height() - 100;
    window.x = (gfx_width() - window.width) / 2;
    window.y = (gfx_height() - 64 - window.height) / 2;
    window.visible = true;
    window.minimized = false;
    start_open = false;
    dragging = false;
    dirty = true;
}

static void draw_desktop_icon(AppId app, int x, int y) {
    gfx_round_rect_alpha(x, y, 112, 82, 12, dark_mode ? 0xFFFFFF : 0x102A44, 15);
    draw_app_icon(app, x + 36, y + 8, 40, false);
    draw_centered_text(x + 4, y + 55, 104, 18, app_names[app], 0xFFFFFF, 1);
}

static void draw_desktop(void) {
    draw_desktop_icon(APP_EXPLORER, 22, 28);
    draw_desktop_icon(APP_BROWSER, 150, 28);
    draw_desktop_icon(APP_NOTEPAD, 22, 124);
    draw_desktop_icon(APP_TERMINAL, 22, 220);
    draw_desktop_icon(APP_COMPILER, 22, 316);
    draw_desktop_icon(APP_AI, 22, 412);
    gfx_text(gfx_width() - 190, 28, "NOVA", 0xFFFFFF, 3);
    gfx_text(gfx_width() - 188, 67, "C DESKTOP", 0xDDEBFF, 1);
}

static void draw_taskbar(void) {
    int height = 58;
    int y = gfx_height() - height - 8;
    int width = 608;
    int x = (gfx_width() - width) / 2;
    gfx_shadow(x, y, width, height, 16);
    gfx_round_rect_alpha(x, y, width, height, 16,
                         dark_mode ? 0x151A22 : 0xFFFFFF, dark_mode ? 225 : 220);

    int icon_x = x + 18;
    draw_nova_logo(icon_x + 7, y + 13, 28);
    for (int app = APP_EXPLORER; app <= APP_AI; ++app) {
        icon_x += 54;
        bool selected = window.visible && !window.minimized && window.app == (AppId)app;
        draw_app_icon((AppId)app, icon_x + 7, y + 12, 30, selected);
        if (window.visible && window.app == (AppId)app) {
            gfx_round_rect(icon_x + 15, y + height - 5, 14, 3, 2, accent());
        }
    }

    char time[8];
    clock_text(time, false);
    gfx_text(x + width - 55, y + 18, time, text_primary(), 1);
}

static int start_x(void) { return (gfx_width() - 560) / 2; }
static int start_y(void) { return gfx_height() - 58 - 8 - 18 - 620; }

static void draw_start_menu(void) {
    if (!start_open) return;
    int x = start_x();
    int y = start_y();
    int width = 560;
    int height = 620;
    gfx_shadow(x, y, width, height, 18);
    gfx_round_rect_alpha(x, y, width, height, 18,
                         dark_mode ? 0x20242C : 0xF8FAFD, 245);

    gfx_round_rect(x + 28, y + 24, width - 56, 42, 9, surface_alt());
    gfx_text(x + 44, y + 39, "Search apps and settings", text_secondary(), 1);
    gfx_text(x + 28, y + 88, "Pinned", text_primary(), 2);
    gfx_text(x + width - 98, y + 93, "All apps >", text_secondary(), 1);

    for (int app = 0; app < APP_COUNT; ++app) {
        int column = app % 3;
        int row = app / 3;
        int card_x = x + 28 + column * 170;
        int card_y = y + 124 + row * 112;
        gfx_round_rect(card_x, card_y, 150, 94, 11, surface_alt());
        draw_app_icon((AppId)app, card_x + 56, card_y + 12, 38, false);
        draw_centered_text(card_x, card_y + 60, 150, 22, app_names[app], text_primary(), 1);
    }

    gfx_fill_rect(x + 1, y + height - 70, width - 2, 1,
                  dark_mode ? 0x3C424D : 0xD8DEE8);
    gfx_circle(x + 54, y + height - 35, 18, accent());
    gfx_text(x + 49, y + height - 39, "E", 0xFFFFFF, 1);
    gfx_text(x + 84, y + height - 40, account_current_name(), text_primary(), 1);
    gfx_text(x + width - 80, y + height - 40, "POWER", text_secondary(), 1);
}

static void draw_title_bar(void) {
    draw_app_icon(window.app, window.x + 14, window.y + 10, 25, false);
    gfx_text(window.x + 50, window.y + 18, app_names[window.app], text_primary(), 1);

    int close_x = window.x + window.width - 46;
    int minimize_x = close_x - 46;
    gfx_round_rect(minimize_x, window.y + 7, 40, 30, 7, surface_alt());
    gfx_fill_rect(minimize_x + 14, window.y + 23, 12, 2, text_secondary());
    gfx_round_rect(close_x, window.y + 7, 40, 30, 7, 0xC9434D);
    gfx_line(close_x + 14, window.y + 15, close_x + 26, window.y + 27, 0xFFFFFF);
    gfx_line(close_x + 26, window.y + 15, close_x + 14, window.y + 27, 0xFFFFFF);
}

static void draw_explorer(int x, int y, int width, int height) {
    gfx_fill_rect(x, y, 150, height, dark_mode ? 0x252A33 : 0xEDF1F7);
    gfx_text(x + 18, y + 22, "Home", accent(), 1);
    gfx_text(x + 18, y + 58, "Desktop", text_primary(), 1);
    gfx_text(x + 18, y + 90, "Documents", text_primary(), 1);
    gfx_text(x + 18, y + 122, "System", text_primary(), 1);

    int main_x = x + 170;
    gfx_text(main_x, y + 18, "Home", text_primary(), 2);
    gfx_text(main_x, y + 48, "Persistent NovaFS storage", text_secondary(), 1);
    int count = fs_file_count();
    int shown = count > 7 ? 7 : count;
    if (!fs_is_ready()) gfx_text(main_x, y + 84, "Storage device unavailable", 0xF05D7B, 1);
    else if (!count) gfx_text(main_x, y + 84, "This drive is empty", text_secondary(), 1);
    for (int index = 0; index < shown; ++index) {
        FsFileInfo info;
        if (!fs_file_info(index, &info)) continue;
        int item_y = y + 80 + index * 55;
        gfx_round_rect(main_x, item_y, width - 200, 43, 8, surface_alt());
        gfx_round_rect(main_x + 12, item_y + 9, 24, 24, 5, 0x4C8DFF);
        gfx_text(main_x + 50, item_y + 16, info.name, text_primary(), 1);
        char size_text[16];
        int_to_string((int)info.size, size_text);
        gfx_text(main_x + width - 270, item_y + 16, size_text, text_secondary(), 1);
        gfx_text(main_x + width - 220, item_y + 16, "bytes", text_secondary(), 1);
        char owner_text[16];
        int_to_string((int)info.owner, owner_text);
        gfx_text(main_x + width - 155, item_y + 16, "uid", text_secondary(), 1);
        gfx_text(main_x + width - 126, item_y + 16, owner_text, text_secondary(), 1);
    }
    char count_text[16];
    int_to_string(count, count_text);
    gfx_text(main_x, y + height - 24, count_text, text_secondary(), 1);
    gfx_text(main_x + gfx_text_width(count_text, 1) + 8, y + height - 24,
             "files  |  ATA persistent storage", text_secondary(), 1);
}

static void draw_notepad(int x, int y, int width, int height) {
    gfx_fill_rect(x, y, width, 36, surface_alt());
    gfx_text(x + 14, y + 14, "File   Edit   View", text_secondary(), 1);
    gfx_round_rect(x + width - 82, y + 6, 68, 24, 7, note_dirty ? accent() : surface());
    draw_centered_text(x + width - 82, y + 6, 68, 24,
                       note_dirty ? "Save" : "Saved", text_primary(), 1);
    gfx_fill_rect(x, y + 36, width, height - 36, dark_mode ? 0x171A20 : 0xFFFFFF);
    Rect clip = {x + 12, y + 48, width - 24, height - 68};
    int cursor_x = clip.x;
    int cursor_y = clip.y;
    for (int index = 0; index < note_length; ++index) {
        char character = note_text[index];
        if (character == '\n' || cursor_x + GFX_FONT_ADVANCE >= clip.x + clip.width) {
            cursor_x = clip.x;
            cursor_y += GFX_FONT_HEIGHT + 3;
            if (character == '\n') continue;
        }
        char text[2] = {character, 0};
        gfx_text_clipped(cursor_x, cursor_y, text, text_primary(), 1, clip);
        cursor_x += GFX_FONT_ADVANCE;
    }
    if (cursor_y < clip.y + clip.height) {
        gfx_fill_rect(cursor_x, cursor_y, 2, GFX_FONT_HEIGHT, accent());
    }
    const char *status = note_save_failed ? "Save failed  |  NovaFS" :
                         (note_dirty ? "Unsaved changes  |  NovaFS" :
                          "Saved to disk  |  NovaFS persistent file");
    gfx_text(x + 12, y + height - 16, status,
             note_save_failed ? 0xF05D7B : text_secondary(), 1);
}

static void draw_terminal(int x, int y, int width, int height) {
    (void)height;
    gfx_fill_rect(x, y, width, height, 0x0D1117);
    gfx_text(x + 12, y + 11, "NovaOS Terminal", 0x8B95A5, 1);
    int line_y = y + 36;
    for (int line = 0; line < terminal_count; ++line) {
        gfx_text(x + 12, line_y, terminal_lines[line], 0xD8DEE9, 1);
        line_y += 19;
    }
    gfx_text(x + 12, line_y + 4, "C:\\NovaOS>", 0x70E3A0, 1);
    gfx_text(x + 78, line_y + 4, terminal_command, 0xFFFFFF, 1);
    int caret_x = 78 + gfx_text_width(terminal_command, 1);
    gfx_fill_rect(x + caret_x, line_y + 3, 2, 10, 0x70E3A0);
}

static void draw_calculator(int x, int y, int width, int height) {
    char display[24];
    if (calculator_error) strcpy(display, "DIVIDE ERROR");
    else int_to_string(calculator_value, display);
    gfx_round_rect(x + 18, y + 14, width - 36, 82, 12, surface_alt());
    int text_width = gfx_text_width(display, 2);
    gfx_text(x + width - 34 - text_width, y + 48, display, text_primary(), 2);

    static const char keys[16] = {'C','/','*','-','7','8','9','+','4','5','6','=','1','2','3','0'};
    int grid_y = y + 114;
    int gap = 9;
    int key_width = (width - 36 - gap * 3) / 4;
    int key_height = (height - 132 - gap * 3) / 4;
    for (int index = 0; index < 16; ++index) {
        int column = index % 4;
        int row = index / 4;
        int key_x = x + 18 + column * (key_width + gap);
        int key_y = grid_y + row * (key_height + gap);
        char label[2] = {keys[index], 0};
        bool action = column == 3 || row == 0;
        gfx_round_rect(key_x, key_y, key_width, key_height, 9,
                       keys[index] == '=' ? accent() : (action ? surface_alt() :
                       (dark_mode ? 0x363C47 : 0xFFFFFF)));
        draw_centered_text(key_x, key_y, key_width, key_height, label,
                           keys[index] == '=' ? 0xFFFFFF : text_primary(), 2);
    }
}

static void draw_toggle(int x, int y, bool enabled) {
    gfx_round_rect(x, y, 46, 24, 12, enabled ? accent() : 0x6E7785);
    gfx_circle(x + (enabled ? 34 : 12), y + 12, 9, 0xFFFFFF);
}

static void draw_wrapped_text(int x, int y, int width, const char *text,
                              uint32_t color, int max_lines);

static void draw_settings(int x, int y, int width, int height) {
    (void)width;
    (void)height;
    gfx_text(x + 24, y + 20, "Personalization", text_primary(), 2);
    gfx_text(x + 24, y + 58, "Make NovaOS feel like yours", text_secondary(), 1);

    gfx_round_rect(x + 24, y + 90, width - 48, 74, 12, surface_alt());
    gfx_text(x + 44, y + 112, "Dark mode", text_primary(), 1);
    gfx_text(x + 44, y + 133, "Reduce brightness and use dark surfaces", text_secondary(), 1);
    draw_toggle(x + width - 92, y + 114, dark_mode);

    gfx_round_rect(x + 24, y + 180, width - 48, 116, 12, surface_alt());
    gfx_text(x + 44, y + 201, "Accent color", text_primary(), 1);
    for (int index = 0; index < 5; ++index) {
        int color_x = x + 52 + index * 66;
        gfx_circle(color_x, y + 253, index == accent_index ? 20 : 16, accent_colors[index]);
        if (index == accent_index) gfx_circle(color_x, y + 253, 6, 0xFFFFFF);
    }

    gfx_round_rect(x + 24, y + 312, width - 48, 78, 12, surface_alt());
    gfx_text(x + 44, y + 334, "Display", text_primary(), 1);
    gfx_text(x + 44, y + 356, "1024 x 768  |  VBE linear framebuffer", text_secondary(), 1);

    gfx_round_rect(x + 24, y + 406, width - 48, 82, 12, surface_alt());
    gfx_text(x + 44, y + 428, "Account", text_primary(), 1);
    gfx_text(x + 44, y + 451, account_current_name(), accent(), 1);
    char uid_text[16];
    int_to_string((int)account_current_uid(), uid_text);
    gfx_text(x + 190, y + 451, "UID", text_secondary(), 1);
    gfx_text(x + 226, y + 451, uid_text, text_secondary(), 1);
}

static void draw_compiler(int x, int y, int width, int height) {
    gfx_fill_rect(x, y, width, height, dark_mode ? 0x151A20 : 0xF4F7FA);
    gfx_text(x + 24, y + 20, "hello.c", text_primary(), 2);
    gfx_text(x + 24, y + 49, "NovaCC freestanding x86-64 compiler", text_secondary(), 1);

    int button_x = x + width - 176;
    gfx_round_rect(button_x, y + 15, 152, 42, 11, accent());
    draw_centered_text(button_x, y + 15, 152, 42,
                       compiler_waiting ? "Running..." : "Build + Run", 0xFFFFFF, 1);

    int editor_x = x + 24;
    int editor_y = y + 80;
    int editor_width = width * 3 / 5 - 36;
    int panel_height = height - 130;
    gfx_round_rect(editor_x, editor_y, editor_width, panel_height, 12, 0x0D1117);
    gfx_fill_rect(editor_x + 42, editor_y, 1, panel_height, 0x29313C);
    Rect clip = {editor_x + 54, editor_y + 16, editor_width - 68, panel_height - 32};
    int cursor_x = clip.x;
    int cursor_y = clip.y;
    int line = 1;
    char line_number[8];
    int_to_string(line, line_number);
    gfx_text(editor_x + 14, cursor_y, line_number, 0x617080, 1);
    for (int index = 0; index < compiler_source_length; ++index) {
        char character = compiler_source[index];
        if (character == '\n' || cursor_x + GFX_FONT_ADVANCE >= clip.x + clip.width) {
            cursor_x = clip.x;
            cursor_y += GFX_FONT_HEIGHT + 5;
            if (character == '\n') {
                line++;
                int_to_string(line, line_number);
                gfx_text(editor_x + 14, cursor_y, line_number, 0x617080, 1);
                continue;
            }
        }
        char glyph[2] = {character, 0};
        uint32_t color = (character == '"') ? 0xE5C07B :
                         (character >= '0' && character <= '9') ? 0xC678DD : 0xD8DEE9;
        gfx_text_clipped(cursor_x, cursor_y, glyph, color, 1, clip);
        cursor_x += GFX_FONT_ADVANCE;
    }
    if (cursor_y < clip.y + clip.height) {
        gfx_fill_rect(cursor_x, cursor_y, 2, GFX_FONT_HEIGHT, 0x6CE6A3);
    }

    int output_x = editor_x + editor_width + 18;
    int output_width = x + width - 24 - output_x;
    gfx_round_rect(output_x, editor_y, output_width, panel_height, 12, surface_alt());
    gfx_text(output_x + 18, editor_y + 18, "Build output", text_primary(), 1);
    gfx_fill_rect(output_x + 18, editor_y + 43, output_width - 36, 1,
                  dark_mode ? 0x444B57 : 0xCFD6E1);
    draw_wrapped_text(output_x + 18, editor_y + 61, output_width - 36,
                      compiler_diagnostic, text_secondary(), 6);
    gfx_text(output_x + 18, editor_y + 174, "Program output", text_primary(), 1);
    gfx_round_rect(output_x + 14, editor_y + 198, output_width - 28,
                   panel_height - 216, 9, 0x0D1117);
    draw_wrapped_text(output_x + 28, editor_y + 215, output_width - 56,
                      compiler_output[0] ? compiler_output : "Output appears here after execution.",
                      compiler_output[0] ? 0x9FE6B8 : 0x617080,
                      (panel_height - 240) / (GFX_FONT_HEIGHT + 5));

    gfx_text(x + 24, y + height - 26,
             "Supported subset: int main(), print(\"text\"), return <integer>.",
             text_secondary(), 1);
}

/* Wrap text to the given width, skipping the first `skip_lines` wrapped lines
   and drawing at most `max_lines`. Returns the total wrapped line count so
   callers can size a scrollbar. */
static int draw_wrapped_text_scrolled(int x, int y, int width, const char *text,
                                      uint32_t color, int max_lines, int skip_lines) {
    int columns = width / GFX_FONT_ADVANCE;
    if (columns < 1) return 0;
    char line[128];
    int line_length = 0;
    int produced = 0;
    int drawn = 0;
    while (*text) {
        if (*text == '\n' || line_length == columns || line_length == (int)sizeof(line) - 1) {
            if (produced >= skip_lines && drawn < max_lines) {
                line[line_length] = 0;
                gfx_text(x, y + drawn * (GFX_FONT_HEIGHT + 5), line, color, 1);
                drawn++;
            }
            produced++;
            line_length = 0;
            if (*text == '\n') text++;
            continue;
        }
        line[line_length++] = *text++;
    }
    if (line_length) {
        if (produced >= skip_lines && drawn < max_lines) {
            line[line_length] = 0;
            gfx_text(x, y + drawn * (GFX_FONT_HEIGHT + 5), line, color, 1);
        }
        produced++;
    }
    return produced;
}

static void draw_wrapped_text(int x, int y, int width, const char *text,
                              uint32_t color, int max_lines) {
    draw_wrapped_text_scrolled(x, y, width, text, color, max_lines, 0);
}

static void draw_browser(int x, int y, int width, int height) {
    gfx_fill_rect(x, y, width, height, dark_mode ? 0x171B22 : 0xEEF2F7);
    gfx_fill_rect(x, y, width, 66, surface_alt());

    bool can_back = browser_history_index > 0;
    bool can_forward = browser_history_index + 1 < browser_history_count;
    gfx_round_rect(x + 16, y + 12, 38, 40, 10, surface());
    draw_centered_text(x + 16, y + 12, 38, 40, "<",
                       can_back ? text_primary() : text_secondary(), 2);
    gfx_round_rect(x + 58, y + 12, 38, 40, 10, surface());
    draw_centered_text(x + 58, y + 12, 38, 40, ">",
                       can_forward ? text_primary() : text_secondary(), 2);
    gfx_round_rect(x + 100, y + 12, 38, 40, 10, surface());
    draw_centered_text(x + 100, y + 12, 38, 40, "R", text_secondary(), 1);

    int address_x = x + 146;
    int go_x = x + width - 78;
    gfx_round_rect(address_x, y + 12, go_x - address_x - 10, 40, 12,
                   browser_address_selected ? gfx_mix(surface(), accent(), 38) : surface());
    gfx_text(address_x + 14, y + 27,
             !strncmp(browser_url, "https://", 8) ? "TLS" : "WEB", 0x58C98B, 1);
    gfx_text_clipped(address_x + 50, y + 27, browser_url, text_primary(), 1,
                     (Rect){address_x + 50, y + 18, go_x - address_x - 72, 26});
    gfx_round_rect(go_x, y + 12, 54, 40, 11,
                   network_tcp_connected() ? accent() : 0x596477);
    draw_centered_text(go_x, y + 12, 54, 40, browser_loading ? "..." : "Go", 0xFFFFFF, 1);

    int page_x = x + 22;
    int page_y = y + 84;
    int page_width = width - 44;
    int page_height = height - 108;
    gfx_shadow(page_x, page_y, page_width, page_height, 12);
    gfx_round_rect(page_x, page_y, page_width, page_height, 12,
                   dark_mode ? 0x20252D : 0xFFFFFF);
    gfx_text(page_x + 28, page_y + 26, browser_title, text_primary(), 2);
    gfx_text(page_x + 28, page_y + 61, browser_status,
             browser_loading ? 0xE5C07B : text_secondary(), 1);
    gfx_fill_rect(page_x + 28, page_y + 84, page_width - 56, 1,
                  dark_mode ? 0x414956 : 0xDDE3EB);

    int download_y = page_y + page_height - 76;

    /* Scrollable content: the article text, then the page's links. One offset
       covers both, so the link list reads as the tail of the document. */
    int line_step = GFX_FONT_HEIGHT + 5;
    int content_x = page_x + 28;
    int content_top = page_y + 106;
    int content_width = page_width - 74;
    int visible_rows = (download_y - 16 - content_top) / line_step;
    if (visible_rows < 1) visible_rows = 1;

    int total_body_lines = draw_wrapped_text_scrolled(content_x, content_top, content_width,
                                                      browser_body, text_primary(), 0, 0);
    int header_rows = browser_link_count ? 2 : 0;
    int total_rows = total_body_lines + header_rows + browser_link_count;
    browser_max_scroll = total_rows - visible_rows;
    if (browser_max_scroll < 0) browser_max_scroll = 0;
    if (browser_scroll > browser_max_scroll) browser_scroll = browser_max_scroll;

    int drawn_body = 0;
    if (browser_scroll < total_body_lines) {
        drawn_body = total_body_lines - browser_scroll;
        if (drawn_body > visible_rows) drawn_body = visible_rows;
        draw_wrapped_text_scrolled(content_x, content_top, content_width, browser_body,
                                   text_primary(), drawn_body, browser_scroll);
    }

    browser_link_row_count = 0;
    browser_link_row_x = content_x;
    browser_link_row_width = content_width;
    for (int row = drawn_body; row < visible_rows; ++row) {
        int global_row = browser_scroll + row;
        int row_y = content_top + row * line_step;
        if (global_row < total_body_lines) continue;
        int link_row = global_row - total_body_lines;
        if (header_rows) {
            if (link_row == 0) continue;            /* blank spacer row */
            if (link_row == 1) {
                gfx_text(content_x, row_y, "Links on this page", accent(), 1);
                continue;
            }
            link_row -= header_rows;
        }
        if (link_row < 0 || link_row >= browser_link_count) continue;

        char row_text[96];
        char number[16];
        int_to_string(link_row + 1, number);
        strcpy(row_text, "[");
        strcpy(row_text + strlen(row_text), number);
        strcpy(row_text + strlen(row_text), "] ");
        strncpy(row_text + strlen(row_text), browser_links[link_row].label,
                sizeof(row_text) - strlen(row_text) - 1);
        row_text[sizeof(row_text) - 1] = 0;
        gfx_text_clipped(content_x, row_y, row_text, 0x6AA9F5, 1,
                         (Rect){content_x, row_y - 2, content_width, line_step});
        if (browser_link_row_count < BROWSER_MAX_LINKS) {
            browser_link_row_y[browser_link_row_count] = row_y;
            browser_link_row_target[browser_link_row_count] = link_row;
            browser_link_row_count++;
        }
    }

    /* Scrollbar, drawn only when the page actually overflows. */
    if (total_rows > visible_rows) {
        int track_x = page_x + page_width - 34;
        int track_y = content_top - 6;
        int track_height = visible_rows * line_step;
        gfx_round_rect(track_x, track_y, 6, track_height, 3,
                       dark_mode ? 0x2C333D : 0xE3E9F1);
        int thumb_height = track_height * visible_rows / total_rows;
        if (thumb_height < 24) thumb_height = 24;
        int thumb_y = track_y + (track_height - thumb_height) *
                                browser_scroll / browser_max_scroll;
        gfx_round_rect(track_x, thumb_y, 6, thumb_height, 3, accent());
    }

    gfx_text_clipped(page_x + 28, download_y, download_status, text_secondary(), 1,
                     (Rect){page_x + 28, download_y - 4, page_width - 230, 22});
    gfx_round_rect(page_x + 28, download_y + 25, page_width - 230, 8, 4,
                   dark_mode ? 0x39414D : 0xDDE3EB);
    int progress_width = (page_width - 230) * download_percent / 100;
    if (progress_width > 0) gfx_round_rect(page_x + 28, download_y + 25,
                                           progress_width, 8, 4, 0x58C98B);
    int chrome_x = page_x + page_width - 164;
    gfx_round_rect(chrome_x, download_y - 4, 136, 38, 10,
                   download_active ? 0x596477 : 0x2788F5);
    draw_centered_text(chrome_x, download_y - 4, 136, 38,
                       download_active ? "Downloading" : "Get Chrome", 0xFFFFFF, 1);
}

static void ai_send_prompt(void) {
    if (!ai_prompt_length) return;
    strcpy(ai_last_prompt, ai_prompt);
    char message[272];
    strcpy(message, "PROMPT ");
    strncpy(message + 7, ai_prompt, sizeof(message) - 9);
    message[sizeof(message) - 2] = 0;
    size_t length = strlen(message);
    message[length++] = '\n';
    message[length] = 0;
    if (network_tcp_send(message, (uint16_t)length)) {
        strcpy(ai_response, "Thinking through the Mac AI bridge...");
        ai_waiting = true;
    } else {
        strcpy(ai_response, "Internet bridge is offline. Restart NovaOS with ./build.sh.");
        ai_waiting = false;
    }
    ai_prompt_length = 0;
    ai_prompt[0] = 0;
    window_dirty = true;
}

static void draw_ai(int x, int y, int width, int height) {
    gfx_fill_rect(x, y, width, height, dark_mode ? 0x171A22 : 0xF5F7FC);
    gfx_text(x + 24, y + 20, "Nova AI", text_primary(), 2);
    gfx_text(x + 24, y + 52, network_status(),
             network_tcp_connected() ? 0x6CE6A3 : text_secondary(), 1);

    if (ai_last_prompt[0]) {
        int bubble_width = width - 180;
        gfx_round_rect(x + width - bubble_width - 24, y + 82, bubble_width, 74, 14,
                       gfx_mix(accent(), surface(), 45));
        gfx_text(x + width - bubble_width - 6, y + 96, "You", 0xFFFFFF, 1);
        draw_wrapped_text(x + width - bubble_width - 6, y + 117, bubble_width - 36,
                          ai_last_prompt, 0xFFFFFF, 2);
    }

    gfx_round_rect(x + 24, y + 174, width - 180, height - 270, 14, surface_alt());
    gfx_text(x + 42, y + 191, "Nova AI", accent(), 1);
    draw_wrapped_text(x + 42, y + 218, width - 216, ai_response, text_primary(),
                      (height - 320) / (GFX_FONT_HEIGHT + 5));

    int input_y = y + height - 72;
    gfx_round_rect(x + 24, input_y, width - 134, 48, 12, surface_alt());
    gfx_text(x + 42, input_y + 18, ">", accent(), 1);
    gfx_text_clipped(x + 62, input_y + 18, ai_prompt, text_primary(), 1,
                     (Rect){x + 62, input_y + 8, width - 210, 32});
    gfx_round_rect(x + width - 98, input_y, 74, 48, 12,
                   network_tcp_connected() ? accent() : 0x596477);
    draw_centered_text(x + width - 98, input_y, 74, 48, ai_waiting ? "Wait" : "Send",
                       0xFFFFFF, 1);
}

static void draw_about(int x, int y, int width, int height) {
    draw_nova_logo(x + 38, y + 36, 72);
    gfx_text(x + 132, y + 38, "NovaOS", text_primary(), 3);
    gfx_text(x + 134, y + 80, "C Desktop Edition 1.0", accent(), 1);
    gfx_text(x + 40, y + 132, "A real bootable learning OS for Apple Silicon Macs", text_primary(), 1);
    gfx_text(x + 40, y + 158, "running through QEMU's x86 PC emulation.", text_secondary(), 1);

    gfx_round_rect(x + 40, y + 198, width - 80, 100, 12, surface_alt());
    gfx_text(x + 60, y + 218, "64-bit x86-64 Long Mode C kernel", text_primary(), 1);
    gfx_text(x + 60, y + 241, "VBE graphics + PS/2 keyboard and mouse", text_primary(), 1);
    gfx_text(x + 60, y + 264, "NASM bootloader + x86-64 ELF toolchain", text_primary(), 1);
    gfx_text(x + 40, y + height - 34, "Click Start to explore the built-in apps.", accent(), 1);
}

static void draw_window_content(void) {
    int content_x = window.x;
    int content_y = window.y + 43;
    int content_width = window.width;
    int content_height = window.height - 43;
    switch (window.app) {
        case APP_EXPLORER: draw_explorer(content_x, content_y, content_width, content_height); break;
        case APP_BROWSER: draw_browser(content_x, content_y, content_width, content_height); break;
        case APP_NOTEPAD: draw_notepad(content_x, content_y, content_width, content_height); break;
        case APP_TERMINAL: draw_terminal(content_x, content_y, content_width, content_height); break;
        case APP_CALCULATOR: draw_calculator(content_x, content_y, content_width, content_height); break;
        case APP_SETTINGS: draw_settings(content_x, content_y, content_width, content_height); break;
        case APP_COMPILER: draw_compiler(content_x, content_y, content_width, content_height); break;
        case APP_AI: draw_ai(content_x, content_y, content_width, content_height); break;
        case APP_ABOUT: draw_about(content_x, content_y, content_width, content_height); break;
        default: break;
    }
}

static void draw_window(void) {
    if (!window.visible || window.minimized) return;
    gfx_shadow(window.x, window.y, window.width, window.height, 14);
    gfx_round_rect(window.x, window.y, window.width, window.height, 14, surface());
    gfx_fill_rect(window.x, window.y + 42, window.width, 1,
                  dark_mode ? 0x3A404B : 0xD8DEE8);
    draw_title_bar();
    draw_window_content();
}

static void draw_cursor(void) {
    for (int row = 0; row < 20; ++row) {
        int width = row < 14 ? row / 2 + 1 : (20 - row) / 2 + 2;
        for (int column = 0; column < width; ++column) {
            uint32_t color = column == 0 || column == width - 1 || row == 0 ? 0x111111 : 0xFFFFFF;
            gfx_pixel(mouse_x + column, mouse_y + row, color);
        }
    }
}

static bool click_taskbar(int x, int y) {
    int bar_width = 608;
    int bar_x = (gfx_width() - bar_width) / 2;
    int bar_y = gfx_height() - 66;
    if (!contains(bar_x, bar_y, bar_width, 58, x, y)) return false;

    if (contains(bar_x + 18, bar_y, 48, 58, x, y)) {
        start_open = !start_open;
        dirty = true;
        return true;
    }
    for (int app = APP_EXPLORER; app <= APP_AI; ++app) {
        int icon_x = bar_x + 18 + 54 * (app + 1);
        if (contains(icon_x, bar_y, 48, 58, x, y)) {
            if (window.visible && window.app == (AppId)app) {
                window.minimized = !window.minimized;
                start_open = false;
                dirty = true;
            } else {
                open_app((AppId)app);
            }
            return true;
        }
    }
    return true;
}

static bool click_start(int x, int y) {
    if (!start_open) return false;
    int menu_x = start_x();
    int menu_y = start_y();
    if (!contains(menu_x, menu_y, 560, 620, x, y)) {
        start_open = false;
        dirty = true;
        return false;
    }
    for (int app = 0; app < APP_COUNT; ++app) {
        int card_x = menu_x + 28 + (app % 3) * 170;
        int card_y = menu_y + 124 + (app / 3) * 112;
        if (contains(card_x, card_y, 150, 94, x, y)) {
            open_app((AppId)app);
            return true;
        }
    }
    if (contains(menu_x + 460, menu_y + 550, 90, 62, x, y)) {
        outw(0x604, 0x2000);
        return true;
    }
    return true;
}

static void settings_click(int x, int y) {
    int content_y = window.y + 43;
    if (contains(window.x + 24, content_y + 90, window.width - 48, 74, x, y)) {
        dark_mode = !dark_mode;
        rebuild_wallpaper();
        return;
    }
    for (int index = 0; index < 5; ++index) {
        int color_x = window.x + 52 + index * 66;
        if (contains(color_x - 24, content_y + 225, 48, 55, x, y)) {
            accent_index = index;
            rebuild_wallpaper();
            return;
        }
    }
}

static void calculator_click(int x, int y) {
    int content_x = window.x;
    int content_y = window.y + 43;
    int content_width = window.width;
    int content_height = window.height - 43;
    static const char keys[16] = {'C','/','*','-','7','8','9','+','4','5','6','=','1','2','3','0'};
    int grid_y = content_y + 114;
    int gap = 9;
    int key_width = (content_width - 36 - gap * 3) / 4;
    int key_height = (content_height - 132 - gap * 3) / 4;
    for (int index = 0; index < 16; ++index) {
        int key_x = content_x + 18 + (index % 4) * (key_width + gap);
        int key_y = grid_y + (index / 4) * (key_height + gap);
        if (contains(key_x, key_y, key_width, key_height, x, y)) {
            calculator_press(keys[index]);
            window_dirty = true;
            return;
        }
    }
}

static bool click_window(int x, int y) {
    if (!window.visible || window.minimized ||
        !contains(window.x, window.y, window.width, window.height, x, y)) return false;

    int close_x = window.x + window.width - 46;
    if (contains(close_x, window.y + 7, 40, 30, x, y)) {
        if (window.app == APP_NOTEPAD && note_dirty) notepad_save();
        window.visible = false;
        dragging = false;
        dirty = true;
        return true;
    }
    if (contains(close_x - 46, window.y + 7, 40, 30, x, y)) {
        if (window.app == APP_NOTEPAD && note_dirty) notepad_save();
        window.minimized = true;
        dragging = false;
        dirty = true;
        return true;
    }
    if (contains(window.x, window.y, window.width - 100, 42, x, y)) {
        dragging = true;
        drag_offset_x = x - window.x;
        drag_offset_y = y - window.y;
        return true;
    }
    if (window.app == APP_NOTEPAD &&
        contains(window.x + window.width - 82, window.y + 49, 68, 24, x, y)) {
        notepad_save();
    } else if (window.app == APP_BROWSER &&
               contains(window.x + 16, window.y + 55, 38, 40, x, y)) {
        browser_go_back();
    } else if (window.app == APP_BROWSER &&
               contains(window.x + 58, window.y + 55, 38, 40, x, y)) {
        browser_go_forward();
    } else if (window.app == APP_BROWSER &&
               (contains(window.x + window.width - 78, window.y + 55, 54, 40, x, y) ||
                contains(window.x + 100, window.y + 55, 38, 40, x, y))) {
        browser_navigate();
    } else if (window.app == APP_BROWSER &&
               contains(window.x + 146, window.y + 55, window.width - 234, 40, x, y)) {
        browser_address_selected = true;
        window_dirty = true;
    } else if (window.app == APP_BROWSER && browser_link_click(x, y)) {
        /* Navigation started by browser_link_click. */
    } else if (window.app == APP_BROWSER &&
               contains(window.x + window.width - 186, window.y + window.height - 104,
                        136, 42, x, y)) {
        browser_download("chrome");
    } else if (window.app == APP_AI &&
               contains(window.x + window.width - 98,
                        window.y + window.height - 72, 74, 48, x, y)) {
        ai_send_prompt();
    } else if (window.app == APP_COMPILER &&
               contains(window.x + window.width - 176, window.y + 58, 152, 42, x, y)) {
        compiler_build_and_run();
    } else if (window.app == APP_SETTINGS) settings_click(x, y);
    else if (window.app == APP_CALCULATOR) calculator_click(x, y);
    return true;
}

static void click_desktop(int x, int y) {
    if (contains(22, 28, 112, 82, x, y)) open_app(APP_EXPLORER);
    else if (contains(150, 28, 112, 82, x, y)) open_app(APP_BROWSER);
    else if (contains(22, 124, 112, 82, x, y)) open_app(APP_NOTEPAD);
    else if (contains(22, 220, 112, 82, x, y)) open_app(APP_TERMINAL);
    else if (contains(22, 316, 112, 82, x, y)) open_app(APP_COMPILER);
    else if (contains(22, 412, 112, 82, x, y)) open_app(APP_AI);
}

void ui_init(void) {
    accent_index = 0;
    dark_mode = true;
    start_open = false;
    dirty = true;
    window_dirty = false;
    cursor_dirty = false;
    mouse_x = gfx_width() / 2;
    mouse_y = gfx_height() / 2;
    rendered_mouse_x = mouse_x;
    rendered_mouse_y = mouse_y;
    old_buttons = 0;
    dragging = false;
    clock_hour = clock_minute = clock_second = 0;

    note_length = fs_read("note.txt", note_text, sizeof(note_text) - 1);
    note_dirty = false;
    note_save_failed = false;
    if (note_length < 0) {
        strcpy(note_text,
               "Welcome to NovaOS Notepad!\n\n"
               "This document is stored in the persistent NovaFS volume.\n"
               "Edit it, then press Save. Your text survives a reboot.\n\n"
               "Open Start to explore the other apps.");
        note_length = (int)strlen(note_text);
        note_dirty = true;
        if (fs_is_ready()) notepad_save();
    } else {
        note_text[note_length] = 0;
    }
    terminal_initialize();
    calculator_value = 0;
    calculator_accumulator = 0;
    calculator_operator = 0;
    calculator_error = false;
    calculator_new_value = true;
    strcpy(browser_url, "https://example.com");
    browser_url_length = (int)strlen(browser_url);
    strcpy(browser_title, "Nova Browser");
    strcpy(browser_body,
           "Enter a public web address above and press Enter or Go. NovaOS sends the request "
           "over its e1000, ARP, IPv4 and TCP stack; the Mac bridge provides HTTPS/TLS and "
           "returns readable page text.\n\n"
           "Links found on a page are listed under the article; click one to follow it. "
           "Use Up and Down to scroll, Home and End to jump, and Left and Right for history.");
    strcpy(browser_status, "Waiting for Nova internet bridge");
    browser_loading = false;
    browser_address_selected = true;
    browser_link_count = 0;
    browser_link_row_count = 0;
    browser_scroll = 0;
    browser_max_scroll = 0;
    browser_history_count = 0;
    browser_history_index = 0;
    browser_navigating_history = false;
    browser_pending_active = false;
    browser_pending_body_length = 0;
    strcpy(download_status, "Downloads are saved to the host downloads/ folder");
    download_percent = 0;
    download_active = false;
    compiler_source_length = fs_read("hello.c", compiler_source, sizeof(compiler_source) - 1);
    if (compiler_source_length < 0) {
        strcpy(compiler_source,
               "int main() {\n"
               "    print(\"Hello from NovaCC!\\n\");\n"
               "    return 0;\n"
               "}\n");
        compiler_source_length = (int)strlen(compiler_source);
        if (fs_is_ready()) fs_write("hello.c", compiler_source, (size_t)compiler_source_length);
    } else {
        compiler_source[compiler_source_length] = 0;
    }
    strcpy(compiler_diagnostic, "Ready. Source is saved on NovaFS when you build.");
    compiler_output[0] = 0;
    compiler_waiting = false;
    ai_prompt_length = 0;
    ai_wire_length = 0;
    ai_prompt[0] = 0;
    ai_last_prompt[0] = 0;
    strcpy(ai_response, "Connect the Mac bridge, then ask Nova AI anything.");
    ai_waiting = false;
    gfx_generate_wallpaper(accent(), dark_mode);
    open_app(APP_ABOUT);
}

void ui_mouse(int x, int y, uint8_t buttons) {
    bool pressed = (buttons & 1) && !(old_buttons & 1);
    bool released = !(buttons & 1) && (old_buttons & 1);
    bool position_changed = x != mouse_x || y != mouse_y;
    mouse_x = x;
    mouse_y = y;

    if (dragging && (buttons & 1)) {
        window.x = x - drag_offset_x;
        window.y = y - drag_offset_y;
        if (window.x < -window.width + 120) window.x = -window.width + 120;
        if (window.x > gfx_width() - 120) window.x = gfx_width() - 120;
        if (window.y < 0) window.y = 0;
        if (window.y > gfx_height() - 110) window.y = gfx_height() - 110;
        dirty = true;
    }
    if (released) dragging = false;

    if (pressed) {
        if (click_taskbar(x, y)) {
        } else if (click_start(x, y)) {
        } else if (click_window(x, y)) {
        } else {
            click_desktop(x, y);
        }
    }
    if (position_changed) cursor_dirty = true;
    old_buttons = buttons;
}

void ui_key(char character, SpecialKey key) {
    if (key == KEY_ESCAPE) {
        if (start_open) start_open = false;
        else if (window.visible) window.minimized = true;
        dirty = true;
        return;
    }
    if (!window.visible || window.minimized) return;

    if (window.app == APP_BROWSER) {
        /* Scrolling and history keys work whenever the address bar is not
           being edited, so reading a page never needs the mouse. */
        if (key == KEY_DOWN) { browser_scroll_by(1); return; }
        if (key == KEY_UP) { browser_scroll_by(-1); return; }
        if (key == KEY_END) { browser_scroll_by(browser_max_scroll); return; }
        if (key == KEY_HOME) { browser_scroll_by(-browser_max_scroll); return; }
        if (key == KEY_LEFT) { browser_go_back(); return; }
        if (key == KEY_RIGHT) { browser_go_forward(); return; }
        if (key == KEY_BACKSPACE && browser_url_length > 0) {
            if (browser_address_selected) {
                browser_url_length = 0;
                browser_url[0] = 0;
            } else browser_url[--browser_url_length] = 0;
            browser_address_selected = false;
        } else if (key == KEY_ENTER) {
            browser_navigate();
        } else if (character >= 32 && character <= 126 &&
                   (browser_address_selected ||
                    browser_url_length < (int)sizeof(browser_url) - 1)) {
            if (browser_address_selected) {
                browser_url_length = 0;
                browser_url[0] = 0;
            }
            browser_url[browser_url_length++] = character;
            browser_url[browser_url_length] = 0;
            browser_address_selected = false;
        }
        window_dirty = true;
    } else if (window.app == APP_NOTEPAD) {
        if (key == KEY_BACKSPACE && note_length > 0) {
            note_text[--note_length] = 0;
        } else if (key == KEY_ENTER && note_length < (int)sizeof(note_text) - 1) {
            note_text[note_length++] = '\n';
            note_text[note_length] = 0;
        } else if (character >= 32 && character <= 126 && note_length < (int)sizeof(note_text) - 1) {
            note_text[note_length++] = character;
            note_text[note_length] = 0;
        }
        note_dirty = true;
        note_save_failed = false;
        window_dirty = true;
    } else if (window.app == APP_TERMINAL) {
        if (key == KEY_BACKSPACE && terminal_length > 0) {
            terminal_command[--terminal_length] = 0;
        } else if (key == KEY_ENTER) {
            terminal_execute();
        } else if (character >= 32 && character <= 126 &&
                   terminal_length < TERMINAL_COLUMNS - 1) {
            terminal_command[terminal_length++] = character;
            terminal_command[terminal_length] = 0;
        }
        window_dirty = true;
    } else if (window.app == APP_CALCULATOR) {
        if ((character >= '0' && character <= '9') || character == '+' || character == '-' ||
            character == '*' || character == '/') calculator_press(character);
        else if (character == 'c' || character == 'C') calculator_press('C');
        else if (key == KEY_ENTER) calculator_press('=');
        else if (key == KEY_BACKSPACE) calculator_value /= 10;
        window_dirty = true;
    } else if (window.app == APP_COMPILER) {
        if (key == KEY_BACKSPACE && compiler_source_length > 0) {
            compiler_source[--compiler_source_length] = 0;
        } else if (key == KEY_ENTER &&
                   compiler_source_length < (int)sizeof(compiler_source) - 1) {
            compiler_source[compiler_source_length++] = '\n';
            compiler_source[compiler_source_length] = 0;
        } else if (character >= 32 && character <= 126 &&
                   compiler_source_length < (int)sizeof(compiler_source) - 1) {
            compiler_source[compiler_source_length++] = character;
            compiler_source[compiler_source_length] = 0;
        }
        strcpy(compiler_diagnostic, "Source changed. Press Build + Run.");
        window_dirty = true;
    } else if (window.app == APP_AI) {
        if (key == KEY_BACKSPACE && ai_prompt_length > 0) {
            ai_prompt[--ai_prompt_length] = 0;
        } else if (key == KEY_ENTER) {
            ai_send_prompt();
        } else if (character >= 32 && character <= 126 &&
                   ai_prompt_length < (int)sizeof(ai_prompt) - 1) {
            ai_prompt[ai_prompt_length++] = character;
            ai_prompt[ai_prompt_length] = 0;
        }
        window_dirty = true;
    }
}

void ui_poll_services(void) {
    if (compiler_waiting && !process_is_running()) {
        strncpy(compiler_output, process_output(), sizeof(compiler_output) - 1);
        compiler_output[sizeof(compiler_output) - 1] = 0;
        strcpy(compiler_diagnostic, "Program finished. Exit code: ");
        char exit_text[16];
        int_to_string(process_exit_code(), exit_text);
        strcpy(compiler_diagnostic + strlen(compiler_diagnostic), exit_text);
        compiler_waiting = false;
        if (window.visible && window.app == APP_COMPILER) window_dirty = true;
    }
    char incoming[256];
    int received = network_tcp_receive(incoming, sizeof(incoming));
    if (received <= 0) return;
    int room = (int)sizeof(ai_wire_buffer) - 1 - ai_wire_length;
    if (received > room) received = room;
    if (received > 0) {
        memcpy(ai_wire_buffer + ai_wire_length, incoming, (size_t)received);
        ai_wire_length += received;
        ai_wire_buffer[ai_wire_length] = 0;
    }
    for (;;) {
        int newline = -1;
        for (int index = 0; index < ai_wire_length; ++index) {
            if (ai_wire_buffer[index] == '\n') {
                newline = index;
                break;
            }
        }
        if (newline < 0) break;
        ai_wire_buffer[newline] = 0;
        if (!strcmp(ai_wire_buffer, "NOVA/1 READY")) {
            strcpy(ai_response, "AI bridge connected. Type a question below and press Enter.");
            strcpy(browser_status, "Internet bridge connected - ready to browse");
        } else if (!strncmp(ai_wire_buffer, "ANSWER ", 7)) {
            strncpy(ai_response, ai_wire_buffer + 7, sizeof(ai_response) - 1);
            ai_response[sizeof(ai_response) - 1] = 0;
            ai_waiting = false;
            if (fs_is_ready()) fs_write("ai-last.txt", ai_response, strlen(ai_response));
        } else if (!strncmp(ai_wire_buffer, "PAGE ", 5)) {
            char *status = ai_wire_buffer + 5;
            char *title = next_wire_field(status);
            char *url = title ? next_wire_field(title) : NULL;
            char *body = url ? next_wire_field(url) : NULL;
            if (title && url && body) {
                strncpy(browser_pending_title, title, sizeof(browser_pending_title) - 1);
                browser_pending_title[sizeof(browser_pending_title) - 1] = 0;
                strncpy(browser_pending_url, url, sizeof(browser_pending_url) - 1);
                browser_pending_url[sizeof(browser_pending_url) - 1] = 0;
                if (!strcmp(status, "0")) strcpy(browser_pending_status, "Request failed");
                else {
                    strcpy(browser_pending_status, "HTTP ");
                    strncpy(browser_pending_status + 5, status,
                            sizeof(browser_pending_status) - 6);
                    browser_pending_status[sizeof(browser_pending_status) - 1] = 0;
                }
                strncpy(browser_pending_body, body, sizeof(browser_pending_body) - 1);
                browser_pending_body[sizeof(browser_pending_body) - 1] = 0;
                browser_pending_body_length = (int)strlen(browser_pending_body);
                browser_pending_link_count = 0;
                browser_pending_active = true;
            }
        } else if (!strncmp(ai_wire_buffer, "BODY ", 5) && browser_pending_active) {
            /* Chunks concatenate verbatim; the bridge split them losslessly. */
            const char *chunk = ai_wire_buffer + 5;
            int room = (int)sizeof(browser_pending_body) - 1 - browser_pending_body_length;
            int length = (int)strlen(chunk);
            if (length > room) length = room;
            if (length > 0) {
                memcpy(browser_pending_body + browser_pending_body_length, chunk,
                       (size_t)length);
                browser_pending_body_length += length;
                browser_pending_body[browser_pending_body_length] = 0;
            }
        } else if (!strncmp(ai_wire_buffer, "LINK ", 5) && browser_pending_active) {
            char *label = ai_wire_buffer + 5;
            char *link_url = next_wire_field(label);
            if (link_url && browser_pending_link_count < BROWSER_MAX_LINKS) {
                BrowserLink *link = &browser_pending_links[browser_pending_link_count++];
                strncpy(link->label, label, sizeof(link->label) - 1);
                link->label[sizeof(link->label) - 1] = 0;
                strncpy(link->url, link_url, sizeof(link->url) - 1);
                link->url[sizeof(link->url) - 1] = 0;
            }
        } else if (!strcmp(ai_wire_buffer, "PAGEEND") && browser_pending_active) {
            strcpy(browser_title, browser_pending_title);
            strcpy(browser_status, browser_pending_status);
            strcpy(browser_url, browser_pending_url);
            browser_url_length = (int)strlen(browser_url);
            browser_address_selected = false;
            /* Redirects change the URL; keep history pointing at where we
               actually landed so Back and Forward replay real pages. */
            if (browser_history_count) {
                strncpy(browser_history[browser_history_index], browser_url,
                        sizeof(browser_history[0]) - 1);
                browser_history[browser_history_index][sizeof(browser_history[0]) - 1] = 0;
            }
            memcpy(browser_body, browser_pending_body,
                   (size_t)browser_pending_body_length + 1);
            for (int index = 0; index < browser_pending_link_count; ++index) {
                browser_links[index] = browser_pending_links[index];
            }
            browser_link_count = browser_pending_link_count;
            browser_scroll = 0;
            browser_loading = false;
            browser_pending_active = false;
            if (fs_is_ready()) fs_write("browser-last.txt", browser_body, strlen(browser_body));
        } else if (!strncmp(ai_wire_buffer, "DOWNLOAD\t", 9)) {
            char *state = ai_wire_buffer + 9;
            char *percent = next_wire_field(state);
            char *received = percent ? next_wire_field(percent) : NULL;
            char *filename = received ? next_wire_field(received) : NULL;
            char *detail = filename ? next_wire_field(filename) : NULL;
            if (percent && received && filename && detail) {
                download_percent = parse_decimal(percent);
                if (!strcmp(state, "START")) {
                    strcpy(download_status, "Starting download: ");
                    strncpy(download_status + strlen(download_status), filename,
                            sizeof(download_status) - strlen(download_status) - 1);
                } else if (!strcmp(state, "ACTIVE")) {
                    strcpy(download_status, "Downloading ");
                    strncpy(download_status + strlen(download_status), filename,
                            sizeof(download_status) - strlen(download_status) - 1);
                    strcpy(download_status + strlen(download_status), " - ");
                    strncpy(download_status + strlen(download_status), percent,
                            sizeof(download_status) - strlen(download_status) - 2);
                    strcpy(download_status + strlen(download_status), "%");
                } else if (!strcmp(state, "DONE")) {
                    strncpy(download_status, detail, sizeof(download_status) - 1);
                    download_status[sizeof(download_status) - 1] = 0;
                    download_percent = 100;
                    download_active = false;
                    if (fs_is_ready()) fs_write("browser-download.txt", download_status,
                                                strlen(download_status));
                } else if (!strcmp(state, "ERROR")) {
                    strcpy(download_status, "Download error: ");
                    strncpy(download_status + strlen(download_status), detail,
                            sizeof(download_status) - strlen(download_status) - 1);
                    download_active = false;
                }
                download_status[sizeof(download_status) - 1] = 0;
            }
        }
        int consumed = newline + 1;
        memmove(ai_wire_buffer, ai_wire_buffer + consumed, (size_t)(ai_wire_length - consumed));
        ai_wire_length -= consumed;
        ai_wire_buffer[ai_wire_length] = 0;
    }
    if (window.visible && (window.app == APP_AI || window.app == APP_BROWSER)) window_dirty = true;
}

void ui_set_time(uint8_t hour, uint8_t minute, uint8_t second) {
    bool displayed_time_changed = hour != clock_hour || minute != clock_minute;
    clock_hour = hour;
    clock_minute = minute;
    clock_second = second;
    if (displayed_time_changed) dirty = true;
}

bool ui_needs_render(void) {
    return dirty || window_dirty || cursor_dirty;
}

void ui_render(void) {
    if (!dirty && window_dirty) {
        int region_x = window.x;
        int region_y = window.y + 43;
        int region_width = window.width;
        int region_height = window.height - 43;
        int cursor_left = rendered_mouse_x < mouse_x ? rendered_mouse_x : mouse_x;
        int cursor_top = rendered_mouse_y < mouse_y ? rendered_mouse_y : mouse_y;
        int cursor_right = rendered_mouse_x > mouse_x ? rendered_mouse_x + 12 : mouse_x + 12;
        int cursor_bottom = rendered_mouse_y > mouse_y ? rendered_mouse_y + 22 : mouse_y + 22;

        gfx_restore_base_rect(rendered_mouse_x, rendered_mouse_y, 12, 22);
        draw_window_content();
        gfx_save_base_rect(region_x, region_y, region_width, region_height);
        draw_cursor();
        gfx_present_rect(region_x, region_y, region_width, region_height);
        gfx_present_rect(cursor_left - 1, cursor_top - 1,
                         cursor_right - cursor_left + 2, cursor_bottom - cursor_top + 2);
        rendered_mouse_x = mouse_x;
        rendered_mouse_y = mouse_y;
        window_dirty = false;
        cursor_dirty = false;
        return;
    }

    if (!dirty && cursor_dirty) {
        int left = rendered_mouse_x < mouse_x ? rendered_mouse_x : mouse_x;
        int top = rendered_mouse_y < mouse_y ? rendered_mouse_y : mouse_y;
        int right = rendered_mouse_x > mouse_x ? rendered_mouse_x + 12 : mouse_x + 12;
        int bottom = rendered_mouse_y > mouse_y ? rendered_mouse_y + 22 : mouse_y + 22;
        gfx_restore_base_rect(rendered_mouse_x, rendered_mouse_y, 12, 22);
        draw_cursor();
        gfx_present_rect(left - 1, top - 1, right - left + 2, bottom - top + 2);
        rendered_mouse_x = mouse_x;
        rendered_mouse_y = mouse_y;
        cursor_dirty = false;
        return;
    }

    gfx_begin_frame();
    draw_desktop();
    draw_window();
    draw_start_menu();
    draw_taskbar();
    gfx_save_base();
    draw_cursor();
    gfx_present();
    rendered_mouse_x = mouse_x;
    rendered_mouse_y = mouse_y;
    dirty = false;
    window_dirty = false;
    cursor_dirty = false;
}
