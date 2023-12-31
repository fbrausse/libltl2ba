// SPDX-License-Identifier: GPL-2.0+
/***** ltl2ba : parse.c *****/

/* Written by Denis Oddoux, LIAFA, France                                 */
/* Copyright (c) 2001  Denis Oddoux                                       */
/* Modified by Paul Gastin, LSV, France                                   */
/* Copyright (c) 2007  Paul Gastin                                        */
/*                                                                        */
/* Some of the code in this file was taken from the Spin software         */
/* Written by Gerard J. Holzmann, Bell Laboratories, U.S.A.               */

#include "internal.h"

extern int tl_yylex(Symtab symtab, Cexprtab *cexpr, Lexer *lex);

static Node	*tl_formula(Symtab symtab, Cexprtab *cexpr, Lexer *lex, Flags);
static Node	*tl_factor(Symtab, Cexprtab *cexpr, Lexer *, Flags);
static Node	*tl_level(Symtab, Cexprtab *cexpr, Lexer *, Flags, int);

static const int prec[5][2] = {
	{ U_OPER, V_OPER, },
	{ AND, },
	{ OR, },
	{ EQUIV, },
	{ IMPLIES, },
};

static const enum { NONE, LEFT, RIGHT } assoc[5] = {
	RIGHT,
	LEFT,
	LEFT,
	NONE,
	RIGHT,
};

static int
implies(Node *a, Node *b)
{
  return
    (isequal(a,b) ||
     b->ntyp == TRUE ||
     a->ntyp == FALSE ||
     (b->ntyp == AND && implies(a, b->lft) && implies(a, b->rgt)) ||
     (a->ntyp == OR && implies(a->lft, b) && implies(a->rgt, b)) ||
     (a->ntyp == AND && (implies(a->lft, b) || implies(a->rgt, b))) ||
     (b->ntyp == OR && (implies(a, b->lft) || implies(a, b->rgt))) ||
     (b->ntyp == U_OPER && implies(a, b->rgt)) ||
     (a->ntyp == V_OPER && implies(a->rgt, b)) ||
     (a->ntyp == U_OPER && implies(a->lft, b) && implies(a->rgt, b)) ||
     (b->ntyp == V_OPER && implies(a, b->lft) && implies(a, b->rgt)) ||
     ((a->ntyp == U_OPER || a->ntyp == V_OPER) && a->ntyp == b->ntyp &&
         implies(a->lft, b->lft) && implies(a->rgt, b->rgt)));
}

static Node *
bin_simpler(Symtab symtab, Node *ptr)
{	Node *a, *b;

	if (ptr)
	switch (ptr->ntyp) {
	case U_OPER:
		if (ptr->rgt->ntyp == TRUE
		||  ptr->rgt->ntyp == FALSE
		||  ptr->lft->ntyp == FALSE)
		{	ptr = ptr->rgt;
			break;
		}
		if (implies(ptr->lft, ptr->rgt)) /* NEW */
		{	ptr = ptr->rgt;
		        break;
		}
		if (ptr->lft->ntyp == U_OPER
		&&  isequal(ptr->lft->lft, ptr->rgt))
		{	/* (p U q) U p = (q U p) */
			ptr->lft = ptr->lft->rgt;
			break;
		}
		if (ptr->rgt->ntyp == U_OPER
		&&  implies(ptr->lft, ptr->rgt->lft))
		{	/* NEW */
			ptr = ptr->rgt;
			break;
		}

		/* X p U X q == X (p U q) */
		if (ptr->rgt->ntyp == NEXT
		&&  ptr->lft->ntyp == NEXT)
		{	ptr = tl_nn(NEXT,
				tl_nn(U_OPER,
					ptr->lft->lft,
					ptr->rgt->lft), NULL);
		        break;
		}

		/* NEW : F X p == X F p */
		if (ptr->lft->ntyp == TRUE &&
		    ptr->rgt->ntyp == NEXT) {
		  ptr = tl_nn(NEXT, tl_nn(U_OPER, True, ptr->rgt->lft), NULL);
		  break;
		}

		/* NEW : F G F p == G F p */
		if (ptr->lft->ntyp == TRUE &&
		    ptr->rgt->ntyp == V_OPER &&
		    ptr->rgt->lft->ntyp == FALSE &&
		    ptr->rgt->rgt->ntyp == U_OPER &&
		    ptr->rgt->rgt->lft->ntyp == TRUE) {
		  ptr = ptr->rgt;
		  break;
		}

		/* NEW */
		if (ptr->lft->ntyp != TRUE &&
		    implies(push_negation(symtab, tl_nn(NOT, dupnode(ptr->rgt), NULL)),
			    ptr->lft))
		{       ptr->lft = True;
		        break;
		}
		break;
	case V_OPER:
		if (ptr->rgt->ntyp == FALSE
		||  ptr->rgt->ntyp == TRUE
		||  ptr->lft->ntyp == TRUE)
		{	ptr = ptr->rgt;
			break;
		}
		if (implies(ptr->rgt, ptr->lft))
		{	/* p V p = p */
			ptr = ptr->rgt;
			break;
		}
		/* F V (p V q) == F V q */
		if (ptr->lft->ntyp == FALSE
		&&  ptr->rgt->ntyp == V_OPER)
		{	ptr->rgt = ptr->rgt->rgt;
			break;
		}

		/* NEW : G X p == X G p */
		if (ptr->lft->ntyp == FALSE &&
		    ptr->rgt->ntyp == NEXT) {
		  ptr = tl_nn(NEXT, tl_nn(V_OPER, False, ptr->rgt->lft), NULL);
		  break;
		}

		/* NEW : G F G p == F G p */
		if (ptr->lft->ntyp == FALSE &&
		    ptr->rgt->ntyp == U_OPER &&
		    ptr->rgt->lft->ntyp == TRUE &&
		    ptr->rgt->rgt->ntyp == V_OPER &&
		    ptr->rgt->rgt->lft->ntyp == FALSE) {
		  ptr = ptr->rgt;
		  break;
		}

		/* NEW */
		if (ptr->rgt->ntyp == V_OPER
		&&  implies(ptr->rgt->lft, ptr->lft))
		{	ptr = ptr->rgt;
			break;
		}

		/* NEW */
		if (ptr->lft->ntyp != FALSE &&
		    implies(ptr->lft,
			    push_negation(symtab, tl_nn(NOT, dupnode(ptr->rgt), NULL))))
		{       ptr->lft = False;
		        break;
		}
		break;

	case NEXT:
		/* NEW : X G F p == G F p */
		if (ptr->lft->ntyp == V_OPER &&
		    ptr->lft->lft->ntyp == FALSE &&
		    ptr->lft->rgt->ntyp == U_OPER &&
		    ptr->lft->rgt->lft->ntyp == TRUE) {
		  ptr = ptr->lft;
		  break;
		}
		/* NEW : X F G p == F G p */
		if (ptr->lft->ntyp == U_OPER &&
		    ptr->lft->lft->ntyp == TRUE &&
		    ptr->lft->rgt->ntyp == V_OPER &&
		    ptr->lft->rgt->lft->ntyp == FALSE) {
		  ptr = ptr->lft;
		  break;
		}
		break;

	case IMPLIES:
		if (implies(ptr->lft, ptr->rgt))
		  {	ptr = True;
			break;
		}
		ptr = tl_nn(OR, Not(ptr->lft), ptr->rgt);
		ptr = rewrite(ptr);
		break;
	case EQUIV:
		if (implies(ptr->lft, ptr->rgt) &&
		    implies(ptr->rgt, ptr->lft))
		  {	ptr = True;
			break;
		}
		a = rewrite(tl_nn(AND,
			dupnode(ptr->lft),
			dupnode(ptr->rgt)));
		b = rewrite(tl_nn(AND,
			Not(ptr->lft),
			Not(ptr->rgt)));
		ptr = tl_nn(OR, a, b);
		ptr = rewrite(ptr);
		break;
	case AND:
		/* p && (q U p) = p */
		if (ptr->rgt->ntyp == U_OPER
		&&  isequal(ptr->rgt->rgt, ptr->lft))
		{	ptr = ptr->lft;
			break;
		}
		if (ptr->lft->ntyp == U_OPER
		&&  isequal(ptr->lft->rgt, ptr->rgt))
		{	ptr = ptr->rgt;
			break;
		}

		/* p && (q V p) == q V p */
		if (ptr->rgt->ntyp == V_OPER
		&&  isequal(ptr->rgt->rgt, ptr->lft))
		{	ptr = ptr->rgt;
			break;
		}
		if (ptr->lft->ntyp == V_OPER
		&&  isequal(ptr->lft->rgt, ptr->rgt))
		{	ptr = ptr->lft;
			break;
		}

		/* (p U q) && (r U q) = (p && r) U q*/
		if (ptr->rgt->ntyp == U_OPER
		&&  ptr->lft->ntyp == U_OPER
		&&  isequal(ptr->rgt->rgt, ptr->lft->rgt))
		{	ptr = tl_nn(U_OPER,
				tl_nn(AND, ptr->lft->lft, ptr->rgt->lft),
				ptr->lft->rgt);
			break;
		}

		/* (p V q) && (p V r) = p V (q && r) */
		if (ptr->rgt->ntyp == V_OPER
		&&  ptr->lft->ntyp == V_OPER
		&&  isequal(ptr->rgt->lft, ptr->lft->lft))
		{	ptr = tl_nn(V_OPER,
				ptr->rgt->lft,
				tl_nn(AND, ptr->lft->rgt, ptr->rgt->rgt));
			break;
		}

		/* X p && X q == X (p && q) */
		if (ptr->rgt->ntyp == NEXT
		&&  ptr->lft->ntyp == NEXT)
		{	ptr = tl_nn(NEXT,
				tl_nn(AND,
					ptr->rgt->lft,
					ptr->lft->lft), NULL);
			break;
		}

		/* (p V q) && (r U q) == p V q */
		if (ptr->rgt->ntyp == U_OPER
		&&  ptr->lft->ntyp == V_OPER
		&&  isequal(ptr->lft->rgt, ptr->rgt->rgt))
		{	ptr = ptr->lft;
			break;
		}

		if (isequal(ptr->lft, ptr->rgt)	/* (p && p) == p */
		||  ptr->rgt->ntyp == FALSE	/* (p && F) == F */
		||  ptr->lft->ntyp == TRUE	/* (T && p) == p */
		||  implies(ptr->rgt, ptr->lft))/* NEW */
		{	ptr = ptr->rgt;
			break;
		}
		if (ptr->rgt->ntyp == TRUE	/* (p && T) == p */
		||  ptr->lft->ntyp == FALSE	/* (F && p) == F */
		||  implies(ptr->lft, ptr->rgt))/* NEW */
		{	ptr = ptr->lft;
			break;
		}

		/* NEW : F G p && F G q == F G (p && q) */
		if (ptr->lft->ntyp == U_OPER &&
		    ptr->lft->lft->ntyp == TRUE &&
		    ptr->lft->rgt->ntyp == V_OPER &&
		    ptr->lft->rgt->lft->ntyp == FALSE &&
		    ptr->rgt->ntyp == U_OPER &&
		    ptr->rgt->lft->ntyp == TRUE &&
		    ptr->rgt->rgt->ntyp == V_OPER &&
		    ptr->rgt->rgt->lft->ntyp == FALSE)
		  {
		    ptr = tl_nn(U_OPER, True,
				tl_nn(V_OPER, False,
				      tl_nn(AND, ptr->lft->rgt->rgt,
					    ptr->rgt->rgt->rgt)));
		    break;
		  }

		/* NEW */
		if (implies(ptr->lft,
			    push_negation(symtab, tl_nn(NOT, dupnode(ptr->rgt), NULL)))
		 || implies(ptr->rgt,
			    push_negation(symtab, tl_nn(NOT, dupnode(ptr->lft), NULL))))
		{       ptr = False;
		        break;
		}
		break;

	case OR:
		/* p || (q U p) == q U p */
		if (ptr->rgt->ntyp == U_OPER
		&&  isequal(ptr->rgt->rgt, ptr->lft))
		{	ptr = ptr->rgt;
			break;
		}

		/* p || (q V p) == p */
		if (ptr->rgt->ntyp == V_OPER
		&&  isequal(ptr->rgt->rgt, ptr->lft))
		{	ptr = ptr->lft;
			break;
		}

		/* (p U q) || (p U r) = p U (q || r) */
		if (ptr->rgt->ntyp == U_OPER
		&&  ptr->lft->ntyp == U_OPER
		&&  isequal(ptr->rgt->lft, ptr->lft->lft))
		{	ptr = tl_nn(U_OPER,
				ptr->rgt->lft,
				tl_nn(OR, ptr->lft->rgt, ptr->rgt->rgt));
			break;
		}

		if (isequal(ptr->lft, ptr->rgt)	/* (p || p) == p */
		||  ptr->rgt->ntyp == FALSE	/* (p || F) == p */
		||  ptr->lft->ntyp == TRUE	/* (T || p) == T */
		||  implies(ptr->rgt, ptr->lft))/* NEW */
		{	ptr = ptr->lft;
			break;
		}
		if (ptr->rgt->ntyp == TRUE	/* (p || T) == T */
		||  ptr->lft->ntyp == FALSE	/* (F || p) == p */
		||  implies(ptr->lft, ptr->rgt))/* NEW */
		{	ptr = ptr->rgt;
			break;
		}

		/* (p V q) || (r V q) = (p || r) V q */
		if (ptr->rgt->ntyp == V_OPER
		&&  ptr->lft->ntyp == V_OPER
		&&  isequal(ptr->lft->rgt, ptr->rgt->rgt))
		{	ptr = tl_nn(V_OPER,
				tl_nn(OR, ptr->lft->lft, ptr->rgt->lft),
				ptr->rgt->rgt);
			break;
		}

		/* (p V q) || (r U q) == r U q */
		if (ptr->rgt->ntyp == U_OPER
		&&  ptr->lft->ntyp == V_OPER
		&&  isequal(ptr->lft->rgt, ptr->rgt->rgt))
		{	ptr = ptr->rgt;
			break;
		}

		/* NEW : G F p || G F q == G F (p || q) */
		if (ptr->lft->ntyp == V_OPER &&
		    ptr->lft->lft->ntyp == FALSE &&
		    ptr->lft->rgt->ntyp == U_OPER &&
		    ptr->lft->rgt->lft->ntyp == TRUE &&
		    ptr->rgt->ntyp == V_OPER &&
		    ptr->rgt->lft->ntyp == FALSE &&
		    ptr->rgt->rgt->ntyp == U_OPER &&
		    ptr->rgt->rgt->lft->ntyp == TRUE)
		  {
		    ptr = tl_nn(V_OPER, False,
				tl_nn(U_OPER, True,
				      tl_nn(OR, ptr->lft->rgt->rgt,
					    ptr->rgt->rgt->rgt)));
		    break;
		  }

		/* NEW */
		if (implies(push_negation(symtab, tl_nn(NOT, dupnode(ptr->rgt), NULL)),
			    ptr->lft)
		 || implies(push_negation(symtab, tl_nn(NOT, dupnode(ptr->lft), NULL)),
			    ptr->rgt))
		{       ptr = True;
		        break;
		}
		break;
	}
	return ptr;
}

static Node *
bin_minimal(Symtab symtab, Node *ptr)
{       if (ptr)
	switch (ptr->ntyp) {
	case IMPLIES:
		return tl_nn(OR, Not(ptr->lft), ptr->rgt);
	case EQUIV:
		return tl_nn(OR,
			     tl_nn(AND,dupnode(ptr->lft),dupnode(ptr->rgt)),
			     tl_nn(AND,Not(ptr->lft),Not(ptr->rgt)));
	}
	return ptr;
}

static Node *
tl_factor(Symtab symtab, Cexprtab *cexpr, Lexer *lex, Flags flags)
{	Node *ptr = NULL;

	switch (lex->tl_yychar) {
	case '(':
		ptr = tl_formula(symtab, cexpr, lex, flags);
		if (lex->tl_yychar != ')')
			tl_yyerror(lex, "expected ')'");
		lex->tl_yychar = tl_yylex(symtab, cexpr, lex);
		goto simpl;
	case NOT:
		ptr = lex->tl_yylval;
		lex->tl_yychar = tl_yylex(symtab, cexpr, lex);
		ptr->lft = tl_factor(symtab, cexpr, lex, flags);
		ptr = push_negation(symtab, ptr);
		goto simpl;
	case ALWAYS:
		lex->tl_yychar = tl_yylex(symtab, cexpr, lex);

		ptr = tl_factor(symtab, cexpr, lex, flags);

		if(flags & LTL2BA_SIMP_LOG) {
		  if (ptr->ntyp == FALSE
		      ||  ptr->ntyp == TRUE)
		    break;	/* [] false == false */

		  if (ptr->ntyp == V_OPER)
		    {	if (ptr->lft->ntyp == FALSE)
		      break;	/* [][]p = []p */

		    ptr = ptr->rgt;	/* [] (p V q) = [] q */
		    }
		}

		ptr = tl_nn(V_OPER, False, ptr);
		goto simpl;

	case NEXT:
		lex->tl_yychar = tl_yylex(symtab, cexpr, lex);

		ptr = tl_factor(symtab, cexpr, lex, flags);

		if ((ptr->ntyp == TRUE || ptr->ntyp == FALSE)&& (flags & LTL2BA_SIMP_LOG))
			break;	/* X true = true , X false = false */

		ptr = tl_nn(NEXT, ptr, NULL);
		goto simpl;

	case EVENTUALLY:
		lex->tl_yychar = tl_yylex(symtab, cexpr, lex);

		ptr = tl_factor(symtab, cexpr, lex, flags);

		if(flags & LTL2BA_SIMP_LOG) {
		  if (ptr->ntyp == TRUE
		      ||  ptr->ntyp == FALSE)
		    break;	/* <> true == true */

		  if (ptr->ntyp == U_OPER
		      &&  ptr->lft->ntyp == TRUE)
		    break;	/* <><>p = <>p */

		  if (ptr->ntyp == U_OPER)
		    {	/* <> (p U q) = <> q */
		      ptr = ptr->rgt;
		      /* fall thru */
		    }
		}

		ptr = tl_nn(U_OPER, True, ptr);
	simpl:
		if (flags & LTL2BA_SIMP_LOG)
		  ptr = bin_simpler(symtab, ptr);
		break;
	case PREDICATE:
		ptr = lex->tl_yylval;
		lex->tl_yychar = tl_yylex(symtab, cexpr, lex);
		break;
	case TRUE:
	case FALSE:
		ptr = lex->tl_yylval;
		lex->tl_yychar = tl_yylex(symtab, cexpr, lex);
		break;
	}
	if (!ptr) tl_yyerror(lex, "expected predicate");
#if 0
	printf("factor:	");
	tl_explain(ptr->ntyp);
	printf("\n");
#endif
	return ptr;
}

static Node *
tl_level(Symtab symtab, Cexprtab *cexpr, Lexer *lex, Flags flags, int nr)
{
	unsigned i;
	Node *ptr = NULL, *bin = NULL, **tail = &bin;

	if (nr < 0)
		return tl_factor(symtab, cexpr, lex, flags);

	ptr = tl_level(symtab, cexpr, lex, flags, nr-1);
again:
	for (i = 0; i < sizeof(prec[nr])/sizeof(*prec[nr]); i++)
		if (lex->tl_yychar == prec[nr][i])
		{
			if (assoc[nr] == NONE && bin)
				tl_yyerror(lex, "non-associative operator chained");
			lex->tl_yychar = tl_yylex(symtab, cexpr, lex);
			Node *rgt = tl_level(symtab, cexpr, lex, flags, nr-1);
			Node *n = tl_nn(prec[nr][i], NULL, rgt);
			switch (assoc[nr]) {
			case NONE: // fall through
			case LEFT:
				// append n to list
				*tail = n;
				tail = &n->nxt;
				break;
			case RIGHT:
				// prepend n to list
				n->nxt = bin;
				bin = n;
				break;
			}
			goto again;
		}

	Node *res = ptr;
	for (Node *head; (head = bin); res = head) {
		bin = head->nxt;
		head->nxt = NULL;

		if (assoc[nr] == RIGHT)
			head->lft = bin ? bin->rgt : ptr;
		else
			head->lft = res;

		if (flags & LTL2BA_SIMP_LOG)
			head = bin_simpler(symtab, head);
		else
			head = bin_minimal(symtab, head);

		if (assoc[nr] == RIGHT && bin)
			bin->rgt = head;
	}
	ptr = res;

	if (!ptr) tl_yyerror(lex, "syntax error");
#if 0
	printf("level %d:	", nr);
	tl_explain(ptr->ntyp);
	printf("\n");
#endif
	return ptr;
}

static Node * tl_formula(Symtab symtab, Cexprtab *cexpr, Lexer *lex, Flags flags)
{
	lex->tl_yychar = tl_yylex(symtab, cexpr, lex);
	return tl_level(symtab, cexpr, lex, flags, sizeof(prec)/sizeof(*prec)-1); /* 5 precedence levels: 4 to 0 */
}

Node * tl_parse(Symtab symtab, Cexprtab *cexpr, Flags flags)
{
	Lexer lex;
	memset(&lex, 0, sizeof(lex));
	Node *f = tl_formula(symtab, cexpr, &lex, flags);
	if (lex.tl_yychar != ';')
		tl_yyerror(&lex, "syntax error");
	return f;
}
