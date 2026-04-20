#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "vga.h"

int
displayioctl(struct file *f, int param, int value)
{
  if (param == 1) {
    if (value == 0x13) {
      vgaMode13();
      return 0;
    } else if (value == 0x3) {
      vgaMode3();
      return 0;
    }
  } else if (param == 2) {
    int index = (value >> 24) & 0xff;
    int r = (value >> 16) & 0xff;
    int g = (value >> 8) & 0xff;
    int b = value & 0xff;
    vgaSetPalette(index, r, g, b);
    return 0;
  }
  return -1;
}

int
displaywrite(struct file *f, char *buf, int n)
{
  char *vga_mem = (char*)KERNBASE + 0xa0000;
  memmove(vga_mem + f->off, buf, n);
  f->off += n;
  return n;
}

void
displayinit(void)
{
  devsw[2].write = displaywrite;
  devsw[2].ioctl = displayioctl;
}
