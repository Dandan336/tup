/* vim: set ts=8 sw=8 sts=8 noet tw=78:
 *
 * tup - A file-based build system
 *
 * Copyright (C) 2008-2012  Mike Shal <marfey@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "tup/config.h"
#include "tup/lock.h"
#include "tup/monitor.h"
#include "tup/fileio.h"
#include "tup/pel_group.h"
#include "tup/updater.h"
#include "tup/debug.h"
#include "tup/graph.h"
#include "tup/db.h"
#include "tup/init.h"
#include "tup/version.h"
#include "tup/path.h"
#include "tup/entry.h"
#include "tup/varsed.h"
#include "tup/access_event.h"
#include "tup/option.h"
#include "tup/privs.h"
#include "tup/flist.h"

#ifdef _WIN32
#define mkdir(a,b) mkdir(a)
#endif

static int init(int argc, char **argv);
static int graph_cb(void *arg, struct tup_entry *tent, int style);
static int graph(int argc, char **argv);
/* Testing commands */
static int mlink(int argc, char **argv);
static int variant(int argc, char **argv);
static int node_exists(int argc, char **argv);
static int link_exists(int argc, char **argv);
static int touch(int argc, char **argv);
static int node(int argc, char **argv);
static int rm(int argc, char **argv);
static int varshow(int argc, char **argv);
static int dbconfig(int argc, char **argv);
static int options(int argc, char **argv);
static int fake_mtime(int argc, char **argv);
static int fake_parser_version(int argc, char **argv);
static int flush(void);
static int ghost_check(void);
static void print_name(const char *s, char c);

static void version(void);
static void usage(void);

int main(int argc, char **argv)
{
	int rc = 0;
	int x;
	int cmd_arg = 0;
	const char *cmd = NULL;
	int in_valgrind = 0;
	int clear_autoupdate = 0;

	for(x=1; x<argc; x++) {
		if(!cmd && argv[x][0] != '-') {
			cmd = argv[x];
			cmd_arg = x;
		}
		if(strcmp(argv[x], "--debug-sql") == 0) {
			tup_db_enable_sql_debug();
		} else if(strcmp(argv[x], "--debug-fuse") == 0) {
			server_enable_debug();
		}
	}

	if(!cmd) {
		if(argc > 1) {
			if(strcmp(argv[1], "--version") == 0 ||
			   strcmp(argv[1], "-v") == 0) {
				version();
				return 0;
			}
		}
		usage();
		return 1;
	}

	if(compat_init() < 0) {
		fprintf(stderr, "tup error: Unable to initialize compatibility lib\n");
		return -1;
	}

	if(getenv("TUP_VALGRIND")) {
		in_valgrind = 1;
	}

	argc = argc - cmd_arg;
	argv = &argv[cmd_arg];

	/* Commands that can run as a sub-process to tup (eg: in a :-rule) */
	if(strcmp(cmd, "varsed") == 0) {
		if(tup_drop_privs() < 0)
			return 1;
		return varsed(argc, argv);
	}

	/* Check if we are a sub-process by looking for the vardict environment
	 * variable that gets set.
	 */
	if(getenv(TUP_VARDICT_NAME)) {
		fprintf(stderr, "tup error: Command '%s' is not valid when running as a sub-process, or is unknown.\n", cmd);
		return -1;
	}

	/* Commands that don't use a normal tup_init() */
	if(strcmp(cmd, "init") == 0) {
		if(tup_drop_privs() < 0)
			return 1;
		return init(argc, argv);
	} else if(strcmp(cmd, "version") == 0) {
		if(tup_drop_privs() < 0)
			return 1;
		version();
		return 0;
	} else if(strcmp(cmd, "stop") == 0) {
		if(tup_drop_privs() < 0)
			return 1;
		return stop_monitor(TUP_MONITOR_SHUTDOWN);
	}

	if(tup_init() < 0)
		return 1;

	if(strcmp(cmd, "monitor") == 0) {
		rc = monitor(argc, argv);
	} else if(strcmp(cmd, "entry") == 0) {
		struct tup_entry *tent;
		if(gimme_tent(argv[1], &tent) < 0) {
			fprintf(stderr, "No tent :(\n");
			return 1;
		}
		print_tup_entry(stdout, tent);
	} else if(strcmp(cmd, "graph") == 0) {
		rc = graph(argc, argv);
	} else if(strcmp(cmd, "scan") == 0) {
		int pid;
		pid = monitor_get_pid(0);
		if(pid > 0) {
			fprintf(stderr, "tup error: monitor appears to be running as pid %i - not doing scan.\n - Run 'tup stop' if you want to kill the monitor and use scan instead.\n", pid);
			rc = 1;
		} else {
			rc = tup_scan();
		}
	} else if(strcmp(cmd, "link") == 0) {
		rc = mlink(argc, argv);
	} else if(strcmp(cmd, "read") == 0) {
		rc = updater(argc, argv, 1);
	} else if(strcmp(cmd, "parse") == 0) {
		rc = updater(argc, argv, 2);
	} else if(strcmp(cmd, "upd") == 0) {
		rc = updater(argc, argv, 0);
	} else if(strcmp(cmd, "autoupdate") == 0) {
		rc = updater(argc, argv, 0);
		clear_autoupdate = 1;
	} else if(strcmp(cmd, "autoparse") == 0) {
		rc = updater(argc, argv, 2);
		clear_autoupdate = 1;
	} else if(strcmp(cmd, "todo") == 0) {
		rc = todo(argc, argv);
	} else if(strcmp(cmd, "variant") == 0) {
		rc = variant(argc, argv);
	} else if(strcmp(cmd, "node_exists") == 0) {
		rc = node_exists(argc, argv);
	} else if(strcmp(cmd, "link_exists") == 0) {
		rc = link_exists(argc, argv);
	} else if(strcmp(cmd, "flags_exists") == 0) {
		rc = tup_db_check_flags(TUP_FLAGS_CONFIG | TUP_FLAGS_CREATE | TUP_FLAGS_MODIFY);
	} else if(strcmp(cmd, "create_flags_exists") == 0) {
		rc = tup_db_check_flags(TUP_FLAGS_CREATE);
	} else if(strcmp(cmd, "touch") == 0) {
		rc = touch(argc, argv);
	} else if(strcmp(cmd, "node") == 0) {
		rc = node(argc, argv);
	} else if(strcmp(cmd, "rm") == 0) {
		rc = rm(argc, argv);
	} else if(strcmp(cmd, "varshow") == 0) {
		rc = varshow(argc, argv);
	} else if(strcmp(cmd, "dbconfig") == 0) {
		rc = dbconfig(argc, argv);
	} else if(strcmp(cmd, "options") == 0) {
		rc = options(argc, argv);
	} else if(strcmp(cmd, "fake_mtime") == 0) {
		rc = fake_mtime(argc, argv);
	} else if(strcmp(cmd, "fake_parser_version") == 0) {
		rc = fake_parser_version(argc, argv);
	} else if(strcmp(cmd, "flush") == 0) {
		rc = flush();
	} else if(strcmp(cmd, "ghost_check") == 0) {
		rc = ghost_check();
	} else if(strcmp(cmd, "monitor_supported") == 0) {
		rc = monitor_supported();
	} else if(strcmp(cmd, "privileged") == 0) {
		rc = tup_privileged();
	} else {
		fprintf(stderr, "Unknown tup command: %s\n", cmd);
		rc = 1;
	}

	if(clear_autoupdate) {
		if(tup_db_begin() < 0)
			return -1;
		if(tup_db_config_set_int(AUTOUPDATE_PID, -1) < 0) {
			fprintf(stderr, "tup error: Unable to clear the autoupdate pid.\n");
			rc = 1;
		}
		if(tup_db_commit() < 0)
			return -1;
	}

	if(tup_cleanup() < 0)
		rc = 1;
	if(in_valgrind) {
		/* Also close out the standard file descriptors, so valgrind
		 * doesn't complain about those as well. The outputs need to be
		 * flushed, otherwise 'tup options | grep foo' will not see the
		 * output from tup. This is done here rather than tup_cleanup()
		 * because other parts of tup (such as flush()) will call
		 * cleanup and then init again. We only want to close the
		 * standard descriptors once though, so we don't impact other
		 * file descriptors that may have been opened.
		 */
		fflush(stdout);
		fflush(stderr);
		close(STDERR_FILENO);
		close(STDOUT_FILENO);
		close(STDIN_FILENO);
	}
	return rc;
}

static int init(int argc, char **argv)
{
	int x;
	int db_sync = 1;
	int force_init = 0;
	int fd;

	for(x=0; x<argc; x++) {
		if(strcmp(argv[x], "--no-sync") == 0) {
			db_sync = 0;
		} else if(strcmp(argv[x], "--force") == 0) {
			/* force should only be used for tup/test */
			force_init = 1;
		}
	}

	fd = open(".", O_RDONLY);
	if(fd < 0) {
		perror(".");
		return -1;
	}

	if(!force_init && find_tup_dir() == 0) {
		char wd[PATH_MAX];
		if(getcwd(wd, sizeof(wd)) == NULL) {
			perror("getcwd");
			fprintf(stderr, "tup error: database already exists somewhere up the tree.\n");
		} else {
			fprintf(stderr, "tup error: database already exists in directory: %s\n", wd);
		}
		goto err_close;
	}
	if(fchdir(fd) < 0) {
		perror("fchdir");
		goto err_close;
	}
	if(close(fd) < 0) {
		perror("close(fd)");
		return -1;
	}

	if(mkdir(TUP_DIR, 0777) != 0) {
		perror(TUP_DIR);
		return -1;
	}

	if(tup_db_create(db_sync) != 0) {
		return -1;
	}

	if(creat(TUP_OBJECT_LOCK, 0666) < 0) {
		perror(TUP_OBJECT_LOCK);
		return -1;
	}
	if(creat(TUP_SHARED_LOCK, 0666) < 0) {
		perror(TUP_SHARED_LOCK);
		return -1;
	}
	if(creat(TUP_TRI_LOCK, 0666) < 0) {
		perror(TUP_TRI_LOCK);
		return -1;
	}
	if(!db_sync) {
		FILE *f = fopen(TUP_OPTIONS_FILE, "w");
		if(!f) {
			perror(TUP_OPTIONS_FILE);
			return -1;
		}
		fprintf(f, "[db]\n");
		fprintf(f, "\tsync = false\n");
		fclose(f);
	}
	return 0;

err_close:
	close(fd);
	return -1;
}

static int graph_cb(void *arg, struct tup_entry *tent, int style)
{
	struct graph *g = arg;
	struct node *n;

	n = find_node(g, tent->tnode.tupid);
	if(n != NULL)
		goto edge_create;
	n = create_node(g, tent);
	if(!n)
		return -1;

edge_create:
	if(style & TUP_LINK_NORMAL && n->expanded == 0) {
		n->expanded = 1;
		TAILQ_REMOVE(&g->node_list, n, list);
		TAILQ_INSERT_HEAD(&g->plist, n, list);
	}
	if(g->cur)
		if(create_edge(g->cur, n, style) < 0)
			return -1;
	return 0;
}

static int graph(int argc, char **argv)
{
	int x;
	struct graph g;
	struct node *n;
	int show_dirs;
	int show_ghosts;
	int show_env;
	int default_graph = 1;
	tupid_t tupid;
	tupid_t sub_dir_dt;

	if(tup_db_begin() < 0)
		return -1;
	show_dirs = tup_option_get_int("graph.dirs");
	show_ghosts = tup_option_get_int("graph.ghosts");
	show_env = tup_option_get_int("graph.environment");

	if(create_graph(&g, 0) < 0)
		return -1;

	sub_dir_dt = get_sub_dir_dt();
	if(sub_dir_dt < 0)
		return -1;

	for(x=1; x<argc; x++) {
		struct tup_entry *tent;

		if(strcmp(argv[x], "--dirs") == 0) {
			show_dirs = 1;
			continue;
		}
		if(strcmp(argv[x], "--ghosts") == 0) {
			show_ghosts = 1;
			continue;
		}
		if(strcmp(argv[x], "--env") == 0) {
			show_env = 1;
			continue;
		}

		tent = get_tent_dt(sub_dir_dt, argv[x]);
		if(!tent) {
			fprintf(stderr, "Unable to find tupid for: '%s'\n", argv[x]);
			return -1;
		}

		n = find_node(&g, tent->tnode.tupid);
		if(n == NULL) {
			n = create_node(&g, tent);
			if(!n)
				return -1;
			n->expanded = 1;
			TAILQ_REMOVE(&g.node_list, n, list);
			TAILQ_INSERT_HEAD(&g.plist, n, list);
		}
		default_graph = 0;
	}
	if(default_graph) {
		if(tup_db_select_node_by_flags(graph_cb, &g, TUP_FLAGS_CREATE) < 0)
			return -1;
		if(tup_db_select_node_by_flags(graph_cb, &g, TUP_FLAGS_MODIFY) < 0)
			return -1;
	}

	while(!TAILQ_EMPTY(&g.plist)) {
		g.cur = TAILQ_FIRST(&g.plist);
		if(tup_db_select_node_by_link(graph_cb, &g, g.cur->tnode.tupid) < 0)
			return -1;
		TAILQ_REMOVE(&g.plist, g.cur, list);
		TAILQ_INSERT_HEAD(&g.node_list, g.cur, list);

		tupid = g.cur->tnode.tupid;
		g.cur = NULL;
		if(show_dirs)
			if(tup_db_select_node_dir(graph_cb, &g, tupid) < 0)
				return -1;
	}

	printf("digraph G {\n");
	TAILQ_FOREACH(n, &g.node_list, list) {
		int color;
		int fontcolor;
		const char *shape;
		const char *style;
		char *s;
		struct edge *e;
		int flags;

		if(n == g.root)
			continue;

		if(!show_env) {
			if(n->tent->tnode.tupid == env_dt() ||
			   n->tent->dt == env_dt())
				continue;
		}

		style = "solid";
		color = 0;
		fontcolor = 0;
		switch(n->tent->type) {
			case TUP_NODE_FILE:
			case TUP_NODE_GENERATED:
				/* Skip Tupfiles in no-dirs mode since they
				 * point to directories.
				 */
				if(!show_dirs && strcmp(n->tent->name.s, "Tupfile") == 0)
					continue;
				shape = "oval";
				break;
			case TUP_NODE_CMD:
				shape = "rectangle";
				break;
			case TUP_NODE_DIR:
				if(!show_dirs)
					continue;
				shape = "diamond";
				break;
			case TUP_NODE_VAR:
				shape = "octagon";
				break;
			case TUP_NODE_GHOST:
				if(!show_ghosts)
					continue;
				/* Ghost nodes won't have flags set */
				color = 0x888888;
				fontcolor = 0x888888;
				style = "dotted";
				shape = "oval";
				break;
			default:
				shape="ellipse";
		}

		flags = tup_db_get_node_flags(n->tnode.tupid);
		if(flags & TUP_FLAGS_MODIFY) {
			color |= 0x0000ff;
			style = "dashed";
		}
		if(flags & TUP_FLAGS_CREATE) {
			color |= 0x00ff00;
			style = "dashed peripheries=2";
		}
		if(n->expanded == 0) {
			if(color == 0) {
				color = 0x888888;
				fontcolor = 0x888888;
			} else {
				/* Might only be graphing a subset. Ie:
				 * graph node foo, which points to command bar,
				 * and command bar is in the modify list. In
				 * this case, bar won't be expanded.
				 */
			}
		}
		printf("\tnode_%lli [label=\"", n->tnode.tupid);
		s = n->tent->name.s;
		if(s[0] == '^') {
			s++;
			while(*s && *s != ' ') {
				/* Skip flags (Currently there are none) */
				s++;
			}
			print_name(s, '^');
		} else {
			print_name(s, 0);
		}
		printf("\\n%lli\" shape=\"%s\" color=\"#%06x\" fontcolor=\"#%06x\" style=%s];\n", n->tnode.tupid, shape, color, fontcolor, style);
		if(show_dirs && n->tent->dt) {
			struct node *tmp;
			tmp = find_node(&g, n->tent->dt);
			if(tmp)
				printf("\tnode_%lli -> node_%lli [dir=back color=\"#888888\" arrowtail=odot]\n", n->tnode.tupid, n->tent->dt);
		}

		LIST_FOREACH(e, &n->edges, list) {
			printf("\tnode_%lli -> node_%lli [dir=back,style=\"%s\",arrowtail=\"%s\"]\n", e->dest->tnode.tupid, n->tnode.tupid, (e->style == TUP_LINK_STICKY) ? "dotted" : "solid", (e->style & TUP_LINK_STICKY) ? "normal" : "empty");
		}
	}
	printf("}\n");
	destroy_graph(&g);
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int mlink(int argc, char **argv)
{
	/* This only works for files in the top-level directory. It's only
	 * used by the benchmarking suite, and in fact may just disappear
	 * entirely. I wouldn't use it for any other purpose.
	 */
	int type;
	int x;
	tupid_t cmdid;
	struct tup_entry *tent;

	if(argc < 4) {
		fprintf(stderr, "Usage: %s cmd -iread_file -owrite_file\n",
			argv[0]);
		return 1;
	}

	if(tup_db_begin() < 0)
		return -1;
	if(tup_entry_add(DOT_DT, NULL) < 0)
		return -1;
	cmdid = create_command_file(DOT_DT, argv[1]);
	if(cmdid < 0) {
		return -1;
	}

	for(x=2; x<argc; x++) {
		char *name = argv[x];
		if(name[0] == '-') {
			if(name[1] == 'i') {
				type = 0;
			} else if(name[1] == 'o') {
				type = 1;
			} else {
				fprintf(stderr, "Invalid argument: '%s'\n",
					name);
				return 1;
			}
		} else {
			fprintf(stderr, "Invalid argument: '%s'\n", name);
			return 1;
		}

		if(tup_db_select_tent(DOT_DT, name+2, &tent) < 0)
			return -1;
		if(!tent)
			return 1;

		if(type == 0) {
			if(tup_db_create_link(tent->tnode.tupid, cmdid, TUP_LINK_NORMAL) < 0)
				return -1;
		} else {
			if(tup_db_create_link(cmdid, tent->tnode.tupid, TUP_LINK_NORMAL) < 0)
				return -1;
		}
	}
	if(tup_db_commit() < 0)
		return -1;

	return 0;
}

static int dir_empty(const char *dirname)
{
	struct flist f = {0, 0, 0};
	if(chdir(dirname) < 0) {
		perror(dirname);
		fprintf(stderr, "tup error: Unable to chdir to variant directory.\n");
		return -1;
	}
	flist_foreach(&f, ".") {
		if(f.filename[0] == '.')
			continue;
		return 0;
	}
	if(fchdir(tup_top_fd()) < 0) {
		perror("fchdir");
		fprintf(stderr, "tup error: Unable to fchdir to the top of the tup hierarchy.\n");
		return -1;
	}
	return 1;
}

static int create_variant(const char *config_path)
{
	char dirname[PATH_MAX];
	char linkpath[PATH_MAX];
	char linkdest[PATH_MAX];
	const char *dot;
	const char *last_slash;
	const char *filename;

	last_slash = strrchr(config_path, '/');
	if(last_slash) {
		filename = last_slash + 1;
	} else {
		filename = config_path;
	}

	dot = strchr(filename, '.');
	if(dot) {
		int len = dot-filename;
		snprintf(dirname, sizeof(dirname), "build-%.*s", len, filename);
	} else {
		snprintf(dirname, sizeof(dirname), "build-%s", filename);
	}
	dirname[sizeof(dirname)-1] = 0;
	if(fchdir(tup_top_fd()) < 0) {
		perror("fchdir");
		fprintf(stderr, "tup error: Unable to fchdir to the top of the tup hierarchy.\n");
		return -1;
	}
	if(mkdir(dirname, 0777) < 0) {
		if(errno == EEXIST) {
			int rc;
			rc = dir_empty(dirname);
			if(rc < 0)
				return -1;
			if(!rc) {
				fprintf(stderr, "tup error: Variant directory '%s' already exists and is not empty.\n", dirname);
				return -1;
			}
		} else {
			perror(dirname);
			fprintf(stderr, "tup error: Unable to create variant directory '%s' at the top of the tup hierarchy.\n", dirname);
			return -1;
		}
	}
	if(get_sub_dir_len()) {
		if(snprintf(linkpath, sizeof(linkpath), "../%s/%s", get_sub_dir(), config_path) >= (signed)sizeof(linkpath)) {
			fprintf(stderr, "tup error: linkpath is too small to fit the tup.config symlink path.\n");
			return -1;
		}
	} else {
		if(snprintf(linkpath, sizeof(linkpath), "../%s", config_path) >= (signed)sizeof(linkpath)) {
			fprintf(stderr, "tup error: linkpath is too small to fit the tup.config symlink path.\n");
			return -1;
		}
	}
	if(snprintf(linkdest, sizeof(linkdest), "%s/tup.config", dirname) >= (signed)sizeof(linkdest)) {
		fprintf(stderr, "tup error: linkdest is too small to fit the tup.config symlink destination.\n");
		return -1;
	}
	if(symlink(linkpath, linkdest) < 0) {
		perror(linkdest);
		fprintf(stderr, "tup error: Unable to create tup.config symlink for config file: %s\n", config_path);
		return -1;
	}
	printf("tup: Added variant '%s' using config file '%s'\n", dirname, config_path);
	return 0;
}

static int variant(int argc, char **argv)
{
	int x;

	if(argc < 2) {
		fprintf(stderr, "Usage: variant foo.config [bar.config] [...]\n");
		fprintf(stderr, "This will create a build-foo directory with a tup.config symlink to foo.config\n");
		return -1;
	}
	if(tup_db_begin() < 0)
		return -1;
	for(x=1; x<argc; x++) {
		if(create_variant(argv[x]) < 0)
			return -1;
	}
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int node_exists(int argc, char **argv)
{
	int x;
	struct tup_entry *tent;
	tupid_t dt;

	if(tup_db_begin() < 0)
		return -1;
	if(argc < 3) {
		fprintf(stderr, "Usage: node_exists dir [n1] [n2...]\n");
		return -1;
	}
	dt = find_dir_tupid(argv[1]);
	if(dt < 0)
		return -1;
	argv++;
	argc--;
	for(x=1; x<argc; x++) {
		char *p = argv[x];
		/* Path replacement is a hack for windows to work. This is
		 * only used by test code to check that commands & files
		 * actually make it into the database. But for wildcarding,
		 * windows will use '\\' instead of '/' for the separator, so
		 * we have to replace those in the strings. This is
		 * potentially the wrong thing to do in some situations, but
		 * only test code will break.
		 */
		while((p = strchr(p, '/')) != NULL) {
			/* Don't translate "./foo" to ".\foo" */
			if(p == argv[x] || p[-1] != '.') {
				*p = PATH_SEP;
			}
			p++;
		}
		if(tup_db_select_tent(dt, argv[x], &tent) < 0)
			return -1;
		if(!tent)
			return -1;
	}
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int link_exists(int argc, char **argv)
{
	struct tup_entry *tenta;
	struct tup_entry *tentb;
	int rc;
	tupid_t dta, dtb;

	if(tup_db_begin() < 0)
		return -1;
	if(argc != 5) {
		fprintf(stderr, "tup error: link_exists requires two dir/name pairs.\n");
		return -1;
	}
	dta = find_dir_tupid(argv[1]);
	if(dta < 0) {
		fprintf(stderr, "[31mError: dir '%s' doesn't exist.[0m\n", argv[1]);
		return -1;
	}

	if(tup_db_select_tent(dta, argv[2], &tenta) < 0)
		return -1;
	if(!tenta) {
		fprintf(stderr, "[31mError: node '%s' doesn't exist.[0m\n", argv[2]);
		return -1;
	}

	dtb = find_dir_tupid(argv[3]);
	if(dtb < 0) {
		fprintf(stderr, "[31mError: dir '%s' doesn't exist.[0m\n", argv[3]);
		return -1;
	}

	if(tup_db_select_tent(dtb, argv[4], &tentb) < 0)
		return -1;
	if(!tentb) {
		fprintf(stderr, "[31mError: node '%s' doesn't exist.[0m\n", argv[4]);
		return -1;
	}
	rc = tup_db_link_exists(tenta->tnode.tupid, tentb->tnode.tupid);
	if(tup_db_commit() < 0)
		return -1;

	return rc;
}

static int touch(int argc, char **argv)
{
	int x;
	tupid_t sub_dir_dt;

	if(tup_db_begin() < 0)
		return -1;
	if(chdir(get_sub_dir()) < 0) {
		perror("chdir");
		return -1;
	}
	sub_dir_dt = get_sub_dir_dt();
	if(sub_dir_dt < 0)
		return -1;

	for(x=1; x<argc; x++) {
		struct stat buf;
		struct path_element *pel = NULL;
		tupid_t dt;

		if(lstat(argv[x], &buf) < 0) {
			close(open(argv[x], O_WRONLY | O_CREAT, 0666));
			if(lstat(argv[x], &buf) < 0) {
				fprintf(stderr, "lstat: ");
				perror(argv[x]);
				return -1;
			}
		}

		dt = find_dir_tupid_dt(sub_dir_dt, argv[x], &pel, 0, 0);
		if(dt <= 0) {
			fprintf(stderr, "Error finding dt for dir '%s' relative to dir %lli\n", argv[x], sub_dir_dt);
			return -1;
		}
		if(S_ISDIR(buf.st_mode)) {
			if(tup_db_create_node(dt, pel->path, TUP_NODE_DIR) == NULL)
				return -1;
		} else if(S_ISREG(buf.st_mode) || S_ISLNK(buf.st_mode)) {
			if(tup_file_mod_mtime(dt, pel->path, buf.MTIME, 1, 0, NULL) < 0)
				return -1;
		}
		free(pel);
	}
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int node(int argc, char **argv)
{
	int x;
	tupid_t sub_dir_dt;

	if(tup_db_begin() < 0)
		return -1;
	sub_dir_dt = get_sub_dir_dt();
	if(sub_dir_dt < 0)
		return -1;

	for(x=1; x<argc; x++) {
		tupid_t dt;
		struct path_element *pel = NULL;

		dt = find_dir_tupid_dt(sub_dir_dt, argv[x], &pel, 0, 0);
		if(dt <= 0) {
			fprintf(stderr, "Unable to find dir '%s' relative to %lli\n", argv[x], sub_dir_dt);
			return -1;
		}
		if(create_name_file(dt, pel->path, -1, NULL) < 0) {
			fprintf(stderr, "Unable to create node for '%s' in dir %lli\n", pel->path, dt);
			return -1;
		}
		free(pel);
	}
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int rm(int argc, char **argv)
{
	int x;
	tupid_t sub_dir_dt;

	if(tup_db_begin() < 0)
		return -1;
	sub_dir_dt = get_sub_dir_dt();
	if(sub_dir_dt < 0)
		return -1;

	for(x=1; x<argc; x++) {
		struct path_element *pel = NULL;
		tupid_t dt;

		dt = find_dir_tupid_dt(sub_dir_dt, argv[x], &pel, 0, 0);
		if(dt < 0) {
			fprintf(stderr, "Unable to find dir '%s' relative to %lli\n", argv[x], sub_dir_dt);
			return -1;
		}
		if(pel) {
			if(tup_file_del(dt, pel->path, pel->len, NULL) < 0)
				return -1;
			free(pel);
		}
	}
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int varshow_cb(void *arg, tupid_t tupid, const char *var, const char *value, int type)
{
	const char *color1 = "";
	const char *color2 = "";
	if(arg) {}
	if(tupid) {}
	if(type == TUP_NODE_GHOST) {
		color1 = "[47;30m";
		color2 = "[0m";
	}
	printf(" - Var[%s%s%s] = '%s'\n", color1, var, color2, value);
	return 0;
}

static int varshow(int argc, char **argv)
{
	struct tup_entry *vartent;
	if(tup_db_begin() < 0)
		return -1;
	if(tup_db_select_tent(DOT_DT, TUP_CONFIG, &vartent) < 0)
		return -1;
	if(vartent) {
		if(argc == 1) {
			if(tup_db_var_foreach(vartent->tnode.tupid, varshow_cb, NULL) < 0)
				return -1;
		} else {
			int x;
			struct tup_entry *tent;
			for(x=1; x<argc; x++) {
				char *value;
				if(tup_db_select_tent(vartent->tnode.tupid, argv[x], &tent) < 0)
					return -1;
				if(!tent) {
					fprintf(stderr, "Unable to find tupid for variable '%s'\n", argv[x]);
					continue;
				}
				if(tent->type == TUP_NODE_VAR) {
					if(tup_db_get_var_id_alloc(tent->tnode.tupid, &value) < 0)
						return -1;
					printf(" - Var[%s] = '%s'\n", argv[x], value);
					free(value);
				} else if(tent->type == TUP_NODE_GHOST) {
					printf(" - Var[[47;30m%s[0m] is a ghost\n", argv[x]);
				} else {
					fprintf(stderr, "Variable '%s' has unknown type %i\n", argv[x], tent->type);
				}
			}
		}
	}
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int dbconfig(int argc, char **argv)
{
	if(argv) {}
	if(argc == 1) {
		if(tup_db_show_config() < 0)
			return -1;
	} else {
		fprintf(stderr, "tup error: 'dbconfig' doesn't take arguments.\n");
		return -1;
	}
	return 0;
}

static int options(int argc, char **argv)
{
	if(argv) {}
	if(argc == 1) {
		if(tup_option_show() < 0)
			return -1;
	} else {
		fprintf(stderr, "tup error: 'options' doesn't take arguments.\n");
		return -1;
	}
	return 0;
}

static int fake_mtime(int argc, char **argv)
{
	struct tup_entry *tent;
	time_t mtime;
	tupid_t dt;
	tupid_t sub_dir_dt;
	struct path_element *pel = NULL;

	if(argc != 3) {
		fprintf(stderr, "tup error: fake_mtime requires a file and an mtime.\n");
		return -1;
	}
	if(tup_db_begin() < 0)
		return -1;
	sub_dir_dt = get_sub_dir_dt();
	if(sub_dir_dt < 0)
		return -1;
	dt = find_dir_tupid_dt(sub_dir_dt, argv[1], &pel, 0, 1);
	if(dt < 0) {
		fprintf(stderr, "tup error: Unable to find dt for node: %s\n", argv[1]);
		return -1;
	}
	if(tup_db_select_tent_part(dt, pel->path, pel->len, &tent) < 0) {
		fprintf(stderr, "Unable to find node '%.*s' in dir %lli\n", pel->len, pel->path, dt);
		return -1;
	}
	mtime = strtol(argv[2], NULL, 0);
	if(tup_db_set_mtime(tent, mtime) < 0)
		return -1;
	free(pel);
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int fake_parser_version(int argc, char **argv)
{
	if(argc || argv) {}
	if(tup_db_begin() < 0)
		return -1;
	if(tup_db_config_set_int("parser_version", 0) < 0)
		return -1;
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static int flush(void)
{
	int autoupdate_pid;
	printf("Flush\n");
	while(1) {
		if(tup_db_begin() < 0)
			return -1;
		if(tup_db_config_get_int(AUTOUPDATE_PID, -1, &autoupdate_pid) < 0)
			return -1;
		if(tup_db_commit() < 0)
			return -1;
		if(autoupdate_pid < 0)
			break;
		printf(" -- flush (try again)\n");
		/* If we got the lock but autoupdate pid was set, it must've
		 * just started but not gotten the lock yet.  So we need to
		 * release our lock and wait a bit.
		 */
		tup_cleanup();
		usleep(10000);
		tup_init();
	}
	printf("Flushed.\n");
	return 0;
}

static int ghost_check(void)
{
	if(tup_db_begin() < 0)
		return -1;
	if(tup_db_debug_add_all_ghosts() < 0)
		return -1;
	if(tup_db_commit() < 0)
		return -1;
	return 0;
}

static void print_name(const char *s, char c)
{
	for(; *s && *s != c; s++) {
		if(*s == '"') {
			printf("\\\"");
		} else if(*s == '\\') {
			printf("\\\\");
		} else {
			printf("%c", *s);
		}
	}
}

static void version(void)
{
	printf("tup %s\n", tup_version());
}

static void usage(void)
{
	printf("tup %s usage: tup command [args]\n", tup_version());
	printf("Where command is:\n");
	printf("  init		Initialize the tup database in .tup/\n");
	printf("  upd		Update everything according to the Tupfiles\n");
	printf("For information on Tupfiles and other commands, see the tup(1) man page.\n");
}
