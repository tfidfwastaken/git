#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "dir.h"
#include "exec-cmd.h"
#include "parse-options.h"
#include "refs.h"
#include "revision.h"
#include "run-command.h"
#include "string-list.h"
#include "strvec.h"
#include "submodule.h"
#include "submodule-config.h"

#define OPT_QUIET (1 << 0)
#define OPT_CACHED (1 << 1)
#define OPT_RECURSIVE (1 << 2)
#define OPT_FORCE (1 << 3)

typedef void (*each_submodule_fn)(const struct cache_entry *list_item,
				  void *cb_data);

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

struct module_list {
	const struct cache_entry **entries;
	int alloc, nr;
};
#define MODULE_LIST_INIT { NULL, 0, 0 }

static int module_list_compute(int argc, const char **argv,
			       const char *prefix,
			       struct pathspec *pathspec,
			       struct module_list *list)
{
	int i, result = 0;
	char *ps_matched = NULL;
	parse_pathspec(pathspec, 0,
		       PATHSPEC_PREFER_FULL,
		       prefix, argv);

	if (pathspec->nr)
		ps_matched = xcalloc(pathspec->nr, 1);

	if (read_cache() < 0)
		die(_("index file corrupt"));

	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];

		if (!match_pathspec(&the_index, pathspec, ce->name, ce_namelen(ce),
				    0, ps_matched, 1) ||
		    !S_ISGITLINK(ce->ce_mode))
			continue;

		ALLOC_GROW(list->entries, list->nr + 1, list->alloc);
		list->entries[list->nr++] = ce;
		while (i + 1 < active_nr &&
		       !strcmp(ce->name, active_cache[i + 1]->name))
			/*
			 * Skip entries with the same name in different stages
			 * to make sure an entry is returned only once.
			 */
			i++;
	}

	if (ps_matched && report_path_error(ps_matched, pathspec))
		result = -1;

	free(ps_matched);

	return result;
}

static void for_each_listed_submodule(const struct module_list *list,
				      each_submodule_fn fn, void *cb_data)
{
	int i;
	for (i = 0; i < list->nr; i++)
		fn(list->entries[i], cb_data);
}

/* the result should be freed by the caller. */
static char *get_submodule_displaypath(const char *path, const char *prefix)
{
	const char *super_prefix = get_super_prefix();

	if (prefix && super_prefix) {
		BUG("cannot have prefix '%s' and superprefix '%s'",
		    prefix, super_prefix);
	} else if (prefix) {
		struct strbuf sb = STRBUF_INIT;
		char *displaypath = xstrdup(relative_path(path, prefix, &sb));
		strbuf_release(&sb);
		return displaypath;
	} else if (super_prefix) {
		return xstrfmt("%s%s", super_prefix, path);
	} else {
		return xstrdup(path);
	}
}

static char *compute_rev_name(const char *sub_path, const char* object_id)
{
	struct strbuf sb = STRBUF_INIT;
	const char ***d;

	static const char *describe_bare[] = { NULL };

	static const char *describe_tags[] = { "--tags", NULL };

	static const char *describe_contains[] = { "--contains", NULL };

	static const char *describe_all_always[] = { "--all", "--always", NULL };

	static const char **describe_argv[] = { describe_bare, describe_tags,
						describe_contains,
						describe_all_always, NULL };

	for (d = describe_argv; *d; d++) {
		struct child_process cp = CHILD_PROCESS_INIT;
		prepare_submodule_repo_env(&cp.env_array);
		cp.dir = sub_path;
		cp.git_cmd = 1;
		cp.no_stderr = 1;

		strvec_push(&cp.args, "describe");
		strvec_pushv(&cp.args, *d);
		strvec_push(&cp.args, object_id);

		if (!capture_command(&cp, &sb, 0)) {
			strbuf_strip_suffix(&sb, "\n");
			return strbuf_detach(&sb, NULL);
		}
	}

	strbuf_release(&sb);
	return NULL;
}

struct status_cb {
	const char *prefix;
	unsigned int flags;
};
#define STATUS_CB_INIT { NULL, 0 }

static void print_status(unsigned int flags, char state, const char *path,
			 const struct object_id *oid, const char *displaypath)
{
	if (flags & OPT_QUIET)
		return;

	printf("%c%s %s", state, oid_to_hex(oid), displaypath);

	if (state == ' ' || state == '+') {
		const char *name = compute_rev_name(path, oid_to_hex(oid));

		if (name)
			printf(" (%s)", name);
	}

	printf("\n");
}

static int handle_submodule_head_ref(const char *refname,
				     const struct object_id *oid, int flags,
				     void *cb_data)
{
	struct object_id *output = cb_data;
	if (oid)
		oidcpy(output, oid);

	return 0;
}

static void status_submodule(const char *path, const struct object_id *ce_oid,
			     unsigned int ce_flags, const char *prefix,
			     unsigned int flags)
{
	char *displaypath;
	struct strvec diff_files_args = STRVEC_INIT;
	struct rev_info rev;
	int diff_files_result;
	struct strbuf buf = STRBUF_INIT;
	const char *git_dir;

	if (!submodule_from_path(the_repository, null_oid(), path))
		die(_("no submodule mapping found in .gitmodules for path '%s'"),
		      path);

	displaypath = get_submodule_displaypath(path, prefix);

	if ((CE_STAGEMASK & ce_flags) >> CE_STAGESHIFT) {
		print_status(flags, 'U', path, null_oid(), displaypath);
		goto cleanup;
	}

	strbuf_addf(&buf, "%s/.git", path);
	git_dir = read_gitfile(buf.buf);
	if (!git_dir)
		git_dir = buf.buf;

	if (!is_submodule_active(the_repository, path) ||
	    !is_git_directory(git_dir)) {
		print_status(flags, '-', path, ce_oid, displaypath);
		strbuf_release(&buf);
		goto cleanup;
	}
	strbuf_release(&buf);

	strvec_pushl(&diff_files_args, "diff-files",
		     "--ignore-submodules=dirty", "--quiet", "--",
		     path, NULL);

	git_config(git_diff_basic_config, NULL);

	repo_init_revisions(the_repository, &rev, NULL);
	rev.abbrev = 0;
	diff_files_args.nr = setup_revisions(diff_files_args.nr,
					     diff_files_args.v,
					     &rev, NULL);
	diff_files_result = run_diff_files(&rev, 0);

	if (!diff_result_code(&rev.diffopt, diff_files_result)) {
		print_status(flags, ' ', path, ce_oid,
			     displaypath);
	} else if (!(flags & OPT_CACHED)) {
		struct object_id oid;
		struct ref_store *refs = get_submodule_ref_store(path);

		if (!refs) {
			print_status(flags, '-', path, ce_oid, displaypath);
			goto cleanup;
		}
		if (refs_head_ref(refs, handle_submodule_head_ref, &oid))
			die(_("could not resolve HEAD ref inside the "
			      "submodule '%s'"), path);

		print_status(flags, '+', path, &oid, displaypath);
	} else {
		print_status(flags, '+', path, ce_oid, displaypath);
	}

	if (flags & OPT_RECURSIVE) {
		struct child_process cpr = CHILD_PROCESS_INIT;

		cpr.git_cmd = 1;
		cpr.dir = path;
		prepare_submodule_repo_env(&cpr.env_array);

		strvec_push(&cpr.args, "--super-prefix");
		strvec_pushf(&cpr.args, "%s/", displaypath);
		strvec_pushl(&cpr.args, "submodule--helper", "status",
			     "--recursive", NULL);

		if (flags & OPT_CACHED)
			strvec_push(&cpr.args, "--cached");

		if (flags & OPT_QUIET)
			strvec_push(&cpr.args, "--quiet");

		if (run_command(&cpr))
			die(_("failed to recurse into submodule '%s'"), path);
	}

cleanup:
	strvec_clear(&diff_files_args);
	free(displaypath);
}

static void status_submodule_cb(const struct cache_entry *list_item,
				void *cb_data)
{
	struct status_cb *info = cb_data;
	status_submodule(list_item->name, &list_item->oid, list_item->ce_flags,
			 info->prefix, info->flags);
}

static int module_status(int argc, const char **argv, const char *prefix)
{
	struct status_cb info = STATUS_CB_INIT;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;

	struct option module_status_options[] = {
		OPT__QUIET(&quiet, N_("suppress submodule status output")),
		OPT_BIT(0, "cached", &info.flags, N_("use commit stored in the index instead of the one stored in the submodule HEAD"), OPT_CACHED),
		OPT_BIT(0, "recursive", &info.flags, N_("recurse into nested submodules"), OPT_RECURSIVE),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule status [--quiet] [--cached] [--recursive] [<path>...]"),
		NULL
	};

	if (argc)
		argc = parse_options(argc, argv, prefix, module_status_options,
				     git_submodule_helper_usage, 0);

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0)
		return 1;

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;

	for_each_listed_submodule(&list, status_submodule_cb, &info);

	return 0;
}

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

/* TODO: Remove this function once all the commands have been implemented */
static int command_implemented(const char *command_name)
{
	struct string_list converted = STRING_LIST_INIT_NODUP;

	string_list_append(&converted, "status");

	if(!command_name)
		return 1;

	if(unsorted_string_list_has_string(&converted, command_name))
		return 1;

	return 0;
}

int cmd_submodule(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};

	/*
	 * Tell the rest of git that any URLs we get don't come directly
	 * from the user, so it can apply policy as appropriate.
	 */
	setenv("GIT_PROTOCOL_FROM_USER", "0", 1);

	/*
	 * NEEDSWORK: Once the builtin submodule is robust and complete,
	 * and  git-legacy-submodule.sh is retired, this preamble can
	 * be removed.
	 */
	if (!can_use_builtin_submodule() || !command_implemented(argv[1])) {
		const char *path = mkpath("%s/git-legacy-submodule", git_exec_path());

		if (sane_execvp(path, (char **)argv) < 0)
			die_errno(_("could not exec %s"), path);
		else
			BUG("sane_execvp() returned???");
	}

	prefix = setup_git_directory();
	trace_repo_setup(prefix);
	setup_work_tree();

	argc = parse_options(argc, argv, prefix, options, git_submodule_helper_usage,
			     PARSE_OPT_KEEP_UNKNOWN | PARSE_OPT_KEEP_DASHDASH);

	if (!argc) {
		return module_status(0, NULL, prefix);
	} else if (!strcmp(argv[0], "status")) {
		return module_status(argc, argv, prefix);
	}

	return 0;
}
