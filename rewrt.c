// SPDX-License-Identifier: GPL-2.0+
/***** ltl2ba : rewrt.c *****/

/* Written by Denis Oddoux, LIAFA, France                                 */
/* Copyright (c) 2001  Denis Oddoux                                       */
/* Modified by Paul Gastin, LSV, France                                   */
/* Copyright (c) 2007  Paul Gastin                                        */
/*                                                                        */
/* Some of the code in this file was taken from the Spin software         */
/* Written by Gerard J. Holzmann, Bell Laboratories, U.S.A.               */

#include "internal.h"

static void
sdump(Node *n, char *dumpbuf)
{
	switch (n->ntyp) {
	case PREDICATE:	strcat(dumpbuf, n->sym->name);
			break;
	case U_OPER:	strcat(dumpbuf, "U");
			goto common2;
	case V_OPER:	strcat(dumpbuf, "V");
			goto common2;
	case OR:	strcat(dumpbuf, "|");
			goto common2;
	case AND:	strcat(dumpbuf, "&");
common2:		sdump(n->rgt, dumpbuf);
common1:		sdump(n->lft, dumpbuf);
			break;
	case NEXT:	strcat(dumpbuf, "X");
			goto common1;
	case NOT:	strcat(dumpbuf, "!");
			goto common1;
	case TRUE:	strcat(dumpbuf, "T");
			break;
	case FALSE:	strcat(dumpbuf, "F");
			break;
	default:	strcat(dumpbuf, "?");
			break;
	}
}

static Symbol *
DoDump(Symtab symtab, Node *n)
{
	if (!n) return NULL;

	if (n->ntyp == PREDICATE)
		return n->sym;

	char dumpbuf[2048];
	dumpbuf[0] = '\0';
	sdump(n, dumpbuf);
	return tl_lookup(symtab, dumpbuf);
}

Node *
right_linked(Node *n)
{
	if (!n) return n;

	if (n->ntyp == AND || n->ntyp == OR)
		while (n->lft && n->lft->ntyp == n->ntyp)
		{	Node *tmp = n->lft;
			n->lft = tmp->rgt;
			tmp->rgt = n;
			n = tmp;
		}

	n->lft = right_linked(n->lft);
	n->rgt = right_linked(n->rgt);

	return n;
}

Node *
canonical(Symtab symtab, Node *n)
{	Node *m;	/* assumes input is right_linked */

	if (!n) return n;
	if ((m = in_cache(n)))
		return m;

	n->rgt = canonical(symtab, n->rgt);
	n->lft = canonical(symtab, n->lft);

	return cached(symtab, n);
}

Node *
push_negation(Symtab symtab, Node *n)
{	Node *m;

	Assert(n->ntyp == NOT, n->ntyp);

	switch (n->lft->ntyp) {
	case TRUE:
		releasenode(0, n->lft);
		n->lft = NULL;
		n->ntyp = FALSE;
		break;
	case FALSE:
		releasenode(0, n->lft);
		n->lft = NULL;
		n->ntyp = TRUE;
		break;
	case NOT:
		m = n->lft->lft;
		releasenode(0, n->lft);
		n->lft = NULL;
		releasenode(0, n);
		n = m;
		break;
	case V_OPER:
		n->ntyp = U_OPER;
		goto same;
	case U_OPER:
		n->ntyp = V_OPER;
		goto same;
	case NEXT:
		n->ntyp = NEXT;
		n->lft->ntyp = NOT;
		n->lft = push_negation(symtab, n->lft);
		break;
	case  AND:
		n->ntyp = OR;
		goto same;
	case  OR:
		n->ntyp = AND;

same:		m = n->lft->rgt;
		n->lft->rgt = NULL;

		n->rgt = Not(m);
		n->lft->ntyp = NOT;
		m = n->lft;
		n->lft = push_negation(symtab, m);
		break;
	}

	return rewrite(n);
}

static void addcan(Symtab symtab, int tok, Node *n, Node **pcan)
{
	Node	*m, *prev = NULL;
	Node	**ptr;
	Node	*N;
	Symbol	*s, *t; int cmp;

	if (!n) return;

	if (n->ntyp == tok)
	{
		addcan(symtab, tok, n->rgt, pcan);
		addcan(symtab, tok, n->lft, pcan);
		return;
	}
#if 0
	if ((tok == AND && n->ntyp == TRUE)
	||  (tok == OR  && n->ntyp == FALSE))
		return;
#endif
	N = dupnode(n);
	if (!*pcan)
	{
		*pcan = N;
		return;
	}

	s = DoDump(symtab, N);
	if ((*pcan)->ntyp != tok)	/* only one element in list so far */
	{
		ptr = pcan;
		goto insert;
	}

	/* there are at least 2 elements in list */
	prev = NULL;
	for (m = *pcan; m->ntyp == tok && m->rgt; prev = m, m = m->rgt)
	{
		t = DoDump(symtab, m->lft);
		cmp = strcmp(s->name, t->name);
		if (cmp == 0)	/* duplicate */
			return;
		if (cmp < 0)
		{
			if (!prev)
			{
				*pcan = tl_nn(tok, N, *pcan);
				return;
			} else
			{
				ptr = &(prev->rgt);
				goto insert;
	}	}	}

	/* new entry goes at the end of the list */
	ptr = &(prev->rgt);
insert:
	t = DoDump(symtab, *ptr);
	cmp = strcmp(s->name, t->name);
	if (cmp == 0)	/* duplicate */
		return;
	if (cmp < 0)
		*ptr = tl_nn(tok, N, *ptr);
	else
		*ptr = tl_nn(tok, *ptr, N);
}

static void
marknode(int tok, Node *m)
{
	if (m->ntyp != tok)
	{	releasenode(0, m->rgt);
		m->rgt = NULL;
	}
	m->ntyp = -1;
}

static int
any_term(Node *srch, Node *in)
{
	if (!in) return 0;

	if (in->ntyp == AND)
		return	any_term(srch, in->lft) ||
			any_term(srch, in->rgt);

	return isequal(in, srch);
}

static int
any_and(Node *srch, Node *in)
{
	if (!in) return 0;

	if (srch->ntyp == AND)
		return	any_and(srch->lft, in) &&
			any_and(srch->rgt, in);

	return any_term(srch, in);
}

static int
any_lor(Node *srch, Node *in)
{
	if (!in) return 0;

	if (in->ntyp == OR)
		return	any_lor(srch, in->lft) ||
			any_lor(srch, in->rgt);

	return isequal(in, srch);
}

static int
anywhere(int tok, Node *srch, Node *in)
{
	if (!in) return 0;

	switch (tok) {
	case AND:	return any_and(srch, in);
	case  OR:	return any_lor(srch, in);
	case   0:	return any_term(srch, in);
	}
	fatal("cannot happen, anywhere");
	return 0;
}

Node * Canonical(Symtab symtab, Node *n)
{
	Node *m, *p, *k1, *k2, *prev, *dflt = NULL;
	int tok;

	if (!n) return NULL;

	tok = n->ntyp;
	if (tok != AND && tok != OR)
		return n;

	Node *can = NULL;
	addcan(symtab, tok, n, &can);
#if 1
	Debug("\nA0: "); Dump(can);
	Debug("\nA1: "); Dump(n); Debug("\n");
#endif
	releasenode(1, n);

	/* mark redundant nodes */
	if (tok == AND)
	{	for (m = can; m; m = (m->ntyp == AND) ? m->rgt : NULL)
		{	k1 = (m->ntyp == AND) ? m->lft : m;
			if (k1->ntyp == TRUE)
			{	marknode(AND, m);
				dflt = True;
				continue;
			}
			if (k1->ntyp == FALSE)
			{	releasenode(1, can);
				can = False;
				goto out;
		}	}
		for (m = can; m; m = (m->ntyp == AND) ? m->rgt : NULL)
		for (p = can; p; p = (p->ntyp == AND) ? p->rgt : NULL)
		{	if (p == m
			||  p->ntyp == -1
			||  m->ntyp == -1)
				continue;
			k1 = (m->ntyp == AND) ? m->lft : m;
			k2 = (p->ntyp == AND) ? p->lft : p;

			if (isequal(k1, k2))
			{	marknode(AND, p);
				continue;
			}
			if (anywhere(OR, k1, k2))
			{	marknode(AND, p);
				continue;
			}
			if (k2->ntyp == U_OPER
			&&  anywhere(AND, k2->rgt, can))
			{	marknode(AND, p);
				continue;
			}	/* q && (p U q) = q */
	}	}
	if (tok == OR)
	{	for (m = can; m; m = (m->ntyp == OR) ? m->rgt : NULL)
		{	k1 = (m->ntyp == OR) ? m->lft : m;
			if (k1->ntyp == FALSE)
			{	marknode(OR, m);
				dflt = False;
				continue;
			}
			if (k1->ntyp == TRUE)
			{	releasenode(1, can);
				can = True;
				goto out;
		}	}
		for (m = can; m; m = (m->ntyp == OR) ? m->rgt : NULL)
		for (p = can; p; p = (p->ntyp == OR) ? p->rgt : NULL)
		{	if (p == m
			||  p->ntyp == -1
			||  m->ntyp == -1)
				continue;
			k1 = (m->ntyp == OR) ? m->lft : m;
			k2 = (p->ntyp == OR) ? p->lft : p;

			if (isequal(k1, k2))
			{	marknode(OR, p);
				continue;
			}
			if (anywhere(AND, k1, k2))
			{	marknode(OR, p);
				continue;
			}
			if (k2->ntyp == V_OPER
			&&  k2->lft->ntyp == FALSE
			&&  anywhere(AND, k2->rgt, can))
			{	marknode(OR, p);
				continue;
			}	/* p || (F V p) = p */
	}	}
	for (m = can, prev = NULL; m; )	/* remove marked nodes */
	{	if (m->ntyp == -1)
		{	k2 = m->rgt;
			releasenode(0, m);
			if (!prev)
			{	m = can = can->rgt;
			} else
			{	m = prev->rgt = k2;
				/* if deleted the last node in a chain */
				if (!prev->rgt && prev->lft
				&&  (prev->ntyp == AND || prev->ntyp == OR))
				{	k1 = prev->lft;
					prev->ntyp = prev->lft->ntyp;
					prev->sym = prev->lft->sym;
					prev->rgt = prev->lft->rgt;
					prev->lft = prev->lft->lft;
					releasenode(0, k1);
				}
			}
			continue;
		}
		prev = m;
		m = m->rgt;
	}
out:
#if 1
	Debug("A2: "); Dump(can); Debug("\n");
#endif
	if (!can)
	{	if (!dflt)
			fatal("cannot happen, Canonical");
		return dflt;
	}

	return can;
}
