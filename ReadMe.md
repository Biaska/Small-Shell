# Small Shell

Small Shell is a lightweight, Bash-like terminal implemented in C.
It supports running programs in the foreground and background, basic input/output redirection, simple built-ins (`cd`, `status`, `exit`), and a *foreground-only* mode toggled with `Ctrl+Z`.

---

## Table of Contents

- [Small Shell](#small-shell)
  - [Table of Contents](#table-of-contents)
  - [Features](#features)
  - [Requirements](#requirements)
  - [Building](#building)
  - [Running](#running)
  - [Basic Usage](#basic-usage)
    - [Prompt \& Comments](#prompt--comments)
    - [Built-in Commands](#built-in-commands)
      - [`exit`](#exit)
      - [`cd`](#cd)
      - [`status`](#status)
    - [Running External Programs](#running-external-programs)
    - [Input/Output Redirection](#inputoutput-redirection)
    - [Background Processes (`&`)](#background-processes-)
      - [Background I/O behavior](#background-io-behavior)
    - [Foreground-Only Mode (Ctrl+Z)](#foreground-only-mode-ctrlz)
    - [Checking Last Exit Status](#checking-last-exit-status)
  - [Signal Behavior](#signal-behavior)
  - [Limits \& Notes](#limits--notes)

---

## Features

* **Simple command execution**: Run any program available in your `PATH` (e.g., `ls`, `grep`, etc.).
* **Built-in commands**:

  * `cd [dir]` – change directory
  * `status` – show exit/termination info of the last *foreground* process
  * `exit` – exit the shell
* **Background processes**:

  * Append `&` to run a command in the background
  * Small Shell prints the background PID when started
  * Notifies you when background jobs complete
* **Foreground-only mode**:

  * Toggle with `Ctrl+Z` (`SIGTSTP`)
  * When enabled, `&` is ignored and all commands run in the foreground
* **Input/output redirection**:

  * `command < input.txt`
  * `command > output.txt`
  * `command < in.txt > out.txt`
* **Safe handling of background I/O**:

  * Background jobs with no explicit redirection use `/dev/null` for stdin and stdout, so they don’t spam your terminal.
* **Signal handling**:

  * Shell ignores `Ctrl+C` itself so it doesn’t kill the shell
  * Foreground child processes can be interrupted by `Ctrl+C`

---

## Requirements

* Linux (uses `<linux/limits.h>`)
* C compiler (e.g., `gcc`)
* POSIX support for:

  * `fork`, `execvp`, `waitpid`
  * `sigaction`, `dup2`, `open`, etc.

---

## Building

Save the code into a file, for example `smallsh.c`, then compile:

```bash
gcc -Wall -Wextra -std=c11 smallsh.c -o smallsh
```

This will produce an executable named `smallsh`.

---

## Running

From a terminal:

```bash
./smallsh
```

You’ll see a simple prompt:

```text
:
```

Type commands at the `:` prompt and press Enter.

To leave the shell:

```text
: exit
```

---

## Basic Usage

### Prompt & Comments

* The shell prompt is a single colon:

  ```text
  :
  ```

* **Empty lines** are ignored.

* Lines starting with `#` are treated as **comments** and ignored:

  ```text
  : # this is a comment and will be ignored
  ```

### Built-in Commands

These are handled directly by the shell (not via `execvp`).

#### `exit`

```text
: exit
```

* Exits Small Shell.
* (Note: the code exits the shell loop immediately; it does **not** explicitly clean up any existing background processes.)

#### `cd`

```text
: cd /path/to/dir
: cd            # goes to $HOME
```

* With an argument: changes to the specified directory using `chdir(argv[1])`.
* With no argument: changes to the directory stored in the `HOME` environment variable.

#### `status`

```text
: status
exit value 0
```

* Prints the status of the **most recent foreground process**:

  * `exit value N` if it exited normally
  * `terminated by signal N` if it was terminated by a signal
* Before any commands run, it reports a default “exit status 0” style message.

---

### Running External Programs

Any command that is **not** `exit`, `cd`, or `status` is treated as an external program.

Examples:

```text
: ls
: ls -l /tmp
: echo hello world
```

Internally, the shell:

1. Parses your input into arguments (split on spaces).
2. Calls `fork()` to create a child.
3. Uses `execvp(argv[0], argv)` in the child to run the command.

If `execvp` fails (e.g., command not found), the shell prints an error using `perror()`.

---

### Input/Output Redirection

You can redirect stdin and/or stdout with `<` and `>`:

* Redirect input:

  ```text
  : sort < unsorted.txt
  ```

* Redirect output:

  ```text
  : ls > listing.txt
  ```

* Redirect both:

  ```text
  : sort < unsorted.txt > sorted.txt
  ```

Implementation details:

* `< file` sets `cmd->input_file`
* `> file` sets `cmd->output_file`
* In the child process:

  * Input: open file with `O_RDONLY`, then `dup2()` to stdin.
  * Output: open file with `O_WRONLY | O_CREAT | O_TRUNC, 0644`, then `dup2()` to stdout.
* On error (e.g., input file does not exist), a helpful message is printed and the child exits with a non-zero status.

---

### Background Processes (`&`)

Appending `&` at the end of the command requests **background** execution:

```text
: sleep 10 &
background pid is 12345
```

Behavior:

* The command runs in the background.
* The shell **does not** wait for it before showing the next prompt.
* The PID is stored in an internal array so the shell can later check when it finishes.
* Up to `MAX_BG_PC` (20) background processes are tracked at once.

When a background process completes, on the **next prompt** cycle, the shell prints a notification:

```text
background pid 12345 is done: exit value 0
```

or

```text
background pid 12345 is done: terminated by signal 2
```

#### Background I/O behavior

If you do **not** specify I/O redirection for a background process:

* `stdin` is redirected from `/dev/null`
* `stdout` is redirected to `/dev/null`

Example:

```text
: my-long-running-command &
```

This means the process won’t accidentally read from the terminal or flood it with output.

If you **do** specify redirection, those files are used instead:

```text
: longjob < input.txt > output.txt &
```

---

### Foreground-Only Mode (Ctrl+Z)

Pressing `Ctrl+Z` sends `SIGTSTP` to the shell.
Small Shell **does not** stop; instead it toggles **foreground-only mode**:

* When entering foreground-only mode:

  ```text
  Entering foreground-only mode (& is now ignored)
  ```

* When leaving foreground-only mode:

  ```text
  Exiting foreground-only mode
  ```

While in foreground-only mode:

* The `&` symbol is **ignored**.
* Commands like:

  ```text
  : sleep 10 &
  ```

  will run as if you typed:

  ```text
  : sleep 10
  ```

Internally, this is tracked via the global `fg_only` flag.
On each command, if `cmd->is_bg` is true **and** `fg_only` is false, it runs in background; otherwise it runs as a normal foreground process.

---

### Checking Last Exit Status

After running a foreground command, you can use `status`:

```text
: ls /tmp
: status
exit value 0

: kill -9 12345
: status
terminated by signal 9
```

The shell maintains a `prev_fg_status` struct that tracks:

* whether the last foreground command:

  * exited normally (`exited`)
  * was terminated by a signal (`terminated`)
* and the numeric code (`code`).

These values are updated after `waitpid()` returns in the foreground path.

---

## Signal Behavior

Small Shell uses `sigaction` to manage signals:

* **SIGINT (`Ctrl+C`):**

  * The shell installs a handler that effectively **ignores** `SIGINT`.
  * The handler is *not* used after an `execvp()`; for external programs, `SIGINT` reverts to the default behavior.
  * Effectively:

    * `Ctrl+C` **does not kill the shell**.
    * `Ctrl+C` **does terminate** the currently running foreground child process (like a normal shell).

* **SIGTSTP (`Ctrl+Z`):**

  * The shell installs a handler that toggles foreground-only mode.
  * It prints the corresponding message and then returns to the prompt.
  * The shell itself is **not** stopped or backgrounded.

---

## Limits & Notes

* **Input length**: max `2048` characters per line.
* **Arguments**: max `512` arguments per command.
* **Background processes tracked**: up to `20` concurrent jobs.
* **Parsing**:

  * Splits on spaces and newlines.
  * No quoting or escaping (`"..."`, `'...'`, `\` etc. are not supported).
  * No variable expansion (`$VAR`, `$$`, etc.).
  * No pipes (`|`), logical operators (`&&`, `||`), or command substitution.