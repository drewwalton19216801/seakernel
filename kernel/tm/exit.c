/* Functions for exiting processes, killing processes, and cleaning up resources.
* Copyright (c) 2012 Daniel Bittman
*/
#include <kernel.h>
#include <memory.h>
#include <task.h>
#include <cpu.h>
#include <atomic.h>

extern struct llist *kill_queue;
extern unsigned running_processes;
void clear_resources(task_t *t)
{
	clear_mmfiles(t, (t->flags&TF_EXITING) ? 1 : 0);
}

void set_as_dead(task_t *t)
{
	assert(t);
	sub_atomic(&running_processes, 1);
	t->state = TASK_DEAD;
	tqueue_remove(primary_queue, t->listnode);
	tqueue_remove(((cpu_t *)t->cpu)->active_queue, t->activenode);
	kfree(t->listnode);
	kfree(t->activenode);
	kfree(t->blocknode);
	mutex_destroy((mutex_t *)&t->exlock);
	sub_atomic(&(((cpu_t *)t->cpu)->numtasks), 1);
	t->listnode = ll_insert(kill_queue, (void *)t);
	/* Add to death */
	__engage_idle();
}

int __KT_try_releasing_tasks()
{
	struct llistnode *node = kill_queue->head;
	if(!node) return 0;
	task_t *t = node->entry;
	assert(t && t != current_task);
	if(!(t->flags & TF_BURIED))
		return 1;
	release_task(t);
	ll_remove(kill_queue, node);
	return ll_is_empty(kill_queue) ? 0 : 1;
}

void release_task(task_t *p)
{
	destroy_task_page_directory(p);
	kfree((void *)p->kernel_stack);
	kfree((void *)p);
}

void task_suicide()
{
	exit(-9);
}

void kill_task(unsigned int pid)
{
	if(pid == 0) return;
	task_t *task = get_task_pid(pid);
	if(!task) {
		printk(KERN_WARN, "kill_task recieved invalid PID\n");
		return;
	}
	task->state = TASK_SUICIDAL;
	task->sigd = 0; /* fuck your signals */
	if(task == current_task)
	{
		for(;;) schedule();
	}
}

int get_exit_status(int pid, int *status, int *retval, int *signum, int *__pid)
{
	if(!pid) 
		return -1;
	ex_stat *es;
	int old = set_int(0);
	mutex_acquire((mutex_t *)&current_task->exlock);
	ex_stat *ex = current_task->exlist;
	if(pid != -1) 
		while(ex && ex->pid != (unsigned)pid) ex=ex->next;
	es=ex;
	mutex_release((mutex_t *)&current_task->exlock);
	set_int(old);
	if(es)
	{
		if(__pid) *__pid = es->pid;
		*status |= es->coredump | es->cause;
		*retval = es->ret;
		*signum = es->sig;
		return 0;
	}
	return 1;
}

void add_exit_stat(task_t *t, ex_stat *e)
{
	e->pid = current_task->pid;
	ex_stat *n = (ex_stat *)kmalloc(sizeof(*e));
	memcpy(n, e, sizeof(*e));
	n->prev=0;
	int old_int = set_int(0);
	mutex_acquire((mutex_t *)&t->exlock);
	ex_stat *old = t->exlist;
	if(old) old->prev = n;
	t->exlist = n;
	n->next=old;
	mutex_release((mutex_t *)&t->exlock);
	set_int(old_int);
}
extern unsigned int init_pid;
void exit(int code)
{
	if(!current_task || current_task->pid == 0) 
		panic(PANIC_NOSYNC, "kernel tried to exit");
	task_t *t = (task_t *)current_task;
	/* Get ready to exit */
	raise_flag(TF_EXITING);
	if(code != -9) t->exit_reason.cause = 0;
	t->exit_reason.ret = code;
	if(t->parent && t->parent->pid != init_pid)
		add_exit_stat((task_t *)t->parent, (ex_stat *)&t->exit_reason);
	/* Clear out system resources */
	free_thread_specific_directory();
	clear_resources(t);
	/* this fixes all tasks that are children of current_task, or are waiting
	 * on current_task. For those waiting, it signals the task. For those that
	 * are children, it fixes the 'parent' pointer. */
	search_tqueue(primary_queue, TSEARCH_EXIT_PARENT | TSEARCH_EXIT_WAITING, 0, 0, 0, 0);
	/* tell our parent that we're dead */
	if(t->parent)
		do_send_signal(t->parent->pid, SIGCHILD, 1);
	/* Lock out everything and modify the linked-lists */
	int old = set_int(0);
	mutex_acquire((mutex_t *)&t->exlock);
	ex_stat *ex = t->exlist;
	while(ex) {
		ex_stat *n = ex->next;
		kfree(ex);
		ex=n;
	}
	mutex_release((mutex_t *)&t->exlock);
	set_int(old);
	if(!sub_atomic(&t->thread->count, 1))
	{
		/* we're the last thread to share this data. Clean it up */
		close_all_files(t);
		if(t->thread->root)iput(t->thread->root);
		if(t->thread->pwd) iput(t->thread->pwd);
		mutex_destroy(&t->thread->files_lock);
		kfree(t->thread);
	}
	raise_flag(TF_DYING);
	set_as_dead(t);
	char flag_last_page_dir_task=0;
	mutex_acquire(&pd_cur_data->lock);
	flag_last_page_dir_task = (sub_atomic(&pd_cur_data->count, 1) == 0) ? 1 : 0;
	mutex_release(&pd_cur_data->lock);
	if(flag_last_page_dir_task) {
		/* no one else is referencing this directory. Clean it up... */
		free_thread_shared_directory();
		vm_unmap(PDIR_DATA);
		raise_flag(TF_LAST_PDIR);
	}
	for(;;) schedule();
}
