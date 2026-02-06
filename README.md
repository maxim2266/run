## `run`: A minimal `pid 1` process for Docker containers.

[![License: BSD 3 Clause](https://img.shields.io/badge/License-BSD_3--Clause-yellow.svg)](https://opensource.org/licenses/BSD-3-Clause)

`run` spawns a single child process, then waits for it and _all descendants_ to complete, while
reaping zombie processes and forwarding Unix signals to the entire process group.

### Motivation
Years ago, when the concept of the "cloud" was just emerging, I imagined it as a collection
of networked Linux computers, each equipped with a service manager I could configure to run
my software. It turned out I was wrong: Docker containers run only one process, and cloud
providers essentially charge us per each process we start. Not a good state of affairs, but I
cannot change the world. All I want is to run inside my container something like:
```shell
/bin/run -ds SIGTERM sh -ec 'service1 & service2 & service3 &'
```
and reduce my cloud bill by two-thirds.

### Invocation
```
▶ ./run -h
Usage:
  run [-qsdt] cmd [args...]
  run [-hv]

Start `cmd`, then wait for it and all its descendants to complete.

Options:
  -q       Reduce logging level (may be given more than once).
  -s SIG   Send signal SIG to all remaining processes when one terminates;
           SIG can be any of: INT, TERM, KILL, QUIT, HUP, USR1, USR2.
  -d       Daemon mode: skip sending the above termination signal when `cmd`
           exits with code 0.
  -t N     Wait N seconds before sending KILL signal to all remaining processes.
  -h       Show this help and exit.
  -v       Show version and exit.
```
With no options `run` simply starts the given command and waits for it and all descendants
to complete, otherwise:
* With `-s` option it sends the given signal to the entire process group when any process
  terminates. _Note_: Linux shells typically block INT and QUIT signals.
* With `-d` option the above signal is _not_ sent when `cmd` exits with code 0. Useful where
  `cmd` only starts the services, but doesn't wait for any of them to complete.
* With `-t` option KILL signal is sent to all the remaining processes after the specified timeout.

Options `-d` and `-t` are meaningless without `-s`. In practice `-s` and `-t` are usually set
to reflect Docker defaults: `-s SIGTERM -t 10`.

Exit code from `run` is the maximum of all exit codes, or 0 if all processes completed
successfully.

_Note_: There is a subtle issue when running a command like
`sh -ec 'service1 & service2'` (i.e. `service1` in the background and `service2` in the foreground):
if `service1` starts, but terminates before `service2`, then the termination will not be detected
by `run`, because at that moment `service1` is still a child process of `sh`. This can be
fixed by simulating a double-fork:
```shell
sh -ec '(service1 &) ; service2'
```
There is no such problem if both services started in the background, and `run` is invoked in
daemon mode.

### Setup
```shell
git clone https://github.com/maxim2266/run.git
cd run
make image
```
The last command builds an image according to the provided [dockerfile](run.dockerfile).
The compiled binary in the container is statically linked with `musl` library.

Other targets:
* `make test` to run all tests in a container;
* `make` to build the program locally.

### Further Development
At the moment the program does the intended job reasonably well, but there are other features
that would be nice to have in the future:
* Collect STDOUT and STDERR from each service individually, to make sure they don't share
  the same Unix pipe.
* Service management, able (at least) to restart a process without shutting down the whole
  container.
* `setuid`, although this is likely to (massively) complicate the code.

I don't know if anything of the above will ever be implemented.

### License
BSD-3-Clause
