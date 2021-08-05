#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "exec-cmd.h"
#include "parse-options.h"
#include "run-command.h"
#include "strvec.h"

static const char * const git_submodule_helper_usage[] = {
	N_("git submodule [--quiet] add [-b <branch>] [-f|--force] [--name <name>] "
	   "[--reference <repository>] [--] <repository> [<path>]"),
	N_("git submodule [--quiet] status [--cached] [--recursive] [--] [<path>...]"),
	N_("git submodule [--quiet] init [--] [<path>...]"),
	N_("git submodule [--quiet] deinit [-f|--force] (--all| [--] <path>...)"),
	N_("git submodule [--quiet] update [--init] [--remote] [-N|--no-fetch] "
	   "[-f|--force] [--checkout|--merge|--rebase] [--[no-]recommend-shallow] "
	   "[--reference <repository>] [--recursive] [--[no-]single-branch] [--] [<path>...]"),
	N_("git submodule [--quiet] set-branch (--default|--branch <branch>) [--] <path>"),
	N_("git submodule [--quiet] set-url [--] <path> <newurl>"),
	N_("git submodule [--quiet] summary [--cached|--files] [--summary-limit <n>] "
	   "[commit] [--] [<path>...]"),
	N_("git submodule [--quiet] foreach [--recursive] <command>"),
	N_("git submodule [--quiet] sync [--recursive] [--] [<path>...]"),
	N_("git submodule [--quiet] absorbgitdirs [--] [<path>...]"),
};

static int can_use_builtin_submodule(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT;
	int env = git_env_bool("GIT_TEST_SUBMODULE_USE_BUILTIN", -1);
	int ret;

	if (env != -1)
		return env;

	cp.git_cmd = 1;
	strvec_pushl(&cp.args, "config", "--type=bool", "submodule.useBuiltin", NULL);

	if (capture_command(&cp, &out, 6)) {
		strbuf_release(&out);
		return 0;
	}

	strbuf_trim(&out);
	ret = !strcmp("true", out.buf);
	strbuf_release(&out);
	return ret;
}

int cmd_submodule(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, git_submodule_helper_usage,
			     PARSE_OPT_KEEP_ARGV0 | PARSE_OPT_KEEP_UNKNOWN |
			     PARSE_OPT_KEEP_DASHDASH);

	/*
	 * NEEDSWORK: Once the builtin submodule is robust and complete,
	 * and  git-legacy-submodule.sh is retired, this preamble can
	 * be removed.
	 */
	if (!can_use_builtin_submodule()) {
		const char *path = mkpath("%s/git-legacy-submodule", git_exec_path());

		if (sane_execvp(path, (char **)argv) < 0)
			die_errno(_("could not exec %s"), path);
		else
			BUG("sane_execvp() returned???");
	}

	die("TODO: Work in progress functionality. If you are surprised by this, "
	    "set submodule.useBuiltin=false in your git config.");

	return 0;
}
