/*
Copyright (c) 2025 Maxim Konakov
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <err.h>
#include <sys/prctl.h>

// max. number of processes to start
#ifndef RUN_MAX_PROCS
// we are not systemd, and this should be enough for all ;)
#define RUN_MAX_PROCS 32
#endif

#ifndef VER
#error "program version constant is not defined"
#endif

// macro argument to string literal
#define XSTR(s)	STR(s)
#define STR(s)	#s

// logging
static
unsigned log_level = 0u;

#define log_info(fmt, ...) ({	\
	if(log_level == 0u)	\
		warnx("[info] " fmt __VA_OPT__(,) __VA_ARGS__);	\
})

#define log_info_errno(fmt, ...) ({	\
	if(log_level == 0u)	\
		warn("[info] " fmt __VA_OPT__(,) __VA_ARGS__);	\
})

#define log_warn(fmt, ...) ({	\
	if(log_level <= 1u)	\
		warnx("[warn] " fmt __VA_OPT__(,) __VA_ARGS__);	\
})

#define log_warn_errno(fmt, ...) ({	\
	if(log_level <= 1u)	\
		warn("[warn] " fmt __VA_OPT__(,) __VA_ARGS__);	\
})

#define log_err(fmt, ...)		 warnx("[error] " fmt __VA_OPT__(,) __VA_ARGS__)
#define log_err_errno(fmt, ...)	 warn("[error] " fmt __VA_OPT__(,) __VA_ARGS__)

// termination
#define die(fmt, ...)		errx(EXIT_FAILURE, "[error] " fmt __VA_OPT__(,) __VA_ARGS__)
#define die_errno(fmt, ...)	err(EXIT_FAILURE, "[error] " fmt __VA_OPT__(,) __VA_ARGS__)

// attributes
#define NORETURN	static __attribute__((noinline,noreturn))

// quit child process
NORETURN
void child_exit(const int code) {
	fflush(stderr);
	_exit((code == ENOENT) ? 127 : 126);
}

#define die_child(fmt, ...) ({	\
	log_err("pid %jd: " fmt, (intmax_t)getpid() __VA_OPT__(,) __VA_ARGS__);	\
	child_exit(ECANCELED);	\
})

#define die_child_errno(fmt, ...) ({	\
	const int code = errno;	\
	log_err_errno("pid %jd: " fmt, (intmax_t)getpid() __VA_OPT__(,) __VA_ARGS__);	\
	child_exit(code);	\
})

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

// signal set
static
const int blocked_signals[] = {
	SIGALRM,
	SIGCHLD,
	SIGHUP,
	SIGINT,
	SIGQUIT,
	SIGTERM,
	SIGUSR1,
	SIGUSR2
};

// ignore signals, fill mask of blocked signals
static
void setup_signals(sigset_t* const sig_set) {
	// signals to ignore
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);

	// mask for blocked signals
	sigemptyset(sig_set);

	for(unsigned i = 0; i < sizeof(blocked_signals)/sizeof(blocked_signals[0]); ++i)
		if(sigaddset(sig_set, blocked_signals[i]))
			die("signal mask: failed to add signal %d", blocked_signals[i]);
}

// state variables
static
int exit_code = 0,
	term_signal = 0,
	kill_timeout = 0;

// process list
static
pid_t procs[RUN_MAX_PROCS];

static
unsigned num_procs = 0;

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
				// no more children left; terminate
				log_info("exit code %d", exit_code);
				exit(exit_code);

			default:
				die_errno("wait on process completion failed");
		}
	}

	return pid;
}

// signal forwarding
static
void broadcast_signal(const int sig, const char* const prefix) {
	log_info("%s %s(%d)", prefix, sig2str(sig), sig);

	for(unsigned i = 0; i < num_procs; ++i)
		if(kill(procs[i], sig) && errno != ESRCH)
			log_warn_errno("signal %s(%d) could not be sent to process %jd",
						   sig2str(sig), sig, (intmax_t)procs[i]);
}

#define forward_signal(sig)	broadcast_signal((sig), "forwarding")
#define send_signal(sig)	broadcast_signal((sig), "sending")

// scan all children and handle their status changes
static
void scan_children(void) {
	pid_t pid;
	int status, done = 0;

	while((pid = next_proc(&status)) > 0) {
		if(WIFEXITED(status)) {
			status = WEXITSTATUS(status);
			log_info("pid %jd: exited with code %d", (intmax_t)pid, status);

		} else if(WIFSIGNALED(status)) {
			status = WTERMSIG(status);
			log_info("pid %jd: killed by %s(%d)", (intmax_t)pid, sig2str(status), status);
			status += 128;

		} else
			continue;

		// find process
		unsigned i;

		for(i = 0; i < num_procs && procs[i] != pid; ++i);

		if(i < num_procs) {
			// remove process from the list
			if(i < --num_procs)
				procs[i] = procs[num_procs];

			// exit code
			if(exit_code == 0)
				exit_code = status;

			// mark the occasion
			done = 1;
		}
	}

	if(num_procs == 0) {
		// exit and let the daemons die
		log_info("exit code %d (ignoring daemons)", exit_code);
		exit(exit_code);
	}

	// signalling
	if(done && term_signal) {
		send_signal(term_signal);

		if(kill_timeout) {
			alarm(kill_timeout);
			term_signal = 0; // don't send term_signal again and wait for SIGALRM
		}
	}
}

// spawn a new process
static
pid_t spawn(char** const cmd, sigset_t* const sig_set) {
	// fork
	const pid_t pid = fork();

	if(pid < 0)
		die_errno("failed to start process `%s`", cmd[0]);

	if(pid == 0) {
		// child process
		if(setsid() < 0)
			die_child_errno("failed to create new session");

		if(getppid() != 1) {
			const int sig = (term_signal && kill_timeout) ? term_signal : SIGTERM;

			if(prctl(PR_SET_PDEATHSIG, sig) < 0)
				die_child_errno("failed to set parent death signal");
		}

		if(sigprocmask(SIG_SETMASK, sig_set, NULL))
			die_child_errno("failed to reset signal mask");

		signal(SIGPIPE, SIG_DFL);
		execv(cmd[0], cmd);
		die_child_errno("failed to exec `%s`", cmd[0]);
	}

	// parent process
	log_info("pid %jd: command `%s`", (intmax_t)pid, cmd[0]);
	return pid;
}

// command splitting
typedef struct {
	unsigned argc, cap;
	char**	 argv;
} command;

static
void cmd_clear(command* const cmd) {
	for(size_t i = 0; i < cmd->argc; ++i)
		free(cmd->argv[i]);

	free(cmd->argv);
}

#define CMD_CAP 8

static
void cmd_add_string(command* const cmd, const char* const str, const size_t len) {
	if(cmd->argc == cmd->cap) {
		cmd->cap = cmd->cap ? (2 * cmd->cap) : CMD_CAP;
		cmd->argv = realloc(cmd->argv, cmd->cap * sizeof(*cmd->argv));
	}

	if(str)
		(cmd->argv[cmd->argc++] = memcpy(malloc(len + 1), str, len))[len] = 0;
	else
		cmd->argv[cmd->argc] = NULL; // terminating NULL does not count
}

static
const char* cmd_split(command* const cmd, const char* p) {
	if(!p)
		return "empty command";

	for(;;) {
		const char* token;

		p += strspn(p, " \t\r\n");

		switch(*p) {
			case 0:
				// end of string
				cmd_add_string(cmd, NULL, 0);
				return cmd->argc ? NULL : "empty command";

			case '"':
			case '\'':
				// quoted string
				token = ++p;

				if((p = strchr(token, token[-1])) == NULL)
					return "missing closing quote";

				cmd_add_string(cmd, token, p++ - token);
				break;

			default:
				// unquoted word
				p += strcspn((token = p), " \t\r\n");
				cmd_add_string(cmd, token, p - token);
				break;
		}

		// just a reality check
		if(cmd->argc == (CMD_CAP * 32)) {
			cmd_clear(cmd);
			return "too many words";
		}
	}
}

// parse and start all commands
static
void spawn_procs(char** const cmds, const unsigned nproc, sigset_t* const sig_set) {
	// parse commands
	command* const parsed = calloc(nproc, sizeof(command));
	const char* ret;

	for(unsigned i = 0; i < nproc; ++i)
		if((ret = cmd_split(&parsed[i], cmds[i])) != NULL)
			die("command #%u: %s", i + 1, ret);

	// become a subreaper
	if(getpid() != 1 && prctl(PR_SET_CHILD_SUBREAPER, 1L) != 0)
		die_errno("failed to become a subreaper");

	// signals
	sigset_t old_set;

	setup_signals(sig_set);

	if(sigprocmask(SIG_BLOCK, sig_set, &old_set))
		die_errno("failed to set signal mask");

	// start processes
	fflush(stderr);

	for(unsigned i = 0; i < nproc; ++i) {
		procs[num_procs++] = spawn(parsed[i].argv, &old_set);
		cmd_clear(&parsed[i]);
	}

	free(parsed);
}

// start all commands and wait for them to complete
NORETURN
void run(char** const cmds, const unsigned nproc) {
	log_info("running as pid %jd", (intmax_t)getpid());

	// parse commands and start processes
	sigset_t sig_set;

	spawn_procs(cmds, nproc, &sig_set);

	// close unneeded handles
	fclose(stdout);
	fclose(stdin);

	// main loop
	for(;;) {
		siginfo_t info;

		while(sigwaitinfo(&sig_set, &info) < 0)
			if(errno != EINTR)
				die_errno("signal wait failed");

		// see what we've got
		switch(info.si_signo) {
			case SIGCHLD:
				scan_children();
				break;

			case SIGALRM:
				if(info.si_code == SI_KERNEL) // alarm from the kernel means we requested it
					send_signal(SIGKILL);
				break;

			default:
				forward_signal(info.si_signo);
				break;
		}
	}
}

// usage string
static
const char usage_string[] =
"Usage:\n"
"  run [-qst] cmd [cmd...]\n"
"  run [-hv]\n"
"\n"
"Start all commands, then wait for them to complete.\n"
"\n"
"Options:\n"
"  -q       Reduce logging level (may be given more than once).\n"
"  -s SIG   Send signal SIG to all remaining processes when one terminates;\n"
"           SIG can be any of: INT, TERM, KILL, QUIT, HUP, USR1, USR2.\n"
"  -t N     Wait N seconds before sending KILL signal to all remaining processes.\n"
"  -h       Show this help and exit.\n"
"  -v       Show version and exit.\n";

// usage string display
NORETURN
void usage_exit(void) {
	fwrite(usage_string, sizeof(usage_string) - 1, 1, stderr);
	exit(EXIT_FAILURE);
}

// parse positive integer
static
int parse_positive_int(const char* s) {
	int val;

	switch(*s) {
		case '1' ... '9':
			val = *s - '0';
			break;

		default:
			return -1;
	}

	for(++s; ; ++s) {
		switch(*s) {
			case 0:
				return val;

			case '0' ... '9':
				if(__builtin_smul_overflow(val, 10, &val))
					return -1;

				if(__builtin_sadd_overflow(val, *s - '0', &val))
					return -1;

				break;

			default:
				return -1;
		}
	}
}

// main
int main(int argc, char** argv) {
	signal(SIGPIPE, SIG_IGN);

	// make STDERR line-buffered
	setvbuf(stderr, NULL, _IOLBF, 0);

	// check arguments
	if(argc == 1)
		usage_exit();

	// parse options
	int c;

	while((c = getopt(argc, argv, "+:qhvs:t:")) != -1) {
		switch(c) {
			case 'q':
				// log level
				++log_level;
				break;

			case 'h':
				// help
				usage_exit();

			case 'v':
				// version
				fwrite(XSTR(VER) "\n", sizeof(XSTR(VER)), 1, stderr);
				exit(EXIT_FAILURE);

			case 's': {
				// terminating signal
				const char* s = (optarg[0] == 'S' && optarg[1] == 'I' && optarg[2] == 'G')
							  ? optarg + 3
							  : optarg;

				term_signal = (strcmp(s, "INT") == 0)	? SIGINT
							: (strcmp(s, "TERM") == 0)	? SIGTERM
							: (strcmp(s, "KILL") == 0)	? SIGKILL
							: (strcmp(s, "QUIT") == 0)	? SIGQUIT
							: (strcmp(s, "HUP") == 0)	? SIGHUP
							: (strcmp(s, "USR1") == 0)	? SIGUSR1
							: (strcmp(s, "USR2") == 0)	? SIGUSR2
							: 0;

				if(term_signal == 0)
					die("unrecognised signal name: `%s`", optarg);

				break;
			}

			case 't':
				// kill timeout
				if((kill_timeout = parse_positive_int(optarg)) <= 0)
					die("invalid timeout value: `%s`", optarg);

				break;

			case '?':
				die("unrecognised option `-%c`", optopt);
		}
	}

	// validate options
	if(optind >= argc)
		die("missing command");

	const unsigned nproc = argc - optind;

	if(nproc > RUN_MAX_PROCS)
		die("too many commands");

	if(kill_timeout && !term_signal) {
		log_warn("option `-t` is meaningless without `-s`; ignored");
		kill_timeout = 0;
	}

	// options ok, go ahead
	run(&argv[optind], nproc);
}
