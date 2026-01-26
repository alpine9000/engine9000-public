/*
MIT License

Copyright (c) 2019 Nicolas Allemand
Copyright (c) 2020-2022 Rupert Carmichael
Copyright (c) 2022 rofl0r

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "z80.h"

#ifndef Z80_READ_BYTE
#define Z80_READ_BYTE(U, A) z->read_byte(U, A)
#define Z80_WRITE_BYTE(U, A, V) z->write_byte(U, A, V)
#endif

enum z80_flagbit {
    cf = 0,
    nf = 1,
    pf = 2,
    xf = 3,
    hf = 4,
    yf = 5,
    zf = 6,
    sf = 7
};

static const uint8_t f_szpxy[] = {
    0x44, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x08, 0x0c, 0x0c, 0x08, 0x0c, 0x08, 0x08, 0x0c,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x0c, 0x08, 0x08, 0x0c, 0x08, 0x0c, 0x0c, 0x08,
    0x20, 0x24, 0x24, 0x20, 0x24, 0x20, 0x20, 0x24, 0x2c, 0x28, 0x28, 0x2c, 0x28, 0x2c, 0x2c, 0x28,
    0x24, 0x20, 0x20, 0x24, 0x20, 0x24, 0x24, 0x20, 0x28, 0x2c, 0x2c, 0x28, 0x2c, 0x28, 0x28, 0x2c,
    0x00, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00, 0x04, 0x0c, 0x08, 0x08, 0x0c, 0x08, 0x0c, 0x0c, 0x08,
    0x04, 0x00, 0x00, 0x04, 0x00, 0x04, 0x04, 0x00, 0x08, 0x0c, 0x0c, 0x08, 0x0c, 0x08, 0x08, 0x0c,
    0x24, 0x20, 0x20, 0x24, 0x20, 0x24, 0x24, 0x20, 0x28, 0x2c, 0x2c, 0x28, 0x2c, 0x28, 0x28, 0x2c,
    0x20, 0x24, 0x24, 0x20, 0x24, 0x20, 0x20, 0x24, 0x2c, 0x28, 0x28, 0x2c, 0x28, 0x2c, 0x2c, 0x28,
    0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x8c, 0x88, 0x88, 0x8c, 0x88, 0x8c, 0x8c, 0x88,
    0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x88, 0x8c, 0x8c, 0x88, 0x8c, 0x88, 0x88, 0x8c,
    0xa4, 0xa0, 0xa0, 0xa4, 0xa0, 0xa4, 0xa4, 0xa0, 0xa8, 0xac, 0xac, 0xa8, 0xac, 0xa8, 0xa8, 0xac,
    0xa0, 0xa4, 0xa4, 0xa0, 0xa4, 0xa0, 0xa0, 0xa4, 0xac, 0xa8, 0xa8, 0xac, 0xa8, 0xac, 0xac, 0xa8,
    0x84, 0x80, 0x80, 0x84, 0x80, 0x84, 0x84, 0x80, 0x88, 0x8c, 0x8c, 0x88, 0x8c, 0x88, 0x88, 0x8c,
    0x80, 0x84, 0x84, 0x80, 0x84, 0x80, 0x80, 0x84, 0x8c, 0x88, 0x88, 0x8c, 0x88, 0x8c, 0x8c, 0x88,
    0xa0, 0xa4, 0xa4, 0xa0, 0xa4, 0xa0, 0xa0, 0xa4, 0xac, 0xa8, 0xa8, 0xac, 0xa8, 0xac, 0xac, 0xa8,
    0xa4, 0xa0, 0xa0, 0xa4, 0xa0, 0xa4, 0xa4, 0xa0, 0xa8, 0xac, 0xac, 0xa8, 0xac, 0xa8, 0xa8, 0xac,
};

// MARK: helpers

// get bit "n" of number "val"
#define GET_BIT(n, val) (((val) >> (n)) & 1)

static inline uint8_t flag_val(enum z80_flagbit bit, bool cond) {
  return (!!cond) << bit;
}

static inline bool flag_get(z80* const z, enum z80_flagbit bit) {
  return !!(z->f & (1 << bit));
}

static inline void flag_set(z80* const z, enum z80_flagbit bit, bool val) {
  z->f &= ~(1<<bit);
  z->f |= (!!val) << bit;
}

static inline uint8_t rb(z80* const z, uint16_t addr) {
  return Z80_READ_BYTE(z->userdata, addr);
}

static inline void wb(z80* const z, uint16_t addr, uint8_t val) {
  Z80_WRITE_BYTE(z->userdata, addr, val);
}

static inline uint16_t rw(z80* const z, uint16_t addr) {
  return (Z80_READ_BYTE(z->userdata, addr + 1) << 8) |
         Z80_READ_BYTE(z->userdata, addr);
}

static inline void ww(z80* const z, uint16_t addr, uint16_t val) {
  Z80_WRITE_BYTE(z->userdata, addr, val & 0xFF);
  Z80_WRITE_BYTE(z->userdata, addr + 1, val >> 8);
}

static inline void pushw(z80* const z, uint16_t val) {
  z->sp -= 2;
  ww(z, z->sp, val);
}

static inline uint16_t popw(z80* const z) {
  z->sp += 2;
  return rw(z, z->sp - 2);
}

static inline uint8_t nextb(z80* const z) {
  return rb(z, z->pc++);
}

static inline uint16_t nextw(z80* const z) {
  z->pc += 2;
  return rw(z, z->pc - 2);
}

// increments R, keeping the highest byte intact
static inline void inc_r(z80* const z) {
  z->r = (z->r & 0x80) | ((z->r + 1) & 0x7f);
}

// returns if there was a carry between bit "bit_no" and "bit_no - 1" when
// executing "a + b + cy"
/*static inline bool carry(int bit_no, uint16_t a, uint16_t b, bool cy) {
  int32_t result = a + b + cy;
  int32_t carry = result ^ a ^ b;
  return carry & (1 << bit_no);
}*/

// returns the parity of byte: 0 if number of 1 bits in `val` is odd, else 1
static inline bool parity(uint8_t v) {
  v ^= v >> 4;
  v &= 0xf;
  return !((0x6996 >> v) & 1);
}

static unsigned exec_opcode(z80* const z, uint8_t opcode);
static unsigned exec_opcode_cb(z80* const z, uint8_t opcode);
static unsigned exec_opcode_dcb(
    z80* const z, const uint8_t opcode, const uint16_t addr);
static unsigned exec_opcode_ed(z80* const z, uint8_t opcode);
static unsigned exec_opcode_ddfd(z80* const z, uint8_t opcode, uint16_t* const iz);

// MARK: opcodes
// jumps to an address
static inline void jump(z80* const z, uint16_t addr) {
  z->pc = addr;
  z->mem_ptr = addr;
}

// jumps to next word in memory if condition is true
static inline void cond_jump(z80* const z, bool condition) {
  const uint16_t addr = nextw(z);
  if (condition) {
    jump(z, addr);
  }
  z->mem_ptr = addr;
}

// calls to next word in memory
static inline void call(z80* const z, uint16_t addr) {
  pushw(z, z->pc);
  z->pc = addr;
  z->mem_ptr = addr;
}

// calls to next word in memory if condition is true
static inline unsigned cond_call(z80* const z, bool condition) {
  const uint16_t addr = nextw(z);
  unsigned cyc = 0;
  if (condition) {
    call(z, addr);
    cyc = 7;
  }
  z->mem_ptr = addr;
  return cyc;
}

// returns from subroutine
static inline void ret(z80* const z) {
  z->pc = popw(z);
  z->mem_ptr = z->pc;
}

// returns from subroutine if condition is true
static inline unsigned cond_ret(z80* const z, bool condition) {
  if (condition) {
    ret(z);
    return 6;
  }
  return 0;
}

static inline void jr(z80* const z, int8_t displacement) {
  z->pc += displacement;
  z->mem_ptr = z->pc;
}

static inline unsigned cond_jr(z80* const z, bool condition) {
  const int8_t b = nextb(z);
  if (condition) {
    jr(z, b);
    return 5;
  }
  return 0;
}

// ADD Byte: adds two bytes together
static inline uint8_t addb(z80* const z, uint32_t a, uint32_t b, bool cy) {
/* as this is one of the core functions, the simple implementation
   was replaced by an "unrolled" one. the basic algorithm is this
   old code:

  const uint8_t result = a + b + cy;
  z->f = (f_szpxy[result] & ~(1 << pf)) |
    flag_val(hf, carry(4, a, b, cy)) |
    flag_val(pf, carry(7, a, b, cy) != carry(8, a, b, cy)) |
    flag_val(cf, carry(8, a, b, cy)) |
    flag_val(nf, 0);
  return result;
*/
  int32_t result = a + b + cy;
  int32_t carry = result ^ a ^ b;
  result &= 0xff;
  z->f = (f_szpxy[result] & ~(1 << pf));
  /* the carry flag we need (4th bit) is already in the right position
     as hf is bit 4 too, so we don't need to shift it around */
  z->f |= carry & (1 << hf);
  /* we now need bits 7 and 8 of the carry - pf is set to one if bit7 != bit8.
     we use the base algorithm (x+1) & 2 which results in 2 only if both low
     bits aren't identical. in order to have the right bits in place, shift
     by 6 and modulate the algorithm to work on the top 2 bits of the lower 3.*/
  carry >>= 6;
  z->f |= (carry+2) & 4;
  /* cf is bit 0, so move the 8th bit of carry to that position. */
  z->f |= (carry >> 2);
  return result;
}

// SUBstract Byte: substracts two bytes (with optional carry)
static inline uint8_t subb(z80* const z, uint32_t a, uint32_t b, bool cy) {
/* like addb, this core functionality was "unrolled".
   the simple original implemenation is this:
  uint8_t val = addb(z, a, ~b, !cy);
  flag_set(z, cf, !flag_get(z, cf));
  flag_set(z, hf, !flag_get(z, hf));
  flag_set(z, nf, 1);
  return val;
  read the comments in addb to see explanations for the below new code.
  */
  int32_t result = a - b - cy;
  int32_t carry = result ^ a ^ b;
  result &= 0xff;
  z->f = (1 << nf) | (f_szpxy[result] & ~(1 << pf));
  z->f |= carry & (1 << hf);
  carry >>= 6;
  z->f |= ((carry+2) & 4);
  z->f |= ((carry >> 2) & 1);
  return result;
}

// ADD Word: adds two words together
static inline uint16_t addw(z80* const z, uint16_t a, uint16_t b, bool cy) {
  uint8_t lsb = addb(z, a, b, cy);
  uint8_t msb = addb(z, a >> 8, b >> 8, flag_get(z, cf));

  uint16_t result = (msb << 8) | lsb;
  flag_set(z, zf, result == 0);
  z->mem_ptr = a + 1;
  return result;
}

// SUBstract Word: substracts two words (with optional carry)
static inline uint16_t subw(z80* const z, uint16_t a, uint16_t b, bool cy) {
  uint8_t lsb = subb(z, a, b, cy);
  uint8_t msb = subb(z, a >> 8, b >> 8, flag_get(z, cf));

  uint16_t result = (msb << 8) | lsb;
  flag_set(z, zf, result == 0);
  z->mem_ptr = a + 1;
  return result;
}

// adds a word to HL
static inline void addhl(z80* const z, uint16_t val) {
  bool sfc = flag_get(z, sf);
  bool zfc = flag_get(z, zf);
  bool pfc = flag_get(z, pf);
  uint16_t result = addw(z, z->hl, val, 0);
  z->hl = result;
  flag_set(z, sf, sfc);
  flag_set(z, zf, zfc);
  flag_set(z, pf, pfc);
}

// adds a word to IX or IY
static inline void addiz(z80* const z, uint16_t* reg, uint16_t val) {
  bool sfc = flag_get(z, sf);
  bool zfc = flag_get(z, zf);
  bool pfc = flag_get(z, pf);
  uint16_t result = addw(z, *reg, val, 0);
  *reg = result;
  flag_set(z, sf, sfc);
  flag_set(z, zf, zfc);
  flag_set(z, pf, pfc);
}

// adds a word (+ carry) to HL
static inline void adchl(z80* const z, uint16_t val) {
  uint16_t result = addw(z, z->hl, val, flag_get(z, cf));
  flag_set(z, sf, result >> 15);
  flag_set(z, zf, result == 0);
  z->hl = result;
}

// substracts a word (+ carry) to HL
static inline void sbchl(z80* const z, uint16_t val) {
  const uint16_t result = subw(z, z->hl, val, flag_get(z, cf));
  flag_set(z, sf, result >> 15);
  flag_set(z, zf, result == 0);
  z->hl = result;
}

// increments a byte value
static inline uint8_t inc(z80* const z, uint8_t a) {
  bool cfc = flag_get(z, cf);
  uint8_t result = addb(z, a, 1, 0);
  flag_set(z, cf, cfc);
  return result;
}

// decrements a byte value
static inline uint8_t dec(z80* const z, uint8_t a) {
  bool cfc = flag_get(z, cf);
  uint8_t result = subb(z, a, 1, 0);
  flag_set(z, cf, cfc);
  return result;
}

// MARK: bitwise

// executes a logic "and" between register A and a byte, then stores the
// result in register A
static inline void land(z80* const z, uint8_t val) {
  const uint8_t result = z->a & val;
  z->f = f_szpxy[result] |
    flag_val(hf,  1) |
    flag_val(nf, 0) |
    flag_val(cf, 0);
  z->a = result;
}

// executes a logic "xor" between register A and a byte, then stores the
// result in register A
static inline void lxor(z80* const z, const uint8_t val) {
  const uint8_t result = z->a ^ val;
  z->f = f_szpxy[result] |
    flag_val(hf, 0) |
    flag_val(nf, 0) |
    flag_val(cf, 0);
  z->a = result;
}

// executes a logic "or" between register A and a byte, then stores the
// result in register A
static inline void lor(z80* const z, const uint8_t val) {
  const uint8_t result = z->a | val;
  z->f = f_szpxy[result] |
    flag_val(hf, 0) |
    flag_val(nf, 0) |
    flag_val(cf, 0);
  z->a = result;
}

// compares a value with register A
static inline void cp(z80* const z, const uint32_t val) {
/* this core function was unrolled like subb and addb.
   original implementation was:
  subb(z, z->a, val, 0);
  // the only difference between cp and sub is that
  // the xf/yf are taken from the value to be substracted,
  // not the result
  flag_set(z, yf, GET_BIT(5, val));
  flag_set(z, xf, GET_BIT(3, val));

  because only 2 out of 5 flag of f_szpxy table would be
  needed here, we instead calculate all of them from
  scratch. see addb() comments for an explanation.
*/
  int32_t result = z->a - val;
  int32_t carry = result ^ z->a ^ val;
  z->f = (1 << nf) | /* nf always set */
         (val & ((1 << xf) | (1 << yf))) | /* yf and xf is taken from val */
         (result & (1 << sf)) | /* sf and zf from result */
         ((!(result & 0xff)) << zf);
  z->f |= carry & (1 << hf); /* adopt bit 4 directly from carry */
  carry >>= 6;
  z->f |= ((carry +2) & 4);
  z->f |= ((carry >> 2) & 1);
}

// 0xCB opcodes
// rotate left with carry
static inline uint8_t cb_rlc(z80* const z, uint8_t val) {
  const bool old = val >> 7;
  val = (val << 1) | old;
  z->f = f_szpxy[val] |
    flag_val(nf, 0) |
    flag_val(hf, 0) |
    flag_val(cf, old);
  return val;
}

// rotate right with carry
static inline uint8_t cb_rrc(z80* const z, uint8_t val) {
  const bool old = val & 1;
  val = (val >> 1) | (old << 7);
  z->f = f_szpxy[val] |
    flag_val(nf, 0) |
    flag_val(hf, 0) |
    flag_val(cf, old);
  return val;
}

// rotate left (simple)
static inline uint8_t cb_rl(z80* const z, uint8_t val) {
  const bool cfc = flag_get(z, cf);
  const bool cfn = val >> 7;
  val = (val << 1) | cfc;
  z->f = f_szpxy[val] |
    flag_val(cf, cfn) |
    flag_val(nf, 0) |
    flag_val(hf, 0);
  return val;
}

// rotate right (simple)
static inline uint8_t cb_rr(z80* const z, uint8_t val) {
  const bool c = flag_get(z, cf);
  const bool cfn = val & 1;
  val = (val >> 1) | (c << 7);
  z->f = f_szpxy[val] |
    flag_val(cf, cfn) |
    flag_val(nf, 0) |
    flag_val(hf, 0);
  return val;
}

// shift left preserving sign
static inline uint8_t cb_sla(z80* const z, uint8_t val) {
  const bool cfn = val >> 7;
  val <<= 1;
  z->f = f_szpxy[val] |
    flag_val(cf, cfn) |
    flag_val(nf, 0) |
    flag_val(hf, 0);
  return val;
}

// SLL (exactly like SLA, but sets the first bit to 1)
static inline uint8_t cb_sll(z80* const z, uint8_t val) {
  const bool cfn = val >> 7;
  val <<= 1;
  val |= 1;
  z->f = f_szpxy[val] |
    flag_val(cf, cfn) |
    flag_val(nf, 0) |
    flag_val(hf, 0);
  return val;
}

// shift right preserving sign
static inline uint8_t cb_sra(z80* const z, uint8_t val) {
  const bool cfn = val & 1;
  val = (val >> 1) | (val & 0x80); // 0b10000000
  z->f = f_szpxy[val] |
    flag_val(cf, cfn) |
    flag_val(nf, 0) |
    flag_val(hf, 0);
  return val;
}

// shift register right
static inline uint8_t cb_srl(z80* const z, uint8_t val) {
  const bool cfn = val & 1;
  val >>= 1;
  z->f = f_szpxy[val] |
    flag_val(cf, cfn) |
    flag_val(nf, 0) |
    flag_val(hf, 0);
  return val;
}

// tests bit "n" from a byte
static inline uint8_t cb_bit(z80* const z, uint8_t val, uint8_t n) {
  const uint8_t result = val & (1 << n);
  z->f = f_szpxy[result] |
    flag_val(cf, flag_get(z, cf)) | /* original code didn't set this one, so use old value */
    flag_val(hf, 1) |
    flag_val(nf, 0); /* yf/xf are overwritten after return */
  return result;
}

static inline void ldi(z80* const z) {
  const uint16_t de = z->de;
  const uint16_t hl = z->hl;
  const uint8_t val = rb(z, hl);

  wb(z, de, val);

  ++z->hl;
  ++z->de;
  --z->bc;

  // see https://wikiti.brandonw.net/index.php?title=Z80_Instruction_Set
  // for the calculation of xf/yf on LDI
  const uint8_t result = val + z->a;
  flag_set(z, xf, GET_BIT(3, result));
  flag_set(z, yf, GET_BIT(1, result));

  flag_set(z, nf, 0);
  flag_set(z, hf, 0);
  flag_set(z, pf, z->bc > 0);
}

static inline void ldd(z80* const z) {
  ldi(z);
  // same as ldi but HL and DE are decremented instead of incremented
  z->hl -= 2;
  z->de -= 2;
}

static inline void cpi(z80* const z) {
  bool cfc = flag_get(z, cf);
  const uint8_t result = subb(z, z->a, rb(z, z->hl), 0);
  ++z->hl;
  --z->bc;
  bool hfc = flag_get(z, hf);
  flag_set(z, xf, GET_BIT(3, result - hfc));
  flag_set(z, yf, GET_BIT(1, result - hfc));
  flag_set(z, pf, z->bc != 0);
  flag_set(z, cf, cfc);
  z->mem_ptr += 1;
}

static inline void cpd(z80* const z) {
  cpi(z);
  // same as cpi but HL is decremented instead of incremented
  z->hl -= 2;
  z->mem_ptr -= 2;
}

static void in_r_c(z80* const z, uint8_t* r) {
  *r = z->port_in(z, z->bc);
  // FIXME: according to z80 wiki this one should set x/y flags too,
  // in which case we should be able to use f_szpxy.
  flag_set(z, zf, *r == 0);
  flag_set(z, sf, *r >> 7);
  flag_set(z, pf, parity(*r));
  flag_set(z, nf, 0);
  flag_set(z, hf, 0);
}

static void ini(z80* const z) {
  unsigned tmp = z->port_in(z, z->bc);
  unsigned tmp2 = tmp + ((z->c + 1) & 0xff);
  z->mem_ptr = z->bc + 1;
  wb(z, z->hl, tmp);
  ++z->hl;
  --z->b;
  z->f = (f_szpxy[z->b] & ~(1 << pf)) |
    flag_val(nf, GET_BIT(7, tmp)) |
    flag_val(pf, parity((tmp2 & 7) ^ z->b)) |
    flag_val(hf, tmp2 > 255) |
    flag_val(cf, tmp2 > 255);
}

static void ind(z80* const z) {
  unsigned tmp = z->port_in(z, z->bc);
  unsigned tmp2 = tmp + ((z->c - 1) & 0xff);
  z->mem_ptr = z->bc - 1;
  wb(z, z->hl, tmp);
  --z->hl;
  --z->b;
  z->f = (f_szpxy[z->b] & ~(1 << pf)) |
    flag_val(nf, GET_BIT(7, tmp)) |
    flag_val(pf, parity((tmp2 & 7) ^ z->b)) |
    flag_val(hf, tmp2 > 255) |
    flag_val(cf, tmp2 > 255);
}

static void outi(z80* const z) {
  unsigned tmp = rb(z, z->hl), tmp2;
  z->port_out(z, z->bc, tmp);
  ++z->hl;
  z->b -= 1;
  z->f = f_szpxy[z->b];
  flag_set(z, nf, GET_BIT(7, tmp));
  tmp2 = tmp + z->l;
  flag_set(z, pf, parity((tmp2 & 7) ^ z->b));
  flag_set(z, hf, tmp2 > 255);
  flag_set(z, cf, tmp2 > 255);
  z->mem_ptr = z->bc + 1;
}

static void outd(z80* const z) {
  outi(z);
  z->hl -= 2;
  z->mem_ptr = z->bc - 2;
}

static void outc(z80* const z, uint8_t data) {
  z->port_out(z, z->bc, data);
  z->mem_ptr = z->bc + 1;
}

static void daa(z80* const z) {
  // "When this instruction is executed, the A register is BCD corrected
  // using the  contents of the flags. The exact process is the following:
  // if the least significant four bits of A contain a non-BCD digit
  // (i. e. it is greater than 9) or the H flag is set, then $06 is
  // added to the register. Then the four most significant bits are
  // checked. If this more significant digit also happens to be greater
  // than 9 or the C flag is set, then $60 is added."
  // > http://z80-heaven.wikidot.com/instructions-set:daa
  uint8_t correction = 0;

  if ((z->a & 0x0F) > 0x09 || flag_get(z, hf)) {
    correction += 0x06;
  }

  if (z->a > 0x99 || flag_get(z, cf)) {
    correction += 0x60;
    flag_set(z, cf, 1);
  }

  const bool substraction = flag_get(z, nf);
  if (substraction) {
    flag_set(z, hf, flag_get(z, hf) && (z->a & 0x0F) < 0x06);
    z->a -= correction;
  } else {
    flag_set(z, hf, (z->a & 0x0F) > 0x09);
    z->a += correction;
  }
  z->f &= ~((1 << sf) | (1 << zf) | (1 << pf) | (1 << xf) | (1 << yf));
  z->f |= f_szpxy[z->a];
}

static inline uint16_t displace(
    z80* const z, uint16_t base_addr, int8_t displacement) {
  const uint16_t addr = base_addr + displacement;
  z->mem_ptr = addr;
  return addr;
}

static inline unsigned process_interrupts(z80* const z) {
  unsigned cyc = 0;
  // "When an EI instruction is executed, any pending interrupt request
  // is not accepted until after the instruction following EI is executed."
  if (z->iff_delay > 0) {
    z->iff_delay -= 1;
    if (z->iff_delay == 0) {
      z->iff1 = 1;
      z->iff2 = 1;
    }
    return cyc;
  }

  if (z->nmi_pending) {
    z->nmi_pending &= ~Z80_PULSE;
    z->halted = 0;
    z->iff1 = 0;
    inc_r(z);

    cyc += 11;
    call(z, 0x66);
    return cyc;
  }

  if (z->irq_pending && z->iff1) {
    z->irq_pending &= ~Z80_PULSE;
    z->halted = 0;
    z->iff1 = 0;
    z->iff2 = 0;
    inc_r(z);

    switch (z->interrupt_mode) {
    case 0:
      cyc += 11;
      cyc += exec_opcode(z, z->irq_data);
      break;

    case 1:
      cyc += 13;
      call(z, 0x38);
      break;

    case 2:
      cyc += 19;
      call(z, rw(z, (z->i << 8) | z->irq_data));
      break;

    default:
      //fprintf(stderr, "unsupported interrupt mode %d\n", z->interrupt_mode);
      break;
    }

    return cyc;
  }
  return cyc;
}

// MARK: interface
// initialises a z80 struct. Note that read_byte, write_byte, port_in, port_out
// and userdata must be manually set by the user afterwards.
Z80_EXPORT void z80_init(z80* const z) {
  z->read_byte = NULL;
  z->write_byte = NULL;
  z->port_in = NULL;
  z->port_out = NULL;
  z->userdata = NULL;

  z->pc = 0;
  z->sp = 0xFFFF;
  z->ix = 0;
  z->iy = 0;
  z->mem_ptr = 0;

  // af and sp are set to 0xFFFF after reset,
  // and the other values are undefined (z80-documented)
  z->af = 0xFFFF;
  z->bc = 0;
  z->de = 0;
  z->hl = 0;

  z->a_f_ = 0;
  z->b_c_ = 0;
  z->d_e_ = 0;
  z->h_l_ = 0;

  z->i = 0;
  z->r = 0;

  z->iff_delay = 0;
  z->interrupt_mode = 0;
  z->iff1 = 0;
  z->iff2 = 0;
  z->halted = 0;
  z->irq_pending = 0;
  z->nmi_pending = 0;
  z->irq_data = 0;
}

Z80_EXPORT void z80_reset(z80* const z) {
  z->pc = 0;
  z->mem_ptr = 0;

  z->i = 0;
  z->r = 0;

  z->interrupt_mode = 0;
  z->iff_delay = 0;
  z->iff1 = 0;
  z->iff2 = 0;
  z->halted = 0;
  z->nmi_pending = 0;
}

static unsigned z80_step_s(z80* const z) {
  unsigned cyc = 0;
  if (z->halted) {
    cyc += exec_opcode(z, 0x00);
  } else {
    const uint8_t opcode = nextb(z);
    cyc += exec_opcode(z, opcode);
  }

  cyc += process_interrupts(z);
  return cyc;
}

// sets the program counter (PC) to a new value
Z80_EXPORT void z80_set_pc(z80* const z, uint16_t pc) {
  z->pc = pc;
}

// sets the stack pointer (SP) to a new value
Z80_EXPORT void z80_set_sp(z80* const z, uint16_t sp) {
  z->sp = sp;
}

// executes the next instruction in memory + handles interrupts
Z80_EXPORT unsigned z80_step(z80* const z) {
  return z80_step_s(z);
}

// executes the next instructions in memory + handles interrupts,
// until the cycle count is >= the requested amount.
Z80_EXPORT unsigned z80_step_n(z80* const z, unsigned cycles) {
  unsigned cyc = 0;
  while (cyc < cycles) {
    cyc += z80_step_s(z);
  }
  return cyc;
}

#ifdef Z80_DEBUG
static inline uint8_t getpcb(z80* const z, unsigned offset) {
  return rb(z, z->pc+offset);
}
static inline uint16_t getpcw(z80* const z, unsigned offset) {
  return rw(z, z->pc+offset);
}

static char* z80_disas_ddfd(z80* const z) {
  uint8_t opcode = getpcb(z, 1);
  switch (opcode) {
  case 0xE1: return "pop iz";
  case 0xE5: return "push iz";

  case 0xE9: return "jp iz";

  case 0x09: return "add iz,bc";
  case 0x19: return "add iz,de";
  case 0x29: return "add iz,iz";
  case 0x39: return "add iz,sp";

  case 0x84: return "add a,izh";
  case 0x85: return "add a,izl";
  case 0x8C: return "adc a,izh";
  case 0x8D: return "adc a,izl";

  case 0x86: return "add a,(iz+*)";
  case 0x8E: return "adc a,(iz+*)";
  case 0x96: return "sub (iz+*)";
  case 0x9E: return "sbc (iz+*)";

  case 0x94: return "sub izh";
  case 0x95: return "sub izl";
  case 0x9C: return "sbc izh";
  case 0x9D: return "sbc izl";

  case 0xA6: return "and (iz+*)";
  case 0xA4: return "and izh";
  case 0xA5: return "and izl";

  case 0xAE: return "xor (iz+*)";
  case 0xAC: return "xor izh";
  case 0xAD: return "xor izl";

  case 0xB6: return "or (iz+*)";
  case 0xB4: return "or izh";
  case 0xB5: return "or izl";

  case 0xBE: return "cp (iz+*)";
  case 0xBC: return "cp izh";
  case 0xBD: return "cp izl";

  case 0x23: return "inc iz";
  case 0x2B: return "dec iz";

  case 0x34: return "inc (iz+*)";

  case 0x35: return "dec (iz+*)";

  case 0x24: return "inc izh";
  case 0x25: return "dec izh";
  case 0x2C: return "inc izl";
  case 0x2D: return "dec izl";

  case 0x2A: return "ld iz,(**)";
  case 0x22: return "ld (**),iz";
  case 0x21: return "ld iz,**";

  case 0x36: return "ld (iz+*),*";

  case 0x70: return "ld (iz+*),b";
  case 0x71: return "ld (iz+*),c";
  case 0x72: return "ld (iz+*),d";
  case 0x73: return "ld (iz+*),e";
  case 0x74: return "ld (iz+*),h";
  case 0x75: return "ld (iz+*),l";
  case 0x77: return "ld (iz+*),a";

  case 0x46: return "ld b,(iz+*)";
  case 0x4E: return "ld c,(iz+*)";
  case 0x56: return "ld d,(iz+*)";
  case 0x5E: return "ld e,(iz+*)";
  case 0x66: return "ld h,(iz+*)";
  case 0x6E: return "ld l,(iz+*)";
  case 0x7E: return "ld a,(iz+*)";

  case 0x44: return "ld b,izh";
  case 0x4C: return "ld c,izh";
  case 0x54: return "ld d,izh";
  case 0x5C: return "ld e,izh";
  case 0x7C: return "ld a,izh";

  case 0x45: return "ld b,izl";
  case 0x4D: return "ld c,izl";
  case 0x55: return "ld d,izl";
  case 0x5D: return "ld e,izl";
  case 0x7D: return "ld a,izl";

  case 0x60: return "ld izh,b";
  case 0x61: return "ld izh,c";
  case 0x62: return "ld izh,d";
  case 0x63: return "ld izh,e";
  case 0x64: return "ld izh,izh";
  case 0x65: return "ld izh,izl";
  case 0x67: return "ld izh,a";
  case 0x26: return "ld izh,*";

  case 0x68: return "ld izl,b";
  case 0x69: return "ld izl,c";
  case 0x6A: return "ld izl,d";
  case 0x6B: return "ld izl,e";
  case 0x6C: return "ld izl,izh";
  case 0x6D: return "ld izl,izl";
  case 0x6F: return "ld izl,a";
  case 0x2E: return "ld izl,*";

  case 0xF9: return "ld sp,iz";

  case 0xE3: return "ex (sp),iz";

  case 0xCB: return "DCBxx"; // FIXME: return what opcode_dcb does
  }
  return "redirect to normal opcode"; // FIXME: this should print regular opcode instead
}

static char* z80_disas(z80* const z) {
  static char buf[32];
  uint8_t opcode = getpcb(z, 0);
  switch (opcode) {
  case 0x7F: return "ld a,a";
  case 0x78: return "ld a,b";
  case 0x79: return "ld a,c";
  case 0x7A: return "ld a,d";
  case 0x7B: return "ld a,e";
  case 0x7C: return "ld a,h";
  case 0x7D: return "ld a,l";

  case 0x47: return "ld b,a";
  case 0x40: return "ld b,b";
  case 0x41: return "ld b,c";
  case 0x42: return "ld b,d";
  case 0x43: return "ld b,e";
  case 0x44: return "ld b,h";
  case 0x45: return "ld b,l";

  case 0x4F: return "ld c,a";
  case 0x48: return "ld c,b";
  case 0x49: return "ld c,c";
  case 0x4A: return "ld c,d";
  case 0x4B: return "ld c,e";
  case 0x4C: return "ld c,h";
  case 0x4D: return "ld c,l";

  case 0x57: return "ld d,a";
  case 0x50: return "ld d,b";
  case 0x51: return "ld d,c";
  case 0x52: return "ld d,d";
  case 0x53: return "ld d,e";
  case 0x54: return "ld d,h";
  case 0x55: return "ld d,l";

  case 0x5F: return "ld e,a";
  case 0x58: return "ld e,b";
  case 0x59: return "ld e,c";
  case 0x5A: return "ld e,d";
  case 0x5B: return "ld e,e";
  case 0x5C: return "ld e,h";
  case 0x5D: return "ld e,l";

  case 0x67: return "ld h,a";
  case 0x60: return "ld h,b";
  case 0x61: return "ld h,c";
  case 0x62: return "ld h,d";
  case 0x63: return "ld h,e";
  case 0x64: return "ld h,h";
  case 0x65: return "ld h,l";

  case 0x6F: return "ld l,a";
  case 0x68: return "ld l,b";
  case 0x69: return "ld l,c";
  case 0x6A: return "ld l,d";
  case 0x6B: return "ld l,e";
  case 0x6C: return "ld l,h";
  case 0x6D: return "ld l,l";

  case 0x7E: return "ld a,(hl)";
  case 0x46: return "ld b,(hl)";
  case 0x4E: return "ld c,(hl)";
  case 0x56: return "ld d,(hl)";
  case 0x5E: return "ld e,(hl)";
  case 0x66: return "ld h,(hl)";
  case 0x6E: return "ld l,(hl)";

  case 0x77: return "ld (hl),a";
  case 0x70: return "ld (hl),b";
  case 0x71: return "ld (hl),c";
  case 0x72: return "ld (hl),d";
  case 0x73: return "ld (hl),e";
  case 0x74: return "ld (hl),h";
  case 0x75: return "ld (hl),l";

  case 0x3E: sprintf(buf, "ld a,%02x", getpcb(z, 1)); return buf;
  case 0x06: sprintf(buf, "ld b,%02x", getpcb(z, 1)); return buf;
  case 0x0E: sprintf(buf, "ld c,%02x", getpcb(z, 1)); return buf;
  case 0x16: sprintf(buf, "ld d,%02x", getpcb(z, 1)); return buf;
  case 0x1E: sprintf(buf, "ld e,%02x", getpcb(z, 1)); return buf;
  case 0x26: sprintf(buf, "ld h,%02x", getpcb(z, 1)); return buf;
  case 0x2E: sprintf(buf, "ld l,%02x", getpcb(z, 1)); return buf;
  case 0x36: sprintf(buf, "ld (hl),%02x", getpcb(z, 1)); return buf;

  case 0x0A: return "ld a,(bc)";
  case 0x1A: return "ld a,(de)";
  case 0x3A: sprintf(buf, "ld a,(%04x)", getpcw(z, 1)); return buf;

  case 0x02: return "ld (bc),a";

  case 0x12: return "ld (de),a";

  case 0x32: sprintf(buf, "ld (%04x), a", getpcw(z, 1)); return buf;

  case 0x01: sprintf(buf, "ld bc,%04x", getpcw(z, 1)); return buf;
  case 0x11: sprintf(buf, "ld de,%04x", getpcw(z, 1)); return buf;
  case 0x21: sprintf(buf, "ld hl,%04x", getpcw(z, 1)); return buf;
  case 0x31: sprintf(buf, "ld sp,%04x", getpcw(z, 1)); return buf;

  case 0x2A: sprintf(buf, "ld hl,(%04x)", getpcw(z, 1)); return buf;

  case 0x22: sprintf(buf, "ld (%04x),hl", getpcw(z, 1)); return buf;

  case 0xF9: return "ld sp,hl";

  case 0xEB: return "ex de,hl";

  case 0xE3: return "ex (sp),hl";

  case 0x87: return "add a,a";
  case 0x80: return "add a,b";
  case 0x81: return "add a,c";
  case 0x82: return "add a,d";
  case 0x83: return "add a,e";
  case 0x84: return "add a,h";
  case 0x85: return "add a,l";
  case 0x86: return "add a,(hl)";
  case 0xC6: sprintf(buf, "add a,%02x", getpcb(z, 1)); return buf;

  case 0x8F: return "adc a,a";
  case 0x88: return "adc a,b";
  case 0x89: return "adc a,c";
  case 0x8A: return "adc a,d";
  case 0x8B: return "adc a,e";
  case 0x8C: return "adc a,h";
  case 0x8D: return "adc a,l";
  case 0x8E: return "adc a,(hl)";
  case 0xCE: sprintf(buf, "adc a,%02x", getpcb(z, 1)); return buf;

  case 0x97: return "sub a,a";
  case 0x90: return "sub a,b";
  case 0x91: return "sub a,c";
  case 0x92: return "sub a,d";
  case 0x93: return "sub a,e";
  case 0x94: return "sub a,h";
  case 0x95: return "sub a,l";
  case 0x96: return "sub a,(hl)";
  case 0xD6: sprintf(buf, "sub a,%02x", getpcb(z, 1)); return buf;

  case 0x9F: return "sbc a,a";
  case 0x98: return "sbc a,b";
  case 0x99: return "sbc a,c";
  case 0x9A: return "sbc a,d";
  case 0x9B: return "sbc a,e";
  case 0x9C: return "sbc a,h";
  case 0x9D: return "sbc a,l";
  case 0x9E: return "sbc a,(hl)";
  case 0xDE: sprintf(buf, "sbc a,%02x", getpcb(z, 1)); return buf;

  case 0x09: return "add hl,bc";
  case 0x19: return "add hl,de";
  case 0x29: return "add hl,hl";
  case 0x39: return "add hl,sp";

  case 0xF3: return "di";
  case 0xFB: return "ei";
  case 0x00: return "nop";
  case 0x76: return "halt";

  case 0x3C: return "inc a";
  case 0x04: return "inc b";
  case 0x0C: return "inc c";
  case 0x14: return "inc d";
  case 0x1C: return "inc e";
  case 0x24: return "inc h";
  case 0x2C: return "inc l";
  case 0x34: return "inc (hl)";

  case 0x3D: return "dec a";
  case 0x05: return "dec b";
  case 0x0D: return "dec c";
  case 0x15: return "dec d";
  case 0x1D: return "dec e";
  case 0x25: return "dec h";
  case 0x2D: return "dec l";
  case 0x35: return "dec (hl)";

  case 0x03: return "inc bc";
  case 0x13: return "inc de";
  case 0x23: return "inc hl";
  case 0x33: return "inc sp";

  case 0x0B: return "dec bc";
  case 0x1B: return "dec de";
  case 0x2B: return "dec hl";
  case 0x3B: return "dec sp";

  case 0x27: return "daa";

  case 0x2F: return "cpl";

  case 0x37: return "scf";

  case 0x3F: return "ccf";

  case 0x07: return "rlca";

  case 0x0F: return "rrca";

  case 0x17: return "rla";

  case 0x1F: return "rra";

  case 0xA7: return "and a";
  case 0xA0: return "and b";
  case 0xA1: return "and c";
  case 0xA2: return "and d";
  case 0xA3: return "and e";
  case 0xA4: return "and h";
  case 0xA5: return "and l";
  case 0xA6: return "and (hl)";
  case 0xE6: sprintf(buf, "and %02x", getpcb(z, 1)); return buf;

  case 0xAF: return "xor a";
  case 0xA8: return "xor b";
  case 0xA9: return "xor c";
  case 0xAA: return "xor d";
  case 0xAB: return "xor e";
  case 0xAC: return "xor h";
  case 0xAD: return "xor l";
  case 0xAE: return "xor (hl)";
  case 0xEE: sprintf(buf, "xor %02x", getpcb(z, 1)); return buf;

  case 0xB7: return "or a";
  case 0xB0: return "or b";
  case 0xB1: return "or c";
  case 0xB2: return "or d";
  case 0xB3: return "or e";
  case 0xB4: return "or h";
  case 0xB5: return "or l";
  case 0xB6: return "or (hl)";
  case 0xF6: sprintf(buf, "or %02x", getpcb(z, 1)); return buf;

  case 0xBF: return "cp a";
  case 0xB8: return "cp b";
  case 0xB9: return "cp c";
  case 0xBA: return "cp d";
  case 0xBB: return "cp e";
  case 0xBC: return "cp h";
  case 0xBD: return "cp l";
  case 0xBE: return "cp (hl)";
  case 0xFE: sprintf(buf, "cp %02x", getpcb(z, 1)); return buf;

  case 0xC3: sprintf(buf, "jm %04x", getpcw(z, 1)); return buf;
  case 0xC2: sprintf(buf, "jp nz, %04x", getpcw(z, 1)); return buf;
  case 0xCA: sprintf(buf, "jp z, %04x", getpcw(z, 1)); return buf;
  case 0xD2: sprintf(buf, "jp nc, %04x", getpcw(z, 1)); return buf;
  case 0xDA: sprintf(buf, "jp c, %04x", getpcw(z, 1)); return buf;
  case 0xE2: sprintf(buf, "jp po, %04x", getpcw(z, 1)); return buf;
  case 0xEA: sprintf(buf, "jp pe, %04x", getpcw(z, 1)); return buf;
  case 0xF2: sprintf(buf, "jp p, %04x", getpcw(z, 1)); return buf;
  case 0xFA: sprintf(buf, "jp m, %04x", getpcw(z, 1)); return buf;

  case 0x10: sprintf(buf, "djnz %02x", getpcb(z, 1)); return buf;
  case 0x18: sprintf(buf, "jr %02x", getpcb(z, 1)); return buf;
  case 0x20: sprintf(buf, "jr nz, %02x", getpcb(z, 1)); return buf;
  case 0x28: sprintf(buf, "jr z, %02x", getpcb(z, 1)); return buf;
  case 0x30: sprintf(buf, "jr nc, %02x", getpcb(z, 1)); return buf;
  case 0x38: sprintf(buf, "jr c, %02x", getpcb(z, 1)); return buf;

  case 0xE9: return "jp (hl)";
  case 0xCD: sprintf(buf, "call %04x", getpcw(z, 1)); return buf;

  case 0xC4: return "cnz";
  case 0xCC: return "cz";
  case 0xD4: return "cnc";
  case 0xDC: return "cc";
  case 0xE4: return "cpo";
  case 0xEC: return "cpe";
  case 0xF4: return "cp";
  case 0xFC: return "cm";

  case 0xC9: return "ret";
  case 0xC0: return "ret nz";
  case 0xC8: return "ret z";
  case 0xD0: return "ret nc";
  case 0xD8: return "ret c";
  case 0xE0: return "ret po";
  case 0xE8: return "ret pe";
  case 0xF0: return "ret p";
  case 0xF8: return "ret m";

  case 0xC7: return "rst 0";
  case 0xCF: return "rst 1";
  case 0xD7: return "rst 2";
  case 0xDF: return "rst 3";
  case 0xE7: return "rst 4";
  case 0xEF: return "rst 5";
  case 0xF7: return "rst 6";
  case 0xFF: return "rst 7";

  case 0xC5: return "push bc";
  case 0xD5: return "push de";
  case 0xE5: return "push hl";
  case 0xF5: return "push af";

  case 0xC1: return "pop bc";
  case 0xD1: return "pop de";
  case 0xE1: return "pop hl";
  case 0xF1: return "pop af";

  case 0xDB: return "in a,(n)";

  case 0xD3: return "out (n), a";

  case 0x08: return "ex af,af'";
  case 0xD9: return "exx";

  case 0xCB: return "CB xx"; // fixme
  case 0xED: return "ED xx"; // fixme
  case 0xDD: return z80_disas_ddfd(z);
  case 0xFD: return z80_disas_ddfd(z);
  }
  return "ILL";
}

// outputs to stdout a debug trace of the emulator
Z80_EXPORT void z80_debug_output(z80* const z) {
  if (z) { }
  printf("PC: %04X, AF: %04X, BC: %04X, DE: %04X, HL: %04X, SP: %04X, "
         "IX: %04X, IY: %04X, IR: %04X, WZ: %04X",
      z->pc, z->af, z->bc, z->de, z->hl, z->sp,
      z->ix, z->iy, (z->i << 8) | z->r, z->mem_ptr);

  printf("\t(%s %02X %02X %02X %02X)\n",
      z80_disas(z),
      rb(z, z->pc), rb(z, z->pc + 1),
      rb(z, z->pc + 2), rb(z, z->pc + 3));
}
#else
Z80_EXPORT void z80_debug_output(z80* const z) {
  if (z) { }
}
#endif /* Z80_DEBUG */

// functions to call when an NMI is to be serviced
Z80_EXPORT void z80_assert_nmi(z80* const z) {
  z->nmi_pending |= Z80_ASSERT;
}

Z80_EXPORT void z80_pulse_nmi(z80* const z) {
  z->nmi_pending |= Z80_PULSE;
}

Z80_EXPORT void z80_clr_nmi(z80* const z) {
  z->nmi_pending = 0;
}

// functions to call when an INT is to be serviced
Z80_EXPORT void z80_assert_irq(z80* const z, uint8_t data) {
  z->irq_pending |= Z80_ASSERT;
  z->irq_data = data;
}

Z80_EXPORT void z80_pulse_irq(z80* const z, uint8_t data) {
  z->irq_pending |= Z80_PULSE;
  z->irq_data = data;
}

Z80_EXPORT void z80_clr_irq(z80* const z) {
    z->irq_pending = 0;
}

// executes a non-prefixed opcode
static unsigned exec_opcode(z80* const z, uint8_t opcode) {
  unsigned cyc = 0;
  inc_r(z);

  switch (opcode) {
  case 0x7F: cyc += 4; z->a = z->a; break; // ld a,a
  case 0x78: cyc += 4; z->a = z->b; break; // ld a,b
  case 0x79: cyc += 4; z->a = z->c; break; // ld a,c
  case 0x7A: cyc += 4; z->a = z->d; break; // ld a,d
  case 0x7B: cyc += 4; z->a = z->e; break; // ld a,e
  case 0x7C: cyc += 4; z->a = z->h; break; // ld a,h
  case 0x7D: cyc += 4; z->a = z->l; break; // ld a,l

  case 0x47: cyc += 4; z->b = z->a; break; // ld b,a
  case 0x40: cyc += 4; z->b = z->b; break; // ld b,b
  case 0x41: cyc += 4; z->b = z->c; break; // ld b,c
  case 0x42: cyc += 4; z->b = z->d; break; // ld b,d
  case 0x43: cyc += 4; z->b = z->e; break; // ld b,e
  case 0x44: cyc += 4; z->b = z->h; break; // ld b,h
  case 0x45: cyc += 4; z->b = z->l; break; // ld b,l

  case 0x4F: cyc += 4; z->c = z->a; break; // ld c,a
  case 0x48: cyc += 4; z->c = z->b; break; // ld c,b
  case 0x49: cyc += 4; z->c = z->c; break; // ld c,c
  case 0x4A: cyc += 4; z->c = z->d; break; // ld c,d
  case 0x4B: cyc += 4; z->c = z->e; break; // ld c,e
  case 0x4C: cyc += 4; z->c = z->h; break; // ld c,h
  case 0x4D: cyc += 4; z->c = z->l; break; // ld c,l

  case 0x57: cyc += 4; z->d = z->a; break; // ld d,a
  case 0x50: cyc += 4; z->d = z->b; break; // ld d,b
  case 0x51: cyc += 4; z->d = z->c; break; // ld d,c
  case 0x52: cyc += 4; z->d = z->d; break; // ld d,d
  case 0x53: cyc += 4; z->d = z->e; break; // ld d,e
  case 0x54: cyc += 4; z->d = z->h; break; // ld d,h
  case 0x55: cyc += 4; z->d = z->l; break; // ld d,l

  case 0x5F: cyc += 4; z->e = z->a; break; // ld e,a
  case 0x58: cyc += 4; z->e = z->b; break; // ld e,b
  case 0x59: cyc += 4; z->e = z->c; break; // ld e,c
  case 0x5A: cyc += 4; z->e = z->d; break; // ld e,d
  case 0x5B: cyc += 4; z->e = z->e; break; // ld e,e
  case 0x5C: cyc += 4; z->e = z->h; break; // ld e,h
  case 0x5D: cyc += 4; z->e = z->l; break; // ld e,l

  case 0x67: cyc += 4; z->h = z->a; break; // ld h,a
  case 0x60: cyc += 4; z->h = z->b; break; // ld h,b
  case 0x61: cyc += 4; z->h = z->c; break; // ld h,c
  case 0x62: cyc += 4; z->h = z->d; break; // ld h,d
  case 0x63: cyc += 4; z->h = z->e; break; // ld h,e
  case 0x64: cyc += 4; z->h = z->h; break; // ld h,h
  case 0x65: cyc += 4; z->h = z->l; break; // ld h,l

  case 0x6F: cyc += 4; z->l = z->a; break; // ld l,a
  case 0x68: cyc += 4; z->l = z->b; break; // ld l,b
  case 0x69: cyc += 4; z->l = z->c; break; // ld l,c
  case 0x6A: cyc += 4; z->l = z->d; break; // ld l,d
  case 0x6B: cyc += 4; z->l = z->e; break; // ld l,e
  case 0x6C: cyc += 4; z->l = z->h; break; // ld l,h
  case 0x6D: cyc += 4; z->l = z->l; break; // ld l,l

  case 0x7E: cyc += 7; z->a = rb(z, z->hl); break; // ld a,(hl)
  case 0x46: cyc += 7; z->b = rb(z, z->hl); break; // ld b,(hl)
  case 0x4E: cyc += 7; z->c = rb(z, z->hl); break; // ld c,(hl)
  case 0x56: cyc += 7; z->d = rb(z, z->hl); break; // ld d,(hl)
  case 0x5E: cyc += 7; z->e = rb(z, z->hl); break; // ld e,(hl)
  case 0x66: cyc += 7; z->h = rb(z, z->hl); break; // ld h,(hl)
  case 0x6E: cyc += 7; z->l = rb(z, z->hl); break; // ld l,(hl)

  case 0x77: cyc += 7; wb(z, z->hl, z->a); break; // ld (hl),a
  case 0x70: cyc += 7; wb(z, z->hl, z->b); break; // ld (hl),b
  case 0x71: cyc += 7; wb(z, z->hl, z->c); break; // ld (hl),c
  case 0x72: cyc += 7; wb(z, z->hl, z->d); break; // ld (hl),d
  case 0x73: cyc += 7; wb(z, z->hl, z->e); break; // ld (hl),e
  case 0x74: cyc += 7; wb(z, z->hl, z->h); break; // ld (hl),h
  case 0x75: cyc += 7; wb(z, z->hl, z->l); break; // ld (hl),l

  case 0x3E: cyc += 7; z->a = nextb(z); break; // ld a,*
  case 0x06: cyc += 7; z->b = nextb(z); break; // ld b,*
  case 0x0E: cyc += 7; z->c = nextb(z); break; // ld c,*
  case 0x16: cyc += 7; z->d = nextb(z); break; // ld d,*
  case 0x1E: cyc += 7; z->e = nextb(z); break; // ld e,*
  case 0x26: cyc += 7; z->h = nextb(z); break; // ld h,*
  case 0x2E: cyc += 7; z->l = nextb(z); break; // ld l,*
  case 0x36: cyc += 10; wb(z, z->hl, nextb(z)); break; // ld (hl),*

  case 0x0A:
    cyc += 7;
    z->a = rb(z, z->bc);
    z->mem_ptr = z->bc + 1;
    break; // ld a,(bc)
  case 0x1A:
    cyc += 7;
    z->a = rb(z, z->de);
    z->mem_ptr = z->de + 1;
    break; // ld a,(de)
  case 0x3A: {
    cyc += 13;
    const uint16_t addr = nextw(z);
    z->a = rb(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld a,(**)

  case 0x02:
    cyc += 7;
    wb(z, z->bc, z->a);
    z->mem_ptr = (z->a << 8) | ((z->bc + 1) & 0xFF);
    break; // ld (bc),a

  case 0x12:
    cyc += 7;
    wb(z, z->de, z->a);
    z->mem_ptr = (z->a << 8) | ((z->de + 1) & 0xFF);
    break; // ld (de),a

  case 0x32: {
    cyc += 13;
    const uint16_t addr = nextw(z);
    wb(z, addr, z->a);
    z->mem_ptr = (z->a << 8) | ((addr + 1) & 0xFF);
  } break; // ld (**),a

  case 0x01: cyc += 10; z->bc = nextw(z); break; // ld bc,**
  case 0x11: cyc += 10; z->de = nextw(z); break; // ld de,**
  case 0x21: cyc += 10; z->hl = nextw(z); break; // ld hl,**
  case 0x31: cyc += 10; z->sp = nextw(z); break; // ld sp,**

  case 0x2A: {
    cyc += 16;
    const uint16_t addr = nextw(z);
    z->hl = rw(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld hl,(**)

  case 0x22: {
    cyc += 16;
    const uint16_t addr = nextw(z);
    ww(z, addr, z->hl);
    z->mem_ptr = addr + 1;
  } break; // ld (**),hl

  case 0xF9: cyc += 6; z->sp = z->hl; break; // ld sp,hl

  case 0xEB: {
    cyc += 4;
    const uint16_t de = z->de;
    z->de = z->hl;
    z->hl = de;
  } break; // ex de,hl

  case 0xE3: {
    cyc += 19;
    const uint16_t val = rw(z, z->sp);
    ww(z, z->sp, z->hl);
    z->hl = val;
    z->mem_ptr = val;
  } break; // ex (sp),hl

  case 0x87: cyc += 4; z->a = addb(z, z->a, z->a, 0); break; // add a,a
  case 0x80: cyc += 4; z->a = addb(z, z->a, z->b, 0); break; // add a,b
  case 0x81: cyc += 4; z->a = addb(z, z->a, z->c, 0); break; // add a,c
  case 0x82: cyc += 4; z->a = addb(z, z->a, z->d, 0); break; // add a,d
  case 0x83: cyc += 4; z->a = addb(z, z->a, z->e, 0); break; // add a,e
  case 0x84: cyc += 4; z->a = addb(z, z->a, z->h, 0); break; // add a,h
  case 0x85: cyc += 4; z->a = addb(z, z->a, z->l, 0); break; // add a,l
  case 0x86: cyc += 7; z->a = addb(z, z->a, rb(z, z->hl), 0); break; // add a,(hl)
  case 0xC6: cyc += 7; z->a = addb(z, z->a, nextb(z), 0); break; // add a,*

  case 0x8F: cyc += 4; z->a = addb(z, z->a, z->a, flag_get(z, cf)); break; // adc a,a
  case 0x88: cyc += 4; z->a = addb(z, z->a, z->b, flag_get(z, cf)); break; // adc a,b
  case 0x89: cyc += 4; z->a = addb(z, z->a, z->c, flag_get(z, cf)); break; // adc a,c
  case 0x8A: cyc += 4; z->a = addb(z, z->a, z->d, flag_get(z, cf)); break; // adc a,d
  case 0x8B: cyc += 4; z->a = addb(z, z->a, z->e, flag_get(z, cf)); break; // adc a,e
  case 0x8C: cyc += 4; z->a = addb(z, z->a, z->h, flag_get(z, cf)); break; // adc a,h
  case 0x8D: cyc += 4; z->a = addb(z, z->a, z->l, flag_get(z, cf)); break; // adc a,l
  case 0x8E: cyc += 7; z->a = addb(z, z->a, rb(z, z->hl), flag_get(z, cf)); break; // adc a,(hl)
  case 0xCE: cyc += 7; z->a = addb(z, z->a, nextb(z), flag_get(z, cf)); break; // adc a,*

  case 0x97: cyc += 4; z->a = subb(z, z->a, z->a, 0); break; // sub a,a
  case 0x90: cyc += 4; z->a = subb(z, z->a, z->b, 0); break; // sub a,b
  case 0x91: cyc += 4; z->a = subb(z, z->a, z->c, 0); break; // sub a,c
  case 0x92: cyc += 4; z->a = subb(z, z->a, z->d, 0); break; // sub a,d
  case 0x93: cyc += 4; z->a = subb(z, z->a, z->e, 0); break; // sub a,e
  case 0x94: cyc += 4; z->a = subb(z, z->a, z->h, 0); break; // sub a,h
  case 0x95: cyc += 4; z->a = subb(z, z->a, z->l, 0); break; // sub a,l
  case 0x96: cyc += 7; z->a = subb(z, z->a, rb(z, z->hl), 0); break; // sub a,(hl)
  case 0xD6: cyc += 7; z->a = subb(z, z->a, nextb(z), 0); break; // sub a,*

  case 0x9F: cyc += 4; z->a = subb(z, z->a, z->a, flag_get(z, cf)); break; // sbc a,a
  case 0x98: cyc += 4; z->a = subb(z, z->a, z->b, flag_get(z, cf)); break; // sbc a,b
  case 0x99: cyc += 4; z->a = subb(z, z->a, z->c, flag_get(z, cf)); break; // sbc a,c
  case 0x9A: cyc += 4; z->a = subb(z, z->a, z->d, flag_get(z, cf)); break; // sbc a,d
  case 0x9B: cyc += 4; z->a = subb(z, z->a, z->e, flag_get(z, cf)); break; // sbc a,e
  case 0x9C: cyc += 4; z->a = subb(z, z->a, z->h, flag_get(z, cf)); break; // sbc a,h
  case 0x9D: cyc += 4; z->a = subb(z, z->a, z->l, flag_get(z, cf)); break; // sbc a,l
  case 0x9E: cyc += 7; z->a = subb(z, z->a, rb(z, z->hl), flag_get(z, cf)); break; // sbc a,(hl)
  case 0xDE: cyc += 7; z->a = subb(z, z->a, nextb(z), flag_get(z, cf)); break; // sbc a,*

  case 0x09: cyc += 11; addhl(z, z->bc); break; // add hl,bc
  case 0x19: cyc += 11; addhl(z, z->de); break; // add hl,de
  case 0x29: cyc += 11; addhl(z, z->hl); break; // add hl,hl
  case 0x39: cyc += 11; addhl(z, z->sp); break; // add hl,sp

  case 0xF3: cyc += 4; z->iff1 = z->iff2 = 0; break; // di
  case 0xFB: cyc += 4; z->iff_delay = 1; break; // ei
  case 0x00: cyc += 4; break; // nop
  case 0x76: cyc += 4; z->halted = 1; break; // halt

  case 0x3C: cyc += 4; z->a = inc(z, z->a); break; // inc a
  case 0x04: cyc += 4; z->b = inc(z, z->b); break; // inc b
  case 0x0C: cyc += 4; z->c = inc(z, z->c); break; // inc c
  case 0x14: cyc += 4; z->d = inc(z, z->d); break; // inc d
  case 0x1C: cyc += 4; z->e = inc(z, z->e); break; // inc e
  case 0x24: cyc += 4; z->h = inc(z, z->h); break; // inc h
  case 0x2C: cyc += 4; z->l = inc(z, z->l); break; // inc l
  case 0x34: {
    cyc += 11;
    uint8_t result = inc(z, rb(z, z->hl));
    wb(z, z->hl, result);
  } break; // inc (hl)

  case 0x3D: cyc += 4; z->a = dec(z, z->a); break; // dec a
  case 0x05: cyc += 4; z->b = dec(z, z->b); break; // dec b
  case 0x0D: cyc += 4; z->c = dec(z, z->c); break; // dec c
  case 0x15: cyc += 4; z->d = dec(z, z->d); break; // dec d
  case 0x1D: cyc += 4; z->e = dec(z, z->e); break; // dec e
  case 0x25: cyc += 4; z->h = dec(z, z->h); break; // dec h
  case 0x2D: cyc += 4; z->l = dec(z, z->l); break; // dec l
  case 0x35: {
    cyc += 11;
    uint8_t result = dec(z, rb(z, z->hl));
    wb(z, z->hl, result);
  } break; // dec (hl)

  case 0x03: cyc += 6; ++z->bc; break; // inc bc
  case 0x13: cyc += 6; ++z->de; break; // inc de
  case 0x23: cyc += 6; ++z->hl; break; // inc hl
  case 0x33: cyc += 6; ++z->sp; break; // inc sp

  case 0x0B: cyc += 6; --z->bc; break; // dec bc
  case 0x1B: cyc += 6; --z->de; break; // dec de
  case 0x2B: cyc += 6; --z->hl; break; // dec hl
  case 0x3B: cyc += 6; --z->sp; break; // dec sp

  case 0x27: cyc += 4; daa(z); break; // daa

  case 0x2F:
    cyc += 4;
    z->a = ~z->a;
    flag_set(z, nf, 1);
    flag_set(z, hf, 1);
    flag_set(z, xf, GET_BIT(3, z->a));
    flag_set(z, yf, GET_BIT(5, z->a));
    break; // cpl

  case 0x37:
    cyc += 4;
    flag_set(z, cf, 1);
    flag_set(z, nf, 0);
    flag_set(z, hf, 0);
    flag_set(z, xf, GET_BIT(3, z->a));
    flag_set(z, yf, GET_BIT(5, z->a));
    break; // scf

  case 0x3F:
    cyc += 4;
    flag_set(z, hf, flag_get(z, cf));
    flag_set(z, cf, !flag_get(z, cf));
    flag_set(z, nf, 0);
    flag_set(z, xf, GET_BIT(3, z->a));
    flag_set(z, yf, GET_BIT(5, z->a));
    break; // ccf

  case 0x07:
    cyc += 4; {
    flag_set(z, cf, z->a >> 7);
    z->a = (z->a << 1) | flag_get(z, cf);
    flag_set(z, nf, 0);
    flag_set(z, hf, 0);
    flag_set(z, xf, GET_BIT(3, z->a));
    flag_set(z, yf, GET_BIT(5, z->a));
  } break; // rlca (rotate left)

  case 0x0F: {
    cyc += 4;
    flag_set(z, cf, z->a & 1);
    z->a = (z->a >> 1) | (flag_get(z, cf) << 7);
    flag_set(z, nf, 0);
    flag_set(z, hf, 0);
    flag_set(z, xf, GET_BIT(3, z->a));
    flag_set(z, yf, GET_BIT(5, z->a));
  } break; // rrca (rotate right)

  case 0x17: {
    cyc += 4;
    const bool cy = flag_get(z, cf);
    flag_set(z, cf, z->a >> 7);
    z->a = (z->a << 1) | cy;
    flag_set(z, nf, 0);
    flag_set(z, hf, 0);
    flag_set(z, xf, GET_BIT(3, z->a));
    flag_set(z, yf, GET_BIT(5, z->a));
  } break; // rla

  case 0x1F: {
    cyc += 4;
    const bool cy = flag_get(z, cf);
    flag_set(z, cf, z->a & 1);
    z->a = (z->a >> 1) | (cy << 7);
    flag_set(z, nf, 0);
    flag_set(z, hf, 0);
    flag_set(z, xf, GET_BIT(3, z->a));
    flag_set(z, yf, GET_BIT(5, z->a));
  } break; // rra

  case 0xA7: cyc += 4; land(z, z->a); break; // and a
  case 0xA0: cyc += 4; land(z, z->b); break; // and b
  case 0xA1: cyc += 4; land(z, z->c); break; // and c
  case 0xA2: cyc += 4; land(z, z->d); break; // and d
  case 0xA3: cyc += 4; land(z, z->e); break; // and e
  case 0xA4: cyc += 4; land(z, z->h); break; // and h
  case 0xA5: cyc += 4; land(z, z->l); break; // and l
  case 0xA6: cyc += 7; land(z, rb(z, z->hl)); break; // and (hl)
  case 0xE6: cyc += 7; land(z, nextb(z)); break; // and *

  case 0xAF: cyc += 4; lxor(z, z->a); break; // xor a
  case 0xA8: cyc += 4; lxor(z, z->b); break; // xor b
  case 0xA9: cyc += 4; lxor(z, z->c); break; // xor c
  case 0xAA: cyc += 4; lxor(z, z->d); break; // xor d
  case 0xAB: cyc += 4; lxor(z, z->e); break; // xor e
  case 0xAC: cyc += 4; lxor(z, z->h); break; // xor h
  case 0xAD: cyc += 4; lxor(z, z->l); break; // xor l
  case 0xAE: cyc += 7; lxor(z, rb(z, z->hl)); break; // xor (hl)
  case 0xEE: cyc += 7; lxor(z, nextb(z)); break; // xor *

  case 0xB7: cyc += 4; lor(z, z->a); break; // or a
  case 0xB0: cyc += 4; lor(z, z->b); break; // or b
  case 0xB1: cyc += 4; lor(z, z->c); break; // or c
  case 0xB2: cyc += 4; lor(z, z->d); break; // or d
  case 0xB3: cyc += 4; lor(z, z->e); break; // or e
  case 0xB4: cyc += 4; lor(z, z->h); break; // or h
  case 0xB5: cyc += 4; lor(z, z->l); break; // or l
  case 0xB6: cyc += 7; lor(z, rb(z, z->hl)); break; // or (hl)
  case 0xF6: cyc += 7; lor(z, nextb(z)); break; // or *

  case 0xBF: cyc += 4; cp(z, z->a); break; // cp a
  case 0xB8: cyc += 4; cp(z, z->b); break; // cp b
  case 0xB9: cyc += 4; cp(z, z->c); break; // cp c
  case 0xBA: cyc += 4; cp(z, z->d); break; // cp d
  case 0xBB: cyc += 4; cp(z, z->e); break; // cp e
  case 0xBC: cyc += 4; cp(z, z->h); break; // cp h
  case 0xBD: cyc += 4; cp(z, z->l); break; // cp l
  case 0xBE: cyc += 7; cp(z, rb(z, z->hl)); break; // cp (hl)
  case 0xFE: cyc += 7; cp(z, nextb(z)); break; // cp *

  case 0xC3: cyc += 10; jump(z, nextw(z)); break; // jm **
  case 0xC2: cyc += 10; cond_jump(z, flag_get(z, zf) == 0); break; // jp nz, **
  case 0xCA: cyc += 10; cond_jump(z, flag_get(z, zf) == 1); break; // jp z, **
  case 0xD2: cyc += 10; cond_jump(z, flag_get(z, cf) == 0); break; // jp nc, **
  case 0xDA: cyc += 10; cond_jump(z, flag_get(z, cf) == 1); break; // jp c, **
  case 0xE2: cyc += 10; cond_jump(z, flag_get(z, pf) == 0); break; // jp po, **
  case 0xEA: cyc += 10; cond_jump(z, flag_get(z, pf) == 1); break; // jp pe, **
  case 0xF2: cyc += 10; cond_jump(z, flag_get(z, sf) == 0); break; // jp p, **
  case 0xFA: cyc += 10; cond_jump(z, flag_get(z, sf) == 1); break; // jp m, **

  case 0x10: cyc += 8; cyc += cond_jr(z, --z->b != 0); break; // djnz *
  case 0x18: cyc += 12; jr(z, nextb(z)); break; // jr *
  case 0x20: cyc += 7; cyc += cond_jr(z, flag_get(z, zf) == 0); break; // jr nz, *
  case 0x28: cyc += 7; cyc += cond_jr(z, flag_get(z, zf) == 1); break; // jr z, *
  case 0x30: cyc += 7; cyc += cond_jr(z, flag_get(z, cf) == 0); break; // jr nc, *
  case 0x38: cyc += 7; cyc += cond_jr(z, flag_get(z, cf) == 1); break; // jr c, *

  case 0xE9: cyc += 4; z->pc = z->hl; break; // jp (hl)
  case 0xCD: cyc += 17; call(z, nextw(z)); break; // call

  case 0xC4: cyc += 10; cyc += cond_call(z, flag_get(z, zf) == 0); break; // cnz
  case 0xCC: cyc += 10; cyc += cond_call(z, flag_get(z, zf) == 1); break; // cz
  case 0xD4: cyc += 10; cyc += cond_call(z, flag_get(z, cf) == 0); break; // cnc
  case 0xDC: cyc += 10; cyc += cond_call(z, flag_get(z, cf) == 1); break; // cc
  case 0xE4: cyc += 10; cyc += cond_call(z, flag_get(z, pf) == 0); break; // cpo
  case 0xEC: cyc += 10; cyc += cond_call(z, flag_get(z, pf) == 1); break; // cpe
  case 0xF4: cyc += 10; cyc += cond_call(z, flag_get(z, sf) == 0); break; // cp
  case 0xFC: cyc += 10; cyc += cond_call(z, flag_get(z, sf) == 1); break; // cm

  case 0xC9: cyc += 10; ret(z); break; // ret
  case 0xC0: cyc += 5; cyc += cond_ret(z, flag_get(z, zf) == 0); break; // ret nz
  case 0xC8: cyc += 5; cyc += cond_ret(z, flag_get(z, zf) == 1); break; // ret z
  case 0xD0: cyc += 5; cyc += cond_ret(z, flag_get(z, cf) == 0); break; // ret nc
  case 0xD8: cyc += 5; cyc += cond_ret(z, flag_get(z, cf) == 1); break; // ret c
  case 0xE0: cyc += 5; cyc += cond_ret(z, flag_get(z, pf) == 0); break; // ret po
  case 0xE8: cyc += 5; cyc += cond_ret(z, flag_get(z, pf) == 1); break; // ret pe
  case 0xF0: cyc += 5; cyc += cond_ret(z, flag_get(z, sf) == 0); break; // ret p
  case 0xF8: cyc += 5; cyc += cond_ret(z, flag_get(z, sf) == 1); break; // ret m

  case 0xC7: cyc += 11; call(z, 0x00); break; // rst 0
  case 0xCF: cyc += 11; call(z, 0x08); break; // rst 1
  case 0xD7: cyc += 11; call(z, 0x10); break; // rst 2
  case 0xDF: cyc += 11; call(z, 0x18); break; // rst 3
  case 0xE7: cyc += 11; call(z, 0x20); break; // rst 4
  case 0xEF: cyc += 11; call(z, 0x28); break; // rst 5
  case 0xF7: cyc += 11; call(z, 0x30); break; // rst 6
  case 0xFF: cyc += 11; call(z, 0x38); break; // rst 7

  case 0xC5: cyc += 11; pushw(z, z->bc); break; // push bc
  case 0xD5: cyc += 11; pushw(z, z->de); break; // push de
  case 0xE5: cyc += 11; pushw(z, z->hl); break; // push hl
  case 0xF5: cyc += 11; pushw(z, z->af); break; // push af

  case 0xC1: cyc += 10; z->bc = popw(z); break; // pop bc
  case 0xD1: cyc += 10; z->de = popw(z); break; // pop de
  case 0xE1: cyc += 10; z->hl = popw(z); break; // pop hl
  case 0xF1: cyc += 10; z->af = popw(z); break; // pop af

  case 0xDB: {
    cyc += 11;
    const uint16_t port = nextb(z) | (z->a << 8);
    z->a = z->port_in(z, port);
    z->mem_ptr = port + 1;
  } break; // in a,(n)

  case 0xD3: {
    cyc += 11;
    const uint16_t port = nextb(z) | (z->a << 8);
    z->port_out(z, port, z->a);
    z->mem_ptr = ((port + 1) & 0xff) | (z->a << 8);
  } break; // out (n), a

  case 0x08: {
    cyc += 4;
    uint16_t af = z->af;
    z->af = z->a_f_;
    z->a_f_ = af;
  } break; // ex af,af'
  case 0xD9: {
    cyc += 4;
    uint16_t bc = z->bc, de = z->de, hl = z->hl;

    z->bc = z->b_c_;
    z->de = z->d_e_;
    z->hl = z->h_l_;

    z->b_c_ = bc;
    z->d_e_ = de;
    z->h_l_ = hl;
  } break; // exx

  case 0xCB: cyc += 0; cyc += exec_opcode_cb(z, nextb(z)); break;
  case 0xED: cyc += 0; cyc += exec_opcode_ed(z, nextb(z)); break;
  case 0xDD: cyc += 0; cyc += exec_opcode_ddfd(z, nextb(z), &z->ix); break;
  case 0xFD: cyc += 0; cyc += exec_opcode_ddfd(z, nextb(z), &z->iy); break;

  default: break; // fprintf(stderr, "unknown opcode %02X\n", opcode); break;
  }
  return cyc;
}

// executes a DD/FD opcode (IZ = IX or IY)
static unsigned exec_opcode_ddfd(z80* const z, uint8_t opcode, uint16_t* const iz) {
  unsigned cyc = 0;
  inc_r(z);

#define IZD displace(z, *iz, nextb(z))
#define IZH (*iz >> 8)
#define IZL (*iz & 0xFF)

  switch (opcode) {
  case 0xE1: cyc += 14; *iz = popw(z); break; // pop iz
  case 0xE5: cyc += 15; pushw(z, *iz); break; // push iz

  case 0xE9: cyc += 8; jump(z, *iz); break; // jp iz

  case 0x09: cyc += 15; addiz(z, iz, z->bc); break; // add iz,bc
  case 0x19: cyc += 15; addiz(z, iz, z->de); break; // add iz,de
  case 0x29: cyc += 15; addiz(z, iz, *iz); break; // add iz,iz
  case 0x39: cyc += 15; addiz(z, iz, z->sp); break; // add iz,sp

  case 0x84: cyc += 8; z->a = addb(z, z->a, IZH, 0); break; // add a,izh
  case 0x85: cyc += 8; z->a = addb(z, z->a, *iz & 0xFF, 0); break; // add a,izl
  case 0x8C: cyc += 8; z->a = addb(z, z->a, IZH, flag_get(z, cf)); break; // adc a,izh
  case 0x8D: cyc += 8; z->a = addb(z, z->a, *iz & 0xFF, flag_get(z, cf)); break; // adc a,izl

  case 0x86: cyc += 19; z->a = addb(z, z->a, rb(z, IZD), 0); break; // add a,(iz+*)
  case 0x8E: cyc += 19; z->a = addb(z, z->a, rb(z, IZD), flag_get(z, cf)); break; // adc a,(iz+*)
  case 0x96: cyc += 19; z->a = subb(z, z->a, rb(z, IZD), 0); break; // sub (iz+*)
  case 0x9E: cyc += 19; z->a = subb(z, z->a, rb(z, IZD), flag_get(z, cf)); break; // sbc (iz+*)

  case 0x94: cyc += 8; z->a = subb(z, z->a, IZH, 0); break; // sub izh
  case 0x95: cyc += 8; z->a = subb(z, z->a, *iz & 0xFF, 0); break; // sub izl
  case 0x9C: cyc += 8; z->a = subb(z, z->a, IZH, flag_get(z, cf)); break; // sbc izh
  case 0x9D: cyc += 8; z->a = subb(z, z->a, *iz & 0xFF, flag_get(z, cf)); break; // sbc izl

  case 0xA6: cyc += 19; land(z, rb(z, IZD)); break; // and (iz+*)
  case 0xA4: cyc += 8; land(z, IZH); break; // and izh
  case 0xA5: cyc += 8; land(z, *iz & 0xFF); break; // and izl

  case 0xAE: cyc += 19; lxor(z, rb(z, IZD)); break; // xor (iz+*)
  case 0xAC: cyc += 8; lxor(z, IZH); break; // xor izh
  case 0xAD: cyc += 8; lxor(z, *iz & 0xFF); break; // xor izl

  case 0xB6: cyc += 19; lor(z, rb(z, IZD)); break; // or (iz+*)
  case 0xB4: cyc += 8; lor(z, IZH); break; // or izh
  case 0xB5: cyc += 8; lor(z, *iz & 0xFF); break; // or izl

  case 0xBE: cyc += 19; cp(z, rb(z, IZD)); break; // cp (iz+*)
  case 0xBC: cyc += 8; cp(z, IZH); break; // cp izh
  case 0xBD: cyc += 8; cp(z, *iz & 0xFF); break; // cp izl

  case 0x23: cyc += 10; *iz += 1; break; // inc iz
  case 0x2B: cyc += 10; *iz -= 1; break; // dec iz

  case 0x34: {
    cyc += 23;
    uint16_t addr = IZD;
    wb(z, addr, inc(z, rb(z, addr)));
  } break; // inc (iz+*)

  case 0x35: {
    cyc += 23;
    uint16_t addr = IZD;
    wb(z, addr, dec(z, rb(z, addr)));
  } break; // dec (iz+*)

  case 0x24: cyc += 8; *iz = IZL | ((inc(z, IZH)) << 8); break; // inc izh
  case 0x25: cyc += 8; *iz = IZL | ((dec(z, IZH)) << 8); break; // dec izh
  case 0x2C: cyc += 8; *iz = (IZH << 8) | inc(z, IZL); break; // inc izl
  case 0x2D: cyc += 8; *iz = (IZH << 8) | dec(z, IZL); break; // dec izl

  case 0x2A: cyc += 20; *iz = rw(z, nextw(z)); break; // ld iz,(**)
  case 0x22: cyc += 20; ww(z, nextw(z), *iz); break; // ld (**),iz
  case 0x21: cyc += 14; *iz = nextw(z); break; // ld iz,**

  case 0x36: {
    cyc += 19;
    uint16_t addr = IZD;
    wb(z, addr, nextb(z));
  } break; // ld (iz+*),*

  case 0x70: cyc += 19; wb(z, IZD, z->b); break; // ld (iz+*),b
  case 0x71: cyc += 19; wb(z, IZD, z->c); break; // ld (iz+*),c
  case 0x72: cyc += 19; wb(z, IZD, z->d); break; // ld (iz+*),d
  case 0x73: cyc += 19; wb(z, IZD, z->e); break; // ld (iz+*),e
  case 0x74: cyc += 19; wb(z, IZD, z->h); break; // ld (iz+*),h
  case 0x75: cyc += 19; wb(z, IZD, z->l); break; // ld (iz+*),l
  case 0x77: cyc += 19; wb(z, IZD, z->a); break; // ld (iz+*),a

  case 0x46: cyc += 19; z->b = rb(z, IZD); break; // ld b,(iz+*)
  case 0x4E: cyc += 19; z->c = rb(z, IZD); break; // ld c,(iz+*)
  case 0x56: cyc += 19; z->d = rb(z, IZD); break; // ld d,(iz+*)
  case 0x5E: cyc += 19; z->e = rb(z, IZD); break; // ld e,(iz+*)
  case 0x66: cyc += 19; z->h = rb(z, IZD); break; // ld h,(iz+*)
  case 0x6E: cyc += 19; z->l = rb(z, IZD); break; // ld l,(iz+*)
  case 0x7E: cyc += 19; z->a = rb(z, IZD); break; // ld a,(iz+*)

  case 0x44: cyc += 8; z->b = IZH; break; // ld b,izh
  case 0x4C: cyc += 8; z->c = IZH; break; // ld c,izh
  case 0x54: cyc += 8; z->d = IZH; break; // ld d,izh
  case 0x5C: cyc += 8; z->e = IZH; break; // ld e,izh
  case 0x7C: cyc += 8; z->a = IZH; break; // ld a,izh

  case 0x45: cyc += 8; z->b = IZL; break; // ld b,izl
  case 0x4D: cyc += 8; z->c = IZL; break; // ld c,izl
  case 0x55: cyc += 8; z->d = IZL; break; // ld d,izl
  case 0x5D: cyc += 8; z->e = IZL; break; // ld e,izl
  case 0x7D: cyc += 8; z->a = IZL; break; // ld a,izl

  case 0x60: cyc += 8; *iz = IZL | (z->b << 8); break; // ld izh,b
  case 0x61: cyc += 8; *iz = IZL | (z->c << 8); break; // ld izh,c
  case 0x62: cyc += 8; *iz = IZL | (z->d << 8); break; // ld izh,d
  case 0x63: cyc += 8; *iz = IZL | (z->e << 8); break; // ld izh,e
  case 0x64: cyc += 8; break; // ld izh,izh
  case 0x65: cyc += 8; *iz = (IZL << 8) | IZL; break; // ld izh,izl
  case 0x67: cyc += 8; *iz = IZL | (z->a << 8); break; // ld izh,a
  case 0x26: cyc += 11; *iz = IZL | (nextb(z) << 8); break; // ld izh,*

  case 0x68: cyc += 8; *iz = (IZH << 8) | z->b; break; // ld izl,b
  case 0x69: cyc += 8; *iz = (IZH << 8) | z->c; break; // ld izl,c
  case 0x6A: cyc += 8; *iz = (IZH << 8) | z->d; break; // ld izl,d
  case 0x6B: cyc += 8; *iz = (IZH << 8) | z->e; break; // ld izl,e
  case 0x6C: cyc += 8; *iz = (IZH << 8) | IZH; break; // ld izl,izh
  case 0x6D: cyc += 8; break; // ld izl,izl
  case 0x6F: cyc += 8; *iz = (IZH << 8) | z->a; break; // ld izl,a
  case 0x2E: cyc += 11; *iz = (IZH << 8) | nextb(z); break; // ld izl,*

  case 0xF9: cyc += 10; z->sp = *iz; break; // ld sp,iz

  case 0xE3: {
    cyc += 23;
    const uint16_t val = rw(z, z->sp);
    ww(z, z->sp, *iz);
    *iz = val;
    z->mem_ptr = val;
  } break; // ex (sp),iz

  case 0xCB: {
    uint16_t addr = IZD;
    uint8_t op = nextb(z);
    cyc += exec_opcode_dcb(z, op, addr);
  } break;

  default: {
    // any other FD/DD opcode behaves as a non-prefixed opcode:
    cyc += 4 + exec_opcode(z, opcode);
    // R should not be incremented twice:
    z->r = (z->r & 0x80) | ((z->r - 1) & 0x7f);
  } break;
  }

#undef IZD
#undef IZH
#undef IZL
  return cyc;
}

// executes a CB opcode
static unsigned exec_opcode_cb(z80* const z, uint8_t opcode) {
  unsigned cyc = 8;
  inc_r(z);

  // decoding instructions from http://z80.info/decoding.htm#cb
  uint8_t x_ = (opcode >> 6) & 3; // 0b11
  uint8_t y_ = (opcode >> 3) & 7; // 0b111
  uint8_t z_ = opcode & 7; // 0b111

  uint8_t hl = 0;
  uint8_t* reg = 0;
  switch (z_) {
  case 0: reg = &z->b; break;
  case 1: reg = &z->c; break;
  case 2: reg = &z->d; break;
  case 3: reg = &z->e; break;
  case 4: reg = &z->h; break;
  case 5: reg = &z->l; break;
  case 6:
    hl = rb(z, z->hl);
    reg = &hl;
    break;
  case 7: reg = &z->a; break;
  }

  switch (x_) {
  case 0: {
    switch (y_) {
    case 0: *reg = cb_rlc(z, *reg); break;
    case 1: *reg = cb_rrc(z, *reg); break;
    case 2: *reg = cb_rl(z, *reg); break;
    case 3: *reg = cb_rr(z, *reg); break;
    case 4: *reg = cb_sla(z, *reg); break;
    case 5: *reg = cb_sra(z, *reg); break;
    case 6: *reg = cb_sll(z, *reg); break;
    case 7: *reg = cb_srl(z, *reg); break;
    }
  } break; // rot[y] r[z]
  case 1: { // BIT y, r[z]
    cb_bit(z, *reg, y_);

    // in bit (hl), x/y flags are handled differently:
    if (z_ == 6) {
      flag_set(z, yf, GET_BIT(5, z->mem_ptr >> 8));
      flag_set(z, xf, GET_BIT(3, z->mem_ptr >> 8));
      cyc += 4;
    } else {
      flag_set(z, yf, GET_BIT(5, *reg));
      flag_set(z, xf, GET_BIT(3, *reg));
    }
  } break;
  case 2: *reg &= ~(1 << y_); break; // RES y, r[z]
  case 3: *reg |= 1 << y_; break; // SET y, r[z]
  }

  if ((x_ != 1) && (z_ == 6)) { // BIT (HL) not included
    wb(z, z->hl, hl);
    cyc += 7;
  }
  return cyc;
}

// executes a displaced CB opcode (DDCB or FDCB)
static unsigned exec_opcode_dcb(z80* const z, uint8_t opcode, uint16_t addr) {
  unsigned cyc = 0;
  uint8_t val = rb(z, addr);
  uint8_t result = 0;

  // decoding instructions from http://z80.info/decoding.htm#ddcb
  uint8_t x_ = (opcode >> 6) & 3; // 0b11
  uint8_t y_ = (opcode >> 3) & 7; // 0b111
  uint8_t z_ = opcode & 7; // 0b111

  switch (x_) {
  case 0: {
    // rot[y] (iz+d)
    switch (y_) {
    case 0: result = cb_rlc(z, val); break;
    case 1: result = cb_rrc(z, val); break;
    case 2: result = cb_rl(z, val); break;
    case 3: result = cb_rr(z, val); break;
    case 4: result = cb_sla(z, val); break;
    case 5: result = cb_sra(z, val); break;
    case 6: result = cb_sll(z, val); break;
    case 7: result = cb_srl(z, val); break;
    }
  } break;
  case 1: {
    result = cb_bit(z, val, y_);
    flag_set(z, yf, GET_BIT(5, addr >> 8));
    flag_set(z, xf, GET_BIT(3, addr >> 8));
  } break; // bit y,(iz+d)
  case 2: result = val & ~(1 << y_); break; // res y, (iz+d)
  case 3: result = val | (1 << y_); break; // set y, (iz+d)

  default: break;
  //fprintf(stderr, "unknown XYCB opcode: %02X\n", opcode); break;
  }

  // ld r[z], rot[y] (iz+d)
  // ld r[z], res y,(iz+d)
  // ld r[z], set y,(iz+d)
  if (x_ != 1 && z_ != 6) {
    switch (z_) {
    case 0: z->b = result; break;
    case 1: z->c = result; break;
    case 2: z->d = result; break;
    case 3: z->e = result; break;
    case 4: z->h = result; break;
    case 5: z->l = result; break;
    case 6: wb(z, z->hl, result); break;
    case 7: z->a = result; break;
    }
  }

  if (x_ == 1) {
    // bit instructions take 20 cycles, others take 23
    cyc += 20;
  } else {
    wb(z, addr, result);
    cyc += 23;
  }
  return cyc;
}

// executes a ED opcode
static unsigned exec_opcode_ed(z80* const z, uint8_t opcode) {
  unsigned cyc = 0;
  inc_r(z);
  switch (opcode) {
  case 0x47: cyc += 9; z->i = z->a; break; // ld i,a
  case 0x4F: cyc += 9; z->r = z->a; break; // ld r,a

  case 0x57: cyc += 9;
    z->a = z->i;
    flag_set(z, sf, z->a >> 7);
    flag_set(z, zf, z->a == 0);
    flag_set(z, hf, 0);
    flag_set(z, nf, 0);
    flag_set(z, pf, z->iff2);
    break; // ld a,i

  case 0x5F: cyc += 9;
    z->a = z->r;
    flag_set(z, sf, z->a >> 7);
    flag_set(z, zf, z->a == 0);
    flag_set(z, hf, 0);
    flag_set(z, nf, 0);
    flag_set(z, pf, z->iff2);
    break; // ld a,r

  case 0x45:
  case 0x55:
  case 0x5D:
  case 0x65:
  case 0x6D:
  case 0x75:
  case 0x7D:
    cyc += 14;
    z->iff1 = z->iff2;
    ret(z);
    break; // retn
  case 0x4D: cyc += 14; ret(z); break; // reti

  case 0xA0: cyc += 16; ldi(z); break; // ldi
  case 0xB0: {
    cyc += 16;
    ldi(z);

    if (z->bc != 0) {
      z->pc -= 2;
      cyc += 5;
      z->mem_ptr = z->pc + 1;
    }
  } break; // ldir

  case 0xA8: cyc += 16; ldd(z); break; // ldd
  case 0xB8: {
    cyc += 16;
    ldd(z);

    if (z->bc != 0) {
      z->pc -= 2;
      cyc += 5;
      z->mem_ptr = z->pc + 1;
    }
  } break; // lddr

  case 0xA1: cyc += 16; cpi(z); break; // cpi
  case 0xA9: cyc += 16; cpd(z); break; // cpd
  case 0xB1: {
    cyc += 16;
    cpi(z);
    if (z->bc != 0 && !flag_get(z, zf)) {
      z->pc -= 2;
      cyc += 5;
      z->mem_ptr = z->pc + 1;
    } else {
      z->mem_ptr += 1;
    }
  } break; // cpir
  case 0xB9: {
    cyc += 16;
    cpd(z);
    if (z->bc != 0 && !flag_get(z, zf)) {
      z->pc -= 2;
      cyc += 5;
    } else {
      z->mem_ptr += 1;
    }
  } break; // cpdr

  case 0x40: cyc += 12; in_r_c(z, &z->b); break; // in b, (c)
  case 0x48: cyc += 12; in_r_c(z, &z->c); break; // in c, (c)
  case 0x50: cyc += 12; in_r_c(z, &z->d); break; // in d, (c)
  case 0x58: cyc += 12; in_r_c(z, &z->e); break; // in e, (c)
  case 0x60: cyc += 12; in_r_c(z, &z->h); break; // in h, (c)
  case 0x68: cyc += 12; in_r_c(z, &z->l); break; // in l, (c)
  case 0x70: {
    cyc += 12;
    uint8_t val;
    in_r_c(z, &val);
  } break; // in (c)
  case 0x78:
    cyc += 12;
    in_r_c(z, &z->a);
    z->mem_ptr = z->bc + 1;
    break; // in a, (c)

  case 0xA2: cyc += 16; ini(z); break; // ini
  case 0xB2:
    cyc += 16;
    ini(z);
    if (z->b > 0) {
      z->pc -= 2;
      cyc += 5;
      z->mem_ptr = z->pc + 1;
    }
    break; // inir
  case 0xAA: cyc += 16; ind(z); break; // ind
  case 0xBA:
    cyc += 16;
    ind(z);
    if (z->b > 0) {
      z->pc -= 2;
      cyc += 5;
      z->mem_ptr = z->pc + 1;
    }
    break; // indr

  case 0x79: cyc += 12; outc(z, z->a); break; // out (c), a
  case 0x41: cyc += 12; outc(z, z->b); break; // out (c), b
  case 0x49: cyc += 12; outc(z, z->c); break; // out (c), c
  case 0x51: cyc += 12; outc(z, z->d); break; // out (c), d
  case 0x59: cyc += 12; outc(z, z->e); break; // out (c), e
  case 0x61: cyc += 12; outc(z, z->h); break; // out (c), h
  case 0x69: cyc += 12; outc(z, z->l); break; // out (c), l
  case 0x71: cyc += 12; outc(z, 0); break; // out (c), 0

  case 0xA3: cyc += 16; outi(z); break; // outi
  case 0xB3: {
    cyc += 16;
    outi(z);
    if (z->b > 0) {
      z->pc -= 2;
      cyc += 5;
      z->mem_ptr = z->pc + 1;
    }
  } break; // otir
  case 0xAB: cyc += 16; outd(z); break; // outd
  case 0xBB: {
    cyc += 16;
    outd(z);
    if (z->b > 0) {
      z->pc -= 2;
      cyc += 5;
      z->mem_ptr = z->pc + 1;
    }
  } break; // otdr

  case 0x42: cyc += 15; sbchl(z, z->bc); break; // sbc hl,bc
  case 0x52: cyc += 15; sbchl(z, z->de); break; // sbc hl,de
  case 0x62: cyc += 15; sbchl(z, z->hl); break; // sbc hl,hl
  case 0x72: cyc += 15; sbchl(z, z->sp); break; // sbc hl,sp

  case 0x4A: cyc += 15; adchl(z, z->bc); break; // adc hl,bc
  case 0x5A: cyc += 15; adchl(z, z->de); break; // adc hl,de
  case 0x6A: cyc += 15; adchl(z, z->hl); break; // adc hl,hl
  case 0x7A: cyc += 15; adchl(z, z->sp); break; // adc hl,sp

  case 0x43: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    ww(z, addr, z->bc);
    z->mem_ptr = addr + 1;
  } break; // ld (**), bc

  case 0x53: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    ww(z, addr, z->de);
    z->mem_ptr = addr + 1;
  } break; // ld (**), de

  case 0x63: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    ww(z, addr, z->hl);
    z->mem_ptr = addr + 1;
  } break; // ld (**), hl

  case 0x73: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    ww(z, addr, z->sp);
    z->mem_ptr = addr + 1;
  } break; // ld (**),sp

  case 0x4B: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    z->bc = rw(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld bc, (**)

  case 0x5B: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    z->de = rw(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld de, (**)

  case 0x6B: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    z->hl = rw(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld hl, (**)

  case 0x7B: {
    cyc += 20;
    const uint16_t addr = nextw(z);
    z->sp = rw(z, addr);
    z->mem_ptr = addr + 1;
  } break; // ld sp,(**)

  case 0x44:
  case 0x54:
  case 0x64:
  case 0x74:
  case 0x4C:
  case 0x5C:
  case 0x6C:
  case 0x7C: cyc += 8; z->a = subb(z, 0, z->a, 0); break; // neg

  case 0x46:
  case 0x66: cyc += 8; z->interrupt_mode = 0; break; // im 0
  case 0x56:
  case 0x76: cyc += 8; z->interrupt_mode = 1; break; // im 1
  case 0x5E:
  case 0x7E: cyc += 8; z->interrupt_mode = 2; break; // im 2

  case 0x67: {
    cyc += 18;
    uint8_t a = z->a;
    uint8_t val = rb(z, z->hl);
    z->a = (a & 0xF0) | (val & 0xF);
    wb(z, z->hl, (val >> 4) | (a << 4));
    z->f = f_szpxy[z->a] |
      flag_val(cf, flag_get(z, cf)) | /* cf unmodified */
      flag_val(nf, 0) |
      flag_val(hf, 0);
    z->mem_ptr = z->hl + 1;
  } break; // rrd

  case 0x6F: {
    cyc += 18;
    uint8_t a = z->a;
    uint8_t val = rb(z, z->hl);
    z->a = (a & 0xF0) | (val >> 4);
    wb(z, z->hl, (val << 4) | (a & 0xF));

    z->f = f_szpxy[z->a] |
      flag_val(cf, flag_get(z, cf)) | /* cf unmodified */
      flag_val(nf, 0) |
      flag_val(hf, 0);
    z->mem_ptr = z->hl + 1;
  } break; // rld

  default: break;
  //fprintf(stderr, "unknown ED opcode: %02X\n", opcode); break;
  }
  return cyc;
}

#undef GET_BIT
