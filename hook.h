#include "config.h"
#include "list.h"
#include "strbuf.h"
#include "strvec.h"
#include "run-command.h"

struct hook
{
	struct list_head list;
	/*
	 * Config file which holds the hook.*.command definition.
	 * (This has nothing to do with the hookcmd.<name>.* configs.)
	 */
	enum config_scope origin;
	/* The literal command to run. */
	struct strbuf command;
	int from_hookdir;

	/*
	 * Use this to keep state for your feed_pipe_fn if you are using
	 * run_hooks_opt.feed_pipe. Otherwise, do not touch it.
	 */
	void *feed_pipe_cb_data;
};

/*
 * Provides a linked list of 'struct hook' detailing commands which should run
 * in response to the 'hookname' event, in execution order.
 */
struct list_head* hook_list(const struct strbuf *hookname);

enum hookdir_opt
{
	HOOKDIR_NO,
	HOOKDIR_WARN,
	HOOKDIR_INTERACTIVE,
	HOOKDIR_YES,
	HOOKDIR_UNKNOWN,
};

/*
 * Provides the hookdir_opt specified in the config without consulting any
 * command line arguments.
 */
enum hookdir_opt configured_hookdir_opt(void);

/* Provides the number of threads to use for parallel hook execution. */
int configured_hook_jobs(void);

struct run_hooks_opt
{
	/* Environment vars to be set for each hook */
	struct strvec env;

	/* Args to be passed to each hook */
	struct strvec args;

	/*
	 * How should the hookdir be handled?
	 * Leave the RUN_HOOKS_OPT_INIT default in most cases; this only needs
	 * to be overridden if the user can override it at the command line.
	 */
	enum hookdir_opt run_hookdir;

	/* Path to file which should be piped to stdin for each hook */
	const char *path_to_stdin;
	/* Pipe each string to stdin, separated by newlines */
	struct string_list str_stdin;
	/*
	 * Callback and state pointer to ask for more content to pipe to stdin.
	 * Will be called repeatedly, for each hook. See
	 * hook.c:pipe_from_stdin() for an example. Keep per-hook state in
	 * hook.feed_pipe_cb_data (per process). Keep initialization context in
	 * feed_pipe_ctx (shared by all processes).
	 */
	feed_pipe_fn feed_pipe;
	void *feed_pipe_ctx;

	/*
	 * Populate this to capture output and prevent it from being printed to
	 * stderr. This will be passed directly through to
	 * run_command:run_parallel_processes(). See t/helper/test-run-command.c
	 * for an example.
	 */
	consume_sideband_fn consume_sideband;

	/* Number of threads to parallelize across */
	int jobs;

	/* Path to initial working directory for subprocess */
	const char *dir;

};

/*
 * Callback provided to feed_pipe_fn and consume_sideband_fn.
 */
struct hook_cb_data {
	int rc;
	struct list_head *head;
	struct list_head *run_me;
	struct run_hooks_opt *options;
};

#define RUN_HOOKS_OPT_INIT_SYNC  {   		\
	.env = STRVEC_INIT, 			\
	.args = STRVEC_INIT, 			\
	.path_to_stdin = NULL,			\
	.jobs = 1,				\
	.dir = NULL,				\
	.str_stdin = STRING_LIST_INIT_DUP,	\
	.feed_pipe = NULL,			\
	.feed_pipe_ctx = NULL,			\
	.consume_sideband = NULL,		\
	.run_hookdir = configured_hookdir_opt()	\
}

#define RUN_HOOKS_OPT_INIT_ASYNC {		\
	.env = STRVEC_INIT, 			\
	.args = STRVEC_INIT, 			\
	.path_to_stdin = NULL,			\
	.jobs = configured_hook_jobs(),		\
	.dir = NULL,				\
	.str_stdin = STRING_LIST_INIT_DUP,	\
	.feed_pipe = NULL,			\
	.feed_pipe_ctx = NULL,			\
	.consume_sideband = NULL,		\
	.run_hookdir = configured_hookdir_opt()	\
}


void run_hooks_opt_init(struct run_hooks_opt *o);
void run_hooks_opt_clear(struct run_hooks_opt *o);

/*
 * Returns 1 if any hooks are specified in the config or if a hook exists in the
 * hookdir. Typically, invoke hook_exsts() like:
 *   hook_exists(hookname, configured_hookdir_opt());
 * Like with run_hooks, if you take a --run-hookdir flag, reflect that
 * user-specified behavior here instead.
 */
int hook_exists(const char *hookname, enum hookdir_opt should_run_hookdir);

/*
 * Runs all hooks associated to the 'hookname' event in order. Each hook will be
 * passed 'env' and 'args'. The file at 'stdin_path' will be closed and reopened
 * for each hook that runs.
 */
int run_hooks(const char *hookname, struct run_hooks_opt *options);

/* Free memory associated with a 'struct hook' */
void free_hook(struct hook *ptr);
/* Empties the list at 'head', calling 'free_hook()' on each entry */
void clear_hook_list(struct list_head *head);
