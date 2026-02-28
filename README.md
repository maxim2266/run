## `run`: A minimalist process launcher and monitor for Docker containers.

[![License: BSD 3 Clause](https://img.shields.io/badge/License-BSD_3--Clause-yellow.svg)](https://opensource.org/licenses/BSD-3-Clause)

`run` launches one or more commands, and then waits for all of them to complete, with
options to either notify or terminate remaining processes when one exits.

### Motivation
Years ago, when the concept of the "cloud" was just emerging, I imagined it as a collection
of networked Linux computers, each equipped with a service manager I could configure to run
my software. It turned out I was wrong: Docker containers run only one process, and cloud
providers essentially charge us per each process we start. Not a good state of affairs, but I
cannot change the world. All I want is to add to my Dockerfile something like:
```docker
CMD [ "/bin/run", "-s", "SIGTERM", "/bin/service1", "/bin/service2", "/bin/service3" ]
```
and reduce my cloud bill by two-thirds.

### Invocation
```
▶ ./run -h
Usage:
  run [-qstg] cmd [cmd...]
  run [-hv]

Start all commands, then wait for them to complete.

Options:
  -q       Reduce logging level (may be given more than once).
  -s SIG   Send signal SIG to all remaining processes when one terminates;
           SIG can be any of: INT, TERM, KILL, QUIT, HUP, USR1, USR2.
  -t N     Wait N seconds before sending KILL signal to all remaining processes.
  -g       Forward signals to the process group of each launched command.
  -h       Show this help and exit.
  -v       Show version and exit.
```

With no options `run` simply starts the given commands and waits for all of them
to complete, otherwise:
* With `-s` option it sends the given Unix signal to all remaining processes each time one
  terminates, subject to SIGCHLD coalescing. _Note_: Linux shells typically block SIGINT and
  SIGQUIT signals.
* With both `-s` and `-t` options the given signal is sent only once upon the first process
  exit, followed by SIGKILL after the specified timeout.

Option `-t` is meaningless without `-s`. In practice, if both `-s` and `-t` are specified,
they are usually set to reflect Docker defaults: `-s TERM -t 10`.

For better isolation each command is started in its own session and process group, and option
`-g` instructs `run` to forward signals to each group instead of each single process.

Exit code from `run` is the first non-zero exit code from any process, or 0 if all completed
successfully.

Since only one `CMD` statement is meaningful in a Dockerfile, each command has to be supplied
as a single string, with arguments separated by spaces or tabs. Quoting with either `"` or `'`
is honored, but no shell-like substitution is made. All pathnames should be given in full,
as no search on the `$PATH` is performed. Example:
```docker
CMD [ "/bin/run", "/bin/echo \"I am the first\"", "/bin/echo 'I am the second'" ]
```
Running a container with this command gives the following output:
```
run: [info] running as pid 1
run: [info] pid 2: command `/bin/echo`
run: [info] pid 3: command `/bin/echo`
I am the first
I am the second
run: [info] pid 2: exited with code 0
run: [info] pid 3: exited with code 0
run: [info] exit code 0
```

### Setup
```shell
git clone https://github.com/maxim2266/run.git
cd run
make image
```
The last command builds an image according to the example [Dockerfile](run.dockerfile). The
compiled binary in the container is statically linked with `musl` library and can be used
in `scratch` containers.

Other targets:
* `make test` to run all tests in a container;
* `make` to build the program locally.

### Further Development
At the moment the program does the intended job reasonably well, but there are other features
that would be nice to have in the future:
* Collect STDOUT and STDERR from each service individually, to make sure they don't share
  the same Unix pipe.
* More advanced service management, able (at least) to restart a process without shutting down
  the whole container.
* `setuid`, although this is likely to (massively) complicate the code.

I don't know if anything of the above will ever be implemented.

### License
BSD-3-Clause
