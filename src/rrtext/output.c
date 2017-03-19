/*
 * Copyright 2014-2017 Katherine Flavel
 *
 * See LICENCE for the full copyright terms.
 */

/*
 * Railroad Diagram ASCII-Art Output
 *
 * Output a plaintext diagram of the abstract representation of railroads
 */

#define _BSD_SOURCE

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "../ast.h"
#include "../xalloc.h"

#include "../rrd/rrd.h"
#include "../rrd/pretty.h"
#include "../rrd/node.h"
#include "../rrd/rrd.h"
#include "../rrd/list.h"
#include "../rrd/stack.h"

#include "io.h"

struct render_context {
	char **lines;
	char *scratch;
	int rtl;
	int x, y;
};

static void node_walk_render(const struct node *n, struct render_context *ctx);

static int
bprintf(struct render_context *ctx, const char *fmt, ...)
{
	va_list ap;
	int n;

	assert(ctx != NULL);
	assert(ctx->scratch != NULL);

	va_start(ap, fmt);
	n = vsprintf(ctx->scratch, fmt, ap);
	va_end(ap);

	memcpy(ctx->lines[ctx->y] + ctx->x, ctx->scratch, n);

	return n;
}

static size_t
loop_label(const struct node *loop, char *s)
{
	char buffer[128];

	if (s == NULL) {
		s = buffer;
	}

	if (loop->u.loop.max == 1 && loop->u.loop.min == 1) {
		return sprintf(s, "(exactly once)");
	} else if (loop->u.loop.max == 0 && loop->u.loop.min > 0) {
		return sprintf(s, "(at least %d times)", loop->u.loop.min);
	} else if (loop->u.loop.max > 0 && loop->u.loop.min == 0) {
		return sprintf(s, "(up to %d times)", loop->u.loop.max);
	} else if (loop->u.loop.max > 0 && loop->u.loop.min == loop->u.loop.max) {
		return sprintf(s, "(%d times)", loop->u.loop.max);
	} else if (loop->u.loop.max > 1 && loop->u.loop.min > 1) {
		return sprintf(s, "(%d-%d times)", loop->u.loop.min, loop->u.loop.max);
	}

	return 0;
}

static unsigned
node_walk_dim_w(const struct node *n)
{
	assert(n != NULL);

	switch (n->type) {
		const struct list *p;
		unsigned w;

	case NODE_SKIP:
		return 0;

	case NODE_LITERAL:
		return strlen(n->u.literal) + 4;

	case NODE_RULE:
		return strlen(n->u.name) + 2;

	case NODE_ALT:
		w = 0;

		for (p = n->u.alt; p != NULL; p = p->next) {
			unsigned wn;

			wn = node_walk_dim_w(p->node);
			if (wn > w) {
				w = wn;
			}
		}

		return w + 6;

	case NODE_SEQ:
		w = 0;

		for (p = n->u.seq; p != NULL; p = p->next) {
			w += node_walk_dim_w(p->node) + 2;
		}

		return w - 2;

	case NODE_LOOP:
		{
			unsigned wf, wb, cw;

			wf = node_walk_dim_w(n->u.loop.forward);
			wb = node_walk_dim_w(n->u.loop.backward);

			w = (wf > wb ? wf : wb) + 6;

			cw = loop_label(n, NULL);

			if (cw > 0) {
				if (cw + 6 > w) {
					w = cw + 6;
				}
			}
		}

		return w;
	}
}

static unsigned
node_walk_dim_y(const struct node *n)
{
	assert(n != NULL);

	switch (n->type) {
		const struct list *p;
		unsigned y;

	case NODE_SKIP:
		return 0;

	case NODE_LITERAL:
		return 0;

	case NODE_RULE:
		return 0;

	case NODE_ALT:
		y = 0;

		for (p = n->u.alt; p != NULL; p = p->next) {
			if (p == n->u.alt) {
				if (p->node->type == NODE_SKIP && p->next && !p->next->next) {
					y = 2 + node_walk_dim_y(p->node) + node_walk_dim_y(p->next->node);
				} else {
					y = node_walk_dim_y(p->node);
				}
			}
		}

		return y;

	case NODE_SEQ:
		{
			unsigned top;

			top = 0;

			for (p = n->u.seq; p != NULL; p = p->next) {
				unsigned z;

				z = node_walk_dim_y(p->node);
				if (z > top) {
					top = z;
				}
			}

			return top;
		}

	case NODE_LOOP:
		return node_walk_dim_y(n->u.loop.forward);
	}
}

static unsigned
node_walk_dim_h(const struct node *n)
{
	assert(n != NULL);

	switch (n->type) {
		const struct list *p;
		unsigned h;

	case NODE_SKIP:
		return 1;

	case NODE_LITERAL:
		return 1;

	case NODE_RULE:
		return 1;

	case NODE_ALT:
		h = 0;

		for (p = n->u.alt; p != NULL; p = p->next) {
			h += 1 + node_walk_dim_h(p->node);
		}

		return h - 1;

	case NODE_SEQ:
		{
			unsigned top = 0, bot = 1;

			for (p = n->u.seq; p != NULL; p = p->next) {
				unsigned y, z;

				y = node_walk_dim_y(p->node);
				if (y > top) {
					top = y;
				}

				z = node_walk_dim_h(p->node);
				if (z - y > bot) {
					bot = z - y;
				}
			}

			return bot + top;
		}

		break;

	case NODE_LOOP:
		h = node_walk_dim_h(n->u.loop.forward) + node_walk_dim_h(n->u.loop.backward) + 1;

		if (loop_label(n, NULL) > 0) {
			if (n->u.loop.backward->type != NODE_SKIP) {
				h += 2;
			}
		}

		return h;
	}
}

static void
segment(struct render_context *ctx, const struct node *n, int delim)
{
	int y = ctx->y;
	ctx->y -= node_walk_dim_y(n);
	node_walk_render(n, ctx);

	ctx->x += node_walk_dim_w(n);
	ctx->y = y;
	if (delim) {
		bprintf(ctx, "--");
		ctx->x += 2;
	}
}

static void
justify(struct render_context *ctx, const struct node *n, int space)
{
	int x = ctx->x;
	int off = (space - node_walk_dim_w(n)) / 2;

	for (; ctx->x < x + off; ctx->x++) {
		bprintf(ctx, "-");
	}

	ctx->y -= node_walk_dim_y(n);
	node_walk_render(n, ctx);

	ctx->y += node_walk_dim_y(n);
	ctx->x += node_walk_dim_w(n);
	for (; ctx->x < x + space; ctx->x++) {
		bprintf(ctx, "-");
	}

	ctx->x = x;
}

static void
node_walk_render(const struct node *n, struct render_context *ctx)
{
	assert(n != NULL);

	assert(ctx != NULL);

	switch (n->type) {
		const struct list *p;

	case NODE_LITERAL:
		bprintf(ctx, " \"%s\" ", n->u.literal);

		break;

	case NODE_RULE:
		bprintf(ctx, " %s ", n->u.name);

		break;

	case NODE_ALT:
		{
			int x, y;
			int line;
			char *a_in, *a_out;

			x = ctx->x;
			y = ctx->y;
			line = y + node_walk_dim_y(n);

			/* XXX: suspicious. is n->u.alt->node always present? */
			a_in  = (node_walk_dim_y(n) - node_walk_dim_y(n->u.alt->node)) ? "v" : "^";
			a_out = (node_walk_dim_y(n) - node_walk_dim_y(n->u.alt->node)) ? "^" : "v";

			ctx->y += node_walk_dim_y(n->u.alt->node);

			for (p = n->u.alt; p != NULL; p = p->next) {
				int i, flush = ctx->y == line;

				ctx->x = x;
				if (!ctx->rtl) {
					bprintf(ctx, flush ? a_out : ">");
				} else {
					bprintf(ctx, flush ? "<" : a_in);
				}

				ctx->x += 1;
				justify(ctx, p->node, node_walk_dim_w(n) - 2);

				ctx->x = x + node_walk_dim_w(n) - 1;
				if (!ctx->rtl) {
					bprintf(ctx, flush ? ">" : a_in);
				} else {
					bprintf(ctx, flush ? a_out : "<");
				}
				ctx->y++;

				if (p->next) {
					for (i = 0; i < node_walk_dim_h(p->node) - node_walk_dim_y(p->node) + node_walk_dim_y(p->next->node); i++) {
						ctx->x = x;
						bprintf(ctx, "|");
						ctx->x = x + node_walk_dim_w(n) - 1;
						bprintf(ctx, "|");
						ctx->y++;
					}
				}
			}

			ctx->x = x;
			ctx->y = y;
		}

		break;

	case NODE_SEQ:
		{
			const struct node *q;
			int x, y;

			x = ctx->x;
			y = ctx->y;

			ctx->y += node_walk_dim_y(n);
			if (!ctx->rtl) {
				for (p = n->u.seq; p != NULL; p = p->next) {
					segment(ctx, p->node, !!p->next);
				}
			} else {
				struct stack *rl;

				rl = NULL;

				for (p = n->u.seq; p != NULL; p = p->next) {
					stack_push(&rl, p->node);
				}

				while (q = stack_pop(&rl), q != NULL) {
					segment(ctx, q, !!rl);
				}
			}

			ctx->x = x;
			ctx->y = y;
		}

		break;

	case NODE_LOOP:
		{
			int x = ctx->x, y = ctx->y;
			int i, cw;

			ctx->y += node_walk_dim_y(n);
			bprintf(ctx, !ctx->rtl ? ">" : "v");
			ctx->x += 1;

			justify(ctx, n->u.loop.forward, node_walk_dim_w(n) - 2);
			ctx->x = x + node_walk_dim_w(n) - 1;
			bprintf(ctx, !ctx->rtl ? "v" : "<");
			ctx->y++;

			for (i = 0; i < node_walk_dim_h(n->u.loop.forward) - node_walk_dim_y(n->u.loop.forward) + node_walk_dim_y(n->u.loop.backward); i++) {
				ctx->x = x;
				bprintf(ctx, "|");
				ctx->x = x + node_walk_dim_w(n) - 1;
				bprintf(ctx, "|");
				ctx->y++;
			}

			ctx->x = x;
			bprintf(ctx, !ctx->rtl ? "^" : ">");
			ctx->x += 1;
			ctx->rtl = !ctx->rtl;

			cw = loop_label(n, NULL);

			justify(ctx, n->u.loop.backward, node_walk_dim_w(n) - 2);

			if (cw > 0) {
				int y = ctx->y;
				char c;
				ctx->x = x + 1 + (node_walk_dim_w(n) - cw - 2) / 2;
				if (n->u.loop.backward->type != NODE_SKIP) {
					ctx->y += 2;
				}
				/* still less horrible than malloc() */
				c = ctx->lines[ctx->y][ctx->x + cw];
				loop_label(n, ctx->lines[ctx->y] + ctx->x);
				ctx->lines[ctx->y][ctx->x + cw] = c;
				ctx->y = y;
			}

			ctx->rtl = !ctx->rtl;
			ctx->x = x + node_walk_dim_w(n) - 1;
			bprintf(ctx, !ctx->rtl ? "<" : "^");

			ctx->x = x;
			ctx->y = y;
		}

		break;

	case NODE_SKIP:
		break;
	}
}

void
rrtext_output(const struct ast_rule *grammar)
{
	const struct ast_rule *p;

	for (p = grammar; p; p = p->next) {
		struct node *rrd;

		rrd = ast_to_rrd(p);
		if (rrd == NULL) {
			perror("ast_to_rrd");
			return;
		}

		if (prettify) {
			rrd_pretty(&rrd);
		}

		printf("%s:\n", p->name);

		{
			struct render_context ctx;
			unsigned w, h;
			int i;

			w = node_walk_dim_w(rrd) + 8;
			h = node_walk_dim_h(rrd);

			ctx.lines = xmalloc(sizeof *ctx.lines * h + 1);
			for (i = 0; i < h; i++) {
				ctx.lines[i] = xmalloc(w + 1);
				memset(ctx.lines[i], ' ', w);
				ctx.lines[i][w] = '\0';
			}

			ctx.rtl = 0;
			ctx.x = 0;
			ctx.y = 0;
			ctx.scratch = xmalloc(w + 1);

			ctx.y = node_walk_dim_y(rrd);
			bprintf(&ctx, "||--");

			ctx.x = w - 4;
			bprintf(&ctx, "--||");

			ctx.x = 4;
			ctx.y = 0;
			node_walk_render(rrd, &ctx);

			for (i = 0; i < h; i++) {
				printf("    %s\n", ctx.lines[i]);
				free(ctx.lines[i]);
			}

			free(ctx.lines);
			free(ctx.scratch);
		}

		printf("\n");

		node_free(rrd);
	}
}

