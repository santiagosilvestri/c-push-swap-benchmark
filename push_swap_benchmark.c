/* ************************************************************************** */
/*                                                                            */
/*                  Portable Push Swap Graph Benchmark                        */
/*                                                                            */
/*   Author: sasilves                                                        */
/*                                                                            */
/*   Copy this file into any push_swap project containing ./push_swap.        */
/*                                                                            */
/*   Compile:                                                                 */
/*     cc -Wall -Wextra -Werror push_swap_benchmark.c -lm -o ps_benchmark     */
/*                                                                            */
/*   Run:                                                                     */
/*     ./ps_benchmark                                                         */
/*     ./ps_benchmark --help                                                  */
/*     ./ps_benchmark --test                                                  */
/*                                                                            */
/* ************************************************************************** */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ALG_COUNT 5
#define ALG_SIMPLE 0
#define ALG_MEDIUM 1
#define ALG_COMPLEX 2
#define ALG_ADAPTIVE 3
#define ALG_DEFAULT 4
#define MASK_ALL 15U
#define PROBE_REJECTED -5
#define EMPTY_OUTPUT -6

#define ROUTE_SIMPLE 0
#define ROUTE_MEDIUM 1
#define ROUTE_COMPLEX 2

#define ADAPTIVE_SIMPLE_LIMIT 20.0
#define ADAPTIVE_MEDIUM_LIMIT 50.0

#define MAX_ELEMENTS 50000
#define MAX_SAMPLES 100
#define MAX_RUNS 100
#define MAX_EXECUTIONS 5000
#define MAX_WORKLOAD 10000000ULL
#define MAX_SIMPLE_ELEMENTS 1000
#define MAX_OPERATIONS 20000000
#define DEFAULT_TIMEOUT 60
#define MAX_TIMEOUT 600
#define PROGRESS_WIDTH 32

#define GREEN "\033[32m"
#define RESET "\033[0m"
#define BOLD "\033[1m"

static volatile sig_atomic_t	g_active_child_pid;
static volatile sig_atomic_t	g_stop_signal;

typedef struct s_config
{
    int             max_n;
    int             samples;
    int             runs;
    int             chart_height;
    int             disorder;
    int             fixed_disorder;
    int             log_scale;
    int             timeout_seconds;
    unsigned int    algorithms;
    unsigned int    seed;
    unsigned int    rng_state;
    const char      *program;
    int             test_mode;
    int             timeout_set;
    int             graph_options_set;
    const char      *project_dir;
    const char      *checker_path;
    const char      *report_dir;
}   t_config;

typedef struct s_result
{
    int     n;
    double  avg[ALG_COUNT];
    double  min[ALG_COUNT];
    double  max[ALG_COUNT];
    double  stddev[ALG_COUNT];
    double  disorder_avg;
    double  disorder_min;
    double  disorder_max;
    int     adaptive_routes[3];
}   t_result;

typedef struct s_progress
{
    int current;
    int total;
    int last_percent;
    int interactive;
}   t_progress;

void        progress_init(t_progress *progress, int total);
void        progress_advance(t_progress *progress);
void        progress_finish(t_progress *progress);
void        progress_abort(t_progress *progress);
int         parse_config(int argc, char **argv, t_config *config);
void        print_usage(const char *program);
int         selected_count(unsigned int mask);
const char  *algorithm_name(int algorithm);
const char  *algorithm_flag(int algorithm);
const char  *algorithm_terminal_color(int algorithm);
int         generate_values(int *values, int size, t_config *config);
double      measure_disorder(int *values, int size);
int         run_push_swap(int algorithm, int *values, int size,
                t_config *config);
void        write_terminal_chart(t_config *config, t_result *results,
                int count);
int         verify_push_swap_output(int fd, pid_t pid, int *values, int size,
                int timeout_seconds, struct timespec *start);

static void	stop_signal_handler(int signal_number)
{
	pid_t	pid;

	g_stop_signal = signal_number;
	pid = (pid_t)g_active_child_pid;
	if (pid > 0)
	{
		kill(-pid, SIGKILL);
		kill(pid, SIGKILL);
	}
	else
		_exit(128 + signal_number);
}

static int	install_stop_signal_handlers(void)
{
	struct sigaction	action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_signal_handler;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) < 0
		|| sigaction(SIGTERM, &action, NULL) < 0
		|| sigaction(SIGHUP, &action, NULL) < 0
		|| sigaction(SIGQUIT, &action, NULL) < 0)
		return (0);
	return (1);
}

static void	fill_stop_signal_set(sigset_t *set)
{
	sigemptyset(set);
	sigaddset(set, SIGINT);
	sigaddset(set, SIGTERM);
	sigaddset(set, SIGHUP);
	sigaddset(set, SIGQUIT);
}

/* ===== Argument parsing and usage ===== */

static int	parse_number(const char *text, long minimum, long maximum,
		long *value)
{
	char	*end;
	long	number;

	errno = 0;
	number = strtol(text, &end, 10);
	if (errno || *text == '\0' || *end != '\0')
		return (0);
	if (number < minimum || number > maximum)
		return (0);
	*value = number;
	return (1);
}

static int	parse_seed(const char *text, unsigned int *seed)
{
	char			*end;
	unsigned long	number;

	if (text[0] == '-')
		return (0);
	errno = 0;
	number = strtoul(text, &end, 10);
	if (errno || *text == '\0' || *end != '\0' || number > UINT_MAX)
		return (0);
	*seed = (unsigned int)number;
	return (1);
}

static int	algorithm_index(const char *name)
{
	if (strcmp(name, "simple") == 0)
		return (ALG_SIMPLE);
	if (strcmp(name, "medium") == 0)
		return (ALG_MEDIUM);
	if (strcmp(name, "complex") == 0)
		return (ALG_COMPLEX);
	if (strcmp(name, "adaptive") == 0)
		return (ALG_ADAPTIVE);
	if (strcmp(name, "default") == 0)
		return (ALG_DEFAULT);
	return (-1);
}

static int	parse_algorithm_words(const char *text, unsigned int *mask,
		int *needs_name)
{
	char	name[32];
	size_t	length;
	int		index;

	if (*text == '\0')
		return (0);
	while (*text)
	{
		if (isspace((unsigned char)*text))
			text++;
		else if (*text == ',')
		{
			if (*needs_name)
				return (0);
			*needs_name = 1;
			text++;
		}
		else
		{
			length = 0;
			while (*text && *text != ',' && !isspace((unsigned char)*text))
			{
				if (length + 1 >= sizeof(name))
					return (0);
				name[length++] = *text++;
			}
			name[length] = '\0';
			index = algorithm_index(name);
			if (strcmp(name, "all") == 0)
				*mask |= MASK_ALL;
			else if (index >= 0)
				*mask |= (1U << index);
			else
				return (0);
			*needs_name = 0;
		}
	}
	return (1);
}

static int	set_algorithms(char **argv, int *i, unsigned int *mask)
{
	unsigned int	selected;
	int				next;
	int				needs_name;

	selected = 0;
	needs_name = 1;
	next = *i + 1;
	while (argv[next] && argv[next][0] != '-')
	{
		if (!parse_algorithm_words(argv[next], &selected, &needs_name))
			return (0);
		next++;
	}
	if (selected == 0 || needs_name)
		return (0);
	*mask = selected;
	*i = next - 1;
	return (1);
}

static int	is_option(const char *arg, const char *short_name,
		const char *long_name)
{
	return (strcmp(arg, short_name) == 0 || strcmp(arg, long_name) == 0);
}

static int	set_integer_option(char **argv, int *i, int *target,
		long minimum, long maximum)
{
	long	value;

	if (!argv[*i + 1] || !parse_number(argv[*i + 1], minimum, maximum,
			&value))
		return (0);
	*target = (int)value;
	(*i)++;
	return (1);
}

static int	set_option(char **argv, int *i, t_config *config)
{
	if (strcmp(argv[*i], "--project") == 0
		|| strcmp(argv[*i], "--checker") == 0
		|| strcmp(argv[*i], "--report-dir") == 0)
	{
		if (!argv[*i + 1] || argv[*i + 1][0] == '\0')
			return (0);
		if (strcmp(argv[*i], "--project") == 0)
			config->project_dir = argv[*i + 1];
		else if (strcmp(argv[*i], "--checker") == 0)
			config->checker_path = argv[*i + 1];
		else
			config->report_dir = argv[*i + 1];
		(*i)++;
		return (1);
	}
	if (is_option(argv[*i], "-b", "--program"))
	{
		if (!argv[*i + 1] || argv[*i + 1][0] == '\0')
			return (0);
		config->program = argv[++(*i)];
		return (1);
	}
	if (is_option(argv[*i], "-m", "--max"))
		return (set_integer_option(argv, i, &config->max_n, 2,
				MAX_ELEMENTS));
	if (is_option(argv[*i], "-p", "--samples"))
		return (set_integer_option(argv, i, &config->samples, 1,
				MAX_SAMPLES));
	if (is_option(argv[*i], "-r", "--runs"))
		return (set_integer_option(argv, i, &config->runs, 1, MAX_RUNS));
	if (is_option(argv[*i], "-H", "--height"))
		return (set_integer_option(argv, i, &config->chart_height, 8, 40));
	if (is_option(argv[*i], "-t", "--timeout"))
	{
		config->timeout_set = 1;
		return (set_integer_option(argv, i, &config->timeout_seconds, 1,
				MAX_TIMEOUT));
	}
	if (is_option(argv[*i], "-d", "--disorder"))
	{
		if (!set_integer_option(argv, i, &config->disorder, 0, 100))
			return (0);
		config->fixed_disorder = 1;
		return (1);
	}
	if (is_option(argv[*i], "-s", "--seed"))
	{
		if (!argv[*i + 1] || !parse_seed(argv[*i + 1], &config->seed))
			return (0);
		(*i)++;
		return (1);
	}
	if (is_option(argv[*i], "-a", "--algorithms"))
		return (set_algorithms(argv, i, &config->algorithms));
	return (0);
}

static int	effective_samples(t_config *config)
{
	int	minimum;
	int	available;

	minimum = 10;
	if (config->max_n < minimum)
		minimum = 2;
	available = config->max_n - minimum + 1;
	if (config->samples < available)
		return (config->samples);
	return (available);
}

static int	validate_large_run(t_config *config,
		unsigned long long executions)
{
	if (config->max_n > 20000 && executions > 10)
		return (fprintf(stderr, "Error: inputs above 20000 are limited to "
				"10 executions.\n"), 0);
	if (config->max_n > 5000 && executions > 50)
		return (fprintf(stderr, "Error: inputs above 5000 are limited to "
				"50 executions.\n"), 0);
	if ((config->algorithms & (1U << ALG_ADAPTIVE))
		&& config->fixed_disorder && config->disorder < 20
		&& config->max_n > MAX_SIMPLE_ELEMENTS)
		return (fprintf(stderr, "Error: adaptive selects simple below 20%% "
				"disorder and is limited to %d elements.\n",
				MAX_SIMPLE_ELEMENTS), 0);
	return (1);
}

static int	validate_workload(t_config *config)
{
	unsigned long long	executions;
	unsigned long long	workload;

	if ((config->algorithms & (1U << ALG_SIMPLE))
		&& config->max_n > MAX_SIMPLE_ELEMENTS)
		return (fprintf(stderr, "Error: simple is limited to %d elements.\n",
				MAX_SIMPLE_ELEMENTS), 0);
	executions = (unsigned long long)effective_samples(config)
		* (unsigned long long)config->runs
		* (unsigned long long)selected_count(config->algorithms);
	if (executions > MAX_EXECUTIONS)
		return (fprintf(stderr, "Error: maximum benchmark executions: %d.\n",
				MAX_EXECUTIONS), 0);
	if (!validate_large_run(config, executions))
		return (0);
	workload = executions * (unsigned long long)config->max_n;
	if (workload > MAX_WORKLOAD)
		return (fprintf(stderr, "Error: benchmark configuration is too large.\n"
				"Reduce --max, --samples, --runs, or algorithms.\n"), 0);
	if (config->max_n > 5000)
		fprintf(stderr, "Warning: large inputs may take a long time.\n");
	return (1);
}

int	parse_config(int argc, char **argv, t_config *config)
{
	int	i;

	*config = (t_config){.max_n = 500, .samples = 10, .runs = 3,
		.chart_height = 18, .timeout_seconds = DEFAULT_TIMEOUT,
		.algorithms = MASK_ALL, .seed = (unsigned int)time(NULL),
		.program = "./push_swap"};
	i = 1;
	while (i < argc)
	{
		if (is_option(argv[i], "-h", "--help"))
			return (2);
		if (is_option(argv[i], "--test", "-test"))
		{
			config->test_mode = 1;
			i++;
			continue ;
		}
		if (is_option(argv[i], "-m", "--max")
			|| is_option(argv[i], "-p", "--samples")
			|| is_option(argv[i], "-a", "--algorithms")
			|| is_option(argv[i], "-d", "--disorder")
			|| is_option(argv[i], "-H", "--height")
			|| is_option(argv[i], "-l", "--log"))
			config->graph_options_set = 1;
		if (is_option(argv[i], "-l", "--log"))
			config->log_scale = 1;
		else if (i + 1 >= argc || !set_option(argv, &i, config))
			return (fprintf(stderr, "Error: invalid option or value: %s\n",
					argv[i]), 0);
		i++;
	}
	config->rng_state = config->seed;
	if (config->rng_state == 0)
		config->rng_state = 0x9e3779b9U;
	if (config->test_mode && config->graph_options_set)
		return (fprintf(stderr, "Error: --test uses fixed test sizes and all "
			"required modes. Use -r, -s, -t, -b, --project, --checker or "
			"--report-dir to customize it.\n"), 0);
	if (!config->test_mode && (config->project_dir || config->checker_path
			|| config->report_dir))
		return (fprintf(stderr, "Error: source, checker and report options "
			"require --test.\n"), 0);
	if (config->test_mode && !config->timeout_set)
		config->timeout_seconds = 5;
	if (!config->test_mode && access(config->program, X_OK) != 0)
		return (fprintf(stderr, "Error: %s was not found or is not "
				"executable.\n", config->program), 0);
	return (1);
}

static void	print_brief_usage(const char *program)
{
	fprintf(stderr, "\nUsage: %s [options]\n", program);
	fprintf(stderr, "  -a LIST   Algorithms to compare (default: all)\n");
	fprintf(stderr, "  -m N      Maximum number of elements (default: 500)\n");
	fprintf(stderr, "  -r N      Runs averaged per size (default: 3)\n");
	fprintf(stderr, "\nExample: %s -a complex, simple -m 100 -r 3\n", program);
	fprintf(stderr, "All options: %s --help (or -h)\n", program);
}

void	print_usage(const char *program)
{
	printf("Usage: %s [options]\n", program);
	printf("      %s --test [-b PATH] [test options]\n", program);
	printf("  -b, --program PATH   Executable path (default: ./push_swap)\n");
	printf("  -m, --max N          Maximum input size, 2-%d (default: 500)\n",
		MAX_ELEMENTS);
	printf("  -p, --samples N      Measured X-axis sizes, 1-%d (default: 10)\n",
		MAX_SAMPLES);
	printf("  -r, --runs N         Runs averaged per size, 1-%d (default: 3)\n",
		MAX_RUNS);
	printf("  -a, --algorithms L   Names separated by commas or spaces\n");
	printf("                       simple,medium,complex,adaptive,default\n");
	printf("                       Any order; spaces around commas are accepted\n");
	printf("                       Example: -a complex, simple, medium\n");
	printf("                       all probes the four flags; skips rejected ones\n");
	printf("                       Falls back to default (no flag) if none remain\n");
	printf("  -d, --disorder P     Exact target disorder percentage, 0-100\n");
	printf("  -l, --log            Use a logarithmic operation scale\n");
	printf("  -H, --height N       Chart height, 8-40 (default: 18)\n");
	printf("  -t, --timeout N      Seconds allowed per execution, 1-%d "
		"(default: %d)\n", MAX_TIMEOUT, DEFAULT_TIMEOUT);
	printf("  -s, --seed N         Reproducible unsigned random seed\n");
	printf("  -h, --help           Show this help\n");
	printf("  --test, -test        Run diagnostic tests instead of the graph\n");
	printf("  --project DIR       Source root for isolated Makefile/Norm checks\n");
	printf("  --checker PATH      Optional checker executable for bonus tests\n");
	printf("  --report-dir DIR    Parent directory for a unique diagnostic report\n");
	printf("  Test mode: -r repeats random cases (at least 3/2 for 100/500);\n");
	printf("             -s reproduces inputs; -t defaults to 5 seconds per case.\n");
}

/* ===== Shared utilities and input generation ===== */

int	selected_count(unsigned int mask)
{
	int	count;
	int	i;

	count = 0;
	i = 0;
	while (i < ALG_COUNT)
	{
		if (mask & (1U << i))
			count++;
		i++;
	}
	return (count);
}

const char	*algorithm_name(int algorithm)
{
	if (algorithm == ALG_SIMPLE)
		return ("simple");
	if (algorithm == ALG_MEDIUM)
		return ("medium");
	if (algorithm == ALG_COMPLEX)
		return ("complex");
	if (algorithm == ALG_DEFAULT)
		return ("default");
	return ("adaptive");
}

const char	*algorithm_flag(int algorithm)
{
	if (algorithm == ALG_SIMPLE)
		return ("--simple");
	if (algorithm == ALG_MEDIUM)
		return ("--medium");
	if (algorithm == ALG_COMPLEX)
		return ("--complex");
	if (algorithm == ALG_DEFAULT)
		return (NULL);
	return ("--adaptive");
}

const char	*algorithm_terminal_color(int algorithm)
{
	if (algorithm == ALG_SIMPLE)
		return ("\033[38;5;203m");
	if (algorithm == ALG_MEDIUM)
		return ("\033[38;5;75m");
	if (algorithm == ALG_COMPLEX)
		return ("\033[38;5;78m");
	if (algorithm == ALG_DEFAULT)
		return ("\033[38;5;220m");
	return ("\033[38;5;177m");
}

static unsigned int	next_random(t_config *config)
{
	unsigned int	x;

	x = config->rng_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	config->rng_state = x;
	return (x);
}

static unsigned int	random_bounded(t_config *config, unsigned int bound)
{
	unsigned int	value;
	unsigned int	limit;

	if (bound <= 1)
		return (0);
	limit = UINT_MAX - (UINT_MAX % bound);
	value = next_random(config);
	while (value >= limit)
		value = next_random(config);
	return (value % bound);
}

static void	shuffle_values(int *values, int size, t_config *config)
{
	int	i;
	int	j;
	int	tmp;

	i = 0;
	while (i < size)
	{
		values[i] = i;
		i++;
	}
	i = size - 1;
	while (i > 0)
	{
		j = (int)random_bounded(config, (unsigned int)i + 1U);
		tmp = values[i];
		values[i] = values[j];
		values[j] = tmp;
		i--;
	}
}

static void	fenwick_add(int *tree, int size, int index, int delta)
{
	index++;
	while (index <= size)
	{
		tree[index] += delta;
		index += index & -index;
	}
}

static int	fenwick_select(int *tree, int size, int rank)
{
	int	index;
	int	step;
	int	next;

	index = 0;
	step = 1;
	while ((step << 1) <= size)
		step <<= 1;
	while (step)
	{
		next = index + step;
		if (next <= size && tree[next] < rank)
		{
			index = next;
			rank -= tree[next];
		}
		step >>= 1;
	}
	return (index);
}

static unsigned long long	max_inversions(int size)
{
	return ((unsigned long long)size * (unsigned long long)(size - 1) / 2);
}

static int	controlled_values(int *values, int size, int disorder,
		t_config *config)
{
	unsigned long long	remaining;
	unsigned long long	max_after;
	unsigned long long	minimum;
	unsigned long long	maximum;
	unsigned long long	choice;
	int					*tree;
	int					position;
	int					capacity;

	tree = calloc((size_t)size + 1, sizeof(int));
	if (!tree)
		return (0);
	position = 0;
	while (position < size)
		fenwick_add(tree, size, position++, 1);
	remaining = (max_inversions(size) * (unsigned long long)disorder + 50) / 100;
	position = 0;
	while (position < size)
	{
		capacity = size - position - 1;
		max_after = max_inversions(capacity);
		minimum = 0;
		if (remaining > max_after)
			minimum = remaining - max_after;
		maximum = remaining;
		if (maximum > (unsigned long long)capacity)
			maximum = (unsigned long long)capacity;
		choice = minimum + random_bounded(config,
				(unsigned int)(maximum - minimum + 1));
		values[position] = fenwick_select(tree, size, (int)choice + 1);
		fenwick_add(tree, size, values[position], -1);
		remaining -= choice;
		position++;
	}
	free(tree);
	return (1);
}

int	generate_values(int *values, int size, t_config *config)
{
	if (config->fixed_disorder)
		return (controlled_values(values, size, config->disorder, config));
	shuffle_values(values, size, config);
	return (1);
}

static unsigned long long	merge_count(int *values, int *copy,
		int left, int middle, int right)
{
	unsigned long long	count;
	int					i;
	int					j;
	int					k;

	count = 0;
	i = left;
	j = middle;
	k = left;
	while (i < middle && j < right)
	{
		if (values[i] <= values[j])
			copy[k++] = values[i++];
		else
		{
			copy[k++] = values[j++];
			count += (unsigned long long)(middle - i);
		}
	}
	while (i < middle)
		copy[k++] = values[i++];
	while (j < right)
		copy[k++] = values[j++];
	while (left < right)
	{
		values[left] = copy[left];
		left++;
	}
	return (count);
}

static unsigned long long	count_inversions(int *values, int *copy,
		int left, int right)
{
	unsigned long long	count;
	int					middle;

	if (right - left < 2)
		return (0);
	middle = left + (right - left) / 2;
	count = count_inversions(values, copy, left, middle);
	count += count_inversions(values, copy, middle, right);
	count += merge_count(values, copy, left, middle, right);
	return (count);
}

double	measure_disorder(int *values, int size)
{
	unsigned long long	inversions;
	unsigned long long	maximum;
	int					*copy;
	int					*work;
	int					i;

	if (size < 2)
		return (0.0);
	copy = malloc(sizeof(int) * (size_t)size);
	work = malloc(sizeof(int) * (size_t)size);
	if (!copy || !work)
		return (free(copy), free(work), -1.0);
	i = 0;
	while (i < size)
	{
		work[i] = values[i];
		i++;
	}
	inversions = count_inversions(work, copy, 0, size);
	maximum = max_inversions(size);
	free(copy);
	free(work);
	return ((double)inversions * 100.0 / (double)maximum);
}

/* ===== Shared operation simulator and output validation ===== */

typedef struct s_sim_stack
{
	int	*data;
	int	capacity;
	int	head;
	int	size;
}t_sim_stack;

typedef struct s_verifier
{
	t_sim_stack	a;
	t_sim_stack	b;
	char		line[4];
	int			line_length;
	int			operations;
}t_verifier;

static int	stack_index(t_sim_stack *stack, int offset)
{
	return ((stack->head + offset) % stack->capacity);
}

static void	swap_top(t_sim_stack *stack)
{
	int	first;
	int	second;
	int	tmp;

	if (stack->size < 2)
		return ;
	first = stack_index(stack, 0);
	second = stack_index(stack, 1);
	tmp = stack->data[first];
	stack->data[first] = stack->data[second];
	stack->data[second] = tmp;
}

static int	pop_top(t_sim_stack *stack)
{
	int	value;

	value = stack->data[stack->head];
	stack->head = (stack->head + 1) % stack->capacity;
	stack->size--;
	return (value);
}

static void	push_top(t_sim_stack *stack, int value)
{
	stack->head = (stack->head - 1 + stack->capacity) % stack->capacity;
	stack->data[stack->head] = value;
	stack->size++;
}

static void	push_stack(t_sim_stack *source, t_sim_stack *target)
{
	if (source->size > 0)
		push_top(target, pop_top(source));
}

static void	rotate_stack(t_sim_stack *stack)
{
	int	value;
	int	bottom;

	if (stack->size < 2)
		return ;
	value = stack->data[stack->head];
	stack->head = (stack->head + 1) % stack->capacity;
	bottom = stack_index(stack, stack->size - 1);
	stack->data[bottom] = value;
}

static void	reverse_rotate_stack(t_sim_stack *stack)
{
	int	value;
	int	bottom;

	if (stack->size < 2)
		return ;
	bottom = stack_index(stack, stack->size - 1);
	value = stack->data[bottom];
	stack->head = (stack->head - 1 + stack->capacity) % stack->capacity;
	stack->data[stack->head] = value;
}

static int	apply_operation(t_verifier *verifier, const char *operation)
{
	if (strcmp(operation, "sa") == 0 || strcmp(operation, "ss") == 0)
		swap_top(&verifier->a);
	if (strcmp(operation, "sb") == 0 || strcmp(operation, "ss") == 0)
		swap_top(&verifier->b);
	if (strcmp(operation, "pa") == 0)
		push_stack(&verifier->b, &verifier->a);
	else if (strcmp(operation, "pb") == 0)
		push_stack(&verifier->a, &verifier->b);
	else if (strcmp(operation, "ra") == 0 || strcmp(operation, "rr") == 0)
		rotate_stack(&verifier->a);
	if (strcmp(operation, "rb") == 0 || strcmp(operation, "rr") == 0)
		rotate_stack(&verifier->b);
	if (strcmp(operation, "rra") == 0 || strcmp(operation, "rrr") == 0)
		reverse_rotate_stack(&verifier->a);
	if (strcmp(operation, "rrb") == 0 || strcmp(operation, "rrr") == 0)
		reverse_rotate_stack(&verifier->b);
	return (strcmp(operation, "sa") == 0 || strcmp(operation, "sb") == 0
		|| strcmp(operation, "ss") == 0 || strcmp(operation, "pa") == 0
		|| strcmp(operation, "pb") == 0 || strcmp(operation, "ra") == 0
		|| strcmp(operation, "rb") == 0 || strcmp(operation, "rr") == 0
		|| strcmp(operation, "rra") == 0 || strcmp(operation, "rrb") == 0
		|| strcmp(operation, "rrr") == 0);
}

static int	verifier_init(t_verifier *verifier, int *values, int size)
{
	int	i;

	memset(verifier, 0, sizeof(*verifier));
	verifier->a.data = malloc(sizeof(int) * (size_t)size);
	verifier->b.data = malloc(sizeof(int) * (size_t)size);
	if (!verifier->a.data || !verifier->b.data)
		return (free(verifier->a.data), free(verifier->b.data), 0);
	verifier->a.capacity = size;
	verifier->b.capacity = size;
	verifier->a.size = size;
	i = 0;
	while (i < size)
	{
		verifier->a.data[i] = values[i];
		i++;
	}
	return (1);
}

static int	feed_character(t_verifier *verifier, char character)
{
	if (character == '\0')
		return (0);
	if (character != '\n')
	{
		if (verifier->line_length >= 3)
			return (0);
		verifier->line[verifier->line_length++] = character;
		return (1);
	}
	if (verifier->line_length == 0)
		return (0);
	verifier->line[verifier->line_length] = '\0';
	if (!apply_operation(verifier, verifier->line))
		return (0);
	verifier->line_length = 0;
	verifier->operations++;
	return (verifier->operations <= MAX_OPERATIONS);
}

static int	feed_buffer(t_verifier *verifier, char *buffer, ssize_t length)
{
	ssize_t	i;

	i = 0;
	while (i < length)
	{
		if (!feed_character(verifier, buffer[i]))
			return (0);
		i++;
	}
	return (1);
}

static int	verifier_is_sorted(t_verifier *verifier)
{
	int	i;

	if (verifier->b.size != 0 || verifier->a.size != verifier->a.capacity)
		return (0);
	i = 1;
	while (i < verifier->a.size)
	{
		if (verifier->a.data[stack_index(&verifier->a, i - 1)]
			> verifier->a.data[stack_index(&verifier->a, i)])
			return (0);
		i++;
	}
	return (1);
}

static long long	elapsed_milliseconds(struct timespec *start)
{
	struct timespec	now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return ((now.tv_sec - start->tv_sec) * 1000LL
		+ (now.tv_nsec - start->tv_nsec) / 1000000LL);
}

static void	kill_child_processes(pid_t pid)
{
	if (pid <= 0)
		return ;
	kill(-pid, SIGKILL);
	kill(pid, SIGKILL);
}

static int	read_output(int fd, pid_t pid, t_verifier *verifier,
		int timeout_seconds, struct timespec *start)
{
	struct pollfd	pollfd;
	char			buffer[4096];
	ssize_t			bytes;
	int				status;

	pollfd.fd = fd;
	pollfd.events = POLLIN | POLLHUP;
	while (1)
	{
		if (elapsed_milliseconds(start) > timeout_seconds * 1000LL)
			return (kill_child_processes(pid), -2);
		status = poll(&pollfd, 1, 100);
		if (status < 0 && errno != EINTR)
			return (kill_child_processes(pid), -1);
		if (status <= 0)
			continue ;
		bytes = read(fd, buffer, sizeof(buffer));
		if (bytes == 0)
			break ;
		if (bytes < 0 && errno != EINTR)
			return (kill_child_processes(pid), -1);
		if (bytes > 0 && !feed_buffer(verifier, buffer, bytes))
			return (kill_child_processes(pid), -3);
	}
	return (0);
}

static int	wait_for_child(pid_t pid, int *status, int timeout_seconds,
		struct timespec *start)
{
	pid_t	result;
	struct timespec	pause;
	sigset_t	stop_set;
	sigset_t	previous_mask;
	int		mask_status;

	pause.tv_sec = 0;
	pause.tv_nsec = 10000000L;
	fill_stop_signal_set(&stop_set);
	while (1)
	{
		mask_status = sigprocmask(SIG_BLOCK, &stop_set, &previous_mask);
		if (mask_status < 0)
			return (kill_child_processes(pid), 0);
		result = waitpid(pid, status, WNOHANG);
		if (result == pid)
		{
			/* A wrapper may leave descendants after closing its output. */
			kill(-pid, SIGKILL);
			g_active_child_pid = 0;
			if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) < 0)
				return (0);
			return (1);
		}
		if (result < 0 && errno != EINTR)
		{
			g_active_child_pid = 0;
			sigprocmask(SIG_SETMASK, &previous_mask, NULL);
			return (0);
		}
		if (elapsed_milliseconds(start) > timeout_seconds * 1000LL)
		{
			kill_child_processes(pid);
			do
				result = waitpid(pid, status, 0);
			while (result < 0 && errno == EINTR);
			g_active_child_pid = 0;
			sigprocmask(SIG_SETMASK, &previous_mask, NULL);
			return (-2);
		}
		if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) < 0)
			return (kill_child_processes(pid), 0);
		nanosleep(&pause, NULL);
	}
}

int	verify_push_swap_output(int fd, pid_t pid, int *values, int size,
		int timeout_seconds, struct timespec *start)
{
	t_verifier	verifier;
	int			status;
	int			operations;

	if (!verifier_init(&verifier, values, size))
		return (kill_child_processes(pid), -1);
	status = read_output(fd, pid, &verifier, timeout_seconds, start);
	operations = verifier.operations;
	if (status == 0 && verifier.line_length != 0)
		status = -3;
	if (status == 0 && !verifier_is_sorted(&verifier))
		status = (operations == 0 ? EMPTY_OUTPUT : -4);
	free(verifier.a.data);
	free(verifier.b.data);
	if (status < 0)
		return (status);
	return (operations);
}

/* ===== Graph mode: child execution and strategy detection ===== */

static char	**build_arguments(int algorithm, int *values, int size,
		char **storage, const char *program)
{
	char	**args;
	int		i;
	int		offset;

	args = malloc(sizeof(*args) * ((size_t)size + 3));
	*storage = malloc((size_t)size * 16);
	if (!args || !*storage)
		return (free(args), free(*storage), NULL);
	args[0] = (char *)program;
	offset = 1;
	if (algorithm_flag(algorithm) != NULL)
		args[offset++] = (char *)algorithm_flag(algorithm);
	i = 0;
	while (i < size)
	{
		snprintf(*storage + (i * 16), 16, "%d", values[i]);
		args[i + offset] = *storage + (i * 16);
		i++;
	}
	args[size + offset] = NULL;
	return (args);
}

static void	run_child(int pipefd[2], char **args)
{
	int	null_fd;

	close(pipefd[0]);
	if (dup2(pipefd[1], STDOUT_FILENO) < 0)
		_exit(127);
	close(pipefd[1]);
	null_fd = open("/dev/null", O_WRONLY);
	if (null_fd >= 0)
	{
		dup2(null_fd, STDERR_FILENO);
		close(null_fd);
	}
	execv(args[0], args);
	_exit(127);
}

static void	print_verification_error(int code, int algorithm, int size,
		int timeout_seconds)
{
	fprintf(stderr, "\nError: %s failed for %d elements: ",
		algorithm_name(algorithm), size);
	if (code == -2)
		fprintf(stderr, "execution exceeded %d seconds.\n", timeout_seconds);
	else if (code == -3)
		fprintf(stderr, "invalid output or more than %d operations.\n",
			MAX_OPERATIONS);
	else if (code == -4)
		fprintf(stderr, "operations did not sort the input.\n");
	else if (code == EMPTY_OUTPUT)
		fprintf(stderr, "no operations for an unsorted input.\n");
	else
		fprintf(stderr, "could not read or validate the output.\n");
}

static void	print_child_error(int status, int algorithm, int size)
{
	fprintf(stderr, "\nError: %s failed for %d elements: ",
		algorithm_name(algorithm), size);
	if (WIFEXITED(status))
		fprintf(stderr, "process exited with status %d.\n", WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		fprintf(stderr, "process terminated by signal %d.\n", WTERMSIG(status));
	else
		fprintf(stderr, "process did not terminate normally.\n");
}

static int	execute_push_swap(int algorithm, int *values, int size,
		t_config *config, int probe)
{
	char	**args;
	char	*storage;
	int		pipefd[2];
	pid_t	pid;
	struct timespec	start;
	sigset_t	stop_set;
	sigset_t	previous_mask;
	int		status;
	int		wait_status;
	int		operations;

	storage = NULL;
	args = build_arguments(algorithm, values, size, &storage, config->program);
	if (!args)
		return (-1);
	if (pipe(pipefd) < 0)
		return (free(args), free(storage), -1);
	fill_stop_signal_set(&stop_set);
	if (sigprocmask(SIG_BLOCK, &stop_set, &previous_mask) < 0)
		return (close(pipefd[0]), close(pipefd[1]), free(args),
			free(storage), -1);
	if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
		return (sigprocmask(SIG_SETMASK, &previous_mask, NULL),
			close(pipefd[0]), close(pipefd[1]), free(args), free(storage), -1);
	pid = fork();
	if (pid < 0)
		return (sigprocmask(SIG_SETMASK, &previous_mask, NULL),
			close(pipefd[0]), close(pipefd[1]), free(args), free(storage), -1);
	if (pid == 0)
	{
		if (setpgid(0, 0) < 0)
			_exit(126);
		sigprocmask(SIG_SETMASK, &previous_mask, NULL);
		run_child(pipefd, args);
	}
	if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH)
	{
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		sigprocmask(SIG_SETMASK, &previous_mask, NULL);
		return (close(pipefd[0]), close(pipefd[1]), free(args),
			free(storage), -1);
	}
	g_active_child_pid = (sig_atomic_t)pid;
	if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) < 0)
	{
		kill_child_processes(pid);
		waitpid(pid, NULL, 0);
		g_active_child_pid = 0;
		return (close(pipefd[0]), close(pipefd[1]), free(args),
			free(storage), -1);
	}
	close(pipefd[1]);
	operations = verify_push_swap_output(pipefd[0], pid, values, size,
			config->timeout_seconds, &start);
	close(pipefd[0]);
	wait_status = wait_for_child(pid, &status, config->timeout_seconds, &start);
	if (wait_status == -2)
		operations = -2;
	else if (wait_status == 0)
		operations = -1;
	free(args);
	free(storage);
	if (g_stop_signal != 0)
		return (-1);
	/* A rejected flag normally exits nonzero without printing operations.
	 * Never turn crashes, timeouts or incorrect operations into missing flags. */
	if (probe && algorithm != ALG_DEFAULT && operations == EMPTY_OUTPUT
		&& WIFEXITED(status) && WEXITSTATUS(status) > 0
		&& WEXITSTATUS(status) < 126)
	{
		fprintf(stderr, "Warning: %s rejected a valid probe (exit %d); "
			"skipped. The flag may be unsupported or its input handling "
			"may be faulty.\n", algorithm_flag(algorithm), WEXITSTATUS(status));
		return (PROBE_REJECTED);
	}
	if (operations == -1 || operations == -2 || operations == -3)
		return (print_verification_error(operations, algorithm, size,
				config->timeout_seconds), -1);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return (print_child_error(status, algorithm, size), -1);
	if (operations == -4 || operations == EMPTY_OUTPUT)
		return (print_verification_error(operations, algorithm, size,
				config->timeout_seconds), -1);
	return (operations);
}

int	run_push_swap(int algorithm, int *values, int size, t_config *config)
{
	return (execute_push_swap(algorithm, values, size, config, 0));
}

/* Probe fixed unsorted inputs without consuming the benchmark RNG sequence.
 * These executions validate compatibility; they are not measured samples. */
static int	probe_algorithm(int algorithm, t_config *config)
{
	int	values[2][5] = {{2, 0, 4, 1, 3}, {4, 3, 2, 1, 0}};
	int	i;
	int	operations;

	i = 0;
	while (i < 2)
	{
		/* Once a flag has succeeded, a later rejection is a real failure. */
		operations = execute_push_swap(algorithm, values[i], 5, config, i == 0);
		if (operations < 0)
			return (operations);
		i++;
	}
	return (0);
}

static int	detect_algorithms(t_config *config)
{
	unsigned int	available;
	int				algorithm;
	int				status;

	available = 0;
	algorithm = 0;
	while (algorithm < ALG_COUNT)
	{
		if (config->algorithms & (1U << algorithm))
		{
			status = probe_algorithm(algorithm, config);
			if (status == 0)
				available |= (1U << algorithm);
			else if (status != PROBE_REJECTED)
				return (fprintf(stderr, "Compatibility check failed; "
						"this is not treated as a missing flag.\n"), 0);
		}
		algorithm++;
	}
	if (available == 0)
	{
		fprintf(stderr, "No selected flag passed the probe; trying default "
			"execution without flags.\n");
		if (probe_algorithm(ALG_DEFAULT, config) < 0)
			return (0);
		available = (1U << ALG_DEFAULT);
	}
	config->algorithms = available;
	return (1);
}

/* ===== Graph mode: progress display ===== */

static void	print_progress_bar(t_progress *progress, int percent)
{
	struct winsize	window;
	const char		*label;
	char			suffix[64];
	char			compact[64];
	int			bar_width;
	int			columns;
	int			max_width;
	int	filled;
	int	i;

	columns = 0;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0)
		columns = window.ws_col;
	max_width = columns - 1;
	if (columns <= 0)
		max_width = INT_MAX;
	label = "Portable Push Swap Graph Benchmark by sasilves ";
	snprintf(suffix, sizeof(suffix), "] %3d%% (%d/%d)", percent,
		progress->current, progress->total);
	bar_width = PROGRESS_WIDTH;
	if (max_width != INT_MAX)
		bar_width = max_width - (int)strlen(label) - (int)strlen(suffix) - 1;
	if (bar_width < 8)
	{
		label = "PS benchmark ";
		bar_width = max_width - (int)strlen(label) - (int)strlen(suffix) - 1;
	}
	printf("\r\033[2K");
	if (bar_width < 1)
	{
		snprintf(compact, sizeof(compact), "%3d%% (%d/%d)", percent,
			progress->current, progress->total);
		if ((int)strlen(compact) <= max_width)
			printf("%s", compact);
		else if (max_width >= 4)
			printf("%3d%%", percent);
		else if (max_width >= 3)
			printf("%3d", percent);
		fflush(stdout);
		return ;
	}
	if (bar_width > PROGRESS_WIDTH)
		bar_width = PROGRESS_WIDTH;
	filled = percent * bar_width / 100;
	printf("%s[", label);
	i = 0;
	while (i < bar_width)
	{
		if (i < filled)
			printf("%s█%s", GREEN, RESET);
		else
			printf("░");
		i++;
	}
	printf("%s", suffix);
	fflush(stdout);
}

static void	progress_update(t_progress *progress)
{
	int	percent;

	if (progress->total <= 0)
		percent = 100;
	else
		percent = progress->current * 100 / progress->total;
	if (percent == progress->last_percent)
		return ;
	progress->last_percent = percent;
	if (progress->interactive)
		print_progress_bar(progress, percent);
}

void	progress_init(t_progress *progress, int total)
{
	progress->current = 0;
	progress->total = total;
	progress->last_percent = -1;
	progress->interactive = isatty(STDOUT_FILENO);
	progress_update(progress);
}

void	progress_advance(t_progress *progress)
{
	if (progress->current < progress->total)
		progress->current++;
	progress_update(progress);
}

void	progress_finish(t_progress *progress)
{
	progress->current = progress->total;
	progress_update(progress);
	if (progress->interactive)
		printf("\n");
	else
		printf("Portable Push Swap Graph Benchmark by sasilves: 100%% (%d/%d)\n",
			progress->total, progress->total);
	fflush(stdout);
}

void	progress_abort(t_progress *progress)
{
	if (progress->interactive)
		printf("\n");
	else
		printf("Portable Push Swap Graph Benchmark by sasilves: stopped (%d/%d)\n",
			progress->current, progress->total);
	fflush(stdout);
}

/* ===== Graph mode: terminal chart and statistics display ===== */

static int	terminal_width(void)
{
	struct winsize	window;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0
		&& window.ws_col > 0)
		return ((int)window.ws_col);
	return (120);
}

static int	compact_group_width(t_config *config)
{
	char	label[32];
	int		width;
	int		label_width;

	width = selected_count(config->algorithms) * 2 + 1;
	label_width = snprintf(label, sizeof(label), "%d", config->max_n);
	if (label_width > width)
		width = label_width;
	return (width);
}

static void	print_terminal_hint(t_config *config, int count)
{
	struct winsize	window;
	int				needed;

	if (!isatty(STDOUT_FILENO)
		|| ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) != 0)
		return ;
	needed = 12 + count * compact_group_width(config);
	if (window.ws_col > 0 && window.ws_col < needed)
	{
		printf("Terminal: %u columns; full chart needs %d.\n",
			(unsigned int)window.ws_col, needed);
		printf("Widen/maximize the terminal or reduce the font size\n"
			"before the benchmark finishes to see every measured size.\n\n");
	}
	if (window.ws_row > 0 && window.ws_row < config->chart_height + 8)
		printf("Tip: increase the terminal height, use --height 8,\n"
			"or scroll to view the complete chart.\n\n");
}

static double	scale_value(t_config *config, double value)
{
	if (config->log_scale)
		return (log10(value + 1.0));
	return (value);
}

static double	unscale_value(t_config *config, double value)
{
	if (config->log_scale)
		return (pow(10.0, value) - 1.0);
	return (value);
}

static double	max_chart_value(t_config *config, t_result *results, int count)
{
	double	maximum;
	int		i;
	int		algorithm;

	maximum = 1.0;
	i = 0;
	while (i < count)
	{
		algorithm = 0;
		while (algorithm < ALG_COUNT)
		{
			if ((config->algorithms & (1U << algorithm))
				&& scale_value(config, results[i].avg[algorithm]) > maximum)
				maximum = scale_value(config, results[i].avg[algorithm]);
			algorithm++;
		}
		i++;
	}
	return (maximum);
}

static double	nice_step(double value)
{
	double	power;
	double	fraction;

	power = 1.0;
	while (value >= 10.0)
	{
		value /= 10.0;
		power *= 10.0;
	}
	while (value > 0.0 && value < 1.0)
	{
		value *= 10.0;
		power /= 10.0;
	}
	fraction = 1.0;
	if (value > 5.0)
		fraction = 10.0;
	else if (value > 2.0)
		fraction = 5.0;
	else if (value > 1.0)
		fraction = 2.0;
	return (fraction * power);
}

static double	chart_maximum(t_config *config, t_result *results, int count)
{
	double	maximum;
	double	step;

	maximum = max_chart_value(config, results, count);
	if (config->log_scale)
		return (ceil(maximum * 5.0) / 5.0);
	step = nice_step(maximum / 5.0);
	return (ceil(maximum / step) * step);
}

static int	visible_groups(t_config *config, int count, int *bar_width,
		int *group_width)
{
	int	available;
	int	selected;
	int	groups;
	int	label_width;
	char	label[32];

	available = terminal_width() - 12;
	selected = selected_count(config->algorithms);
	label_width = snprintf(label, sizeof(label), "%d", config->max_n);
	*bar_width = 2;
	*group_width = selected * (*bar_width + 1) + 1;
	if (*group_width < label_width)
		*group_width = label_width;
	if (*group_width * count > available)
	{
		*bar_width = 1;
		*group_width = compact_group_width(config);
	}
	groups = available / *group_width;
	if (groups < 0)
		groups = 0;
	if (groups > count)
		groups = count;
	return (groups);
}

static int	result_index(int shown_index, int shown_count, int total_count)
{
	if (shown_count <= 1)
		return (total_count - 1);
	return ((shown_index * (total_count - 1)) / (shown_count - 1));
}

static int	grid_tick(int row, int height)
{
	int	i;
	int	tick_row;

	i = 0;
	while (i < 5)
	{
		tick_row = (i * height) / 5;
		if (row == tick_row)
			return (1);
		i++;
	}
	return (0);
}

static const char	*bar_cell(double value, double maximum, int height,
		int row)
{
	static const char	*blocks[9] = {
		" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"
	};
	double				scaled;
	double				bottom;
	int					level;

	scaled = value * height / maximum;
	bottom = height - row - 1;
	if (scaled >= bottom + 1.0)
		return (blocks[8]);
	if (scaled <= bottom)
		return (blocks[0]);
	level = (int)ceil((scaled - bottom) * 8.0);
	if (level < 1)
		level = 1;
	if (level > 8)
		level = 8;
	return (blocks[level]);
}

static void	print_bar_cell(const char *cell, int algorithm, int width,
		int color, int grid)
{
	int	i;

	i = 0;
	while (i < width)
	{
		if (strcmp(cell, " ") != 0)
		{
			if (color)
				printf("%s", algorithm_terminal_color(algorithm));
			printf("%s", cell);
			if (color)
				printf("%s", RESET);
		}
		else if (grid)
			printf("─");
		else
			printf(" ");
		i++;
	}
}

static void	print_chart_row(t_config *config, t_result *results, int count,
		int shown, int bar_width, int group_width, double maximum, int row)
{
	int	grid;
	int	i;
	int	algorithm;
	int	index;
	int	color;
	int	padding;
	int	used_width;

	grid = grid_tick(row, config->chart_height);
	if (grid)
		printf("%8.0f ┤", unscale_value(config, maximum
				* (config->chart_height - row) / config->chart_height));
	else
		printf("         │");
	color = isatty(STDOUT_FILENO);
	i = 0;
	while (i < shown)
	{
		index = result_index(i, shown, count);
		algorithm = 0;
		while (algorithm < ALG_COUNT)
		{
			if (config->algorithms & (1U << algorithm))
			{
				print_bar_cell(bar_cell(scale_value(config,
						results[index].avg[algorithm]), maximum,
						config->chart_height, row), algorithm, bar_width,
					color, grid);
				printf(grid ? "─" : " ");
			}
			algorithm++;
		}
		printf(grid ? "─" : " ");
		used_width = selected_count(config->algorithms) * (bar_width + 1) + 1;
		padding = group_width - used_width;
		while (padding-- > 0)
			printf(grid ? "─" : " ");
		i++;
	}
	printf("\n");
}

static void	print_centered(const char *text, int width)
{
	int	length;
	int	left;
	int	right;

	length = (int)strlen(text);
	if (length > width)
		length = width;
	left = (width - length) / 2;
	right = width - length - left;
	printf("%*s%.*s%*s", left, "", length, text, right, "");
}

static void	print_x_axis(t_result *results, int count, int shown,
		int group_width)
{
	char	label[32];
	int		i;
	int		index;

	printf("       0 ┼");
	i = 0;
	while (i < shown * group_width)
	{
		printf("─");
		i++;
	}
	printf("\n          ");
	i = 0;
	while (i < shown)
	{
		index = result_index(i, shown, count);
		snprintf(label, sizeof(label), "%d", results[index].n);
		print_centered(label, group_width);
		i++;
	}
	printf("\n%*sNumber of elements (n)\n", 10, "");
}

static void	print_legend(t_config *config)
{
	int	algorithm;
	int	color;

	color = isatty(STDOUT_FILENO);
	printf("Legend: ");
	algorithm = 0;
	while (algorithm < ALG_COUNT)
	{
		if (config->algorithms & (1U << algorithm))
		{
			if (color)
				printf("%s", algorithm_terminal_color(algorithm));
			printf("██");
			if (color)
				printf("%s", RESET);
			printf(" %s  ", algorithm_name(algorithm));
		}
		algorithm++;
	}
	printf("\n");
}

static const char	*performance_rating(double operations, int size)
{
	if (size == 100)
	{
		if (operations < 700.0)
			return ("Excellent");
		if (operations < 1500.0)
			return ("Good");
		if (operations < 2000.0)
			return ("Minimum pass");
	}
	else
	{
		if (operations < 5500.0)
			return ("Excellent");
		if (operations < 8000.0)
			return ("Good");
		if (operations < 12000.0)
			return ("Minimum pass");
	}
	return ("Below minimum");
}

static void	print_maximum_summary(t_config *config, t_result *result)
{
	int	algorithm;

	printf("\nStatistics for %d elements\n\n", result->n);
	if (terminal_width() >= 60)
		printf("%-12s %11s %11s %11s %11s\n", "Algorithm", "Average",
			"Best", "Worst", "Std. dev.");
	algorithm = 0;
	while (algorithm < ALG_COUNT)
	{
		if (config->algorithms & (1U << algorithm))
		{
			if (terminal_width() < 60)
				printf("%s\n  Average: %.1f\n  Best: %.0f\n"
					"  Worst: %.0f\n  Std. dev.: %.1f\n",
					algorithm_name(algorithm), result->avg[algorithm],
					result->min[algorithm], result->max[algorithm],
					result->stddev[algorithm]);
			else
				printf("%-12s %11.1f %11.0f %11.0f %11.1f\n",
				algorithm_name(algorithm), result->avg[algorithm],
				result->min[algorithm], result->max[algorithm],
				result->stddev[algorithm]);
		}
		algorithm++;
	}
	printf("\nMeasured disorder\n  Average: %.2f%%\n  Minimum: %.2f%%\n"
		"  Maximum: %.2f%%\n", result->disorder_avg, result->disorder_min,
		result->disorder_max);
}

static void	print_adaptive_routes(t_config *config, t_result *result)
{
	if (!(config->algorithms & (1U << ALG_ADAPTIVE)))
		return ;
	printf("Estimated adaptive routing at %d elements: simple %d, medium %d, "
		"complex %d\n", result->n,
		result->adaptive_routes[ROUTE_SIMPLE],
		result->adaptive_routes[ROUTE_MEDIUM],
		result->adaptive_routes[ROUTE_COMPLEX]);
}

static void	print_performance_rating(t_config *config, t_result *result)
{
	int	algorithm;

	if (config->max_n != 100 && config->max_n != 500)
		return ;
	printf("\nPerformance rating for %d elements\n\n", config->max_n);
	if (terminal_width() >= 60)
		printf("%-12s %18s  %s\n", "Algorithm", "Average operations", "Rating");
	algorithm = 0;
	while (algorithm < ALG_COUNT)
	{
		if (config->algorithms & (1U << algorithm))
		{
			if (terminal_width() < 60)
				printf("%s\n  Average operations: %.1f\n  Rating: %s\n",
					algorithm_name(algorithm), result->avg[algorithm],
					performance_rating(result->avg[algorithm], config->max_n));
			else
				printf("%-12s %18.1f  %s\n", algorithm_name(algorithm),
					result->avg[algorithm], performance_rating(
						result->avg[algorithm], config->max_n));
		}
		algorithm++;
	}
	printf("\nRating based on average operations measured by this benchmark.\n");
	if (config->runs < 10)
		printf("Statistical note: use --runs 10 or more for a more stable "
			"rating.\n");
	if (config->fixed_disorder)
		printf("Note: official thresholds are intended for random inputs; "
			"this run used a %d%% disorder target.\n", config->disorder);
}

void	write_terminal_chart(t_config *config, t_result *results, int count)
{
	double	maximum;
	int		bar_width;
	int		group_width;
	int		shown;
	int		row;
	int		color;

	maximum = chart_maximum(config, results, count);
	shown = visible_groups(config, count, &bar_width, &group_width);
	color = isatty(STDOUT_FILENO);
	printf("\n");
	if (shown == 0)
	{
		printf("Chart omitted: terminal too narrow (%d columns).\n",
			terminal_width());
		printf("Widen/maximize it to at least %d columns for the chart,\n",
			12 + compact_group_width(config));
		printf("or %d columns to show all %d measured sizes.\n",
			12 + count * compact_group_width(config), count);
		printf("Then rerun the same command with --seed %u.\n", config->seed);
		print_maximum_summary(config, &results[count - 1]);
		print_adaptive_routes(config, &results[count - 1]);
		print_performance_rating(config, &results[count - 1]);
		return ;
	}
	if (color)
		printf("%s", BOLD);
	printf("Algorithm comparison");
	if (color)
		printf("%s", RESET);
	printf("\n%s average operations — %d runs per size — seed %u\n\n",
		config->log_scale ? "Logarithmic" : "Linear", config->runs,
		config->seed);
	row = 0;
	while (row < config->chart_height)
	{
		print_chart_row(config, results, count, shown, bar_width, group_width,
			maximum, row);
		row++;
	}
	print_x_axis(results, count, shown, group_width);
	print_legend(config);
	if (shown < count)
	{
		printf("Note: terminal width shows %d of %d measured sizes.\n",
			shown, count);
		printf("Widen/maximize it to at least %d columns to show them all.\n",
			12 + count * compact_group_width(config));
		printf("Then rerun the same command with --seed %u.\n", config->seed);
	}
	print_maximum_summary(config, &results[count - 1]);
	print_adaptive_routes(config, &results[count - 1]);
	print_performance_rating(config, &results[count - 1]);
}

/* ===== Graph mode: measurements and statistics ===== */

static int	result_count(t_config *config)
{
	int	minimum;
	int	available;

	minimum = 10;
	if (config->max_n < minimum)
		minimum = 2;
	available = config->max_n - minimum + 1;
	if (config->samples > available)
		return (available);
	return (config->samples);
}

static void	set_sizes(t_config *config, t_result *results, int count)
{
	int	minimum;
	int	i;

	minimum = 10;
	if (config->max_n < minimum)
		minimum = 2;
	i = 0;
	while (i < count)
	{
		if (count == 1)
			results[i].n = config->max_n;
		else
			results[i].n = minimum
				+ ((config->max_n - minimum) * i) / (count - 1);
		i++;
	}
}

static void	print_algorithms(unsigned int algorithms)
{
	int	algorithm;
	int	first;

	algorithm = 0;
	first = 1;
	while (algorithm < ALG_COUNT)
	{
		if (algorithms & (1U << algorithm))
		{
			if (!first)
				printf(", ");
			printf("%s", algorithm_name(algorithm));
			first = 0;
		}
		algorithm++;
	}
	printf("\n");
}

static void	print_configuration(t_config *config, int count)
{
	printf("sasilves | Portable Push Swap Graph Benchmark\n\n");
	printf("Executable:     %s\n", config->program);
	printf("Maximum size:   %d\n", config->max_n);
	printf("Measured sizes: %d\n", count);
	printf("Runs per size:  %d\n", config->runs);
	printf("Algorithms:     ");
	print_algorithms(config->algorithms);
	if (config->fixed_disorder)
		printf("Input mode:     exact disorder target (%d%%)\n",
			config->disorder);
	else
		printf("Input mode:     random permutation\n");
	printf("Chart scale:    %s\n", config->log_scale ? "logarithmic" : "linear");
	printf("Timeout:        %d seconds per execution\n",
		config->timeout_seconds);
	printf("Validation:     operations and final stacks\n");
	printf("Seed:           %u\n\n", config->seed);
}

static void	set_adaptive_route(t_result *result, double disorder)
{
	if (disorder < ADAPTIVE_SIMPLE_LIMIT)
		result->adaptive_routes[ROUTE_SIMPLE]++;
	else if (disorder < ADAPTIVE_MEDIUM_LIMIT)
		result->adaptive_routes[ROUTE_MEDIUM]++;
	else
		result->adaptive_routes[ROUTE_COMPLEX]++;
}

static void	update_statistics(t_result *result, int algorithm,
		double operations, double *totals, double *squares)
{
	totals[algorithm] += operations;
	squares[algorithm] += operations * operations;
	if (operations < result->min[algorithm])
		result->min[algorithm] = operations;
	if (operations > result->max[algorithm])
		result->max[algorithm] = operations;
}

static void	finish_statistics(t_config *config, t_result *result,
		double *totals, double *squares)
{
	double	variance;
	int		algorithm;

	algorithm = 0;
	while (algorithm < ALG_COUNT)
	{
		if (config->algorithms & (1U << algorithm))
		{
			result->avg[algorithm] = totals[algorithm] / config->runs;
			variance = squares[algorithm] / config->runs
				- result->avg[algorithm] * result->avg[algorithm];
			if (variance < 0.0)
				variance = 0.0;
			result->stddev[algorithm] = sqrt(variance);
		}
		algorithm++;
	}
}

static int	benchmark_size(t_config *config, t_result *result,
		t_progress *progress)
{
	int		*values;
	double	totals[ALG_COUNT];
	double	squares[ALG_COUNT];
	double	disorder;
	int		run;
	int		algorithm;
	int		operations;

	values = malloc(sizeof(int) * (size_t)result->n);
	if (!values)
		return (0);
	memset(totals, 0, sizeof(totals));
	memset(squares, 0, sizeof(squares));
	algorithm = 0;
	while (algorithm < ALG_COUNT)
		result->min[algorithm++] = DBL_MAX;
	result->disorder_min = DBL_MAX;
	run = 0;
	while (run < config->runs)
	{
		if (g_stop_signal != 0)
			return (free(values), 0);
		if (!generate_values(values, result->n, config))
			return (free(values), 0);
		disorder = measure_disorder(values, result->n);
		if (disorder < 0.0)
			return (free(values), 0);
		result->disorder_avg += disorder;
		if (disorder < result->disorder_min)
			result->disorder_min = disorder;
		if (disorder > result->disorder_max)
			result->disorder_max = disorder;
		if (config->algorithms & (1U << ALG_ADAPTIVE))
			set_adaptive_route(result, disorder);
		algorithm = 0;
		while (algorithm < ALG_COUNT)
		{
			if (config->algorithms & (1U << algorithm))
			{
				operations = run_push_swap(algorithm, values, result->n, config);
				if (operations < 0)
					return (free(values), 0);
				update_statistics(result, algorithm, operations, totals, squares);
				progress_advance(progress);
			}
			algorithm++;
		}
		run++;
	}
	result->disorder_avg /= config->runs;
	finish_statistics(config, result, totals, squares);
	free(values);
	return (1);
}

static int	run_benchmark(t_config *config, t_result *results, int count)
{
	t_progress	progress;
	int			i;
	int			total;

	total = count * config->runs * selected_count(config->algorithms);
	progress_init(&progress, total);
	i = 0;
	while (i < count)
	{
		if (!benchmark_size(config, &results[i], &progress))
		{
			progress_abort(&progress);
			return (0);
		}
		i++;
	}
	progress_finish(&progress);
	return (1);
}

/* ===== Optional diagnostic test suite (independent of graph mode) ===== */

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif
#define TEST_CAPTURE_LIMIT (8 * 1024 * 1024)
#define TEST_OK 0
#define TEST_KO 1
#define TEST_SKIP 2
#define TEST_INFO 3

typedef struct s_test_suite
{
	t_config *config;
	char root[PATH_MAX];
	char project[PATH_MAX];
	char program[PATH_MAX];
	char checker[PATH_MAX];
	char sandbox[PATH_MAX];
	FILE *report;
	int number;
	int counts[4];
	int section_before[4];
	int infrastructure_error;
	int consecutive_timeouts;
	t_config random;
} t_test_suite;

typedef struct s_test_case
{
	int number;
	char name[160];
	char stem[PATH_MAX];
	FILE *trace;
	int status;
	int runtime;
	long long duration;
	char *out;
	char *err;
	size_t out_size;
	size_t err_size;
	char reason[1024];
} t_test_case;

static int ts_join(char *dest, size_t size, const char *a, const char *b)
{
	int n = snprintf(dest, size, "%s/%s", a, b);
	if (n < 0 || (size_t)n >= size) { errno = ENAMETOOLONG; return (0); }
	return (1);
}

static int ts_regular(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int ts_absolute(const char *path, char *dest, size_t size)
{
	char cwd[PATH_MAX];
	int n;
	if (path[0] == '/')
		n = snprintf(dest, size, "%s", path);
	else
	{
		if (!getcwd(cwd, sizeof(cwd)))
			return (0);
		n = snprintf(dest, size, "%s/%s", cwd, path);
	}
	if (n < 0 || (size_t)n >= size) { errno = ENAMETOOLONG; return (0); }
	return (1);
}

static int ts_mkdirs(const char *path)
{
	char copy[PATH_MAX];
	struct stat st;
	size_t i;
	if (strlen(path) >= sizeof(copy))
		return (errno = ENAMETOOLONG, 0);
	strcpy(copy, path);
	i = 1;
	while (1)
	{
		if (copy[i] == '/' || copy[i] == '\0')
		{
			char saved = copy[i];
			copy[i] = '\0';
			if (mkdir(copy, 0700) < 0 && errno != EEXIST)
				return (0);
			if (stat(copy, &st) < 0) return (0);
			if (!S_ISDIR(st.st_mode)) return (errno = ENOTDIR, 0);
			copy[i] = saved;
			if (!saved)
				break ;
		}
		i++;
	}
	return (1);
}

static void ts_quote(FILE *file, const char *text)
{
	fputc('\'', file);
	while (*text)
	{
		if (*text == '\'')
			fputs("'\\''", file);
		else
			fputc(*text, file);
		text++;
	}
	fputc('\'', file);
}

static void ts_preview(FILE *file, const char *data, size_t size)
{
	size_t i;
	fputc('"', file);
	for (i = 0; i < size && i < 240; i++)
	{
		unsigned char c = (unsigned char)data[i];
		if (c == '\n') fputs("\\n", file);
		else if (c == '\r') fputs("\\r", file);
		else if (c == '\t') fputs("\\t", file);
		else if (c == '"' || c == '\\') fprintf(file, "\\%c", c);
		else if (c < 32 || c > 126) fprintf(file, "\\x%02x", c);
		else fputc(c, file);
	}
	if (i < size) fputs("...", file);
	fputc('"', file);
}

static FILE *ts_open(const char *path, const char *mode)
{
	FILE *file = fopen(path, mode);
	if (file)
		fcntl(fileno(file), F_SETFD, FD_CLOEXEC);
	return (file);
}

static char *ts_read(const char *path, size_t *size)
{
	FILE *file = ts_open(path, "rb");
	char *data;
	long length;
	*size = 0;
	if (!file || fseek(file, 0, SEEK_END) != 0)
	{
		if (file) fclose(file);
		return (NULL);
	}
	length = ftell(file);
	if (length < 0 || length > TEST_CAPTURE_LIMIT || fseek(file, 0, SEEK_SET))
		return (fclose(file), NULL);
	data = malloc((size_t)length + 1);
	if (!data) return (fclose(file), NULL);
	*size = fread(data, 1, (size_t)length, file);
	if (*size != (size_t)length || ferror(file))
		return (free(data), fclose(file), NULL);
	data[*size] = '\0';
	fclose(file);
	return (data);
}

/* Keep ANSI escapes out of redirected output and saved reports. */
static void ts_print_status(const char *label)
{
	const char *color = "";
	const char *term = getenv("TERM");
	if (isatty(STDOUT_FILENO) && !getenv("NO_COLOR")
		&& (!term || strcmp(term, "dumb")))
	{
		if (!strcmp(label, "OK")) color = "\033[32m";
		else if (!strcmp(label, "KO")) color = "\033[31m";
	}
	printf("%s[ %s ]%s", color, label, *color ? "\033[0m" : "");
}

static void ts_section(t_test_suite *suite, const char *name)
{
	int i;
	printf("\n-- %s --\n", name);
	fprintf(suite->report, "\n-- %s --\n", name);
	for (i = 0; i < 4; i++) suite->section_before[i] = suite->counts[i];
}

static void ts_section_end(t_test_suite *suite)
{
	int ok = suite->counts[TEST_OK] - suite->section_before[TEST_OK];
	int ko = suite->counts[TEST_KO] - suite->section_before[TEST_KO];
	int skip = suite->counts[TEST_SKIP] - suite->section_before[TEST_SKIP];
	const char *state = ko ? "KO" : (skip ? "INCOMPLETE" : "OK");
	printf("Section "); ts_print_status(state);
	printf(": %d OK, %d KO, %d SKIP\n", ok, ko, skip);
	fprintf(suite->report, "Section [%s]: %d OK, %d KO, %d SKIP\n", state, ok, ko, skip);
	fflush(suite->report);
}

static int ts_begin(t_test_suite *suite, t_test_case *test,
		const char *name, const char *expected)
{
	char file[PATH_MAX];
	int n;
	memset(test, 0, sizeof(*test));
	test->number = ++suite->number;
	snprintf(test->name, sizeof(test->name), "%s", name);
	n = snprintf(test->stem, sizeof(test->stem), "%s/T%04d", suite->root, test->number);
	if (n < 0 || (size_t)n >= sizeof(test->stem)
		|| snprintf(file, sizeof(file), "%s.details.txt", test->stem) >= (int)sizeof(file))
		return (suite->infrastructure_error = 1, 0);
	test->trace = ts_open(file, "w");
	if (!test->trace) return (suite->infrastructure_error = 1, 0);
	fprintf(test->trace, "T%04d %s\nSeed: %u\nExpected: %s\n", test->number, name, suite->config->seed, expected);
	fflush(stdout);
	return (1);
}

static void ts_finish(t_test_suite *suite, t_test_case *test, int state,
		const char *format, ...)
{
	static const char *states[] = {"OK", "KO", "SKIP", "INFO"};
	char message[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (test->runtime == -5 && state == TEST_KO) state = TEST_SKIP;
	suite->counts[state]++;
	ts_print_status(states[state]);
	printf(" T%04d %s: %s\n", test->number, test->name, message);
	fprintf(suite->report, "[%s] T%04d %s: %s\n  Details: %s.details.txt\n",
		states[state], test->number, test->name, message, test->stem);
	if (test->trace)
	{
		fprintf(test->trace, "\nResult: %s\nDiagnosis: %s\n", states[state], message);
		int failed = ferror(test->trace);
		if (fclose(test->trace)) failed = 1;
		if (failed) suite->infrastructure_error = 1;
		test->trace = NULL;
	}
	if (state == TEST_KO)
	{
		char command[PATH_MAX];
		printf("  Details: %s.details.txt\n", test->stem);
		if (snprintf(command, sizeof(command), "%s.command.sh", test->stem) < (int)sizeof(command)
			&& ts_regular(command))
		{
			printf("  Reproduce: sh "); ts_quote(stdout, command); printf("\n");
		}
	}
	free(test->out); free(test->err);
	test->out = NULL; test->err = NULL;
	if (fflush(suite->report) || ferror(stdout)) suite->infrastructure_error = 1;
}

static void ts_notice(t_test_suite *suite, const char *name, int state, const char *message)
{
	t_test_case test;
	if (ts_begin(suite, &test, name, message)) ts_finish(suite, &test, state, "%s", message);
}

static int ts_wait(pid_t pid, int *status, int out_fd, int err_fd,
		int seconds, struct timespec *start)
{
	struct timespec pause = {0, 10000000L};
	struct stat out_st, err_st;
	sigset_t set, previous;
	int reason = 0;
	pid_t result;
	fill_stop_signal_set(&set);
	while (1)
	{
		if (g_stop_signal) reason = -4;
		else if (elapsed_milliseconds(start) > seconds * 1000LL) reason = -2;
		else if (fstat(out_fd, &out_st) || fstat(err_fd, &err_st)) reason = -1;
		else if (out_st.st_size > TEST_CAPTURE_LIMIT || err_st.st_size > TEST_CAPTURE_LIMIT) reason = -3;
		if (reason) kill_child_processes(pid);
		if (sigprocmask(SIG_BLOCK, &set, &previous) < 0)
		{
			kill_child_processes(pid);
			do result = waitpid(pid, status, 0); while (result < 0 && errno == EINTR);
			g_active_child_pid = 0;
			return (-1);
		}
		result = waitpid(pid, status, reason ? 0 : WNOHANG);
		if (result == pid || (result < 0 && errno != EINTR))
		{
			g_active_child_pid = 0;
			sigprocmask(SIG_SETMASK, &previous, NULL);
			/* Also stop descendants that outlive their direct parent. */
			kill(-pid, SIGKILL);
			return (result == pid ? reason : -1);
		}
		sigprocmask(SIG_SETMASK, &previous, NULL);
		nanosleep(&pause, NULL);
	}
}

static int ts_capture(t_test_suite *suite, t_test_case *test, char *const args[],
		const char *input, const char *cwd, int seconds)
{
	char in_path[PATH_MAX], out_path[PATH_MAX], err_path[PATH_MAX], command_path[PATH_MAX];
	FILE *file;
	int in_fd = -1, out_fd = -1, err_fd = -1;
	int i;
	pid_t pid;
	struct timespec start;
	sigset_t set, previous;
	if (snprintf(in_path, sizeof(in_path), "%s.stdin.txt", test->stem) >= (int)sizeof(in_path)
		|| snprintf(out_path, sizeof(out_path), "%s.stdout", test->stem) >= (int)sizeof(out_path)
		|| snprintf(err_path, sizeof(err_path), "%s.stderr", test->stem) >= (int)sizeof(err_path)
		|| snprintf(command_path, sizeof(command_path), "%s.command.sh", test->stem) >= (int)sizeof(command_path))
		goto failure;
	file = ts_open(in_path, "w");
	if (!file) goto failure;
	int write_failed = fputs(input ? input : "", file) == EOF;
	if (fclose(file)) write_failed = 1;
	if (write_failed) goto failure;
	file = ts_open(command_path, "w");
	if (!file) goto failure;
	fputs("#!/bin/sh\ncd ", file); ts_quote(file, cwd); fputs(" || exit 1\n", file);
	for (i = 0; args[i]; i++) { if (i) fputc(' ', file); ts_quote(file, args[i]); }
	fputs(" < ", file); ts_quote(file, in_path); fputc('\n', file);
	if (fclose(file)) goto failure;
	fprintf(test->trace, "Working directory: %s\nReplay: sh %s\nCommand: ", cwd, command_path);
	for (i = 0; args[i]; i++) { if (i) fputc(' ', test->trace); ts_quote(test->trace, args[i]); }
	fprintf(test->trace, "\nstdin: %s\nstdout: %s\nstderr: %s\n", in_path, out_path, err_path);
	fflush(test->trace);
	in_fd = open(in_path, O_RDONLY);
	out_fd = open(out_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	err_fd = open(err_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (in_fd < 0 || out_fd < 0 || err_fd < 0 || clock_gettime(CLOCK_MONOTONIC, &start)) goto failure;
	if (suite->consecutive_timeouts >= 3)
	{
		close(in_fd); close(out_fd); close(err_fd);
		test->runtime = -5;
		test->out = strdup(""); test->err = strdup("");
		if (!test->out || !test->err) suite->infrastructure_error = 1;
		snprintf(test->reason, sizeof(test->reason), "not executed after three consecutive timeouts; fix the hang or increase -t, then rerun this saved command");
		fputs("Process: NOT RUN (three-timeout circuit breaker)\n", test->trace);
		return (0);
	}
	fill_stop_signal_set(&set);
	if (sigprocmask(SIG_BLOCK, &set, &previous)) goto failure;
	pid = fork();
	if (pid == 0)
	{
		struct rlimit no_core = {0, 0};
		setrlimit(RLIMIT_CORE, &no_core);
		if (setpgid(0, 0) || chdir(cwd) || dup2(in_fd, STDIN_FILENO) < 0
			|| dup2(out_fd, STDOUT_FILENO) < 0 || dup2(err_fd, STDERR_FILENO) < 0) _exit(126);
		close(in_fd); close(out_fd); close(err_fd);
		sigprocmask(SIG_SETMASK, &previous, NULL);
		execvp(args[0], args);
		dprintf(STDERR_FILENO, "Could not execute %s: %s\n", args[0], strerror(errno));
		_exit(127);
	}
	if (pid < 0)
	{
		sigprocmask(SIG_SETMASK, &previous, NULL);
		goto failure;
	}
	if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH)
	{
		kill(pid, SIGKILL);
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) ;
		sigprocmask(SIG_SETMASK, &previous, NULL);
		goto failure;
	}
	g_active_child_pid = (sig_atomic_t)pid;
	if (sigprocmask(SIG_SETMASK, &previous, NULL))
	{
		kill_child_processes(pid);
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) ;
		g_active_child_pid = 0;
		goto failure;
	}
	test->runtime = ts_wait(pid, &test->status, out_fd, err_fd, seconds, &start);
	if (test->runtime == -2) suite->consecutive_timeouts++;
	else suite->consecutive_timeouts = 0;
	if (suite->consecutive_timeouts == 3)
	{
		printf("Three consecutive timeouts: remaining subprocess cases will be SKIP.\n");
		fprintf(suite->report, "Three consecutive timeouts: remaining subprocess cases will be SKIP.\n");
	}
	test->duration = elapsed_milliseconds(&start);
	struct stat observed_out = {0}, observed_err = {0};
	fstat(out_fd, &observed_out); fstat(err_fd, &observed_err);
	close(in_fd); close(out_fd); close(err_fd);
	test->out = ts_read(out_path, &test->out_size);
	test->err = ts_read(err_path, &test->err_size);
	if (!test->out && observed_out.st_size > 0) test->out_size = (size_t)observed_out.st_size;
	if (!test->err && observed_err.st_size > 0) test->err_size = (size_t)observed_err.st_size;
	if (test->runtime == -2) snprintf(test->reason, sizeof(test->reason), "timeout after %d seconds", seconds);
	else if (test->runtime == -3) snprintf(test->reason, sizeof(test->reason), "stdout or stderr exceeded %d bytes", TEST_CAPTURE_LIMIT);
	else if (test->runtime == -4) snprintf(test->reason, sizeof(test->reason), "interrupted by signal %d", (int)g_stop_signal);
	else if (test->runtime < 0) snprintf(test->reason, sizeof(test->reason), "could not monitor child process");
	else if (WIFSIGNALED(test->status)) snprintf(test->reason, sizeof(test->reason), "terminated by signal %d", WTERMSIG(test->status));
	else if (!WIFEXITED(test->status)) snprintf(test->reason, sizeof(test->reason), "child did not exit normally");
	else if (!test->out || !test->err) snprintf(test->reason, sizeof(test->reason), "could not read captured output");
	fprintf(test->trace, "Elapsed: %lld ms\nProcess: ", test->duration);
	if (WIFEXITED(test->status)) fprintf(test->trace, "exit %d\n", WEXITSTATUS(test->status));
	else if (WIFSIGNALED(test->status)) fprintf(test->trace, "signal %d\n", WTERMSIG(test->status));
	else fputs("unknown state\n", test->trace);
	fprintf(test->trace, "stdout bytes: %zu\nstdout preview: ", test->out_size);
	if (test->out) ts_preview(test->trace, test->out, test->out_size);
	fprintf(test->trace, "\nstderr bytes: %zu\nstderr preview: ", test->err_size);
	if (test->err) ts_preview(test->trace, test->err, test->err_size);
	fputc('\n', test->trace);
	return (test->reason[0] == '\0');
failure:
	if (in_fd >= 0) close(in_fd);
	if (out_fd >= 0) close(out_fd);
	if (err_fd >= 0) close(err_fd);
	suite->infrastructure_error = 1;
	snprintf(test->reason, sizeof(test->reason), "tester infrastructure error: %s", strerror(errno));
	return (0);
}

static int ts_success(t_test_case *test)
{
	if (test->reason[0]) return (0);
	if (!WIFEXITED(test->status) || WEXITSTATUS(test->status) != 0)
	{
		snprintf(test->reason, sizeof(test->reason), "expected exit 0, received exit %d", WEXITSTATUS(test->status));
		return (0);
	}
	return (1);
}

static int ts_equal(const char *data, size_t size, const char *expected)
{
	return (data && size == strlen(expected) && memcmp(data, expected, size) == 0);
}

static void ts_stack_dump(FILE *file, t_sim_stack *stack, const char *label)
{
	int i;
	fprintf(file, "%s (%d):", label, stack->size);
	for (i = 0; i < stack->size; i++) fprintf(file, " %d", stack->data[stack_index(stack, i)]);
	fputc('\n', file);
}

static int ts_sort_output(t_test_case *test, int *values, int size,
		int maximum, int allow_stderr)
{
	t_verifier verifier;
	size_t i, line = 1;
	int valid = 1;
	if (!ts_success(test)) return (-1);
	if (!allow_stderr && test->err_size)
	{
		snprintf(test->reason, sizeof(test->reason), "unexpected stderr (%zu bytes); see captured stderr", test->err_size);
		return (-1);
	}
	if (!verifier_init(&verifier, values, size))
	{
		snprintf(test->reason, sizeof(test->reason), "tester could not allocate stack verifier");
		return (-1);
	}
	for (i = 0; i < test->out_size; i++)
	{
		if (!feed_character(&verifier, test->out[i]))
		{
			snprintf(test->reason, sizeof(test->reason), "invalid operation at stdout line %zu, byte %zu (0x%02x); accepted operations: %d",
				line, i + 1, (unsigned char)test->out[i], verifier.operations);
			valid = 0; break ;
		}
		if (test->out[i] == '\n') line++;
	}
	if (valid && verifier.line_length)
	{
		snprintf(test->reason, sizeof(test->reason), "stdout line %zu is missing its final newline", line);
		valid = 0;
	}
	if (valid && !verifier_is_sorted(&verifier))
	{
		int inversion = -1;
		for (int j = 1; j < verifier.a.size; j++)
			if (verifier.a.data[stack_index(&verifier.a, j - 1)] > verifier.a.data[stack_index(&verifier.a, j)]) { inversion = j; break ; }
		snprintf(test->reason, sizeof(test->reason), "incorrect final stacks after %d operations: A=%d, B=%d; first inversion in A at index %d (-1 means none)",
			verifier.operations, verifier.a.size, verifier.b.size, inversion);
		valid = 0;
	}
	if (valid && maximum >= 0 && verifier.operations > maximum)
	{
		snprintf(test->reason, sizeof(test->reason), "sorting is correct but used %d operations; maximum accepted for this case is %d", verifier.operations, maximum);
		valid = 0;
	}
	fprintf(test->trace, "Operations parsed: %d\n", verifier.operations);
	ts_stack_dump(test->trace, &verifier.a, "Final A");
	ts_stack_dump(test->trace, &verifier.b, "Final B");
	int operations = verifier.operations;
	free(verifier.a.data); free(verifier.b.data);
	return (valid ? operations : -1);
}

static char **ts_arguments(const char *program, int algorithm, int bench,
		int *values, int size, char **storage)
{
	char **args = malloc(sizeof(*args) * ((size_t)size + 5));
	int offset = 1;
	*storage = malloc((size_t)(size ? size : 1) * 16);
	if (!args || !*storage)
	{
		free(args); free(*storage); *storage = NULL;
		return (NULL);
	}
	args[0] = (char *)program;
	if (bench) args[offset++] = "--bench";
	if (algorithm != ALG_DEFAULT) args[offset++] = (char *)algorithm_flag(algorithm);
	for (int i = 0; i < size; i++)
	{
		snprintf(*storage + i * 16, 16, "%d", values[i]);
		args[offset++] = *storage + i * 16;
	}
	args[offset] = NULL;
	return (args);
}

static int ts_sort_case(t_test_suite *suite, const char *name, int algorithm,
		int *values, int size, int maximum)
{
	t_test_case test;
	char expectation[160], *storage = NULL, **args;
	int count = -1;
	snprintf(expectation, sizeof(expectation), "exit 0; empty stderr; valid operations sorting %d values; maximum operations %d (-1 = unlimited)", size, maximum);
	if (!ts_begin(suite, &test, name, expectation)) return (-1);
	args = ts_arguments(suite->program, algorithm, 0, values, size, &storage);
	if (!args)
	{
		suite->infrastructure_error = 1;
		snprintf(test.reason, sizeof(test.reason), "tester allocation failed");
	}
	else if (ts_capture(suite, &test, args, "", suite->project, suite->config->timeout_seconds))
		count = ts_sort_output(&test, values, size, maximum, 0);
	if (count >= 0) ts_finish(suite, &test, TEST_OK, "%d operations; final A sorted and B empty", count);
	else ts_finish(suite, &test, TEST_KO, "%s", test.reason);
	free(args); free(storage);
	return (count);
}

static int ts_exact_case(t_test_suite *suite, const char *name, char *const args[],
		const char *input, const char *out, const char *err, int require_zero)
{
	t_test_case test;
	int ok;
	if (!ts_begin(suite, &test, name, "exact stdout/stderr (recorded below); normal exit; no timeout or crash")) return (0);
	fputs("Expected stdout: ", test.trace); ts_preview(test.trace, out, strlen(out));
	fputs("\nExpected stderr: ", test.trace); ts_preview(test.trace, err, strlen(err)); fputc('\n', test.trace);
	ok = ts_capture(suite, &test, args, input, suite->project, suite->config->timeout_seconds);
	if (ok && require_zero) ok = ts_success(&test);
	if (ok && !ts_equal(test.out, test.out_size, out))
	{
		snprintf(test.reason, sizeof(test.reason), "stdout mismatch: expected %zu bytes, got %zu; inspect expected/actual previews and .stdout", strlen(out), test.out_size);
		ok = 0;
	}
	if (ok && !ts_equal(test.err, test.err_size, err))
	{
		snprintf(test.reason, sizeof(test.reason), "stderr mismatch: expected %zu bytes, got %zu; expected diagnostics must be on stderr", strlen(err), test.err_size);
		ok = 0;
	}
	ts_finish(suite, &test, ok ? TEST_OK : TEST_KO, "%s", ok ? "stdout, stderr and process termination match" : test.reason);
	return (ok);
}

static int ts_find_tool(const char *name, char *dest, size_t size)
{
	const char *path = getenv("PATH");
	char copy[PATH_MAX * 2], candidate[PATH_MAX], *cursor, *end;
	if (!path || strlen(path) >= sizeof(copy)) return (0);
	strcpy(copy, path); cursor = copy;
	while (cursor)
	{
		end = strchr(cursor, ':'); if (end) *end = '\0';
		if (ts_join(candidate, sizeof(candidate), *cursor ? cursor : ".", name)
			&& ts_regular(candidate) && access(candidate, X_OK) == 0)
			return (ts_absolute(candidate, dest, size));
		cursor = end ? end + 1 : NULL;
	}
	return (0);
}

static void ts_error_tests(t_test_suite *suite)
{
	const char *vectors[][5] = {
		{"not-a-number", "4", NULL}, {"7", "-2", "7", NULL},
		{"2147483648", NULL}, {"-2147483649", NULL},
		{"999999999999999999999999999999", NULL},
		{"", NULL}, {"   ", NULL}, {"+", NULL}, {"--", NULL},
		{"2.5", NULL}, {"3x", NULL}, {"+0", "-0", NULL}
	};
	const char *names[] = {"non-numeric token", "duplicate integer", "above INT_MAX",
		"below INT_MIN", "very long overflow", "empty argument", "blank argument",
		"isolated plus", "invalid minus token", "decimal token", "numeric suffix", "equivalent zero duplicates"};
	char *args[8];
	ts_section(suite, "Input validation");
	for (size_t i = 0; i < sizeof(names) / sizeof(*names) && !g_stop_signal; i++)
	{
		int j = 0; args[0] = suite->program;
		while (vectors[i][j]) { args[j + 1] = (char *)vectors[i][j]; j++; }
		args[j + 1] = NULL;
		ts_exact_case(suite, names[i], args, "", "", "Error\n", 0);
	}
	args[0] = suite->program; args[1] = NULL;
	if (!g_stop_signal) ts_exact_case(suite, "no arguments", args, "", "", "", 1);
	ts_section_end(suite);
}

static void ts_identity_tests(t_test_suite *suite)
{
	int values[10];
	const int sizes[] = {1, 2, 4, 10};
	char name[100];
	ts_section(suite, "Already sorted inputs");
	for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes) && !g_stop_signal; i++)
	{
		for (int j = 0; j < sizes[i]; j++) values[j] = j * 7 - 21;
		snprintf(name, sizeof(name), "sorted input, %d elements", sizes[i]);
		ts_sort_case(suite, name, ALG_DEFAULT, values, sizes[i], 0);
	}
	ts_section_end(suite);
}

static void ts_permutations(t_test_suite *suite, int *values, int size, int at, int *number)
{
	if (g_stop_signal || suite->infrastructure_error) return ;
	if (at == size)
	{
		char name[100];
		snprintf(name, sizeof(name), "%d elements, permutation %d", size, ++(*number));
		ts_sort_case(suite, name, ALG_DEFAULT, values, size, size == 3 ? 5 : 15);
		return ;
	}
	for (int i = at; i < size; i++)
	{
		int tmp = values[at]; values[at] = values[i]; values[i] = tmp;
		ts_permutations(suite, values, size, at + 1, number);
		tmp = values[at]; values[at] = values[i]; values[i] = tmp;
		if (g_stop_signal || suite->infrastructure_error) break ;
	}
}

static void ts_small_tests(t_test_suite *suite)
{
	int three[] = {-9, 0, 14}, five[] = {-11, -3, 2, 8, 21};
	int number = 0;
	int bounds[] = {INT_MAX, 0, INT_MIN, -1, 1};
	int quoted[] = {8, -4, 0, 3};
	char *quoted_args[] = {suite->program, "8 -4 0 3", NULL};
	t_test_case test;
	int count;
	ts_section(suite, "Small inputs and operation budgets");
	ts_permutations(suite, three, 3, 0, &number);
	number = 0; ts_permutations(suite, five, 5, 0, &number);
	if (!g_stop_signal) ts_sort_case(suite, "integer boundaries and mixed signs", ALG_DEFAULT, bounds, 5, -1);
	if (!g_stop_signal && ts_begin(suite, &test, "single quoted numeric argument", "exit 0 and operations sorting the four supplied integers"))
	{
		count = -1;
		if (ts_capture(suite, &test, quoted_args, "", suite->project, suite->config->timeout_seconds))
			count = ts_sort_output(&test, quoted, 4, -1, 0);
		ts_finish(suite, &test, count >= 0 ? TEST_OK : TEST_KO, "%s", count >= 0 ? "quoted list sorted correctly" : test.reason);
	}
	ts_section_end(suite);
}

static int ts_random_values(t_test_suite *suite, int *values, int size, int universe)
{
	int *pool = malloc(sizeof(int) * (size_t)universe);
	if (!pool) return (suite->infrastructure_error = 1, 0);
	for (int i = 0; i < universe; i++) pool[i] = i + 1;
	for (int i = 0; i < size; i++)
	{
		int j = i + (int)random_bounded(&suite->random, (unsigned int)(universe - i));
		int tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
		values[i] = pool[i];
	}
	free(pool);
	return (1);
}

static void ts_strategy_tests(t_test_suite *suite)
{
	int reverse[] = {19, 13, 7, 2, -5};
	int values[50];
	int counts[4];
	char name[120];
	ts_section(suite, "Required strategy flags");
	for (int a = 0; a < ALG_DEFAULT && !g_stop_signal; a++)
	{
		snprintf(name, sizeof(name), "%s on reverse input", algorithm_flag(a));
		ts_sort_case(suite, name, a, reverse, 5, -1);
	}
	if (!g_stop_signal) ts_sort_case(suite, "unflagged execution", ALG_DEFAULT, reverse, 5, -1);
	if (!g_stop_signal && ts_random_values(suite, values, 50, 200))
	{
		for (int a = 0; a < ALG_DEFAULT && !g_stop_signal; a++)
		{
			snprintf(name, sizeof(name), "%s on the same 50-element input", algorithm_flag(a));
			counts[a] = ts_sort_case(suite, name, a, values, 50, -1);
		}
		if (!g_stop_signal)
		{
			char info[200];
			snprintf(info, sizeof(info), "Observed counts: simple=%d, medium=%d, complex=%d, adaptive=%d (-1 = failed). Relative counts are informational, not a proof of complexity.", counts[0], counts[1], counts[2], counts[3]);
			ts_notice(suite, "strategy performance comparison", TEST_INFO, info);
		}
	}
	if (!g_stop_signal && !suite->infrastructure_error)
	{
		char *storage_a = NULL, *storage_b = NULL, **a, **b;
		t_test_case test_a, test_b;
		char *saved = NULL;
		size_t saved_size = 0;
		int ok_a = 0, ok_b = 0;
		if (ts_begin(suite, &test_a, "default strategy observation on 50 elements", "valid default output; comparison with explicit adaptive follows"))
		{
			a = ts_arguments(suite->program, ALG_DEFAULT, 0, values, 50, &storage_a);
			if (!a) { suite->infrastructure_error = 1; snprintf(test_a.reason, sizeof(test_a.reason), "tester allocation failed"); }
			if (a && ts_capture(suite, &test_a, a, "", suite->project, suite->config->timeout_seconds)
				&& ts_sort_output(&test_a, values, 50, -1, 0) >= 0)
			{
				ok_a = 1; saved_size = test_a.out_size;
				saved = malloc(saved_size + 1);
				if (saved) memcpy(saved, test_a.out, saved_size + 1);
				else suite->infrastructure_error = 1;
			}
			ts_finish(suite, &test_a, ok_a ? TEST_OK : TEST_KO, "%s", ok_a ? "default output captured for comparison" : test_a.reason);
			free(a); free(storage_a);
		}
		if (!g_stop_signal && ts_begin(suite, &test_b, "default versus adaptive output", "same valid operations for a deterministic default/adaptive implementation"))
		{
			b = ts_arguments(suite->program, ALG_ADAPTIVE, 0, values, 50, &storage_b);
			if (!b) { suite->infrastructure_error = 1; snprintf(test_b.reason, sizeof(test_b.reason), "tester allocation failed"); }
			if (b && ts_capture(suite, &test_b, b, "", suite->project, suite->config->timeout_seconds)
				&& ts_sort_output(&test_b, values, 50, -1, 0) >= 0) ok_b = 1;
			if (!ok_b) ts_finish(suite, &test_b, TEST_KO, "%s", test_b.reason);
			else if (saved && test_b.out_size == saved_size && !memcmp(saved, test_b.out, saved_size))
				ts_finish(suite, &test_b, TEST_OK, "default and adaptive produced identical operations on this input; equality is an observation on this case only");
			else ts_finish(suite, &test_b, TEST_INFO, "valid outputs differ or the reference failed; comparison is observational and does not establish internal strategy selection");
			free(b); free(storage_b);
		}
		free(saved);
	}
	ts_section_end(suite);
}

static void ts_large_tests(t_test_suite *suite)
{
	int values[500];
	char name[100];
	int runs;
	ts_section(suite, "Random input correctness and per-run operation budgets");
	for (int size = 100; size <= 500 && !g_stop_signal; size += 400)
	{
		runs = suite->config->runs;
		if (runs < (size == 100 ? 3 : 2)) runs = size == 100 ? 3 : 2;
		for (int i = 0; i < runs && !g_stop_signal; i++)
		{
			if (!ts_random_values(suite, values, size, size == 100 ? 500 : 1000)) break ;
			snprintf(name, sizeof(name), "%d elements, random run %d", size, i + 1);
			ts_sort_case(suite, name, ALG_DEFAULT, values, size, size == 100 ? 1999 : 11999);
		}
	}
	ts_section_end(suite);
}

/* Recognize common text labels without requiring one exact presentation. */
static int ts_metric(const char *data, const char *wanted, double *value)
{
	const char *line = data;
	while (line && *line)
	{
		const char *end = strchr(line, '\n');
		const char *label = line, *sep, *bracket;
		char key[100]; size_t k = 0;
		if (!end) end = line + strlen(line);
		bracket = memchr(line, ']', (size_t)(end - line));
		if (bracket) label = bracket + 1;
		sep = memchr(label, ':', (size_t)(end - label));
		if (!sep) sep = memchr(label, '=', (size_t)(end - label));
		if (sep)
		{
			for (const char *p = label; p < sep && k + 1 < sizeof(key); p++)
				if (isalnum((unsigned char)*p)) key[k++] = (char)tolower((unsigned char)*p);
			key[k] = '\0';
			if (!strcmp(key, wanted))
			{
				char *parsed;
				errno = 0; *value = strtod(sep + 1, &parsed);
				if (parsed == sep + 1 || parsed > end || errno || !isfinite(*value)) return (-1);
				while (parsed < end && isspace((unsigned char)*parsed)) parsed++;
				if (parsed < end && *parsed == '%') parsed++;
				while (parsed < end && isspace((unsigned char)*parsed)) parsed++;
				/* Extra annotations may be valid, but are not verified numbers. */
				return (parsed == end ? 1 : 0);
			}
		}
		line = *end ? end + 1 : NULL;
	}
	return (0);
}

static int ts_any_metric(const char *data, const char *a, const char *b, const char *c, double *number)
{
	int found = ts_metric(data, a, number);
	if (!found && b) found = ts_metric(data, b, number);
	if (!found && c) found = ts_metric(data, c, number);
	return (found);
}

static int ts_has_label(const char *data, const char *label)
{
	char *copy = strdup(data);
	int found;
	if (!copy) return (0);
	for (char *p = copy; *p; p++) *p = (char)tolower((unsigned char)*p);
	found = strstr(copy, label) != NULL;
	free(copy);
	return (found);
}

static int ts_strategy_label(const char *data)
{
	char *copy = strdup(data), *line, *save = NULL;
	int found = 0;
	if (!copy) return (0);
	for (char *p = copy; *p; p++) *p = (char)tolower((unsigned char)*p);
	for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
	{
		if (strstr(line, "strategy") || strstr(line, "estrategia"))
		{
			char *value = strchr(line, ':'); if (!value) value = strchr(line, '=');
			if (value) for (value++; *value; value++) if (isalpha((unsigned char)*value)) found = 1;
		}
	}
	free(copy);
	return (found);
}

static int ts_bench_metadata(t_test_case *test, int *values, int size, int operations)
{
	static const char *names[] = {"sa", "sb", "ss", "pa", "pb", "ra", "rb", "rr", "rra", "rrb", "rrr"};
	int counts[11] = {0}, missing = 0;
	double value = 0, expected = measure_disorder(values, size);
	int found;
	char *copy, *line, *save = NULL;
	if (!test->err_size)
	{
		snprintf(test->reason, sizeof(test->reason), "--bench produced no metadata on stderr");
		return (TEST_KO);
	}
	if (memchr(test->err, '\0', test->err_size))
	{
		snprintf(test->reason, sizeof(test->reason), "benchmark metadata contains a NUL byte");
		return (TEST_KO);
	}
	found = ts_any_metric(test->err, "disorder", "disorderpercentage", "desorden", &value);
	if (found < 0 || (found && (expected < 0 || fabs(value - expected) > 0.02)))
	{
		snprintf(test->reason, sizeof(test->reason), "disorder mismatch: computed %.4f%% from input, reported %.4f%%", expected, value);
		return (TEST_KO);
	}
	missing += found == 0;
	found = ts_any_metric(test->err, "totalops", "totaloperations", "total", &value);
	if (found < 0 || (found && value != operations))
	{
		snprintf(test->reason, sizeof(test->reason), "total operation mismatch: stdout has %d, metadata reports %.2f", operations, value);
		return (TEST_KO);
	}
	missing += found == 0;
	copy = strdup(test->out);
	if (!copy) { snprintf(test->reason, sizeof(test->reason), "tester allocation failed while reading counters"); return (TEST_KO); }
	for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
		for (int i = 0; i < 11; i++) if (!strcmp(line, names[i])) counts[i]++;
	free(copy);
	for (int i = 0; i < 11; i++)
	{
		found = ts_metric(test->err, names[i], &value);
		if (found < 0 || (found && value != counts[i]))
		{
			snprintf(test->reason, sizeof(test->reason), "counter %s mismatch: stdout has %d, metadata reports %.2f", names[i], counts[i], value);
			return (TEST_KO);
		}
		missing += found == 0;
	}
	if (!ts_strategy_label(test->err)) missing++;
	if (!(ts_has_label(test->err, "o(") || ts_has_label(test->err, "o ("))) missing++;
	if (missing)
	{
		snprintf(test->reason, sizeof(test->reason), "%d metadata fields not recognized; inspect stderr for absent fields or a different text format. No automatic pass is claimed", missing);
		return (TEST_SKIP);
	}
	snprintf(test->reason, sizeof(test->reason), "disorder %.2f%%, total %d and all 11 counters agree with the captured operations; strategy and complexity labels present", expected, operations);
	return (TEST_OK);
}

static void ts_bench_tests(t_test_suite *suite)
{
	int data[3][5] = {{-6, 0, 5, 11, 27}, {27, 11, 5, 0, -6}, {-6, 5, 0, 11, 27}};
	const char *labels[] = {"sorted", "reverse", "partially shuffled"};
	char name[120], *storage, **args;
	t_test_case test;
	int count, state;
	ts_section(suite, "Benchmark metadata and independently computed disorder");
	for (int i = 0; i < 3 && !g_stop_signal; i++)
	{
		snprintf(name, sizeof(name), "--bench --simple, %s input", labels[i]);
		if (!ts_begin(suite, &test, name, "valid stdout operations; stderr disorder, strategy, complexity, total and per-operation counters")) break ;
		storage = NULL; args = ts_arguments(suite->program, ALG_SIMPLE, 1, data[i], 5, &storage);
		count = -1; state = TEST_KO;
		if (args && ts_capture(suite, &test, args, "", suite->project, suite->config->timeout_seconds))
			count = ts_sort_output(&test, data[i], 5, -1, 1);
		if (count >= 0) state = ts_bench_metadata(&test, data[i], 5, count);
		if (!args) { suite->infrastructure_error = 1; snprintf(test.reason, sizeof(test.reason), "tester allocation failed"); }
		ts_finish(suite, &test, state, "%s", test.reason);
		free(args); free(storage);
	}
	ts_section_end(suite);
}

static int ts_copy_file(const char *source, const char *dest, mode_t mode)
{
	int in_fd = open(source, O_RDONLY), out_fd;
	char buffer[16384]; ssize_t count;
	if (in_fd < 0) return (0);
	out_fd = open(dest, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
	if (out_fd < 0) return (close(in_fd), 0);
	while ((count = read(in_fd, buffer, sizeof(buffer))) != 0)
	{
		if (count < 0) { if (errno == EINTR) continue ; close(in_fd); close(out_fd); return (0); }
		for (ssize_t offset = 0; offset < count; )
		{
			ssize_t written = write(out_fd, buffer + offset, (size_t)(count - offset));
			if (written < 0 && errno == EINTR) continue ;
			if (written <= 0) return (close(in_fd), close(out_fd), 0);
			offset += written;
		}
	}
	close(in_fd);
	return (close(out_fd) == 0);
}

static int ts_copy_tree(t_test_suite *suite, const char *source, const char *dest,
		int depth, int *files, unsigned long long *bytes, char *reason, size_t reason_size)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char from[PATH_MAX], to[PATH_MAX];
	int ok = 1;
	if (depth > 40) { snprintf(reason, reason_size, "source nesting exceeds 40 levels: %s", source); return (0); }
	dir = opendir(source);
	if (!dir) { snprintf(reason, reason_size, "cannot open source directory: %s", source); return (0); }
	while ((entry = readdir(dir)) && ok)
	{
		const char *name = entry->d_name;
		size_t length = strlen(name);
		if (!strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, ".git")
			|| !strcmp(name, "__pycache__") || !strcmp(name, ".venv") || !strcmp(name, "venv")
			|| !strcmp(name, "push_swap_benchmark.c") || !strcmp(name, "ps_benchmark")
			|| !strcmp(name, "ps-benchmark") || !strcmp(name, "install.sh")
			|| !strcmp(name, "benchmark-preview.png") || !strncmp(name, "ps-benchmark-tests-", 19)
			|| (length > 2 && (!strcmp(name + length - 2, ".o") || !strcmp(name + length - 2, ".a")))) continue ;
		if (!ts_join(from, sizeof(from), source, name) || !ts_join(to, sizeof(to), dest, name))
			{ snprintf(reason, reason_size, "source path is too long"); ok = 0; break ; }
		if (!strcmp(from, suite->root)) continue ;
		if (lstat(from, &st)) { snprintf(reason, reason_size, "cannot inspect %.3000s", from); ok = 0; break ; }
		if (S_ISLNK(st.st_mode)) { snprintf(reason, reason_size, "source copy does not follow symlinks: %.3000s", from); ok = 0; break ; }
		if (S_ISDIR(st.st_mode))
		{
			if (mkdir(to, 0700) < 0 || !ts_copy_tree(suite, from, to, depth + 1, files, bytes, reason, reason_size)) ok = 0;
		}
		else if (S_ISREG(st.st_mode))
		{
			if (!strcmp(name, "push_swap") || !strcmp(name, "checker")) continue ;
			*bytes += (unsigned long long)st.st_size;
			if (++(*files) > 4096 || *bytes > 64ULL * 1024 * 1024)
				{ snprintf(reason, reason_size, "source copy exceeds 4096 files or 64 MiB"); ok = 0; break ; }
			if (!ts_copy_file(from, to, st.st_mode)) { snprintf(reason, reason_size, "cannot copy %.3000s: %.500s", from, strerror(errno)); ok = 0; }
		}
	}
	closedir(dir);
	return (ok);
}

static int ts_count_objects(const char *root)
{
	DIR *dir = opendir(root);
	struct dirent *entry;
	struct stat st;
	char path[PATH_MAX];
	int count = 0, sub;
	if (!dir) return (-1);
	while ((entry = readdir(dir)))
	{
		size_t n = strlen(entry->d_name);
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue ;
		if (!ts_join(path, sizeof(path), root, entry->d_name) || lstat(path, &st)) { count = -1; break ; }
		if (S_ISDIR(st.st_mode))
		{
			sub = ts_count_objects(path);
			if (sub < 0) { count = -1; break ; }
			count += sub;
		}
		else if (S_ISREG(st.st_mode) && n > 2 && !strcmp(entry->d_name + n - 2, ".o")) count++;
	}
	closedir(dir);
	return (count);
}

static int ts_definition(const char *text, const char *pattern)
{
	regex_t regex;
	int matched;
	if (regcomp(&regex, pattern, REG_EXTENDED | REG_NEWLINE)) return (0);
	matched = regexec(&regex, text, 0, NULL, 0) == 0;
	regfree(&regex);
	return (matched);
}

static int ts_build_command(t_test_suite *suite, const char *name, char *const args[],
		const char *binary, int expectation)
{
	t_test_case test;
	struct stat before = {0}, after = {0};
	int ok, objects;
	stat(binary, &before);
	if (!ts_begin(suite, &test, name, "successful command in isolated source copy; original project is unchanged")) return (0);
	ok = ts_capture(suite, &test, args, "", suite->sandbox,
		suite->config->timeout_seconds < 30 ? 30 : suite->config->timeout_seconds);
	if (ok) ok = ts_success(&test);
	if (ok && (expectation == 1 || expectation == 2))
	{
		if (stat(binary, &after) || !S_ISREG(after.st_mode) || access(binary, X_OK))
		{
			snprintf(test.reason, sizeof(test.reason), "command completed but expected executable was not created: %s", binary); ok = 0;
		}
	}
	if (ok && expectation == 2)
	{
#ifdef __APPLE__
		int changed = before.st_mtimespec.tv_sec != after.st_mtimespec.tv_sec || before.st_mtimespec.tv_nsec != after.st_mtimespec.tv_nsec;
#else
		int changed = before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec;
#endif
		if (changed || before.st_ino != after.st_ino || before.st_size != after.st_size)
		{
			snprintf(test.reason, sizeof(test.reason), "second make changed the executable (timestamp, inode or size): unnecessary relink"); ok = 0;
		}
	}
	if (ok && (expectation == 3 || expectation == 4))
	{
		objects = ts_count_objects(suite->sandbox);
		if (objects != 0 || (expectation == 4 && access(binary, F_OK) == 0))
		{
			snprintf(test.reason, sizeof(test.reason), "cleanup left %d object files%s", objects, expectation == 4 ? " or retained the executable" : ""); ok = 0;
		}
	}
	ts_finish(suite, &test, ok ? TEST_OK : TEST_KO, "%s", ok ? "command and resulting build state match" : test.reason);
	return (ok);
}

static void ts_source_tests(t_test_suite *suite)
{
	char makefile[PATH_MAX], tool[PATH_MAX], binary[PATH_MAX], reason[PATH_MAX] = "source copy failed";
	char *args[5];
	int files = 0, built = 0;
	unsigned long long bytes = 0;
	t_test_case test;
	if (!ts_join(makefile, sizeof(makefile), suite->project, "Makefile")) { suite->infrastructure_error = 1; return ; }
	ts_section(suite, "Isolated source, compilation and cleanup");
	if (!ts_join(makefile, sizeof(makefile), suite->project, "Makefile") || !ts_regular(makefile))
	{
		ts_notice(suite, "Makefile availability", suite->config->project_dir ? TEST_KO : TEST_SKIP, "No Makefile at the source root; executable tests can continue. Use --project DIR for source checks.");
		ts_notice(suite, "Norminette", TEST_SKIP, "Source build copy was not prepared because no Makefile was found.");
		ts_section_end(suite); return ;
	}
	size_t make_size; char *make_text = ts_read(makefile, &make_size);
	if (make_text && ts_definition(make_text, "^[[:blank:]]*(export[[:blank:]]+)?NAME[[:blank:]]*[:+?]?="))
		ts_notice(suite, "Makefile executable-name variable", TEST_OK, "a NAME assignment is present in the root Makefile");
	else ts_notice(suite, "Makefile executable-name variable", TEST_SKIP, "no direct NAME assignment recognized; check included makefiles or generated definitions");
	free(make_text);
	if (!ts_begin(suite, &test, "source sandbox", "copy regular project files; exclude VCS, build outputs, this tester and generated reports")) return ;
	if (!ts_join(suite->sandbox, sizeof(suite->sandbox), suite->root, "source-copy")
		|| mkdir(suite->sandbox, 0700) || !ts_copy_tree(suite, suite->project, suite->sandbox, 0, &files, &bytes, reason, sizeof(reason)))
	{
		ts_finish(suite, &test, TEST_SKIP, "%s; build and Norm checks were not run", reason);
		suite->sandbox[0] = '\0'; ts_section_end(suite); return ;
	}
	ts_finish(suite, &test, TEST_OK, "%d files copied (%llu bytes); original source is unchanged", files, bytes);
	const char *base = strrchr(suite->program, '/'); base = base ? base + 1 : suite->program;
	if (!ts_join(binary, sizeof(binary), suite->sandbox, "push_swap")) { suite->infrastructure_error = 1; return ; }
	if (!ts_find_tool("make", tool, sizeof(tool)))
		ts_notice(suite, "Makefile tests", TEST_SKIP, "make is not available in PATH");
	else
	{
		args[0] = tool; args[1] = NULL;
		built = ts_build_command(suite, "make creates the executable", args, binary, 1);
		if (!g_stop_signal && built) ts_build_command(suite, "make does not relink", args, binary, 2);
		if (!g_stop_signal) { args[1] = "all"; args[2] = NULL; ts_build_command(suite, "make all", args, binary, 1); }
		if (!g_stop_signal) { args[1] = "clean"; ts_build_command(suite, "make clean removes objects", args, binary, 3); }
		if (!g_stop_signal) { args[1] = "fclean"; ts_build_command(suite, "make fclean removes executable and objects", args, binary, 4); }
		if (!g_stop_signal) { args[1] = "re"; built = ts_build_command(suite, "make re rebuilds the executable", args, binary, 1); }
		if (!g_stop_signal && ts_begin(suite, &test, "observed compiler warning flags", "every observable C compilation command includes -Wall -Wextra -Werror"))
		{
			args[1] = "-n"; args[2] = "-B"; args[3] = "all"; args[4] = NULL;
			int ok = ts_capture(suite, &test, args, "", suite->sandbox, 30);
			int observed = 0, bad = 0;
			if (ok) ok = ts_success(&test);
			if (ok)
			{
				char *copy = strdup(test.out), *line, *save = NULL;
				if (!copy) { suite->infrastructure_error = 1; ok = 0; }
				for (line = copy ? strtok_r(copy, "\n", &save) : NULL; line; line = strtok_r(NULL, "\n", &save))
					if (strstr(line, ".c") && (strstr(line, " -c ") || strstr(line, " -o ")))
					{
						observed++;
						if (!strstr(line, "-Wall") || !strstr(line, "-Wextra") || !strstr(line, "-Werror")) { bad++; fprintf(test.trace, "Missing flags in: %s\n", line); }
					}
				free(copy);
			}
			if (!ok) ts_finish(suite, &test, TEST_KO, "%s", test.reason[0] ? test.reason : "could not inspect compiler commands");
			else if (!observed) ts_finish(suite, &test, TEST_SKIP, "no recognizable C compile commands in make -n -B all; inspect Makefile or its build wrappers");
			else ts_finish(suite, &test, bad ? TEST_KO : TEST_OK, "%d compilation commands inspected; %d missing required flags", observed, bad);
		}
		if (built && !strcmp(base, "push_swap") && access(suite->program, X_OK) != 0) snprintf(suite->program, sizeof(suite->program), "%s", binary);
		/* Building optional bonus targets is attempted only when declared. */
		if (!g_stop_signal && !suite->config->checker_path)
		{
			size_t length; char *text = ts_read(makefile, &length);
			if (text && ts_definition(text, "^[[:blank:]]*bonus[[:blank:]]*:"))
			{
				args[1] = "bonus"; args[2] = NULL;
				ts_build_command(suite, "make bonus (declared optional target)", args, binary, 0);
			}
			free(text);
		}
	}
	if (!g_stop_signal && ts_find_tool("norminette", tool, sizeof(tool)))
	{
		args[0] = tool; args[1] = "."; args[2] = NULL;
		if (ts_begin(suite, &test, "Norminette on isolated source", "Norminette exits 0 on the copied source"))
		{
			int ok = ts_capture(suite, &test, args, "", suite->sandbox, 60);
			if (ok) ok = ts_success(&test);
			ts_finish(suite, &test, ok ? TEST_OK : TEST_KO, "%s", ok ? "Norminette succeeded on the tested source copy" : test.reason);
		}
	}
	else if (!g_stop_signal) ts_notice(suite, "Norminette", TEST_SKIP, "norminette is not available in PATH; no Norm compliance is claimed");
	ts_section_end(suite);
}

static void ts_memory_tests(t_test_suite *suite)
{
	char tool[PATH_MAX], log_path[PATH_MAX + 32], log_option[PATH_MAX + 64];
	t_test_case test;
	char *args[16];
	const char *vectors[][6] = {{"9", "-1", "4", "0", "2", NULL}, {"0", "1", "2", NULL}, {NULL}, {"bad", NULL}, {"3", "3", NULL}, {"2147483648", NULL}};
	const char *names[] = {"valid unsorted input", "sorted input", "no arguments", "invalid token", "duplicate", "overflow", "checker valid sequence", "checker invalid instruction"};
	const char *checker_vector[] = {"0", "1", "2", NULL};
	ts_section(suite, "Memory diagnostics (Valgrind)");
	if (!ts_find_tool("valgrind", tool, sizeof(tool)))
	{
		ts_notice(suite, "memory tools", TEST_SKIP, "Valgrind is not available. Memory correctness is unverified; run an appropriate platform memory tool separately.");
		ts_section_end(suite); return ;
	}
	for (int i = 0; i < (suite->checker[0] ? 8 : 6) && !g_stop_signal; i++)
	{
		char name[100]; snprintf(name, sizeof(name), "Valgrind: %s", names[i]);
		if (!ts_begin(suite, &test, name, "no memory errors or remaining allocations on this input; complete Valgrind summary")) break ;
		snprintf(log_path, sizeof(log_path), "%s.valgrind.txt", test.stem);
		snprintf(log_option, sizeof(log_option), "--log-file=%s", log_path);
		args[0] = tool; args[1] = "--leak-check=full"; args[2] = "--show-leak-kinds=all";
		args[3] = "--errors-for-leak-kinds=all"; args[4] = "--error-exitcode=99"; args[5] = log_option; args[6] = i < 6 ? suite->program : suite->checker;
		const char *const *vector = i < 6 ? vectors[i] : checker_vector;
		const char *input = i == 6 ? "pb\npa\n" : (i == 7 ? "invalid\n" : "");
		int j = 0; while (vector[j]) { args[j + 7] = (char *)vector[j]; j++; } args[j + 7] = NULL;
		int ok = ts_capture(suite, &test, args, input, suite->project,
			suite->config->timeout_seconds < 20 ? 20 : suite->config->timeout_seconds);
		size_t size; char *log = ts_read(log_path, &size);
		fprintf(test.trace, "Full memory trace: %s\n", log_path);
		if (!ok) ts_finish(suite, &test, TEST_KO, "%s", test.reason);
		else if (WEXITSTATUS(test.status) == 99) ts_finish(suite, &test, TEST_KO, "Valgrind found memory errors or leaks; inspect %s for stack traces and allocation sites", log_path);
		else if (!log || !strstr(log, "ERROR SUMMARY:")) ts_finish(suite, &test, TEST_SKIP, "Valgrind did not produce a recognizable completion summary; inspect stderr and %s", log_path);
		else if ((i < 3 || i == 6) && WEXITSTATUS(test.status) != 0) ts_finish(suite, &test, TEST_KO, "valid memory-test input exited with status %d; inspect stderr and %s", WEXITSTATUS(test.status), log_path);
		else if (!strstr(log, "ERROR SUMMARY: 0 errors")
			|| !(strstr(log, "All heap blocks were freed") || strstr(log, "in use at exit: 0 bytes")))
			ts_finish(suite, &test, TEST_KO, "nonzero or incomplete memory-clean summary; inspect %s", log_path);
		else ts_finish(suite, &test, TEST_OK, "Valgrind reported zero errors and no remaining allocations for this case");
		free(log);
	}
	ts_section_end(suite);
}

static void ts_checker_tests(t_test_suite *suite)
{
	char *args[16];
	char path[PATH_MAX];
	ts_section(suite, "Optional checker diagnostics");
	if (suite->config->checker_path)
		ts_absolute(suite->config->checker_path, suite->checker, sizeof(suite->checker));
	else
	{
		if (ts_join(path, sizeof(path), suite->project, "checker") && ts_regular(path) && access(path, X_OK) == 0)
			snprintf(suite->checker, sizeof(suite->checker), "%s", path);
		else if (suite->sandbox[0] && ts_join(path, sizeof(path), suite->sandbox, "checker") && ts_regular(path) && access(path, X_OK) == 0)
			snprintf(suite->checker, sizeof(suite->checker), "%s", path);
	}
	if (!suite->checker[0] || !ts_regular(suite->checker) || access(suite->checker, X_OK))
	{
		ts_notice(suite, "checker availability", suite->config->checker_path ? TEST_KO : TEST_SKIP,
			"checker executable is unavailable; pass --checker PATH to test it. An optional checker is not downloaded or substituted.");
		suite->checker[0] = '\0'; ts_section_end(suite); return ;
	}
	args[0] = suite->checker; args[1] = "invalid"; args[2] = NULL;
	ts_exact_case(suite, "checker rejects non-numeric input", args, "", "", "Error\n", 0);
	args[1] = "5"; args[2] = "5"; args[3] = NULL;
	if (!g_stop_signal) ts_exact_case(suite, "checker rejects duplicate integers", args, "", "", "Error\n", 0);
	args[1] = "2147483648"; args[2] = NULL;
	if (!g_stop_signal) ts_exact_case(suite, "checker rejects upper overflow", args, "", "", "Error\n", 0);
	args[1] = "-2147483649";
	if (!g_stop_signal) ts_exact_case(suite, "checker rejects lower overflow", args, "", "", "Error\n", 0);
	args[1] = NULL;
	if (!g_stop_signal) ts_exact_case(suite, "checker with no arguments", args, "", "", "", 1);
	args[1] = "0"; args[2] = "1"; args[3] = "2"; args[4] = NULL;
	const char *invalid[] = {"invalid\n", " sa\n", "sa \n", "sa\t\n", "\n"};
	const char *labels[] = {"unknown instruction", "leading space", "trailing space", "trailing tab", "blank instruction"};
	for (int i = 0; i < 5 && !g_stop_signal; i++)
	{
		char name[100]; snprintf(name, sizeof(name), "checker rejects %s", labels[i]);
		ts_exact_case(suite, name, args, invalid[i], "", "Error\n", 0);
	}
	if (!g_stop_signal) ts_exact_case(suite, "checker accepts already sorted input at EOF", args, "", "OK\n", "", 1);
	if (!g_stop_signal) ts_exact_case(suite, "checker rejects a nonempty B stack", args, "pb\n", "KO\n", "", 1);
	if (!g_stop_signal) ts_exact_case(suite, "checker accepts all eleven operations", args,
		"sa\nsa\nsb\nss\nsa\nrr\nrra\nrrb\nrrr\nra\nrb\npb\npa\n", "OK\n", "", 1);
	args[1] = "0"; args[2] = "1"; args[3] = "2"; args[4] = "3"; args[5] = "4"; args[6] = "5"; args[7] = NULL;
	if (!g_stop_signal) ts_exact_case(suite, "checker operations with both stacks occupied", args,
		"pb\npb\npb\nsa\nsa\nsb\nsb\nss\nss\nra\nrra\nrb\nrrb\nrr\nrrr\npa\npa\npa\n", "OK\n", "", 1);
	/* Each corrective suffix assumes the named operation actually ran. */
	const char *occupied_ops[] = {"sa", "sb", "ss", "ra", "rb", "rr", "rra", "rrb", "rrr"};
	const char *occupied_sequences[] = {
		"pb\npb\npb\nsa\npa\npa\npa\npb\npb\npb\nra\npb\nra\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nsb\npa\npa\npa\npb\nra\npb\nra\nra\nra\npb\npb\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nss\npa\npa\npa\npb\nra\npb\nra\nra\nra\npb\nra\npb\nra\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nra\npa\npa\npa\npb\npb\npb\nra\nra\npb\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nrb\npa\npa\npa\nra\npb\npb\nra\nra\nra\npb\npb\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nrr\npa\npa\npa\nra\npb\npb\nra\nra\nra\npb\nra\nra\npb\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nrra\npa\npa\npa\npb\npb\npb\nra\npb\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nrrb\npa\npa\npa\nra\nra\npb\nra\nra\nra\npb\npb\npb\npb\npb\npa\npa\npa\npa\npa\npa\n",
		"pb\npb\npb\nrrr\npa\npa\npa\nra\nra\npb\nra\nra\nra\npb\npb\nra\npb\npb\npb\npa\npa\npa\npa\npa\npa\n"
	};
	for (int i = 0; i < 9 && !g_stop_signal; i++)
	{
		char name[100]; snprintf(name, sizeof(name), "checker verifies %s on occupied stacks", occupied_ops[i]);
		ts_exact_case(suite, name, args, occupied_sequences[i], "OK\n", "", 1);
	}
	args[1] = "4"; args[2] = "-3"; args[3] = NULL;
	if (!g_stop_signal) ts_exact_case(suite, "checker rejects an unsorted input at EOF", args, "", "KO\n", "", 1);
	if (!g_stop_signal) ts_exact_case(suite, "checker accepts a corrective swap", args, "sa\n", "OK\n", "", 1);
	for (int attempt = 0; attempt < 6 && !g_stop_signal; attempt++)
	{
		int values[5]; char storage[5][16], instructions[1024], name[100];
		int remaining = 5, used = 0;
		if (!ts_random_values(suite, values, 5, 100)) break ;
		args[0] = suite->checker;
		for (int i = 0; i < 5; i++) { snprintf(storage[i], sizeof(storage[i]), "%d", values[i]); args[i + 1] = storage[i]; }
		args[6] = NULL;
		instructions[0] = '\0';
		while (remaining)
		{
			int minimum = 0;
			for (int i = 1; i < remaining; i++) if (values[i] < values[minimum]) minimum = i;
			for (int i = 0; i < minimum; i++)
			{
				int first = values[0]; memmove(values, values + 1, (size_t)(remaining - 1) * sizeof(int)); values[remaining - 1] = first;
				used += snprintf(instructions + used, sizeof(instructions) - (size_t)used, "ra\n");
			}
			memmove(values, values + 1, (size_t)(remaining - 1) * sizeof(int)); remaining--;
			used += snprintf(instructions + used, sizeof(instructions) - (size_t)used, "pb\n");
		}
		for (int i = 0; i < 5; i++) used += snprintf(instructions + used, sizeof(instructions) - (size_t)used, "pa\n");
		snprintf(name, sizeof(name), "checker valid generated sequence %d", attempt + 1);
		ts_exact_case(suite, name, args, instructions, "OK\n", "", 1);
		if (!g_stop_signal)
		{
			snprintf(name, sizeof(name), "checker incorrect generated sequence %d", attempt + 1);
			ts_exact_case(suite, name, args, "pb\n", "KO\n", "", 1);
		}
	}
	ts_section_end(suite);
}

static int ts_init(t_test_suite *suite, t_config *config)
{
	char parent[PATH_MAX], raw_project[PATH_MAX], path[PATH_MAX];
	const char *slash;
	memset(suite, 0, sizeof(*suite));
	suite->config = config; suite->random = *config;
	if (!ts_absolute(config->program, suite->program, sizeof(suite->program))) return (0);
	if (config->project_dir)
	{
		if (!realpath(config->project_dir, suite->project)) return (0);
	}
	else
	{
		snprintf(raw_project, sizeof(raw_project), "%s", suite->program);
		slash = strrchr(raw_project, '/');
		if (!slash) return (0);
		raw_project[slash - raw_project] = '\0';
		if (!realpath(raw_project[0] ? raw_project : "/", suite->project)) return (0);
	}
	struct stat project_stat;
	if (stat(suite->project, &project_stat)) return (0);
	if (!S_ISDIR(project_stat.st_mode)) return (errno = ENOTDIR, 0);
	if (config->report_dir)
	{
		if (!ts_absolute(config->report_dir, parent, sizeof(parent)) || !ts_mkdirs(parent)) return (0);
	}
	else
	{
		const char *cache = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
		char cache_base[PATH_MAX];
		if (cache && cache[0] == '/')
		{
			if (!ts_join(parent, sizeof(parent), cache, "ps-benchmark") || !ts_mkdirs(parent)) return (0);
		}
		else if (home && home[0] == '/')
		{
			if (!ts_join(cache_base, sizeof(cache_base), home, ".cache")
				|| !ts_join(parent, sizeof(parent), cache_base, "ps-benchmark") || !ts_mkdirs(parent)) return (0);
		}
		else if (snprintf(parent, sizeof(parent), "/tmp") >= (int)sizeof(parent)) return (0);
	}
	if (!ts_join(suite->root, sizeof(suite->root), parent, "ps-benchmark-tests-XXXXXX") || !mkdtemp(suite->root)) return (0);
	if (!realpath(suite->root, path)) return (0);
	snprintf(suite->root, sizeof(suite->root), "%s", path);
	if (!ts_join(path, sizeof(path), suite->root, "report.txt")) return (0);
	suite->report = ts_open(path, "w");
	if (!suite->report) return (0);
	fprintf(suite->report, "Push Swap diagnostic suite\nSeed: %u\nRequested executable: %s\nSource root: %s\nTimeout: %d seconds (build/memory tools have a larger minimum)\n",
		config->seed, suite->program, suite->project, config->timeout_seconds);
	return (1);
}

static int run_test_suite(t_config *config)
{
	t_test_suite suite;
	int result;
	if (!ts_init(&suite, config))
		return (fprintf(stderr, "Error: could not initialize diagnostic suite paths or report: %s\n", strerror(errno)), 1);
	printf("Push Swap diagnostic suite\nSeed: %u\nReport: %s/report.txt\n", config->seed, suite.root);
	printf("Every case records its command, stdin, stdout, stderr and diagnosis.\n");
	printf("Source checks use an isolated copy. Required flags are not skipped.\n");
	ts_source_tests(&suite);
	if (!g_stop_signal && ts_regular(suite.program) && access(suite.program, X_OK) == 0)
	{
		printf("\nExecution target: %s\n", suite.program);
		fprintf(suite.report, "\nExecution target: %s\n", suite.program);
		ts_error_tests(&suite);
		if (!g_stop_signal && !suite.infrastructure_error) ts_identity_tests(&suite);
		if (!g_stop_signal && !suite.infrastructure_error) ts_small_tests(&suite);
		if (!g_stop_signal && !suite.infrastructure_error) ts_strategy_tests(&suite);
		if (!g_stop_signal && !suite.infrastructure_error) ts_bench_tests(&suite);
		if (!g_stop_signal && !suite.infrastructure_error) ts_large_tests(&suite);
		if (!g_stop_signal && !suite.infrastructure_error) ts_checker_tests(&suite);
		if (!g_stop_signal && !suite.infrastructure_error) ts_memory_tests(&suite);
	}
	else if (!g_stop_signal)
	{
		char reason[PATH_MAX + 100]; snprintf(reason, sizeof(reason), "executable unavailable after source checks: %s; compile it or pass -b PATH", suite.program);
		ts_notice(&suite, "executable availability", TEST_KO, reason);
		ts_notice(&suite, "execution-dependent tests", TEST_SKIP, "sorting, error, strategy, benchmark, checker and memory cases were not run because the target is unavailable");
	}
	printf("\nSummary: %d OK, %d KO, %d SKIP, %d INFO\n",
		suite.counts[0], suite.counts[1], suite.counts[2], suite.counts[3]);
	fprintf(suite.report, "\nSummary: %d OK, %d KO, %d SKIP, %d INFO\n",
		suite.counts[0], suite.counts[1], suite.counts[2], suite.counts[3]);
	printf("This is a strict diagnostic report, not an official evaluation grade.\n");
	printf("Full report and per-case reproductions: %s\n", suite.root);
	result = suite.counts[TEST_KO] || suite.infrastructure_error ? 1 : (suite.counts[TEST_SKIP] ? 2 : 0);
	if (suite.infrastructure_error) fprintf(stderr, "Tester infrastructure or report-writing error; results are incomplete.\n");
	if (g_stop_signal) { fprintf(suite.report, "Interrupted by signal %d; remaining cases were not run.\n", (int)g_stop_signal); result = 128 + g_stop_signal; }
	int report_error = ferror(suite.report);
	if (fclose(suite.report)) report_error = 1;
	if (report_error) result = 1;
	return (result);
}

/* ===== Entry point: choose diagnostic tests or graph benchmark ===== */

int	main(int argc, char **argv)
{
	t_config	config;
	t_result	*results;
	int			count;
	int			status;

	status = parse_config(argc, argv, &config);
	if (status != 1)
	{
		if (status == 2)
			print_usage(argv[0]);
		else
			print_brief_usage(argv[0]);
		return (status == 2 ? 0 : 1);
	}
	if (!install_stop_signal_handlers())
		return (perror("sigaction"), 1);
	if (config.test_mode)
		return (run_test_suite(&config));
	if (!detect_algorithms(&config) || !validate_workload(&config))
		return (g_stop_signal != 0 ? 128 + g_stop_signal : 1);
	count = result_count(&config);
	results = calloc((size_t)count, sizeof(t_result));
	if (!results)
		return (perror("calloc"), 1);
	set_sizes(&config, results, count);
	print_configuration(&config, count);
	print_terminal_hint(&config, count);
	if (!run_benchmark(&config, results, count))
	{
		free(results);
		if (g_stop_signal != 0)
			return (128 + g_stop_signal);
		return (fprintf(stderr, "Benchmark failed.\n"), 1);
	}
	write_terminal_chart(&config, results, count);
	free(results);
	return (0);
}

