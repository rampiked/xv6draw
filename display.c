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

#define TEXT_MEM_SIZE (80 * 25 * 2)
#define CRTPORT 0x3d4

static uchar saved_text_mem[TEXT_MEM_SIZE];
static int text_saved = 0;
static int saved_cursor_pos = 0;

static int
get_cursor_pos(void)
{
  int pos;

  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;

  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  return pos;
}

static void
set_cursor_pos(int pos)
{
  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);

  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
}

int
displayioctl(struct file *f, int param, int value)
{
  char *textmem = (char*)P2V(0xB8000);

  if(param == 1){
    if(value == 0x13){

      if(!text_saved){
        memmove(saved_text_mem, textmem, TEXT_MEM_SIZE);
        saved_cursor_pos = get_cursor_pos();
        text_saved = 1;
      }

      vgaMode13();
      return 0;

    } else if(value == 0x3){

      vgaMode3();

      if(text_saved){
        memmove(textmem, saved_text_mem, TEXT_MEM_SIZE);
        set_cursor_pos(saved_cursor_pos);
        text_saved = 0;
      }

      return 0;
    }

  } else if(param == 2){
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
