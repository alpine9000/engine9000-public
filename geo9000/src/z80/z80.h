#ifndef Z80_Z80_H_
#define Z80_Z80_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#if !defined(__BYTE_ORDER__)
# error need __BYTE_ORDER__ macro
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define Z80REG(A, B) union {struct { uint8_t B, A; }; uint16_t A ## B;}
#else
#define Z80REG(A, B) union {struct { uint8_t A, B; }; uint16_t A ## B;}
#endif

#ifndef Z80_EXPORT
#define Z80_EXPORT
#endif

enum {
  Z80_PULSE = 1,
  Z80_ASSERT = 2
};

typedef struct z80 z80;
struct z80 {
  uint8_t (*read_byte)(void*, uint16_t);
  void (*write_byte)(void*, uint16_t, uint8_t);
  uint8_t (*port_in)(z80*, uint16_t);
  void (*port_out)(z80*, uint16_t, uint8_t);
  void* userdata;

  uint16_t pc, sp, ix, iy; // special purpose registers
  uint16_t mem_ptr; // "wz" register
  Z80REG(a, f);
  Z80REG(b, c);
  Z80REG(d, e);
  Z80REG(h, l);
  Z80REG(a_, f_);
  Z80REG(b_, c_);
  Z80REG(d_, e_);
  Z80REG(h_, l_);
  uint8_t i, r; // interrupt vector, memory refresh
  uint8_t iff_delay;
  uint8_t interrupt_mode;
  uint8_t irq_data;
  uint8_t irq_pending;
  uint8_t nmi_pending;
  bool iff1 : 1, iff2 : 1;
  bool halted : 1;
};

Z80_EXPORT void z80_init(z80* const z);
Z80_EXPORT void z80_reset(z80* const z);
Z80_EXPORT void z80_set_pc(z80* const z, uint16_t pc);
Z80_EXPORT void z80_set_sp(z80* const z, uint16_t sp);
Z80_EXPORT unsigned z80_step(z80* const z); /* return cycles used */
Z80_EXPORT unsigned z80_step_n(z80* const z, unsigned cycles);
Z80_EXPORT void z80_debug_output(z80* const z);
Z80_EXPORT void z80_assert_nmi(z80* const z);
Z80_EXPORT void z80_pulse_nmi(z80* const z);
Z80_EXPORT void z80_clr_nmi(z80* const z);
Z80_EXPORT void z80_assert_irq(z80* const z, uint8_t data);
Z80_EXPORT void z80_pulse_irq(z80* const z, uint8_t data);
Z80_EXPORT void z80_clr_irq(z80* const z);

#endif // Z80_Z80_H_
