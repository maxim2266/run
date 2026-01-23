## `run`: A minimal `pid 1` process for Docker containers.

[![License: BSD 3 Clause](https://img.shields.io/badge/License-BSD_3--Clause-yellow.svg)](https://opensource.org/licenses/BSD-3-Clause)

`run` spawns a single child process, then waits for it and _all descendants_ to complete, while
reaping zombie processes and forwarding Unix signals to the entire process group.

### Motivation
Years ago, when the concept of the "cloud" was just emerging, I imagined it as a collection of
networked Linux computers, each fully set up, and with a service manager I could configure to
run my software. It turned out I was wrong: Docker containers run only one process, and cloud
providers essentially charge us per each process we start. Not a good state of affairs, but
I cannot change the world. All I want is to run inside my container something as simple as:
```shell
/bin/run sh -c 'service1 & service2 & service3 &'
```
and reduce my cloud bill by two-thirds.

### Invocation
```
▶ ./run -h
Usage:
  run [-qset] cmd [args...]
  run [-hv]

Start `cmd`, then wait for it and all its descendants to complete.

Options:
  -q       Reduce logging level (may be given more than once).
  -s SIG   Send signal SIG to all remaining processes when one terminates;
           SIG can be any of: INT, TERM, KILL, QUIT, HUP, USR1, USR2.
  -e CODE  Minimal process exit code to trigger the above signal (default: 0).
  -t N     Wait N seconds before sending KILL signal to all remaining processes.
  -h       Show this help and exit.
  -v       Show version and exit.
```
With no options `run` simply starts the given command and waits for it and all descendants
to complete, otherwise:
* With `-s` option it sends the given signal to the entire process group when any process
  terminates. _Note_: Linux shells typically block INT and QUIT signals.
* With `-e` option the above signal is sent only when a process exits with a code greater
  or equal to the one specified.
* With `-t` option KILL signal is sent to all the remaining processes after the specified timeout.

Options `-e` and `-t` are meaningless without `-s`. In practice `-s` and `-t` are usually set
to reflect Docker defaults: `-s SIGTERM -t 10`.

Exit code from `run` is that of the first process terminated with non-zero code, or 0 if all
completed successfully.

### Setup
```shell
git clone https://github.com/maxim2266/run.git
cd run
make image
```
The last command builds an image according to the provided [dockerfile](runner.dockerfile).
The compiled binary in the container is statically linked with `musl` library.

Other targets:
* `make test` to run all tests in a container;
* `make` to build the program locally.

### Further Development
At the moment the program does the intended job reasonably well, but there are other features
that would be nice to have in the future:
* Ability to launch multiple services without a shell.
* Collect STDOUT and STDERR from each service individually, to make sure they don't share
  the same Unix pipe.
* Service management, able (at least) to restart a process without shutting down the whole
  container.
* `setuid`, although this is likely to (massively) complicate the code.

I don't know if anything of the above will ever be implemented.

### License
BSD-3-Clause
