## `run`: A minimal `pid 1` process for Docker containers.

[![License: BSD 3 Clause](https://img.shields.io/badge/License-BSD_3--Clause-yellow.svg)](https://opensource.org/licenses/BSD-3-Clause)

`run` spawns a single child process, then waits for it and all descendants to complete, while
reaping zombie processes and forwarding Unix signals to the entire process group.

### Motivation
Years ago, when the concept of the "cloud" was just emerging, I imagined it as a collection of
networked Linux computers, each with a basic setup and an `init` daemon I could configure to
run my software. It turned out I was wrong: Docker containers run only one process, and cloud
providers essentially charge per process. Not a good state of affairs, but I cannot change the
world. All I want is to run inside my container something as simple as:
```shell
/bin/run sh -c 'service1 & service2 & service3 &'
```
and reduce my cloud cost by two-thirds.

### Invocation
```
▶ ./run -h
Usage:
  run [-q] [-s SIG] cmd [args...]
  run [-hv]

Start `cmd`, then wait for it and all its descendants to complete.

Options:
  -q       Reduce logging level (may be given more than once).
  -s SIG   Send signal SIG to all processes when the main one terminates;
           SIG can be any of: INT, TERM, KILL, QUIT, HUP, USR1, USR2.
  -h       Show this help and exit.
  -v       Show version and exit.
```

### Setup
```shell
git clone https://github.com/maxim2266/run.git
cd run
make image
```
The last command creates an image according to the provided [dockerfile](runner.dockerfile).
The program can also be built locally with `make` command. Tests can be run with
`make test`.

### License
BSD-3-Clause
