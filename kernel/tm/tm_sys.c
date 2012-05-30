#include <kernel.h>
#include <memory.h>
#include <task.h>

/* Low-level memory allocator implementation */
int sys_sbrk(int inc)
{
	assert(current_task);
	if(0 && inc < 0 && current_task->heap_start < current_task->heap_end) {
		int dec = -inc;
		unsigned del_pages=0;
		unsigned new_end = current_task->heap_end - dec;
		if(new_end < current_task->heap_start)
			new_end = current_task->heap_start;
		unsigned old_end = current_task->heap_end;
		unsigned free_start = (new_end&PAGE_MASK) + PAGE_SIZE;
		unsigned free_end = old_end&PAGE_MASK;
		int x=0;
		while(free_start <= free_end)
			vm_unmap(free_start), free_start += 0x1000, x++;
		current_task->heap_end = new_end;
		//printk(0, "[sbrk]: reclaiming memory %d. oldend=%x, newend=%x. start=%x. freed %d\n", dec, old_end, new_end, current_task->heap_start, x);
		return old_end;
	}
	if(!inc)
		return current_task->heap_end;
	unsigned end = current_task->heap_end;
	assert(end);
	if(end + inc >= TOP_TASK_MEM)
		send_signal(current_task->pid, SIGSEGV);
	current_task->heap_end += inc;
	current_task->he_red = end + inc;
	return end;
}

int sys_isstate(int pid, int state)
{
	task_t *task = get_task_pid(pid);
	if(!task) return -ESRCH;
	return (task->state == state) ? 1 : 0;
}

int sys_gsetpriority(int set, int which, int id, int val)
{
	if(set)
		return -ENOSYS;
	return current_task->priority;
}

int sys_nice(int which, int who, int val, int flags)
{
	if(!flags || which == PRIO_PROCESS)
	{
		if(who && (unsigned)who != current_task->pid)
			return -ENOTSUP;
		if(!flags && val < 0 && !ISGOD(current_task))
			return -EPERM;
		/* Yes, this is correct */
		if(!flags)
			current_task->priority += -val;
		else
			current_task->priority = (-val)+1;
		return 0;
	}
	val=-val;
	val++; /* our default is 1, POSIX default is 0 */
	task_t *t = (task_t *)kernel_task;
	int c=0;
	for(;t;t=t->next)
	{
		if(which == PRIO_USER && (t->uid == who || t->_uid == who))
			t->priority = val, c++;
	}
	return c ? 0 : -ESRCH;
}

int sys_setsid(int ex, int cmd)
{
	if(cmd) {
		return -ENOTSUP;
	}
	if(!ex) {
	if(current_task->exe)
		iput(current_task->exe);
		current_task->exe=0;
	}
	current_task->tty=0;
	return 0;
}

int sys_setpgid(int a, int b)
{
	return -ENOSYS;
}

int get_pid()
{
	return current_task->pid;
}

int sys_getppid()
{
	return current_task->parent->pid;
}

int set_gid(int new)
{
	current_task->_gid = current_task->gid;
	current_task->gid = new;
	return 0;
}

int set_uid(int new)
{
	current_task->_uid = current_task->uid;
	current_task->uid = new;
	return 0;
}

int get_gid()
{
	return current_task->gid;
}

int get_uid()
{
	return current_task->uid;
}

int task_pstat(unsigned int pid, struct task_stat *s)
{
	task_t *t=get_task_pid(pid);
	if(!t || !s)
		return -EINVAL;
	s->stime = t->stime;
	s->utime = t->utime;
	s->waitflag = (unsigned *)t->waitflag;
	s->state = t->state;
	s->uid = t->uid;
	s->gid = t->gid;
	s->system = t->system;
	s->ppid = t->parent->pid;
	s->tty = t->tty;
	s->argv = t->argv;
	s->exe = (struct inode *)t->exe->name;
	s->pid = t->pid;
	if(s->exe) strcpy(s->exe_name, s->exe->name);
	s->mem_usage = get_task_mem_usage(t) * 0x1000;
	return 0;
}

int task_stat(unsigned int num, struct task_stat *s)
{
	if(!s) return -EINVAL;
	lock_scheduler();
	task_t *t=(task_t *)kernel_task;
	int i=num;
	while(t && num--)
		t = t->next;
	unlock_scheduler();
	if(!t) 
		return -1;
	s->stime = t->stime;
	s->utime = t->utime;
	s->waitflag = (unsigned *)t->waitflag;
	s->state = t->state;
	s->uid = t->uid;
	s->gid = t->gid;
	s->system = t->system;
	s->ppid = t->parent->pid;
	s->tty = t->tty;
	s->argv = t->argv;
	s->exe = (struct inode *)t->exe->name;
	s->pid = t->pid;
	if(s->exe) strcpy(s->exe_name, s->exe->name);
	s->mem_usage = get_task_mem_usage(t) * 0x1000;
	return 0;
}

