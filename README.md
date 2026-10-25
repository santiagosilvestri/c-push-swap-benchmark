# Push Swap Graph Benchmark

*Created by **Santiago Silvestri** as a portable benchmarking tool for multi-strategy `push_swap` implementations.*

A single-file C benchmark that compares the available `--simple`, `--medium`, `--complex`, and `--adaptive` modes, validates their output, and displays the results as a terminal graph. It also works with standard `push_swap` programs that accept no strategy flags. An optional `--test` mode runs a diagnostic suite with a report and a reproducible command for every executed case.

## One-command installation

Once `install.sh` and `push_swap_benchmark.c` are published on this repository's `main` branch, users can paste this command:

```bash
bash -c 'ps_benchmark_installer=$(curl -fsSL https://raw.githubusercontent.com/santiagosilvestri/c-push-swap-benchmark/main/install.sh) && bash -c "$ps_benchmark_installer"'
```

It downloads the source, selects an available C compiler, builds the benchmark, installs it as `~/.local/bin/ps-benchmark`, and configures that directory in the user's Bash or Zsh startup files.

**Paste it inside a folder containing an executable `./push_swap` and the benchmark starts automatically after installation.** Otherwise, it finishes installing and shows how to run it later:

```bash
ps-benchmark
ps-benchmark -a complex, simple, medium -m 100 -r 3
```

If the command directory is new to your current shell, open a new terminal for future runs. The installer starts the first benchmark using its full path, so that first run does not require a terminal restart.

The installer needs Bash and an existing C compiler (`cc`, `clang`, or `gcc`). Remote installation also needs a downloader; the command above uses `curl`. Installation takes place in the user's home directory and does not request administrator privileges. Unsupported shells receive instructions to add the command directory to `PATH`.

After downloading the repository, local installation also works offline when the C source is beside the installer:

```bash
bash install.sh
```

Use `bash install.sh --no-run` to install without starting a benchmark, or `bash install.sh --run -m 100 -r 3` to install and run with custom benchmark options. Rerunning the installer updates the executable and keeps a single managed PATH block in each startup file. Downloads and compilation are staged before replacing an existing executable.

The URL above becomes usable after these files are uploaded to GitHub. If the repository owner, name, or branch changes, update both the installation command and the source URL at the top of `install.sh`.

## Uninstall

Remove the installed command, its managed Bash/Zsh PATH blocks and the default diagnostic report caches:

```bash
bash -c 'ps_benchmark_installer=$(curl -fsSL https://raw.githubusercontent.com/santiagosilvestri/c-push-swap-benchmark/main/install.sh) && bash -c "$ps_benchmark_installer" -- --uninstall'
```

With the repository already downloaded, the same operation works offline:

```bash
bash install.sh --uninstall
```

No compiler or administrator privileges are needed. Other shell settings and files in `~/.local/bin` are preserved. The uninstaller removes `~/.cache/ps-benchmark` and `$XDG_CACHE_HOME/ps-benchmark` when configured. Open a new terminal afterwards to refresh its command cache. Running the command again is safe.

The remote command becomes available after the updated `install.sh` is published. Source files you downloaded or compiled yourself, reports saved with `--report-dir`, benchmark logs, fallback reports in `/tmp` and caches under a previous custom `XDG_CACHE_HOME` cannot be located reliably; delete those at their chosen locations if you want to remove every local copy. The uninstaller does not delete the project you are testing.

## Use only the C file

Place `push_swap_benchmark.c` in the same directory as your compiled `push_swap` executable:

```text
push_swap/
├── push_swap
└── push_swap_benchmark.c
```

Compile the benchmark:

```bash
cc -Wall -Wextra -Werror push_swap_benchmark.c -lm -o ps_benchmark
```

Run it:

```bash
./ps_benchmark
```

For a first custom run, start with just three options:

```bash
./ps_benchmark -a complex, simple, medium -m 100 -r 3
```

- `-a`: choose which algorithms to compare.
- `-m`: set the maximum number of elements.
- `-r`: set the runs averaged for each size.

An invalid command shows only these basic options and a working example. Use `-h` or `--help` to see every option. Full help is printed to stdout; errors and brief usage guidance go to stderr.

The benchmark is compiled independently and does not modify your `push_swap`, headers, libraries, or checker.

You can keep the benchmark outside your project and pass the executable path:

```bash
./ps_benchmark --program ../c-push-swap/push_swap
```

Paths are used directly, without a shell or a `PATH` search. Quote paths containing spaces.

## Preview

![Push Swap Graph Benchmark preview](benchmark-preview.png)

## Requirements

The tested program must:

- be available as `./push_swap` or at the path passed to `--program`;
- accept at least one selected strategy flag, or work without strategy flags;
- accept the numbers as separate command-line arguments;
- print only valid `push_swap` operations, one per line;
- exit with status `0` after a valid execution.

Example:

```bash
./push_swap --medium 4 1 3 0 2
```

The benchmark requires a POSIX environment and a C11 compiler. Linux and WSL are the primary targets; macOS compatibility is intended but has not been validated in this review. Native Windows terminals require WSL or another suitable POSIX environment.

## Optional strategy flags

Before measuring, the benchmark checks each selected mode with two fixed, unsorted five-element inputs. It validates operations and final stacks using the same verifier as the benchmark.

- A flag that exits with status 1–125 and prints no operations on its first probe is reported as rejected and skipped. The warning names the flag: it may be unsupported or have faulty input handling. A rejection after a successful first probe stops the run.
- Available modes continue with the same generated inputs, graphs, and statistics.
- If every selected flag is rejected, the benchmark tries execution without a strategy flag, displayed as `default`.
- To test a standard program directly, use `--algorithms default`.
- To compare unflagged execution with specific strategies, use, for example, `--algorithms default,medium,complex`.

The startup checks do not consume the random generator or contribute to operation statistics or benchmark progress. Workload limits are checked after removing rejected modes.

These are compatibility checks, not proof that an internal strategy exists. A program that silently ignores a flag can pass them. Only selected flags are checked; an unselected mode is not reported as missing.

Crashes, timeouts, invalid operations, empty output with exit status 0 on unsorted input, and incorrect sorting stop the benchmark, including during startup. They are not silently classified as missing flags. Once measurement begins, any failure stops the run as before. Rejected flags should report diagnostics on stderr; text such as `Error` on stdout is invalid operation output and stops the run.

## Recommended commands

Compare every strategy up to 500 elements:

```bash
./ps_benchmark \
    --max 500 \
    --samples 10 \
    --runs 10 \
    --algorithms all \
    --log \
    --seed 42
```

Test a fixed disorder level:

```bash
./ps_benchmark \
    --max 500 \
    --algorithms medium,complex,adaptive \
    --disorder 30 \
    --runs 10 \
    --seed 42
```

Display all available options:

```bash
./ps_benchmark --help
```

## Options

| Short | Long | Default | Description |
|---|---|---:|---|
| `-b` | `--program` | `./push_swap` | Path to the executable to test. |
| `-m` | `--max` | `500` | Maximum input size, from 2 to 50000. |
| `-p` | `--samples` | `10` | Number of input sizes represented in the graph. |
| `-r` | `--runs` | `3` | Runs performed for each size. |
| `-a` | `--algorithms` | `all` | Strategies to compare; rejected flags are skipped. |
| `-d` | `--disorder` | random | Target disorder percentage, from 0 to 100. |
| `-l` | `--log` | disabled | Use a logarithmic vertical scale. |
| `-H` | `--height` | `18` | Graph height, from 8 to 40 rows. |
| `-t` | `--timeout` | `60` | Maximum seconds allowed per execution. |
| `-s` | `--seed` | current time | Seed used to reproduce generated inputs. |
| `-h` | `--help` | — | Display the help message. |

`--test` (also accepted as `-test`) runs the diagnostic suite described below. Its additional options appear in `--help`; invalid commands keep the short beginner usage message.

Algorithm names can be provided in any order, separated by commas, spaces, or both. These examples select the same modes:

```bash
./ps_benchmark -a complex,simple,medium
./ps_benchmark -a complex, simple, medium
./ps_benchmark -a "complex, simple, medium"
./ps_benchmark -a medium simple complex
```

Spaces around commas are optional; quotation marks are not required for the unquoted examples. Repeated names are measured once. Empty entries such as `simple,,medium` or a trailing comma are rejected. The next option, such as `-m` or `-r`, ends the algorithm list.

Valid names:

```text
simple,medium,complex,adaptive,default,all
```

`all` selects the four strategy flags, with automatic fallback to `default` when none remain. It does not add an extra unflagged comparison when a flagged mode is available. You can explicitly request all five with `all,default` or `simple,medium,complex,adaptive,default`.

## How the benchmark works

For every input size and run, the benchmark:

1. Generates one permutation.
2. Sends the same permutation to every selected algorithm.
3. Executes the selected program in an independent child process.
4. Captures and counts its operations.
5. Applies the operations to internal stacks.
6. Verifies that stack A is sorted and stack B is empty.
7. Stores the result and calculates statistics.
8. Draws the comparison graph in the terminal.

Using the same input for every algorithm is essential. It prevents one strategy from receiving an easier permutation than another during the same run.

The benchmark measures the **number of operations**, not execution time.

## Diagnostic test mode

Run from a project folder, using either the installed command or the manually compiled binary:

```bash
ps-benchmark --test
./ps_benchmark --test
```

Specify another executable or a source directory when needed:

```bash
ps-benchmark --test -b ../another-project/push_swap
ps-benchmark --test --project ../another-project -b ../another-project/push_swap
```

The suite checks all four required strategy flags, independently of which modes are available in graph mode. A rejected required flag is a `KO`. The suite's sizes are fixed; graph-only options such as `-a`, `-m`, `-p`, `-d`, `-H`, and `--log` are rejected with `--test`.

Useful test options:

| Option | Purpose |
|---|---|
| `-b PATH`, `--program PATH` | Executable used for behavioral checks. |
| `--project DIR` | Source root for Makefile and Norminette checks; otherwise inferred from the executable directory. |
| `--checker PATH` | Checker to test; otherwise searches for the project's `checker`, including one built by its declared `bonus` target. |
| `--report-dir DIR` | Parent directory for a new, uniquely named report folder. |
| `-s N`, `--seed N` | Reproduce the generated input sequence. |
| `-r N`, `--runs N` | Random repetitions, with at least three 100-element cases and two 500-element cases. |
| `-t N`, `--timeout N` | Seconds per behavioral execution; default 5 in test mode. Build and memory commands have larger minimum time allowances. |

For example:

```bash
ps-benchmark --test --project ../another-project \
    -b ../another-project/push_swap \
    --checker ../another-project/checker \
    --report-dir ./diagnostics --seed 42 --runs 3 --timeout 5
```

### What is tested

- Non-numeric tokens, duplicates, integer overflow, malformed tokens and execution without arguments. Error cases require empty stdout and exactly `Error\n` on stderr; they may exit normally with a nonzero status.
- Sorted inputs and a single value, with zero operations.
- Every permutation of three and five original test values, with respective operation budgets of 5 and 15, plus mixed signs, integer boundaries and a quoted argument list.
- Each strategy on reverse input and on the same 50-element random input. Default/adaptive output is compared on that 50-element input; observed equality is not proof of internal routing, and differing valid output is reported as `INFO`.
- `--bench --simple` on sorted, reverse and partially shuffled inputs. Stdout is simulated; reported disorder, total and all eleven counters are compared with independently computed values. Strategy and complexity labels must be present. Unrecognized text formats receive `SKIP` rather than an automatic pass; recognized incorrect values receive `KO`.
- Random 100- and 500-element inputs. Each run must sort correctly and use fewer than 2000 or 12000 operations respectively. A low average cannot hide a failing individual run.
- Makefile build, `all`, repeated `make` without relinking, `clean`, `fclean`, `re`, observed warning flags, and an optional declared `bonus` target. A source copy is used; existing project build artifacts are not removed by the tester. Source build checks expect the assignment's `push_swap` executable. An explicit `-b` may still select a differently named binary for behavioral tests.
- Norminette when available in `PATH`, on the copied source.
- Valgrind when available in `PATH`, on valid, sorted, empty and invalid inputs, plus checker instruction cases when a checker is present. Full memory logs are retained. Missing tools or an unrecognized tool completion report receive `SKIP`; successful functional cases alone do not establish memory correctness.
- Optional checker handling of invalid arguments and instructions, whitespace errors, correct and incorrect final stacks, all eleven operations and generated instruction sequences.

This is a strict development diagnostic, not an official grade calculator: it reports individual failures rather than applying partial-credit tolerances. The suite includes automated checks only; it does not assess authorship, participation, source restrictions, complexity proofs, explanation quality, the oral defense or live coding. No evaluation sheet is bundled or published with the project.

### Follow a failing case

Each case has an ID such as `T0024`, an explicit expected result and a final `OK`, `KO`, `SKIP` or `INFO` status. The terminal prints one result line per completed case, without a separate start message. Every section also prints a summary. In an interactive terminal, `[ OK ]` is green and `[ KO ]` is red. Colors are disabled for redirected output, `TERM=dumb`, or when `NO_COLOR` is set. Saved reports contain plain text.

Reports are kept outside the tested project by default, under `$XDG_CACHE_HOME/ps-benchmark` or `~/.cache/ps-benchmark`. A fresh `ps-benchmark-tests-XXXXXX` folder is created for every run; its absolute path is printed at startup and completion. A private temporary-directory fallback is used when no home/cache location is available. `--report-dir` overrides the parent location.

For each executed case, that folder contains:

- `T0024.details.txt`: expectation, complete argument list, working directory, exit status or signal, duration, captured-output previews and the diagnosis.
- `T0024.command.sh`: an exact replay command with safely quoted arguments and the saved stdin.
- `T0024.stdin.txt`: the input supplied to the process.
- `T0024.stdout` and `T0024.stderr`: original bytes from both streams.
- `T0024.valgrind.txt`: the memory tool's full trace, for memory cases.

`report.txt` is the overall index. The retained `source-copy` contains the isolated project used for build checks. Unavailable checks have a details file explaining why they were not automatically validated.

On a failure, the terminal prints the details path and a replay command. Use the exact path it prints, for example:

```bash
sh '/absolute/path/to/ps-benchmark-tests-XXXXXX/T0024.command.sh'
```

Invalid operations identify the stdout line and byte offset. An incorrect final state records both complete stacks and the first inversion in A. Budget failures report the measured count and limit. Crashes, timeouts and unexpectedly large output have separate diagnoses. Source file/line locations come from tools such as Norminette or Valgrind when available; build the tested executable with debug symbols (`-g`) for useful memory stack traces. A black-box failure alone cannot identify the faulty C source line.

Captured stdout and stderr are limited to 8 MiB per stream; processes exceeding that limit are stopped. After three consecutive timeouts, further subprocess cases are recorded as `SKIP` with their intended replay command, rather than waiting repeatedly on the same hang. Ctrl+C stops the active process group; already completed case reports remain available after an interruption.

Exit status is `0` when automatic checks have no failures or skips, `1` for a failure or tester infrastructure error, `2` for skipped automatic checks without failures, or `128 + signal` after interruption. A missing optional checker also leaves its coverage explicitly skipped.

## Input generation

### Random mode

Without `--disorder`, the benchmark creates random permutations.

```bash
./ps_benchmark --seed 42
```

A custom `xorshift32` generator is used so that the same seed produces the same input sequence across supported systems.

### Controlled disorder

The `--disorder` option creates inputs with a target percentage of inverted pairs:

```bash
./ps_benchmark --disorder 20
./ps_benchmark --disorder 50
./ps_benchmark --disorder 100
```

Disorder is calculated as:

```text
inverted pairs / total possible pairs × 100
```

This gives the following reference points:

- `0%`: sorted input;
- approximately `50%`: highly mixed input;
- `100%`: reverse-sorted input.

For small lists, not every percentage can be represented exactly. The benchmark uses the closest possible number of inversions and reports the actual measured disorder.

The controlled generator uses randomized inversion sequences instead of always taking the lowest or highest remaining value. This produces more varied inputs while preserving the requested inversion count.

## Output validation

The benchmark does not trust the operation count alone.

It recognizes and simulates all 11 valid operations:

```text
sa sb ss
pa pb
ra rb rr
rra rrb rrr
```

An execution is accepted only when:

- every line contains a valid operation;
- the child process exits normally;
- stack A finishes in ascending order;
- stack B finishes empty.

Invalid output, an incorrect final state, a timeout, or an excessive operation count stops the benchmark with an error.

This internal validation removes the need for an external `checker` executable.

## Statistics

For every size and algorithm, the benchmark reports:

- average operations;
- best result;
- worst result;
- standard deviation;
- measured disorder range.

The average is used for the graph, while the other values show how consistent the algorithm is across different permutations.

A low standard deviation indicates stable behavior. A large difference between the best and worst results suggests that performance depends strongly on the input arrangement.

## Graph scales

The default graph uses a linear vertical scale:

```bash
./ps_benchmark
```

Use logarithmic mode when comparing algorithms with very different operation counts:

```bash
./ps_benchmark --log
```

This is particularly useful when `simple` is included, because its quadratic behavior can otherwise compress the curves of the other strategies.

The logarithmic graph changes only the visualization. It does not change the measured values.

## Terminal size

For an interactive terminal, the benchmark checks the window size before measurement and suggests widening or maximizing it, or reducing the font size, when the full graph will not fit. You can resize the terminal while the benchmark is running; the graph uses the width available when it is printed.

The recommended width depends on the number of measured sizes and available modes. For example, ten sizes with four modes require at least 102 columns in the compact graph.

If the terminal stays too narrow:

- the graph shows a representative subset and reports how many sizes are visible;
- the message gives the minimum width needed to show every size;
- when even one group of bars cannot fit, the graph is omitted and the statistics for the maximum size are still printed;
- below 60 columns, statistics and ratings use a vertical layout;
- a short terminal receives a suggestion to increase its height, use `--height 8`, or scroll.

All configured sizes are still measured. Terminal size affects presentation only. To redraw after the program has finished, widen the terminal and rerun the same command with the displayed `--seed` value.

## Performance ratings

A rating is shown only when the maximum input size is exactly 100 or 500.

### 100 elements

| Average operations | Rating |
|---:|---|
| Fewer than 700 | Excellent |
| Fewer than 1500 | Good |
| Fewer than 2000 | Minimum pass |
| 2000 or more | Below minimum |

### 500 elements

| Average operations | Rating |
|---:|---|
| Fewer than 5500 | Excellent |
| Fewer than 8000 | Good |
| Fewer than 12000 | Minimum pass |
| 12000 or more | Below minimum |

The rating is based on the benchmark average. It is a performance reference, not a replacement for an official project evaluation.

When controlled disorder is used, the benchmark warns that these thresholds are normally intended for random inputs.

## Adaptive strategy estimate

The benchmark cannot inspect which internal strategy your `--adaptive` implementation actually selected. Instead, it reports an **estimated routing** based on the measured disorder and the benchmark's own thresholds:

```text
below 20%        simple
20% to below 50% medium
50% or more      complex
```

The terminal output labels this value as `Estimated adaptive routing`. Treat it as a reference only. If your adaptive implementation uses different thresholds, the estimate will not represent its real internal decision.

## Safety conditions

The benchmark includes protections against accidental heavy executions:

- maximum input size of 50000;
- maximum number of samples and runs;
- combined workload validation;
- stricter limits for the quadratic `simple` strategy;
- timeout for each child process;
- maximum accepted operation count;
- termination of blocked or excessively long processes, plus remaining descendants in the child process group;
- validation that the selected program exists and is executable.

Large values remain expensive even when permitted. Start with 100 or 500 elements before testing larger inputs.

## Important interpretation notes

- More runs produce a more reliable average.
- Use the same seed when comparing code changes.
- Random and controlled-disorder tests answer different questions.
- A lower operation count is useful only when the output is valid.
- The graph shows empirical results, not a formal proof of complexity.
- Radix-based algorithms can produce similar counts for every permutation of the same size.
- The selected sample sizes are representative points, not every integer between 2 and `--max`.

## Repository structure

The standalone repository only needs:

```text
c-push-swap-benchmark/
├── push_swap_benchmark.c
├── install.sh
├── benchmark-preview.png
└── README.md
```

## Author

Developed by **Santiago Silvestri**.
