#ifndef BITOPS_X86_H
#define BITOPS_X86_H

#define ADDR (*(volatile long *) addr)
#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)
#endif
