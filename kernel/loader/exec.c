/* Contains functions for exec'ing files */
#include <kernel.h>
#include <task.h>
#include <memory.h>
#include <fs.h>
#include <dev.h>
#include <mod.h>
#include <init.h>
#include <sys/fcntl.h>
#include <cpu.h>
#include <elf.h>

/* Prepares a process to recieve a new executable. Desc is the descriptor of 
 * the executable. We keep it open through here so that we dont have to 
 * re-open it. */
void arch_specific_exec_initializer(task_t *t, unsigned argc, addr_t eip);
int process_elf_other(char *mem, int fp, addr_t *start, addr_t *end);
task_t *preexec(task_t *t, int desc)
{
	if(t->magic != TASK_MAGIC)
		panic(0, "Invalid task in exec (%d)", t->pid);
	clear_resources(t);
	free_thread_shared_directory();
	t->sigd=0;
	memset((void *)t->thread->signal_act, 0, sizeof(struct sigaction) * 128);
	return 0;
}

void free_dp(char **mem, int num)
{
	/* an error occured and free need to kfree some things */
	int i;
	for(i=0;i<num;i++)
		kfree(mem[i]);
	kfree(mem);
}

int do_exec(task_t *t, char *path, char **argv, char **env)
{
	unsigned int i=0;
	addr_t end, eip;
	unsigned int argc=0, envc=0;
	int desc;
	char **backup_argv=0, **backup_env=0;
	/* Sanity */
	if(!t) panic(PANIC_NOSYNC, "Tried to execute with empty task");
	if(t == kernel_task) panic(0, "Kernel is being executed at the gallows!");
	if(t != current_task)
		panic(0, "I don't know, was really drunk at the time");
	if(t->magic != TASK_MAGIC)
		panic(0, "Invalid task in exec (%d)", t->pid);
	if(!path || !*path)
		return -EINVAL;
	/* Load the file, and make sure that it is valid and accessable */
	if(EXEC_LOG == 2) 
		printk(0, "[%d]: Checking executable file (%s)\n", t->pid, path);
	struct file *efil;
	int err_open, num;
	efil=d_sys_open(path, O_RDONLY, 0, &err_open, &num);
	if(efil)
		desc = num;
	else
		desc = err_open;
	if(desc < 0 || !efil)
		return -ENOENT;
	if(!permissions(efil->inode, MAY_EXEC))
	{
		sys_close(desc);
		return -EACCES;
	}
	/* Detirmine if the file is a valid ELF */
	int header_size = 0;
#if CONFIG_ARCH == TYPE_ARCH_X86_64
	header_size = sizeof(elf64_header_t);
#elif CONFIG_ARCH == TYPE_ARCH_X86
	header_size = sizeof(elf32_header_t);
#endif
	char mem[header_size];
	read_data(desc, mem, 0, header_size);
	int other_bitsize=0;
#if CONFIG_ARCH == TYPE_ARCH_X86_64
	if(is_valid_elf32_otherarch(mem, 2))
		other_bitsize = 1;
#endif
	if(!is_valid_elf(mem, 2) && !other_bitsize) {
		sys_close(desc);
		return -ENOEXEC;
	}
	
	if(EXEC_LOG == 2) 
		printk(0, "[%d]: Copy data\n", t->pid);
	/* okay, lets back up argv and env so that we can
	 * clear out the address space and not lose data..*/
	if(__is_valid_user_ptr(SYS_EXECVE, argv, 0)) {
		while(__is_valid_user_ptr(SYS_EXECVE, argv[argc], 0) && *argv[argc]) argc++;
		backup_argv = (char **)kmalloc(sizeof(addr_t) * argc);
		for(i=0;i<argc;i++) {
			backup_argv[i] = (char *)kmalloc(strlen(argv[i]) + 1);
			_strcpy(backup_argv[i], argv[i]);
		}
	}
	if(__is_valid_user_ptr(SYS_EXECVE, env, 0)) {
		while(__is_valid_user_ptr(SYS_EXECVE, env[envc], 0) && *env[envc]) envc++;
		backup_env = (char **)kmalloc(sizeof(addr_t) * envc);
		for(i=0;i<envc;i++) {
			backup_env[i] = (char *)kmalloc(strlen(env[i]) + 1);
			_strcpy(backup_env[i], env[i]);
		}
	}
	/* and the path too! */
	char *path_backup = (char *)kmalloc(strlen(path) + 1);
	_strcpy((char *)path_backup, path);
	path = path_backup;
	
	if(pd_cur_data->count > 1)
		printk(0, "[exec]: Not sure what to do here...\n");
	/* Preexec - This is the point of no return. Here we close out unneeded 
	 * file descs, free up the page directory and clear up the resources 
	 * of the task */
	if(EXEC_LOG)
		printk(0, "Executing (task %d, cpu %d, tty %d, cwd=%s): %s\n", t->pid, ((cpu_t *)t->cpu)->apicid, t->tty, current_task->thread->pwd->name, path);
	preexec(t, desc);
	strncpy((char *)t->command, path, 128);
	if(other_bitsize)
	{
#if CONFIG_ARCH == TYPE_ARCH_X86_64
		if(!process_elf_other(mem, desc, &eip, &end))
			eip=0;
#endif
	} else if(!process_elf(mem, desc, &eip, &end))
		eip=0;
	sys_close(desc);
	if(!eip) {
		printk(5, "[exec]: Tried to execute an invalid ELF file!\n");
		free_dp(backup_argv, argc);
		free_dp(backup_env, envc);
#if DEBUG
		panic(0, "");
#endif
		exit(0);
	}
	
	if(EXEC_LOG == 2) 
		printk(0, "[%d]: Updating task values\n", t->pid);
	/* Setup the task with the proper values (libc malloc stack) */
	addr_t end_l = end;
	end = (end&PAGE_MASK);
	user_map_if_not_mapped_noclear(end);
	/* now we need to copy back the args and env into userspace
	 * writeable memory...yippie. */
	addr_t args_start = end + PAGE_SIZE;
	addr_t env_start = args_start;
	addr_t alen = 0;
	if(backup_argv) {
		for(i=0;i<(sizeof(addr_t) * (argc+1))/PAGE_SIZE + 2;i++)
			user_map_if_not_mapped_noclear(args_start + i * PAGE_SIZE);
		memcpy((void *)args_start, backup_argv, sizeof(addr_t) * argc);
		alen += sizeof(addr_t) * argc;
		*(addr_t *)(args_start + alen) = 0; /* set last argument value to zero */
		alen += sizeof(addr_t);
		argv = (char **)args_start;
		for(i=0;i<argc;i++)
		{
			char *old = argv[i];
			char *new = (char *)(args_start+alen);
			user_map_if_not_mapped_noclear((addr_t)new);
			unsigned len = strlen(old) + 4;
			user_map_if_not_mapped_noclear((addr_t)new + len + 1);
			argv[i] = new;
			_strcpy(new, old);
			kfree(old);
			alen += len;
		}
		kfree(backup_argv);
	}
	env_start = args_start + alen;
	alen = 0;
	if(backup_env) {
		for(i=0;i<(((sizeof(addr_t) * (envc+1))/PAGE_SIZE) + 2);i++)
			user_map_if_not_mapped_noclear(env_start + i * PAGE_SIZE);
		memcpy((void *)env_start, backup_env, sizeof(addr_t) * envc);
		alen += sizeof(addr_t) * envc;
		*(addr_t *)(env_start + alen) = 0; /* set last argument value to zero */
		alen += sizeof(addr_t);
		env = (char **)env_start;
		for(i=0;i<envc;i++)
		{
			char *old = env[i];
			char *new = (char *)(env_start+alen);
			user_map_if_not_mapped_noclear((addr_t)new);
			unsigned len = strlen(old) + 1;
			user_map_if_not_mapped_noclear((addr_t)new + len + 1);
			env[i] = new;
			_strcpy(new, old);
			kfree(old);
			alen += len;
		}
		kfree(backup_env);
	}
	end = (env_start + alen) & PAGE_MASK;
	t->env = env;
	t->argv = argv;
	kfree(path);
	
	t->heap_start = t->heap_end = end + PAGE_SIZE;
	if(other_bitsize)
		raise_task_flag(t, TF_OTHERBS);
	user_map_if_not_mapped_noclear(t->heap_start);
	/* Zero the heap and stack */
	memset((void *)end_l, 0, PAGE_SIZE-(end_l%PAGE_SIZE));
	memset((void *)(end+PAGE_SIZE), 0, PAGE_SIZE);
	memset((void *)(STACK_LOCATION - STACK_SIZE), 0, STACK_SIZE);
	/* Release everything */
	if(EXEC_LOG == 2) 
		printk(0, "[%d]: Performing call\n", t->pid);
	
	/* we clear this out, so we don't accidentally handle a signal...*/
	set_int(0);
	lower_task_flag(t, TF_SCHED);
	if(!(kernel_state_flags & KSF_HAVEEXECED))
		set_ksf(KSF_HAVEEXECED);
	arch_specific_exec_initializer(t, argc, eip);
	return 0;
}

int execve(char *path, char **argv, char **env)
{
	return do_exec((task_t *)current_task, path, argv, env);
}
