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
int exit_code = 0,
	has_tty = 0,
	term_signal = 0,
	kill_timeout = 0,
	min_error = 0,
	terminating = 0;

static
pid_t proc_group = 0;

// TTY assignment
static
void assign_tty(const pid_t pgid) {
	if(has_tty && tcsetpgrp(STDIN_FILENO, pgid))
		log_warn_errno("failed to assign TTY to process group %jd", (intmax_t)pgid);
}

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
void forward_signal(const int sig) {
	if(kill(-proc_group, sig) == 0)
		log_info("signal %s(%d) sent to group %jd",
				 sig2str(sig), sig, (intmax_t)proc_group);
	else
		log_warn_errno("signal %s(%d) could not be sent to group %jd",
					   sig2str(sig), sig, (intmax_t)proc_group);
}

// scan all children and handle their status changes
static
void scan_children(void) {
	pid_t pid;
	int status, notify = 0;

	while((pid = next_proc(&status)) > 0) {
		if(WIFEXITED(status)) {
			status = WEXITSTATUS(status);

			if(status == 0)
				log_info("pid %jd: exited with code %d", (intmax_t)pid, status);
			else
				log_warn("pid %jd: failed with code %d", (intmax_t)pid, status);

		} else if(WIFSIGNALED(status)) {
			const int sig = WTERMSIG(status);

			log_warn("pid %jd: killed by %s(%d)", (intmax_t)pid, sig2str(sig), sig);
			status = sig + 128;

		} else
			continue;

		// grab TTY on main process exit
		if(pid == proc_group)
			assign_tty(getpgrp());

		// exit code
		if(!exit_code)
			exit_code = status;

		// notification flag
		notify |= (status >= min_error);
	}

	// initiate shutdown if required
	if(notify && term_signal && !terminating) {
		log_info("shutting down");
		forward_signal(term_signal);
		alarm(kill_timeout);
		terminating = 1;
	}
}

// exec the given command
NORETURN
void do_exec(char** const cmd, sigset_t* const sig_set) {
	// create new process group
	setpgid(0, 0);

	// grab TTY, if any
	assign_tty(getpgrp());

	// send termination signal if parent is dead
	// (doesn't help grandchildren and daemons)
	if(prctl(PR_SET_PDEATHSIG, term_signal ? term_signal : SIGTERM) < 0)
		log_warn_errno("pid %jd: failed to set parent death signal", (intmax_t)getpid());

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
		case EACCES: _exit(126);
		case ENOENT: _exit(127);
		default:     _exit(EXIT_FAILURE);
	}
}

// spawn a new process
static
pid_t spawn(char** const cmd, sigset_t* const sig_set) {
	// fork
	const pid_t pid = fork();

	if(pid < 0)
		die_errno("failed to start process `%s`", cmd[0]);

	// child process
	if(pid == 0)
		do_exec(cmd, sig_set);	// never returns

	// parent process
	setpgid(pid, pid);
	log_info("pid %jd: command `%s`", (intmax_t)pid, cmd[0]);

	return pid;
}

// start the command and wait on it to complete
NORETURN
void run(char** const cmd) {
	// flush STDERR, as it may be buffered
	if(fflush(stderr) != 0)
		exit(125);	// because here STDERR is dead

	// become a subreaper
	if(getpid() != 1 && prctl(PR_SET_CHILD_SUBREAPER, 1L) != 0)
		die_errno("failed to become a subreaper");

	// TTY
	has_tty = isatty(STDIN_FILENO) && (tcgetpgrp(STDIN_FILENO) == getpgrp());

	// ignore SIGPIPE
	signal(SIGPIPE, SIG_IGN);

	// mask all signals
	sigset_t sig_set, old_set;

	sigfillset(&sig_set);
	sigprocmask(SIG_SETMASK, &sig_set, &old_set);

	// start the process
	proc_group = spawn(cmd, &old_set);

	// close unneeded handles
    if(!has_tty) {
		fclose(stdout);
		fclose(stdin);
	}

	// main loop
	int sig;

	while(sigwait(&sig_set, &sig) == 0) {
		switch(sig) {
			case SIGPIPE: // just in case
			case SIGTTOU: // we are in the background trying terminal I/O
			case SIGTTIN:
			case SIGTSTP:
				// ignore
				break;

			case SIGCHLD:
				scan_children();
				break;

			case SIGALRM:
				forward_signal(terminating ? SIGKILL : SIGALRM);
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
"  run [-qset] cmd [args...]\n"
"  run [-hv]\n"
"\n"
"Start `cmd`, then wait for it and all its descendants to complete.\n"
"\n"
"Options:\n"
"  -q       Reduce logging level (may be given more than once).\n"
"  -s SIG   Send signal SIG to all remaining processes when one terminates with an error;\n"
"           SIG can be any of: INT, TERM, KILL, QUIT, HUP, USR1, USR2.\n"
"  -e CODE  Minimal process exit code to be treated as an error (default: 0).\n"
"  -t N     Wait N seconds before sending KILL signal to all remaining processes.\n"
"  -h       Show this help and exit.\n"
"  -v       Show version and exit.\n";

// usage string display
NORETURN
void usage_exit(void) {
	fwrite(usage_string, sizeof(usage_string) - 1, 1, stderr);
	exit(EXIT_FAILURE);
}

// parse integer (for command line options)
static
int parse_int(const char* s) {
	int digits = 0, val = 0;

	for(;; ++s) {
		switch(*s) {
			case 0:
				return (digits > 0) ? val : -1;
			case '0' ... '9':
				val = 10 * val + (*s - '0');

				if(++digits < 10)
					continue;
				// fall through
			default:
				return -1;
		}
	}
}

// main
int main(int argc, char** argv) {
	if(argc == 1)
		usage_exit();

	// parse options
	int c;

	while((c = getopt(argc, argv, "+:qhvs:t:e:")) != -1) {
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
				if((kill_timeout = parse_int(optarg)) <= 0)
					die("invalid timeout value: `%s`", optarg);

				break;

			case 'e':
				// error threshold
				min_error = parse_int(optarg);

				if(min_error < 0 || min_error > 255)
					die("invalid error threshold: `%s`", optarg);

				break;

			case '?':
				die("unrecognised option `-%c`", optopt);
		}
	}

	// validate options
	if(optind == argc)
		die("missing command");

	if(!term_signal) {
		if(kill_timeout) {
			log_warn("option `-t %d` is meaningless without `-s`", kill_timeout);
			kill_timeout = 0;
		}

		if(min_error) {
			log_warn("option `-e %d` is meaningless without `-s`", min_error);
			min_error = 0;
		}
	}

	// options ok, go ahead
	run(&argv[optind]);
}
