#include <stdint.h>

typedef uint64_t Elf64_Addr;

// Relocation structures with addend
typedef struct {
    Elf64_Addr  r_offset;
    uint64_t    r_info;
    int64_t     r_addend;
} Elf64_Rela;

// Relocation structures without addend
typedef struct {
    Elf64_Addr  r_offset;
    uint64_t    r_info;
} Elf64_Rel;

// ARM64 relocation types
#define ELF64_R_TYPE(x)         ((x) & 0xffffffff)
#define R_AARCH64_ABS64         257
#define R_AARCH64_AUTH_ABS64    580
#define R_AARCH64_COPY          1024
#define R_AARCH64_GLOB_DAT      1025
#define R_AARCH64_JUMP_SLOT     1026
#define R_AARCH64_RELATIVE      1027
#define R_AARCH64_TLS_IMPDEF1   1028
#define R_AARCH64_TLS_IMPDEF2   1029
#define R_AARCH64_TLS_TPREL     1030
#define R_AARCH64_TLSDESC       1031
#define R_AARCH64_IRRELATIVE    1032
#define R_AARCH64_AUTH_RELATIVE     1041
#define R_AARCH64_AUTH_GLOB_DAT     1042
#define R_AARCH64_AUTH_TLSDESC      1043
#define R_AARCH64_AUTH_IRELATIVE    1044
