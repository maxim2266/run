#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <err.h>

#ifndef VER
#error "program version constant is not defined"
#endif

// macro argument to string literal
#define XSTR(s)	STR(s)
#define STR(s)	#s

// logging
static
unsigned log_level = 0u;

#define log_info(fmt, ...)	\
do {	\
	if(log_level == 0u)	\
		warnx("[info] " fmt __VA_OPT__(,) __VA_ARGS__);	\
} while(0)

#define log_info_errno(fmt, ...)	\
do {	\
	if(log_level == 0u)	\
		warn("[info] " fmt __VA_OPT__(,) __VA_ARGS__);	\
} while(0)

#define log_warn(fmt, ...)	\
do {	\
	if(log_level <= 1u)	\
		warnx("[warn] " fmt __VA_OPT__(,) __VA_ARGS__);	\
} while(0)

#define log_warn_errno(fmt, ...)	\
do {	\
	if(log_level <= 1u)	\
		warn("[warn] " fmt __VA_OPT__(,) __VA_ARGS__);	\
} while(0)

#define log_err(fmt, ...)		 warnx("[error] " fmt __VA_OPT__(,) __VA_ARGS__)
#define log_err_errno(fmt, ...)	 warn("[error] " fmt __VA_OPT__(,) __VA_ARGS__)

// termination
#define die(fmt, ...)		errx(EXIT_FAILURE, "[error] " fmt __VA_OPT__(,) __VA_ARGS__)
#define die_errno(fmt, ...)	err(EXIT_FAILURE, "[error] " fmt __VA_OPT__(,) __VA_ARGS__)

// signal name mapper
static
const char* sig2str(const int sig)
{
	// https://man7.org/linux/man-pages/man7/signal.7.html
	switch(sig) {
		case SIGABRT:	return "SIGABRT";
		case SIGALRM:	return "SIGALRM";
		case SIGBUS:	return "SIGBUS";
		case SIGCHLD:	return "SIGCHLD";
		case SIGCONT:	return "SIGCONT";
		case SIGFPE:	return "SIGFPE";
		case SIGHUP:	return "SIGHUP";
		case SIGILL:	return "SIGILL";
		case SIGINT:	return "SIGINT";
		case SIGKILL:	return "SIGKILL";
		case SIGPIPE:	return "SIGPIPE";
		case SIGPOLL:	return "SIGPOLL";
		case SIGPROF:	return "SIGPROF";
		case SIGPWR:	return "SIGPWR";
		case SIGQUIT:	return "SIGQUIT";
		case SIGSEGV:	return "SIGSEGV";
		case SIGSTKFLT:	return "SIGSTKFLT";
		case SIGSTOP:	return "SIGSTOP";
		case SIGTSTP:	return "SIGTSTP";
		case SIGSYS:	return "SIGSYS";
		case SIGTERM:	return "SIGTERM";
		case SIGTRAP:	return "SIGTRAP";
		case SIGTTIN:	return "SIGTTIN";
		case SIGTTOU:	return "SIGTTOU";
		case SIGURG:	return "SIGURG";
		case SIGUSR1:	return "SIGUSR1";
		case SIGUSR2:	return "SIGUSR2";
		case SIGVTALRM:	return "SIGVTALRM";
		case SIGXCPU:	return "SIGXCPU";
		case SIGXFSZ:	return "SIGXFSZ";
		case SIGWINCH:	return "SIGWINCH";
        default: 		return "<unknown>";
    }
}

// get `pid` of any sub-process that has changed its status
static
pid_t next_proc(int* const status) //-> pid, or -1 when no children left, or 0 when scan is complete
{
	pid_t pid;

	while((pid = waitpid(-1, status, WNOHANG)) == -1) {
		switch(errno) {
			case EINTR:  continue;
			case EAGAIN: return 0;
			case ECHILD: return -1;
			default:
				die_errno("wait on process completion failed");
		}
	}

	return pid;
}

// exit code for this process
static
int exit_code = EXIT_SUCCESS,
	got_tty = 0;

static
void on_cmd_exit(const int code)
{
	if(exit_code == EXIT_SUCCESS)
		exit_code = code;

	// get TTY back
	if(got_tty && tcsetpgrp(STDIN_FILENO, getpgid(0)) != 0)
		log_warn_errno("failed to get back TTY");
}

// process grouping
static
int use_group = 0;

// scan all children and handle their status changes
static
void scan_children(const pid_t cmd_pid)
{
	pid_t pid;
	int status;

	while((pid = next_proc(&status)) > 0) {
		// log process status change
		if(WIFEXITED(status)) {
			const int code = WEXITSTATUS(status);

			if(code == EXIT_SUCCESS)
				log_info("pid %jd: completed", (intmax_t)pid);
			else
				log_warn("pid %jd: failed with code %d", (intmax_t)pid, code);

			if(pid == cmd_pid)
				on_cmd_exit(code);

		} else if(WIFSIGNALED(status)) {
			const int sig = WTERMSIG(status);

			log_warn("pid %jd: killed by %s (%d)", (intmax_t)pid, sig2str(sig), sig);

			if(pid == cmd_pid)
				on_cmd_exit(128 + sig);
		}
	}

	// terminate when no more children left
	if(pid == -1) {
		log_info("exit code %d", exit_code);
		exit(exit_code);
	}
}

// spawn a new process
static
pid_t spawn(char** const cmd, sigset_t* const sig_set)
{
	got_tty = isatty(STDIN_FILENO);

	const pid_t pid = fork();

	if(pid < 0)
		die_errno("failed to start process `%s`", cmd[0]);

	if(pid > 0) {	// parent process
		log_info("started process `%s` (pid %jd)", cmd[0], (intmax_t)pid);
		fclose(stdout);

		return pid;
	}

	// child process

	// new process group
	if(setpgid(0, 0) != 0)
		die_errno("failed to set process group for `%s`", cmd[0]);

	// TTY for the new process group
	if(got_tty && tcsetpgrp(STDIN_FILENO, getpgid(0)) != 0)
		die_errno("failed to acquire TTY for `%s`", cmd[0]);

	// restore signals
	signal(SIGTTIN, SIG_DFL);
	signal(SIGTTOU, SIG_DFL);

	// restore signal mask
	sigprocmask(SIG_SETMASK, sig_set, NULL);

	// exec the command
	execvp(cmd[0], cmd);

	// `exec` failed
	const int err = errno;

	log_err_errno("failed to start `%s`", cmd[0]);

	// exit code, see https://tldp.org/LDP/abs/html/exitcodes.html#EXITCODESREF
	switch(err) {
		case EACCES:
			_exit(126);
		case ENOENT:
			_exit(127);
		default:
			_exit(EXIT_FAILURE);
	}
}

// signal forwarding
static
void forward_signal(const pid_t pid, const int sig)
{
	const char* const entity = use_group ? "group" : "process";

	if(kill(use_group ? -pid : pid, sig) == 0)
		log_info("signal %s (%d) forwarded to %s %jd",
				 sig2str(sig), sig, entity, (intmax_t)pid);
	else
		log_warn_errno("signal %s (%d) could not be forwarded to %s %jd",
					   sig2str(sig), sig, entity, (intmax_t)pid);
}

// start the command and wait on it to complete
static __attribute__((noreturn))
void run(char** const cmd)
{
	// become a subreaper
	if(getpid() != 1 && prctl(PR_SET_CHILD_SUBREAPER, 1L) != 0)
		die_errno("cannot become a subreaper");

	// signals we want to handle
	sigset_t sig_set, old_set;

	sigemptyset(&sig_set);

	sigaddset(&sig_set, SIGCHLD);
	sigaddset(&sig_set, SIGTERM);
	sigaddset(&sig_set, SIGINT);
	sigaddset(&sig_set, SIGHUP);
	sigaddset(&sig_set, SIGQUIT);
	sigaddset(&sig_set, SIGUSR1);
	sigaddset(&sig_set, SIGUSR2);
	sigaddset(&sig_set, SIGPWR);

	sigprocmask(SIG_SETMASK, &sig_set, &old_set);

	// ignore signals
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	// start the process
	const pid_t pid = spawn(cmd, &old_set);

	// wait on signals
	int sig;

	while(sigwait(&sig_set, &sig) == 0) {
		if(sig == SIGCHLD)
			scan_children(pid);
		else
			forward_signal(pid, sig);
	}

	die_errno("signal wait failed");
}

// usage string
static
const char usage_string[] =
"Usage:\n"
"  run [-gq] cmd [args...]\n"
"  run [-hv]\n"
"\n"
"Start `cmd`, then wait for it and all its descendants to complete.\n"
"\n"
"Options:\n"
"  -q  Reduce logging level (may be given more than once).\n"
"  -g  Start `cmd` in its own process group.\n"
"  -h  Show this help and exit.\n"
"  -v  Show version and exit.\n";

// usage string display
static __attribute__((noinline,noreturn))
void usage_exit(void)
{
	fwrite(usage_string, sizeof(usage_string) - 1, 1, stderr);
	exit(EXIT_FAILURE);
}

// this is for testing only
static __attribute__((noinline,noreturn))
void do_test(void)
{
	unsigned seconds = 5, count = 0;

	warnx("### TEST MODE: entering sleep for %u seconds", seconds);

	while((seconds = sleep(seconds)) > 0)
		++count;

	warnx("### TEST MODE: sleep completed (%u interrupts)", count);
	exit(EXIT_SUCCESS);
}

// main
int main(int argc, char** argv)
{
	if(argc == 1)
		usage_exit();

	int c;

	while((c = getopt(argc, argv, "+:qghv!")) != -1) {
		switch(c) {
			case 'q':
				++log_level;
				break;

			case 'g':
				use_group = 1;
				break;

			case '!':
				do_test();

			case 'h':
				usage_exit();

			case 'v':
				fwrite(XSTR(VER) "\n", sizeof(XSTR(VER)), 1, stderr);
				exit(EXIT_FAILURE);

			case '?':
				die("unrecognised option `-%c`", optopt);
		}
	}

	if(optind == argc)
		die("missing command");

	run(&argv[optind]);
}
