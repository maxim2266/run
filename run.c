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

// attributes
#define NORETURN	static __attribute__((noinline,noreturn))

// signal name mapper
static
const char* sig2str(const int sig) {
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

// state variables
static
int cmd_exit_code = EXIT_SUCCESS,
	has_tty = 0;

static
pid_t cmd_pid = 0;

// get `pid` of any sub-process that has changed its status
static
pid_t next_proc(int* const status) { //-> pid, or 0 when scan is complete
	pid_t pid;

	while((pid = waitpid(-1, status, WNOHANG)) == -1) {
		switch(errno) {
			case EINTR:
				continue;
			case EAGAIN:
				return 0;
			case ECHILD:
                // restore terminal before exit
                if(has_tty && tcsetpgrp(STDIN_FILENO, getpgrp()))
					log_warn_errno("failed to restore terminal foreground process group");

				// no more children left; terminate
				log_info("exit code %d", cmd_exit_code);
				exit(cmd_exit_code);
			default:
				die_errno("wait on process completion failed");
		}
	}

	return pid;
}

// exit code extractor
static
void on_proc_exit(const pid_t pid, const int code) {
	// exit code from the main command
	if(pid == cmd_pid && cmd_exit_code == EXIT_SUCCESS)
		cmd_exit_code = code;
}

// scan all children and handle their status changes
static
void scan_children(void) {
	pid_t pid;
	int status;

	while((pid = next_proc(&status)) > 0) {
		if(WIFEXITED(status)) {
			const int code = WEXITSTATUS(status);

			if(code == EXIT_SUCCESS)
				log_info("pid %jd: completed", (intmax_t)pid);
			else
				log_warn("pid %jd: failed with code %d", (intmax_t)pid, code);

			on_proc_exit(pid, code);

		} else if(WIFSIGNALED(status)) {
			const int sig = WTERMSIG(status);

			log_warn("pid %jd: killed by %s(%d)", (intmax_t)pid, sig2str(sig), sig);
			on_proc_exit(pid, 128 + sig);
		}
	}
}

// exec the given command
NORETURN
void do_exec(char** const cmd, sigset_t* const sig_set) {
	// always create new process group
	setpgid(0, 0);

	// restore signal mask
	sigprocmask(SIG_SETMASK, sig_set, NULL);

	// exec the command
	execvp(cmd[0], cmd);

	// `exec` failed
	const int code = errno;

	log_err_errno("failed to exec `%s`", cmd[0]);
	fflush(NULL);

	// exit code, see https://tldp.org/LDP/abs/html/exitcodes.html#EXITCODESREF
	switch(code) {
		case EACCES:
			_exit(126);
		case ENOENT:
			_exit(127);
		default:
			_exit(EXIT_FAILURE);
	}
}

// spawn a new process
static
pid_t spawn(char** const cmd, sigset_t* const sig_set) {
	// tty
	has_tty = isatty(STDIN_FILENO);

	// flush STDERR, as it may be buffered
	if(fflush(stderr) != 0)
		exit(125);	// because here STDERR is dead

	// fork
	const pid_t pid = fork();

	if(pid < 0)
		die_errno("failed to start process `%s`", cmd[0]);

	// child process
	if(pid == 0)
		do_exec(cmd, sig_set);	// never returns

	// parent process: always set process group
	setpgid(pid, pid);

    // give terminal (if any) to child
    if(has_tty) {
        if(tcsetpgrp(STDIN_FILENO, pid))
			log_warn_errno("failed to set terminal foreground process group");
	} else {
		fclose(stdout);
		fclose(stdin);
	}

	log_info("pid %jd: command `%s`", (intmax_t)pid, cmd[0]);
	return pid;
}

// signal forwarding
static
void forward_signal(const int sig) {
	if(kill(-cmd_pid, sig) == 0)
		log_info("signal %s(%d) forwarded to group %jd",
				 sig2str(sig), sig, (intmax_t)cmd_pid);
	else
		log_warn_errno("signal %s(%d) could not be forwarded to group %jd",
					   sig2str(sig), sig, (intmax_t)cmd_pid);
}

// start the command and wait on it to complete
NORETURN
void run(char** const cmd) {
	// become a subreaper
	if(getpid() != 1 && prctl(PR_SET_CHILD_SUBREAPER, 1L) != 0)
		die_errno("failed to become a subreaper");

	// mask all signals
	sigset_t sig_set, old_set;

	sigfillset(&sig_set);
	sigprocmask(SIG_SETMASK, &sig_set, &old_set);

	// start the process
	cmd_pid = spawn(cmd, &old_set);

	// wait on signals
	int sig;

	while(sigwait(&sig_set, &sig) == 0) {
		switch(sig) {
			case SIGCHLD:
				scan_children();
				break;
			case SIGPIPE:
				log_warn("SIGPIPE (ignored)");
				break;
			default:
				forward_signal(sig);
				break;
		}
	}

	die_errno("signal wait failed");
}

// usage string
static
const char usage_string[] =
"Usage:\n"
"  run [-q] cmd [args...]\n"
"  run [-hv]\n"
"\n"
"Start `cmd`, then wait for it and all its descendants to complete.\n"
"\n"
"Options:\n"
"  -q  Reduce logging level (may be given more than once).\n"
"  -h  Show this help and exit.\n"
"  -v  Show version and exit.\n";

// usage string display
NORETURN
void usage_exit(void) {
	fwrite(usage_string, sizeof(usage_string) - 1, 1, stderr);
	exit(EXIT_FAILURE);
}

// main
int main(int argc, char** argv) {
	if(argc == 1)
		usage_exit();

	int c;

	while((c = getopt(argc, argv, "+:qhv")) != -1) {
		switch(c) {
			case 'q':
				++log_level;
				break;

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
