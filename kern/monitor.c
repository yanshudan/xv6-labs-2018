// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#define CMDBUF_SIZE 80 // enough for one VGA text line

struct Command
{
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char **argv, struct Trapframe *tf);
};

static struct Command commands[] = {
	{"help", "Display this list of commands", mon_help},
	{"kerninfo", "Display information about the kernel", mon_kerninfo},
	{"backtrace", "Show back trace infomation on stack(kern only)", mon_backtrace},
	{"showmap","Show mappings of virtural memory to physical memory", show_mappings}
};

/***** Implementations of basic kernel monitor commands *****/

int mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
			ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	int *ebp = (int *)read_ebp();
	cprintf("Stack backtrace:\n");
	struct Eipdebuginfo info, lastinfo;
	int *last = (int *)&mon_backtrace;
	debuginfo_eip((int)last, &info);
	lastinfo = info;
	while (ebp)
	{
		debuginfo_eip(*(ebp + 1), &info);
		cprintf("  ebp %08x  eip %08x", ebp, *(ebp + 1));
		cprintf("  %.*s(", lastinfo.eip_fn_namelen, lastinfo.eip_fn_name);
		for (int i = 0; i < lastinfo.eip_fn_narg; ++i)
			cprintf("%x, ", *(ebp + 2 + i));
		cprintf("\b\b)\n       %s:%d:", info.eip_file, info.eip_line);
		cprintf("  %.*s+%x\n", info.eip_fn_namelen, info.eip_fn_name, *(ebp + 1) - info.eip_fn_addr);
		lastinfo = info;
		ebp = (int *)(*ebp);
	}
	return 0;
}
uint32_t read_hex(char* input){
	char* pc=input;
	char c=*pc;
	uint32_t ret=0;
	if(c!='0') return ~0;
	c=*++pc;
	if(c!='x') return ~0;
	c=*++pc;
	while(c!='\0'){
		ret<<=4;
		if(c>='0' && c<='9') ret+=c-'0';
		else if(c>='a' && c<='f') ret+=c-'a'+10;
		else return ~0;
		c=*++pc;
	}
	return ret;
}
extern pte_t *pgdir_walk(pde_t *pgdir, const void *va, int create);
int show_mappings(int argc, char **argv, struct Trapframe *tf)
{
	if (argc > 3)
	{
		cprintf("too many args!\n");
		return 0;
	}
	uint32_t begin = read_hex(argv[1]) & ~0xFFF;
	uint32_t end = (read_hex(argv[2])) & ~0xFFF;

	cprintf("begin=%x, end=%x\n",(void*)begin,(void*)end);
	//CR3 contains the physical addr,what if we are in user mode?
	pde_t *pgdir = (pde_t *)(rcr3()+KERNBASE);
	cprintf("pgdir=%x\n",pgdir);
	uint32_t left = begin, right = begin;
	uint32_t ibegin = 0, iend=0;
	for (uint32_t i = begin; i <= end; i += PGSIZE)
	{
		pte_t *tmp = pgdir_walk(pgdir, (void *)i, 0);
		if (tmp && *tmp)
		{
			//found page
			if((right+PGSIZE)==((*tmp)&~0xFFF)){
				//continous mapping
				right+=PGSIZE;
				iend=i;
			}else{
				//concatination
				cprintf("va[%05x-%05x]->pa[%05x-%05x]\n", (void *)(ibegin>>12), (void *)(iend >> 12), left>>12, right>>12);
				left=right=(*tmp)&~0xFFF;
				ibegin=iend=i;
			}
		}
		// overflow
		if(i==end) break;
	}
	cprintf("va[%05x-%05x]->pa[%05x-%05x]\n", (void *)(ibegin>>12), (void *)(iend >> 12), left>>12, right>>12);
	return 0;
};
/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1)
	{
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS - 1)
		{
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++)
	{
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("hq@jos> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
