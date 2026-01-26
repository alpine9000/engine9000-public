#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "geo_profiler_elf.h"

// Read helpers for endianness-safe parsing
static inline uint16_t r_u16(const uint8_t *p, int be) {
    return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t r_u32(const uint8_t *p, int be) {
    if (be)
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    else
        return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

// Find a section by name in an ELF32 file buffer
static const uint8_t* find_section(const uint8_t *buf, size_t fsz, const char *sname, size_t *out_sz, int be) {
    if (fsz < 52) return NULL;
    const uint8_t *eh = buf;
    uint16_t e_shentsize = r_u16(eh + 46, be);
    uint32_t e_shoff = r_u32(eh + 32, be);
    uint16_t e_shnum = r_u16(eh + 48, be);
    uint16_t e_shstrndx = r_u16(eh + 50, be);
    if (e_shoff == 0 || e_shentsize == 0 || e_shnum == 0) return NULL;
    if ((uint64_t)e_shoff + (uint64_t)e_shentsize * e_shnum > (uint64_t)fsz) return NULL;
    const uint8_t *shdr = buf + e_shoff;
    if (e_shstrndx >= e_shnum) return NULL;
    const uint8_t *sh_str = shdr + e_shstrndx * e_shentsize;
    uint32_t shstr_off = r_u32(sh_str + 16, be);
    uint32_t shstr_size = r_u32(sh_str + 20, be);
    if ((uint64_t)shstr_off + shstr_size > (uint64_t)fsz) return NULL;
    const char *strtab = (const char*)(buf + shstr_off);
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const uint8_t *sh = shdr + i * e_shentsize;
        uint32_t sh_name = r_u32(sh + 0, be);
        uint32_t sh_offset = r_u32(sh + 16, be);
        uint32_t sh_size = r_u32(sh + 20, be);
        if (sh_name < shstr_size) {
            const char *nm = strtab + sh_name;
            if (strcmp(nm, sname) == 0) {
                if ((uint64_t)sh_offset + sh_size > (uint64_t)fsz) return NULL;
                *out_sz = sh_size;
                return buf + sh_offset;
            }
        }
    }
    return NULL;
}

// Free resources owned by a LineTable
void geo_elf_free_line_table(LineTable *lt) {
    if (!lt) return;
    if (lt->dirs) { for (size_t i=0;i<lt->ndirs;i++) free(lt->dirs[i]); free(lt->dirs); lt->dirs=NULL; lt->ndirs=0; }
    if (lt->files) { for (size_t i=0;i<lt->nfiles;i++) free(lt->files[i]); free(lt->files); lt->files=NULL; lt->nfiles=0; }
    if (lt->file_dir) { free(lt->file_dir); lt->file_dir=NULL; }
    if (lt->rows) { free(lt->rows); lt->rows=NULL; lt->nrows=0; }
}

// .debug_line v2-v4 parser (subset sufficient for PC->file:line)
static int parse_debug_line_v4(const uint8_t *sec, size_t secsz, int be, LineTable *out) {
    LineTable lt = (LineTable){0};
    const uint8_t *p = sec;
    const uint8_t *end = sec + secsz;
    while (p + 4 <= end) {
        uint32_t unit_length = r_u32(p, be); p += 4; if (unit_length == 0 || p + unit_length > end) break;
        const uint8_t *cu_end = p + unit_length;
        if (p + 2 > cu_end) break; uint16_t version = r_u16(p, be); p += 2;
        if (version < 2 || version > 4) { p = cu_end; continue; }
        if (p + 4 + 1 + 1 + 1 + 1 > cu_end) break; uint32_t header_length = r_u32(p, be); p += 4;
        const uint8_t *hdr_end = p + header_length;
        if (hdr_end > cu_end) break;
        uint8_t min_inst_len = *p++;
        uint8_t default_is_stmt = *p++;
        int8_t line_base = (int8_t)*p++;
        uint8_t line_range = *p++;
        uint8_t opcode_base = *p++;
        // skip standard opcode lengths
        for (uint8_t i=1; i<opcode_base; ++i) { if (p >= hdr_end) break; p++; }
        // include directories
        while (p < hdr_end && *p) { const char *s=(const char*)p; size_t l=strlen(s)+1; char *ns=(char*)malloc(l); if(!ns){ geo_elf_free_line_table(&lt); return 0;} memcpy(ns,s,l); lt.dirs=(char**)realloc(lt.dirs, (lt.ndirs+1)*sizeof(char*)); lt.dirs[lt.ndirs++]=ns; p+=l; }
        if (p < hdr_end) p++;
        // file names
        while (p < hdr_end && *p) {
            const char *s=(const char*)p; size_t l=strlen(s)+1; char *ns=(char*)malloc(l); if(!ns){ geo_elf_free_line_table(&lt); return 0;} memcpy(ns,s,l); p+=l;
            // dir_index, time, size (uleb128)
            // read uleb
            #define RD_ULEB(dst, pp) do { uint64_t v_=0; int sh_=0; const uint8_t *bp_=*(pp); for(;;){ uint8_t b_=*bp_++; v_|=(uint64_t)(b_&0x7f)<<sh_; if(!(b_&0x80)) break; sh_+=7;} *(pp)=bp_; (dst)=v_; } while(0)
            uint64_t dir_index=0, dummy=0;
            RD_ULEB(dir_index, &p); RD_ULEB(dummy, &p); RD_ULEB(dummy, &p);
            lt.files=(char**)realloc(lt.files,(lt.nfiles+1)*sizeof(char*)); lt.file_dir=(uint32_t*)realloc(lt.file_dir,(lt.nfiles+1)*sizeof(uint32_t)); lt.files[lt.nfiles]=ns; lt.file_dir[lt.nfiles]=(uint32_t)dir_index; lt.nfiles++;
        }
        if (p < hdr_end) p++;
        // program
        uint32_t address = 0; uint32_t file = 1; int32_t line = 1; uint8_t is_stmt = default_is_stmt; (void)is_stmt; int end_seq = 0;
        // emit helper
        #define EMIT_ROW() do { if (!end_seq) { size_t n=lt.nrows; lt.rows=(LineRow*)realloc(lt.rows,(n+1)*sizeof(LineRow)); lt.rows[n].start=address; lt.rows[n].end=address; lt.rows[n].file=file; lt.rows[n].line=(uint32_t)(line>0?line:0); lt.nrows=n+1; } } while(0)
        while (p < cu_end) {
            uint8_t op = *p++;
            if (op >= opcode_base) {
                uint8_t adj = op - opcode_base; int32_t line_inc = line_base + (adj % line_range); uint32_t addr_inc = (adj / line_range) * min_inst_len; line += line_inc; address += addr_inc; EMIT_ROW();
            } else if (op == 0) {
                // extended
                // uleb decode length
                uint64_t ext_len = 0; int sh=0; for(;;){ uint8_t b=*p++; ext_len|=(uint64_t)(b&0x7f)<<sh; if(!(b&0x80)) break; sh+=7; }
                const uint8_t *ext_end = p + ext_len; if (ext_end > cu_end) break;
                uint8_t sub = *p++;
                if (sub == 1) { // end_sequence
                    end_seq = 1; if (lt.nrows) lt.rows[lt.nrows-1].end = address; address=0; file=1; line=1; is_stmt=default_is_stmt; end_seq=0; p=ext_end;
                } else if (sub == 2) { // set_address (assume 4 bytes)
                    if (p+4>ext_end){p=ext_end;continue;} address = r_u32(p, be); p = ext_end;
                } else if (sub == 3) { // define_file
                    const char *s2=(const char*)p; size_t l2=strlen(s2)+1; char *ns2=(char*)malloc(l2); if(!ns2){ geo_elf_free_line_table(&lt); return 0;} memcpy(ns2,s2,l2); p+=l2;
                    // dir_index, time, size (uleb128)
                    uint64_t dir_index=0, dummy2=0;
                    RD_ULEB(dir_index, &p); RD_ULEB(dummy2, &p); RD_ULEB(dummy2, &p);
                    lt.files=(char**)realloc(lt.files,(lt.nfiles+1)*sizeof(char*)); lt.file_dir=(uint32_t*)realloc(lt.file_dir,(lt.nfiles+1)*sizeof(uint32_t)); lt.files[lt.nfiles]=ns2; lt.file_dir[lt.nfiles]=(uint32_t)dir_index; lt.nfiles++;
                    p = ext_end;
                } else {
                    p = ext_end;
                }
            } else {
                switch (op) {
                    case 1: { EMIT_ROW(); break; }
                    case 2: { // advance_pc (uleb)
                        uint64_t ad=0; int sh3=0; for(;;){ uint8_t b=*p++; ad|=(uint64_t)(b&0x7f)<<sh3; if(!(b&0x80)) break; sh3+=7; } address += (uint32_t)(ad * min_inst_len); break; }
                    case 3: { // advance_line (sleb)
                        int64_t v=0; int sh4=0; uint8_t b; do{ b=*p++; v|=(int64_t)(b&0x7f)<<sh4; sh4+=7;}while(b&0x80); if ((b&0x40) && sh4<64) v |= -((int64_t)1<<sh4); line += (int32_t)v; break; }
                    case 4: { // set_file
                        // uleb
                        uint64_t fv=0; int sh5=0; for(;;){ uint8_t b=*p++; fv|=(uint64_t)(b&0x7f)<<sh5; if(!(b&0x80)) break; sh5+=7; } file=(uint32_t)fv; break; }
                    case 5: { // set_column
                        // skip uleb
                        while (*p & 0x80) p++; p++; break; }
                    case 6: { // negate_stmt
                        is_stmt = !is_stmt; break; }
                    case 7: { // set_basic_block
                        break; }
                    case 8: { // const_add_pc
                        { uint8_t adj2 = 255 - opcode_base; uint32_t addr_inc2 = (adj2 / line_range) * min_inst_len; address += addr_inc2; } break; }
                    case 9: { // fixed_advance_pc
                        if (p+2>cu_end){break;} uint16_t fa = (uint16_t)(p[0] | (p[1] << 8)); p += 2; address += fa; break; }
                    default: { break; }
                }
            }
        }
        // finalize end of rows
        for (size_t i=1;i<lt.nrows;++i) if (lt.rows[i-1].end == lt.rows[i-1].start) lt.rows[i-1].end = lt.rows[i].start;
        p = cu_end;
    }
    *out = lt; return lt.nrows > 0;
}

int geo_elf_load_line_table(const char *elf_path, LineTable *out) {
    if (!out) return 0; *out = (LineTable){0};
    FILE *f = fopen(elf_path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsz <= 0) { fclose(f); return 0; }
    uint8_t *buf = (uint8_t*)malloc((size_t)fsz); if (!buf){ fclose(f); return 0; }
    if (fread(buf,1,(size_t)fsz,f)!=(size_t)fsz){ free(buf); fclose(f); return 0; } fclose(f);
    int be = (buf[5] == 2);
    size_t lsz = 0; const uint8_t *ls = find_section(buf, (size_t)fsz, ".debug_line", &lsz, be);
    int ok = 0;
    if (ls && lsz) {
        LineTable lt = {0};
        if (parse_debug_line_v4(ls, lsz, be, &lt)) { *out = lt; ok = 1; }
    }
    free(buf);
    return ok;
}
