## run: a `pid 1` process for Docker containers.

[![License: BSD 3 Clause](https://img.shields.io/badge/License-BSD_3--Clause-yellow.svg)](https://opensource.org/licenses/BSD-3-Clause)

The purpose of `run` is to spawn a single child process, and then to wait for it and all its
descendants to complete, meanwhile reaping zombies and forwarding Unix signals to either the main
child process, or to the whole process group.

```
▶ ./run
Usage:
  run [-gq] cmd [args...]
  run [-hv]

Start `cmd`, then wait for it and all its descendants to complete.

Options:
  -q  Reduce logging level (may be given more than once).
  -g  Start `cmd` in its own process group.
  -h  Show this help and exit.
  -v  Show version and exit.
```

### Rationale
The key point of `run` is waiting for **all** running processes, not just for the main one, so
that the `run` process is always the first to start and the last to terminate. This is how it
looks in practice:
```
▶ ./run sh -c 'sleep 5 &'
run: [info] started process `sh` (pid 4499)
run: [info] pid 4499: completed
run: [info] pid 4500: completed
run: [info] exit code 0
▶ ps -o pid,ppid,pgid,args
    PID    PPID    PGID COMMAND
   3903    3887    3903 /bin/bash
   4501    3903    4501 ps -o pid,ppid,pgid,args
```
For some reason, most of the existing implementations have chosen _not_ to wait for all processes
to complete, for example here we have a stray child `sleep 5` still running after the `init`
process is gone:
```
▶ tini -svv -- sh -c 'sleep 5 &'
[INFO  tini (4518)] Spawned child process 'sh' with pid '4519'
[DEBUG tini (4518)] Received SIGCHLD
[DEBUG tini (4518)] Reaped child with pid: '4519'
[INFO  tini (4518)] Main child exited normally (with status '0')
▶ ps -o pid,ppid,pgid,args
    PID    PPID    PGID COMMAND
   3903    3887    3903 /bin/bash
   4520       1    4519 sleep 5
   4521    3903    4521 ps -o pid,ppid,pgid,args
```
IMO this simply undermines the whole idea of the `init` process, because in a container environment
abandoned children will get no process to be reparented to, and will be killed. Using `run`
a number of services can be launched with one simple command, for example
```
sh -c 'service1 & service2 & service3 &'
```
and then all of them will monitored by the `run` process until they all terminate. Other `init`s
would require the launching script to also wait for all its children, a pointless re-implementation
of the `init` functionality.

In terms of the overall functionality `run` is more or less a replacement for
[tini](https://github.com/krallin/tini).

### Installation
The program can be compiled locally by running `make`, or `CC='musl-gcc -static' make` to
produce a statically liked binary. Alternatively, a statically linked binary can be compiled
in a container using the provided `docker-build` script:
```
▶ ./docker-build
Usage: docker-build [-cbh] [-o FILE]

Options (at least one must be given):
  -c       delete all previous images
  -b       build a new image
  -o FILE  copy the compiled binary to the given local FILE

Environment variables:
  DOCKER      docker command (default: "docker")
  IMAGE_NAME  name of the docker image (default: "run-local:latest")
```
Also, see example [Docker file](dockerfile-example).

### Running in interactive mode
Generally, `init` daemons do not interact with TTY, and all implementations I've tried so far
exibit various issues when invoked in interactive mode. Here I ignore this problem just for now,
but I may have a look at it in the future.
