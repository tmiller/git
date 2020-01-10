#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "string-list.h"
#include "rerere.h"
#include "xdiff/xdiff.h"
#include "xdiff-interface.h"
#include "pathspec.h"
#include "run-command.h"

static const char * const rerere_usage[] = {
	N_("git rerere [--rerere-autoupdate]"),
	N_("git rerere clear"),
	N_("git rerere forget <path>..."),
	N_("git rerere status"),
	N_("git rerere remaining"),
	N_("git rerere diff"),
	N_("git rerere gc"),
	N_("git rerere train [-o | --overwrite] <commit>..."),
	NULL,
};

static const char * const rerere_train_usage[] = {
	N_("git rerere train [<options>] <commit>..."),
	NULL
};

static int outf(void *dummy, mmbuffer_t *ptr, int nbuf)
{
	int i;
	for (i = 0; i < nbuf; i++)
		if (write_in_full(1, ptr[i].ptr, ptr[i].size) < 0)
			return -1;
	return 0;
}

static int diff_two(const char *file1, const char *label1,
		const char *file2, const char *label2)
{
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;
	mmfile_t minus, plus;
	int ret;

	if (read_mmfile(&minus, file1) || read_mmfile(&plus, file2))
		return -1;

	printf("--- a/%s\n+++ b/%s\n", label1, label2);
	fflush(stdout);
	memset(&xpp, 0, sizeof(xpp));
	xpp.flags = 0;
	memset(&xecfg, 0, sizeof(xecfg));
	xecfg.ctxlen = 3;
	ecb.out_hunk = NULL;
	ecb.out_line = outf;
	ret = xdi_diff(&minus, &plus, &xpp, &xecfg, &ecb);

	free(minus.ptr);
	free(plus.ptr);
	return ret;
}

int cmd_rerere(int argc, const char **argv, const char *prefix)
{
	struct string_list merge_rr = STRING_LIST_INIT_DUP;
	int i, autoupdate = -1, flags = 0, overwrite = 0;

	struct option options[] = {
		OPT_SET_INT(0, "rerere-autoupdate", &autoupdate,
			N_("register clean resolutions in index"), 1),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, rerere_usage,
			PARSE_OPT_STOP_AT_NON_OPTION);

	git_config(git_xmerge_config, NULL);

	if (autoupdate == 1)
		flags = RERERE_AUTOUPDATE;
	if (autoupdate == 0)
		flags = RERERE_NOAUTOUPDATE;

	if (argc < 1)
		return repo_rerere(the_repository, flags);

	if (!strcmp(argv[0], "forget")) {
		struct pathspec pathspec;
		if (argc < 2)
			warning(_("'git rerere forget' without paths is deprecated"));
		parse_pathspec(&pathspec, 0, PATHSPEC_PREFER_CWD,
			       prefix, argv + 1);
		return rerere_forget(the_repository, &pathspec);
	}

	if (!strcmp(argv[0], "clear")) {
		rerere_clear(the_repository, &merge_rr);
	} else if (!strcmp(argv[0], "gc"))
		rerere_gc(the_repository, &merge_rr);
	else if (!strcmp(argv[0], "status")) {
		if (setup_rerere(the_repository, &merge_rr,
				 flags | RERERE_READONLY) < 0)
			return 0;
		for (i = 0; i < merge_rr.nr; i++)
			printf("%s\n", merge_rr.items[i].string);

	} else if (!strcmp(argv[0], "remaining")) {
		rerere_remaining(the_repository, &merge_rr);
		for (i = 0; i < merge_rr.nr; i++) {
			if (merge_rr.items[i].util != RERERE_RESOLVED)
				printf("%s\n", merge_rr.items[i].string);
			else
				/* prepare for later call to
				 * string_list_clear() */
				merge_rr.items[i].util = NULL;
		}
	} else if (!strcmp(argv[0], "diff")) {
		if (setup_rerere(the_repository, &merge_rr,
				 flags | RERERE_READONLY) < 0)
			return 0;
		for (i = 0; i < merge_rr.nr; i++) {
			const char *path = merge_rr.items[i].string;
			const struct rerere_id *id = merge_rr.items[i].util;
			if (diff_two(rerere_path(id, "preimage"), path, path, path))
				die(_("unable to generate diff for '%s'"), rerere_path(id, NULL));
		}
	} else if (!strcmp(argv[0], "train")) {

		struct option options[] = {
			OPT_BOOL('o',"overwrite", &overwrite,
					N_("overwrite any existing rerere cache")),
			OPT_END()
		};
		argc = parse_options(argc, argv, NULL, options,
				rerere_train_usage, 0);
		struct argv_array train_argv = ARGV_ARRAY_INIT;

		argv_array_push(&train_argv, "git-rerere--train");
		if (overwrite)
			argv_array_push(&train_argv, "--overwrite");
		argv_array_pushv(&train_argv, argv);

		return run_command_v_opt(train_argv.argv, RUN_USING_SHELL);
	// Does not use autoupdate
	} else
		usage_with_options(rerere_usage, options);

	string_list_clear(&merge_rr, 1);
	return 0;
}
