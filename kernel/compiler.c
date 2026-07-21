#include "compiler.h"
#include "filesystem.h"
#include "heap.h"
#include "runtime.h"

#define USER_ENTRY 0x40001000UL
#define ELF_CODE_OFFSET 4096

typedef struct __attribute__((packed)) {
    uint8_t magic[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} ElfHeader;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} ProgramHeader;

static const char *find_text(const char *source, const char *needle) {
    size_t length = strlen(needle);
    while (*source) {
        if (!strncmp(source, needle, length)) return source;
        source++;
    }
    return NULL;
}

static bool parse_return(const char *source, int *value) {
    const char *position = find_text(source, "return");
    if (!position) return false;
    position += 6;
    while (*position == ' ' || *position == '\t') position++;
    bool negative = false;
    if (*position == '-') {
        negative = true;
        position++;
    }
    if (*position < '0' || *position > '9') return false;
    int result = 0;
    while (*position >= '0' && *position <= '9') {
        if (result > 100000000) return false;
        result = result * 10 + (*position++ - '0');
    }
    while (*position == ' ' || *position == '\t') position++;
    if (*position != ';') return false;
    *value = negative ? -result : result;
    return true;
}

static bool parse_print(const char *source, char *message, size_t capacity, size_t *length) {
    const char *position = find_text(source, "print");
    if (!position) {
        message[0] = 0;
        *length = 0;
        return true;
    }
    position += 5;
    while (*position == ' ' || *position == '\t') position++;
    if (*position++ != '(') return false;
    while (*position == ' ' || *position == '\t') position++;
    if (*position++ != '"') return false;
    size_t output = 0;
    while (*position && *position != '"') {
        char character = *position++;
        if (character == '\\') {
            if (*position == 'n') {
                character = '\n';
                position++;
            } else if (*position == '"' || *position == '\\') {
                character = *position++;
            } else return false;
        }
        if (output + 1 >= capacity) return false;
        message[output++] = character;
    }
    if (*position++ != '"') return false;
    while (*position == ' ' || *position == '\t') position++;
    if (*position++ != ')') return false;
    while (*position == ' ' || *position == '\t') position++;
    if (*position != ';') return false;
    message[output] = 0;
    *length = output;
    return true;
}

static void emit_u64(uint8_t *code, size_t *offset, uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) code[(*offset)++] = (uint8_t)(value >> (byte * 8));
}

static void emit_mov_imm64(uint8_t *code, size_t *offset, uint8_t register_opcode,
                           uint64_t value) {
    code[(*offset)++] = 0x48;
    code[(*offset)++] = register_opcode;
    emit_u64(code, offset, value);
}

bool compiler_compile(const char *source, const char *output_name,
                      char *diagnostic, size_t diagnostic_size) {
    if (!source || !output_name || !diagnostic || diagnostic_size < 32 || !fs_is_ready()) return false;
    if (!find_text(source, "int main") || !find_text(source, "{") || !find_text(source, "}")) {
        strncpy(diagnostic, "error: expected int main() { ... }", diagnostic_size - 1);
        diagnostic[diagnostic_size - 1] = 0;
        return false;
    }
    int return_value;
    if (!parse_return(source, &return_value)) {
        strncpy(diagnostic, "error: expected return <integer>;", diagnostic_size - 1);
        diagnostic[diagnostic_size - 1] = 0;
        return false;
    }
    char message[512];
    size_t message_length;
    if (!parse_print(source, message, sizeof(message), &message_length)) {
        strncpy(diagnostic, "error: invalid print(\"text\");", diagnostic_size - 1);
        diagnostic[diagnostic_size - 1] = 0;
        return false;
    }

    size_t image_capacity = ELF_CODE_OFFSET + 256 + message_length;
    uint8_t *image = heap_calloc(1, image_capacity);
    if (!image) {
        strcpy(diagnostic, "error: compiler out of memory");
        return false;
    }
    ElfHeader *header = (ElfHeader *)image;
    header->magic[0] = 0x7F;
    header->magic[1] = 'E';
    header->magic[2] = 'L';
    header->magic[3] = 'F';
    header->magic[4] = 2;
    header->magic[5] = 1;
    header->magic[6] = 1;
    header->type = 2;
    header->machine = 0x3E;
    header->version = 1;
    header->entry = USER_ENTRY;
    header->program_header_offset = sizeof(ElfHeader);
    header->header_size = sizeof(ElfHeader);
    header->program_header_size = sizeof(ProgramHeader);
    header->program_header_count = 1;

    uint8_t *code = image + ELF_CODE_OFFSET;
    size_t code_size = 0;
    size_t message_address_patch = 0;
    if (message_length) {
        emit_mov_imm64(code, &code_size, 0xB8, 1);
        message_address_patch = code_size + 2;
        emit_mov_imm64(code, &code_size, 0xBF, 0);
        emit_mov_imm64(code, &code_size, 0xBE, message_length);
        code[code_size++] = 0xCD;
        code[code_size++] = 0x80;
    }
    emit_mov_imm64(code, &code_size, 0xB8, 3);
    emit_mov_imm64(code, &code_size, 0xBF, (uint64_t)(int64_t)return_value);
    code[code_size++] = 0xCD;
    code[code_size++] = 0x80;
    code[code_size++] = 0xEB;
    code[code_size++] = 0xFE;
    if (message_length) {
        uint64_t message_address = USER_ENTRY + code_size;
        for (int byte = 0; byte < 8; ++byte) {
            code[message_address_patch + byte] = (uint8_t)(message_address >> (byte * 8));
        }
        memcpy(code + code_size, message, message_length);
    }
    size_t segment_size = code_size + message_length;

    ProgramHeader *program = (ProgramHeader *)(image + sizeof(ElfHeader));
    program->type = 1;
    program->flags = 5;
    program->offset = ELF_CODE_OFFSET;
    program->virtual_address = USER_ENTRY;
    program->physical_address = USER_ENTRY;
    program->file_size = segment_size;
    program->memory_size = segment_size;
    program->alignment = 4096;
    size_t file_size = ELF_CODE_OFFSET + segment_size;
    bool saved = fs_write(output_name, image, file_size);
    heap_free(image);
    if (!saved) {
        strcpy(diagnostic, "error: could not write ELF to NovaFS");
        return false;
    }
    strcpy(diagnostic, "build succeeded: x86-64 ELF written to NovaFS");
    return true;
}
