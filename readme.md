# nyush

A simple UNIX shell written in C, supporting basic command execution, built-in commands, and background job control.

## Repository Name

**nyush**

## Short Description

nyush is a minimal command-line interpreter that parses input, executes programs, handles built-in commands like `cd` and `exit`, and manages background processes with `&`.

## Contents

- **nyush.c**: Main source file implementing the shell loop, parsing, and execution logic.
- **Makefile**: Build targets for compiling the `nyush` executable and cleaning artifacts.
- **nyush-autograder/**: Autograder scripts and resources for testing shell functionality.
- **.gitignore**: Specifies files and directories to be ignored by Git.

## Building

To compile the shell, run:

```sh
make all
```

This produces the `nyush` executable.

## Usage

Start the shell:

```sh
./nyush
```

Within `nyush`, enter any system command, for example:

```sh
nyush> ls -l
nyush> grep foo file.txt
```

To run a command in the background:

```sh
nyush> sleep 10 &
```

Supported built-ins:

- `cd [dir]`: Change the current working directory.
- `exit`: Exit the shell.

## Cleaning

Remove generated files:

```sh
make clean
```

## License

MIT License. See [LICENSE](LICENSE) for details.

