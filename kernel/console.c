/* console.c: Copyright (c) 2010 Daniel Bittman
 * Provides functions for the virtual aspect of terminals. 
 */
#include <kernel.h>
#include <asm/system.h>
#include <mutex.h>
#include <console.h>
extern void update_cursor(int);
struct vterm *curcons=0;
struct vterm *kernel_console, *log_console=0;
extern console_driver_t crtc_drv;
#define VIDEO_MEMORY 0xb8000

/* Simple way to display messages before tty and vsprintf get working */
void console_puts(struct vterm *c, char *s)
{
	while(s && *s && c) {
		c->rend.putch(c, *s);
		if(*(s++) == '\n')
			c->rend.putch(c, '\r');
	}
}

void puts(char *s)
{
	console_puts(kernel_console, s);
}

void destroy_console(struct vterm *con)
{
	if(con == curcons)
		curcons = kernel_console;
	kfree(con->vmem);
	mutex_destroy(&con->wlock);
	mutex_destroy(&con->inlock);
	con->flag=0;
}

void create_console(struct vterm *con)
{
	if(con->flag) return;
	con->term.c_lflag=ECHO | ISIG | ECHONL | ICANON;
	con->term.c_oflag=OPOST | ONLCR;
	con->term.c_iflag=ICRNL;
	mutex_create(&con->wlock, 0);
	mutex_create(&con->inlock, 0);
	ll_create(&con->input_block);
	con->flag=1;
}

void init_console(struct vterm *con, console_driver_t *driver)
{
	driver->init(con);
	con->driver = driver;
	printk(0, "[console]: Initialized console %d (%x:%x): %s\n", con->tty, 
				con, driver->init, driver->name);
}

void switch_console(struct vterm *new)
{
	/* Copy screen to old console */
	struct vterm *old = curcons;
	mutex_acquire(&old->wlock);
	memcpy(curcons->vmem, (char *)curcons->video, 
				curcons->h*curcons->w*curcons->bd);
	curcons->cur_mem = curcons->vmem;
	curcons = new;
	curcons->cur_mem = (char *)curcons->video;
	if(curcons->rend.switch_in)
		curcons->rend.switch_in(curcons);
	memcpy(curcons->cur_mem, curcons->vmem, curcons->w*curcons->h*curcons->bd);
	if(curcons->rend.update_cursor)
		curcons->rend.update_cursor(curcons);
	mutex_release(&old->wlock);
}

void console_init_stage1()
{
	tty_init(&kernel_console);
	create_console(kernel_console);
	kernel_console->vmem=kernel_console->cur_mem
						=kernel_console->video=(char *)VIDEO_MEMORY;
	curcons = kernel_console;
	init_console(kernel_console, &crtc_drv);
	curcons->rend.clear(curcons);
	printk(0, "[console]: Video output ready\n");
}
