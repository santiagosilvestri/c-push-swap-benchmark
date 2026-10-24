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
/*                                                                            */
/* ************************************************************************** */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
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

/* ===== benchmark_args.c ===== */

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
		return (set_integer_option(argv, i, &config->timeout_seconds, 1,
				MAX_TIMEOUT));
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

	*config = (t_config){500, 10, 3, 18, 0, 0, 0, DEFAULT_TIMEOUT,
		MASK_ALL, (unsigned int)time(NULL), 0, "./push_swap"};
	i = 1;
	while (i < argc)
	{
		if (is_option(argv[i], "-h", "--help"))
			return (2);
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
	if (access(config->program, X_OK) != 0)
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
}

/* ===== benchmark_utils.c ===== */

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

/* ===== benchmark_verify.c ===== */

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

/* ===== benchmark_exec.c ===== */

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

/* ===== benchmark_progress.c ===== */

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

/* ===== benchmark_terminal.c ===== */

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

/* ===== benchmark.c ===== */

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

