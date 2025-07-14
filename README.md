# posix-printer-spooler

A POSIX-compliant print spooler and job scheduler written in C, designed for Unix-based environments. It features job queuing, dynamic printer management, file-type conversion pipelines, and robust process control using Unix system calls and signal handling.

## Features

* Interactive CLI for job and printer management
* Job lifecycle control: creation, execution, pausing, resuming, cancellation, deletion
* Printer lifecycle control: enable/disable states, file-type compatibility
* Conversion pipeline execution using `fork()`, `pipe()`, `dup2()`, and `execvp()`
* Signal-safe process management using `SIGCHLD`, `SIGSTOP`, `SIGCONT`, `SIGTERM`
* Custom file-type inference and conversion graph resolution
* Dynamic printer connection via Unix domain sockets
* Prompt job dispatching with millisecond-level response time guarantees
* Strict adherence to POSIX and async-signal-safe programming constraints

## Technologies Used

* C (C99 standard)
* POSIX system calls: `fork`, `execvp`, `waitpid`, `pipe`, `dup2`, `kill`, `sigaction`, `sigprocmask`
* Inter-process communication (pipes, signals, sockets)
* Custom CLI parsing and dynamic memory management
* Unix process groups and job control

## How It Works

The spooler accepts user commands via a custom CLI (`run_cli()`), processes file-type declarations, registers printers, and handles print job submissions. For each job, it:

1. Verifies the file type based on extension
2. Resolves the required conversion path (if needed)
3. Launches a conversion pipeline using a master process and child processes
4. Connects the pipeline output to an eligible printer using Unix sockets
5. Monitors and updates job/printer state transitions via a `SIGCHLD` handler

## Build and Run

```
make           # Builds the spooler and supporting tools  
bin/presi      # Launches the interactive CLI  
```

### Example Usage

```
presi> type pdf  
presi> type ps  
presi> printer alice ps  
presi> conversion pdf ps convert-pdf-to-ps  
presi> print document.pdf alice  
```

## Testing

```
make test  
bin/presi_tests -j1 --verbose  
```

Includes extensive unit and integration tests using the Criterion framework, testing:

* Job creation, scheduling, cancellation, and timeout handling
* Signal propagation correctness
* Pipeline construction and error recovery
* Printer connection logic and eligibility constraints

## Logging and Debugging

* Debug output via `debug()` macro
* Signal-safe event logging via `sf_*` functions
* Logs and printed output written to `spool/` directory

## Known Constraints

* All pipeline setup and process management done without `system()` or shell wrappers
* All signal handling adheres to async-signal-safe best practices
* File descriptor and memory leaks checked with `valgrind --leak-check=full --track-fds=yes`

## License

MIT License
