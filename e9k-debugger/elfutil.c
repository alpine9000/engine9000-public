/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "elfutil.h"
#include "alloc.h"

#ifndef EI_NIDENT
#define EI_NIDENT 16
#endif

// ELF identification indexes
#define EI_CLASS 4
#define EI_DATA  5

#define ELFCLASS32 1
#define ELFCLASS64 2

#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

typedef struct { unsigned char e_ident[EI_NIDENT]; uint16_t e_type; uint16_t e_machine; uint32_t e_version; uint32_t e_entry; uint32_t e_phoff; uint32_t e_shoff; uint32_t e_flags; uint16_t e_ehsize; uint16_t e_phentsize; uint16_t e_phnum; uint16_t e_shentsize; uint16_t e_shnum; uint16_t e_shstrndx; } Elf32_Ehdr;
typedef struct { unsigned char e_ident[EI_NIDENT]; uint16_t e_type; uint16_t e_machine; uint32_t e_version; uint64_t e_entry; uint64_t e_phoff; uint64_t e_shoff; uint32_t e_flags; uint16_t e_ehsize; uint16_t e_phentsize; uint16_t e_phnum; uint16_t e_shentsize; uint16_t e_shnum; uint16_t e_shstrndx; } Elf64_Ehdr;

typedef struct { uint32_t sh_name; uint32_t sh_type; uint32_t sh_flags; uint32_t sh_addr; uint32_t sh_offset; uint32_t sh_size; uint32_t sh_link; uint32_t sh_info; uint32_t sh_addralign; uint32_t sh_entsize; } Elf32_Shdr;
typedef struct { uint32_t sh_name; uint32_t sh_type; uint64_t sh_flags; uint64_t sh_addr; uint64_t sh_offset; uint64_t sh_size; uint32_t sh_link; uint32_t sh_info; uint64_t sh_addralign; uint64_t sh_entsize; } Elf64_Shdr;

typedef struct { uint32_t p_type; uint32_t p_offset; uint32_t p_vaddr; uint32_t p_paddr; uint32_t p_filesz; uint32_t p_memsz; uint32_t p_flags; uint32_t p_align; } Elf32_Phdr;
typedef struct { uint32_t p_type; uint32_t p_flags; uint64_t p_offset; uint64_t p_vaddr; uint64_t p_paddr; uint64_t p_filesz; uint64_t p_memsz; uint64_t p_align; } Elf64_Phdr;

#ifndef PT_LOAD
#define PT_LOAD 1
#endif
#ifndef PF_X
#define PF_X 0x1
#endif

static int host_is_le(void) { uint16_t x = 1; return *((uint8_t*)&x) == 1; }

static uint16_t bswap16(uint16_t v) { return (uint16_t)((v>>8) | (v<<8)); }
static uint32_t bswap32(uint32_t v) { return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24); }
static uint64_t bswap64(uint64_t v) {
    return ((v & 0x00000000000000FFull) << 56) |
           ((v & 0x000000000000FF00ull) << 40) |
           ((v & 0x0000000000FF0000ull) << 24) |
           ((v & 0x00000000FF000000ull) << 8)  |
           ((v & 0x000000FF00000000ull) >> 8)  |
           ((v & 0x0000FF0000000000ull) >> 24) |
           ((v & 0x00FF000000000000ull) >> 40) |
           ((v & 0xFF00000000000000ull) >> 56);
}

static void swap_ehdr32_if_needed(Elf32_Ehdr *h, int file_le)
{
    int host_le = host_is_le(); if (host_le == file_le) return;
    h->e_type      = bswap16(h->e_type);
    h->e_machine   = bswap16(h->e_machine);
    h->e_version   = bswap32(h->e_version);
    h->e_entry     = bswap32(h->e_entry);
    h->e_phoff     = bswap32(h->e_phoff);
    h->e_shoff     = bswap32(h->e_shoff);
    h->e_flags     = bswap32(h->e_flags);
    h->e_ehsize    = bswap16(h->e_ehsize);
    h->e_phentsize = bswap16(h->e_phentsize);
    h->e_phnum     = bswap16(h->e_phnum);
    h->e_shentsize = bswap16(h->e_shentsize);
    h->e_shnum     = bswap16(h->e_shnum);
    h->e_shstrndx  = bswap16(h->e_shstrndx);
}

static void swap_ehdr64_if_needed(Elf64_Ehdr *h, int file_le)
{
    int host_le = host_is_le(); if (host_le == file_le) return;
    h->e_type      = bswap16(h->e_type);
    h->e_machine   = bswap16(h->e_machine);
    h->e_version   = bswap32(h->e_version);
    h->e_entry     = bswap64(h->e_entry);
    h->e_phoff     = bswap64(h->e_phoff);
    h->e_shoff     = bswap64(h->e_shoff);
    h->e_flags     = bswap32(h->e_flags);
    h->e_ehsize    = bswap16(h->e_ehsize);
    h->e_phentsize = bswap16(h->e_phentsize);
    h->e_phnum     = bswap16(h->e_phnum);
    h->e_shentsize = bswap16(h->e_shentsize);
    h->e_shnum     = bswap16(h->e_shnum);
    h->e_shstrndx  = bswap16(h->e_shstrndx);
}

static void swap_shdr32_if_needed(Elf32_Shdr *s, int file_le)
{
    int host_le = host_is_le(); if (host_le == file_le) return;
    s->sh_name      = bswap32(s->sh_name);
    s->sh_type      = bswap32(s->sh_type);
    s->sh_flags     = bswap32(s->sh_flags);
    s->sh_addr      = bswap32(s->sh_addr);
    s->sh_offset    = bswap32(s->sh_offset);
    s->sh_size      = bswap32(s->sh_size);
    s->sh_link      = bswap32(s->sh_link);
    s->sh_info      = bswap32(s->sh_info);
    s->sh_addralign = bswap32(s->sh_addralign);
    s->sh_entsize   = bswap32(s->sh_entsize);
}

static void swap_shdr64_if_needed(Elf64_Shdr *s, int file_le)
{
    int host_le = host_is_le(); if (host_le == file_le) return;
    s->sh_name      = bswap32(s->sh_name);
    s->sh_type      = bswap32(s->sh_type);
    s->sh_flags     = bswap64(s->sh_flags);
    s->sh_addr      = bswap64(s->sh_addr);
    s->sh_offset    = bswap64(s->sh_offset);
    s->sh_size      = bswap64(s->sh_size);
    s->sh_link      = bswap32(s->sh_link);
    s->sh_info      = bswap32(s->sh_info);
    s->sh_addralign = bswap64(s->sh_addralign);
    s->sh_entsize   = bswap64(s->sh_entsize);
}

static void swap_phdr32_if_needed(Elf32_Phdr *p, int file_le)
{
    int host_le = host_is_le(); if (host_le == file_le) return;
    p->p_type   = bswap32(p->p_type);
    p->p_offset = bswap32(p->p_offset);
    p->p_vaddr  = bswap32(p->p_vaddr);
    p->p_paddr  = bswap32(p->p_paddr);
    p->p_filesz = bswap32(p->p_filesz);
    p->p_memsz  = bswap32(p->p_memsz);
    p->p_flags  = bswap32(p->p_flags);
    p->p_align  = bswap32(p->p_align);
}

static void swap_phdr64_if_needed(Elf64_Phdr *p, int file_le)
{
    int host_le = host_is_le(); if (host_le == file_le) return;
    p->p_type   = bswap32(p->p_type);
    p->p_flags  = bswap32(p->p_flags);
    p->p_offset = bswap64(p->p_offset);
    p->p_vaddr  = bswap64(p->p_vaddr);
    p->p_paddr  = bswap64(p->p_paddr);
    p->p_filesz = bswap64(p->p_filesz);
    p->p_memsz  = bswap64(p->p_memsz);
    p->p_align  = bswap64(p->p_align);
}

static int starts_with(const char *s, const char *pfx) { return s && pfx && strncmp(s, pfx, strlen(pfx)) == 0; }

int
elfutil_getTextBounds(const char *elf_path, uint64_t *out_lo, uint64_t *out_hi)
{
    if (!elf_path || !*elf_path || !out_lo || !out_hi) return 0;
    *out_lo = 0; *out_hi = 0;
    FILE *f = fopen(elf_path, "rb");
    if (!f) return 0;
    unsigned char ident[EI_NIDENT];
    if (fread(ident, 1, EI_NIDENT, f) != EI_NIDENT) { fclose(f); return 0; }
    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') { fclose(f); return 0; }
    int cls = ident[EI_CLASS];
    int file_le = (ident[EI_DATA] == ELFDATA2LSB) ? 1 : 0;
    int host_le = host_is_le(); (void)host_le;
    if (cls == ELFCLASS32) {
        Elf32_Ehdr eh;
        // We already read ident; read rest of header
        if (fread(((unsigned char*)&eh) + EI_NIDENT, 1, sizeof(Elf32_Ehdr) - EI_NIDENT, f) != sizeof(Elf32_Ehdr) - EI_NIDENT) { fclose(f); return 0; }
        memcpy(eh.e_ident, ident, EI_NIDENT);
        swap_ehdr32_if_needed(&eh, file_le);
        if (eh.e_shoff == 0 || eh.e_shentsize < sizeof(Elf32_Shdr)) { fclose(f); return 0; }
        // Handle extended section numbering
        uint16_t shnum = eh.e_shnum;
        uint16_t shstrndx = eh.e_shstrndx;
        if (shnum == 0 || shstrndx == 0xFFFF) {
            // Read section[0]
            if (fseek(f, (long)eh.e_shoff, SEEK_SET) != 0) { fclose(f); return 0; }
            Elf32_Shdr sh0; if (fread(&sh0, 1, sizeof(sh0), f) != sizeof(sh0)) { fclose(f); return 0; }
            swap_shdr32_if_needed(&sh0, file_le);
            if (shnum == 0) shnum = (uint16_t)sh0.sh_size; // actual number of sections
            if (shstrndx == 0xFFFF) shstrndx = (uint16_t)sh0.sh_link; // actual string table index
        }
        if (shnum == 0) { fclose(f); return 0; }
        // Read shstrtab
        if (fseek(f, (long)(eh.e_shoff + (uint64_t)shstrndx * eh.e_shentsize), SEEK_SET) != 0) { fclose(f); return 0; }
        Elf32_Shdr shstr; if (fread(&shstr, 1, sizeof(shstr), f) != sizeof(shstr)) { fclose(f); return 0; }
        swap_shdr32_if_needed(&shstr, file_le);
        char *strtab = NULL; size_t strsz = (size_t)shstr.sh_size;
        if (strsz > 0) {
            strtab = (char*)alloc_alloc(strsz);
            if (!strtab) { fclose(f); return 0; }
            if (fseek(f, (long)shstr.sh_offset, SEEK_SET) != 0) { alloc_free(strtab); fclose(f); return 0; }
            if (fread(strtab, 1, strsz, f) != strsz) { alloc_free(strtab); fclose(f); return 0; }
        }
        // Iterate sections
        if (fseek(f, (long)eh.e_shoff, SEEK_SET) != 0) { alloc_free(strtab); fclose(f); return 0; }
        Elf32_Shdr sh;
        uint64_t lo=0, hi=0;
        for (uint16_t i=0;i<shnum;i++) {
            if (fread(&sh, 1, sizeof(sh), f) != sizeof(sh)) break;
            swap_shdr32_if_needed(&sh, file_le);
            const char *nm = (strtab && sh.sh_name < strsz) ? (strtab + sh.sh_name) : NULL;
            if (nm && (strcmp(nm, ".text") == 0 || starts_with(nm, ".text."))) {
                lo = (uint64_t)sh.sh_addr; hi = lo + (uint64_t)sh.sh_size; break;
            }
        }
        if (lo != 0 && hi > lo) { alloc_free(strtab); fclose(f); *out_lo = lo; *out_hi = hi; return 1; }
        // Fallback: program headers executable load
        // Handle extended phnum
        uint16_t phnum = eh.e_phnum;
        if (phnum == 0xFFFF) {
            if (fseek(f, (long)eh.e_shoff, SEEK_SET) == 0) {
                Elf32_Shdr sh0; if (fread(&sh0, 1, sizeof(sh0), f) == sizeof(sh0)) { swap_shdr32_if_needed(&sh0, file_le); phnum = (uint16_t)sh0.sh_info; }
            }
        }
        if (eh.e_phoff != 0 && eh.e_phentsize >= sizeof(Elf32_Phdr) && phnum > 0) {
            if (fseek(f, (long)eh.e_phoff, SEEK_SET) == 0) {
                uint64_t plo = 0, phi = 0;
                for (uint16_t i=0;i<phnum;i++) {
                    Elf32_Phdr ph; if (fread(&ph, 1, sizeof(ph), f) != sizeof(ph)) break;
                    swap_phdr32_if_needed(&ph, file_le);
                    if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X)) {
                        uint64_t a = ph.p_vaddr; uint64_t b = a + ph.p_memsz;
                        if (plo == 0 || a < plo) plo = a;
                        if (b > phi) phi = b;
                    }
                }
                if (plo != 0 && phi > plo) { alloc_free(strtab); fclose(f); *out_lo = plo; *out_hi = phi; return 1; }
            }
        }
        alloc_free(strtab);
        fclose(f);
        return 0;
    } else if (cls == ELFCLASS64) {
        Elf64_Ehdr eh;
        if (fread(((unsigned char*)&eh) + EI_NIDENT, 1, sizeof(Elf64_Ehdr) - EI_NIDENT, f) != sizeof(Elf64_Ehdr) - EI_NIDENT) { fclose(f); return 0; }
        memcpy(eh.e_ident, ident, EI_NIDENT);
        swap_ehdr64_if_needed(&eh, file_le);
        if (eh.e_shoff == 0 || eh.e_shentsize < sizeof(Elf64_Shdr)) { fclose(f); return 0; }
        uint16_t shnum = eh.e_shnum;
        uint16_t shstrndx = eh.e_shstrndx;
        if (shnum == 0 || shstrndx == 0xFFFF) {
            if (fseek(f, (long)eh.e_shoff, SEEK_SET) != 0) { fclose(f); return 0; }
            Elf64_Shdr sh0; if (fread(&sh0, 1, sizeof(sh0), f) != sizeof(sh0)) { fclose(f); return 0; }
            swap_shdr64_if_needed(&sh0, file_le);
            if (shnum == 0) shnum = (uint16_t)sh0.sh_size;
            if (shstrndx == 0xFFFF) shstrndx = (uint16_t)sh0.sh_link;
        }
        if (fseek(f, (long)(eh.e_shoff + (uint64_t)shstrndx * eh.e_shentsize), SEEK_SET) != 0) { fclose(f); return 0; }
        Elf64_Shdr shstr; if (fread(&shstr, 1, sizeof(shstr), f) != sizeof(shstr)) { fclose(f); return 0; }
        swap_shdr64_if_needed(&shstr, file_le);
        char *strtab = NULL; size_t strsz = (size_t)shstr.sh_size;
        if (strsz > 0) {
            strtab = (char*)alloc_alloc(strsz);
            if (!strtab) { fclose(f); return 0; }
            if (fseek(f, (long)shstr.sh_offset, SEEK_SET) != 0) { alloc_free(strtab); fclose(f); return 0; }
            if (fread(strtab, 1, strsz, f) != strsz) { alloc_free(strtab); fclose(f); return 0; }
        }
        if (fseek(f, (long)eh.e_shoff, SEEK_SET) != 0) { alloc_free(strtab); fclose(f); return 0; }
        Elf64_Shdr sh; uint64_t lo=0, hi=0;
        for (uint16_t i=0;i<shnum;i++) {
            if (fread(&sh, 1, sizeof(sh), f) != sizeof(sh)) break;
            swap_shdr64_if_needed(&sh, file_le);
            const char *nm = (strtab && sh.sh_name < strsz) ? (strtab + sh.sh_name) : NULL;
            if (nm && (strcmp(nm, ".text") == 0 || starts_with(nm, ".text."))) {
                lo = (uint64_t)sh.sh_addr; hi = lo + (uint64_t)sh.sh_size; break;
            }
        }
        if (lo != 0 && hi > lo) { alloc_free(strtab); fclose(f); *out_lo = lo; *out_hi = hi; return 1; }
        // Fallback: program headers executable load
        uint16_t phnum = eh.e_phnum;
        if (phnum == 0xFFFF) {
            if (fseek(f, (long)eh.e_shoff, SEEK_SET) == 0) {
                Elf64_Shdr sh0; if (fread(&sh0, 1, sizeof(sh0), f) == sizeof(sh0)) { swap_shdr64_if_needed(&sh0, file_le); phnum = (uint16_t)sh0.sh_info; }
            }
        }
        if (eh.e_phoff != 0 && eh.e_phentsize >= sizeof(Elf64_Phdr) && phnum > 0) {
            if (fseek(f, (long)eh.e_phoff, SEEK_SET) == 0) {
                uint64_t plo = 0, phi = 0;
                for (uint16_t i=0;i<phnum;i++) {
                    Elf64_Phdr ph; if (fread(&ph, 1, sizeof(ph), f) != sizeof(ph)) break;
                    swap_phdr64_if_needed(&ph, file_le);
                    if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X)) {
                        uint64_t a = ph.p_vaddr; uint64_t b = a + ph.p_memsz;
                        if (plo == 0 || a < plo) plo = a;
                        if (b > phi) phi = b;
                    }
                }
                if (plo != 0 && phi > plo) { alloc_free(strtab); fclose(f); *out_lo = plo; *out_hi = phi; return 1; }
            }
        }
        alloc_free(strtab);
        fclose(f);
        return 0;
    } else {
        fclose(f);
        return 0;
    }
}
