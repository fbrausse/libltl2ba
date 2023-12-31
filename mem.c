// SPDX-License-Identifier: GPL-2.0+
/***** ltl2ba : mem.c *****/

/* Written by Denis Oddoux, LIAFA, France                                 */
/* Copyright (c) 2001  Denis Oddoux                                       */
/* Modified by Paul Gastin, LSV, France                                   */
/* Copyright (c) 2007  Paul Gastin                                        */
/*                                                                        */
/* Some of the code in this file was taken from the Spin software         */
/* Written by Gerard J. Holzmann, Bell Laboratories, U.S.A.               */

#include "internal.h"

#if 1
#define log(e, u, d)	event[e][(int) u] += (long) d;
#else
#define log(e, u, d)
#endif

#define A_LARGE		80
#define A_USER		0x55000000
#define NOTOOBIG	32768

#define POOL		0
#define ALLOC		1
#define FREE		2
#define NREVENT		3

static ATrans *atrans_list = (ATrans *)0;
static GTrans *gtrans_list = (GTrans *)0;
static BTrans *btrans_list = (BTrans *)0;

static int aallocs = 0, afrees = 0, apool = 0;
static int gallocs = 0, gfrees = 0, gpool = 0;
static int ballocs = 0, bfrees = 0, bpool = 0;

static unsigned long All_Mem = 0;

union M {
	long size;
	union M *link;
};

static union M *freelist[A_LARGE];
static long	req[A_LARGE];
static long	event[NREVENT][A_LARGE];

void *
tl_emalloc(int U)
{	union M *m;
  	long r, u;
	void *rp;

	u = (long) ((U-1)/sizeof(union M) + 2);

	if (u >= A_LARGE)
	{	log(ALLOC, 0, 1);
#if TL_EMALLOC_VERBOSE
		fprintf(stderr, "tl_spin: memalloc %ld bytes\n", u);
#endif
		m = (union M *) emalloc((int) u*sizeof(union M));
		All_Mem += (unsigned long) u*sizeof(union M);
	} else
	{	if (!freelist[u])
		{	r = req[u] += req[u] ? req[u] : 1;
			if (r >= NOTOOBIG)
				r = req[u] = NOTOOBIG;
			log(POOL, u, r);
			freelist[u] = (union M *)
				emalloc((int) r*u*sizeof(union M));
			All_Mem += (unsigned long) r*u*sizeof(union M);
			m = freelist[u] + (r-2)*u;
			for ( ; m >= freelist[u]; m -= u)
				m->link = m+u;
		}
		log(ALLOC, u, 1);
		m = freelist[u];
		freelist[u] = m->link;
	}
	m->size = (u|A_USER);

	for (r = 1; r < u; )
		(&m->size)[r++] = 0;

	rp = (void *) (m+1);
	memset(rp, 0, U);
	return rp;
}

void
tfree(void *v)
{	union M *m = (union M *) v;
	long u;

	--m;
	if ((m->size&0xFF000000) != A_USER)
		fatal("releasing a free block");

	u = (m->size &= 0xFFFFFF);
	if (u >= A_LARGE)
	{	log(FREE, 0, 1);
		/* free(m); */
	} else
	{	log(FREE, u, 1);
		m->link = freelist[u];
		freelist[u] = m;
	}
}

ATrans* emalloc_atrans(int sym_size, int node_size) {
  ATrans *result;
  if(!atrans_list) {
    result = (ATrans *)tl_emalloc(sizeof(ATrans));
    result->pos = new_set(sym_size);
    result->neg = new_set(sym_size);
    result->to  = new_set(node_size);
    apool++;
  }
  else {
    result = atrans_list;
    atrans_list = atrans_list->nxt;
    result->nxt = (ATrans *)0;
  }
  aallocs++;
  return result;
}

void free_atrans(ATrans *t, int rec) {
  if(!t) return;
  if(rec) free_atrans(t->nxt, rec);
  t->nxt = atrans_list;
  atrans_list = t;
  afrees++;
}

void free_all_atrans() {
  ATrans *t;
  while(atrans_list) {
    t = atrans_list;
    atrans_list = t->nxt;
    tfree(t->to);
    tfree(t->pos);
    tfree(t->neg);
    tfree(t);
  }
}

GTrans* emalloc_gtrans(int sym_size, int node_size) {
  GTrans *result;
  if(!gtrans_list) {
    result = (GTrans *)tl_emalloc(sizeof(GTrans));
    result->pos   = new_set(sym_size);
    result->neg   = new_set(sym_size);
    result->final = new_set(node_size);
    gpool++;
  }
  else {
    result = gtrans_list;
    gtrans_list = gtrans_list->nxt;
  }
  gallocs++;
  return result;
}

void free_gtrans(GTrans *t, GTrans *sentinel, int fly) {
  gfrees++;
  if(sentinel && (t != sentinel)) {
    free_gtrans(t->nxt, sentinel, fly);
    if(fly) t->to->incoming--;
  }
  t->nxt = gtrans_list;
  gtrans_list = t;
}

BTrans* emalloc_btrans(int sym_size) {
  BTrans *result;
  if(!btrans_list) {
    result = (BTrans *)tl_emalloc(sizeof(BTrans));
    result->pos = new_set(sym_size);
    result->neg = new_set(sym_size);
    bpool++;
  }
  else {
    result = btrans_list;
    btrans_list = btrans_list->nxt;
  }
  ballocs++;
  return result;
}

void free_btrans(BTrans *t, BTrans *sentinel, int fly) {
  bfrees++;
  if(sentinel && (t != sentinel)) {
    free_btrans(t->nxt, sentinel, fly);
    if(fly) t->to->incoming--;
  }
  t->nxt = btrans_list;
  btrans_list = t;
}

void a_stats(void)
{
	long p, a, f;
	int i;

	/*extern int Stack_mx;*/
	fprintf(stderr, "\ntotal memory used: %9ld\n", All_Mem);
	/*fprintf(stderr, "largest stack sze: %9d\n", Stack_mx);*/

	fprintf(stderr, " size\t  pool\tallocs\t frees\n");

	for (i = 0; i < A_LARGE; i++)
	{	p = event[POOL][i];
		a = event[ALLOC][i];
		f = event[FREE][i];

		if(p|a|f)
		fprintf(stderr, "%5d\t%6ld\t%6ld\t%6ld\n",
			i, p, a, f);
	}

	fprintf(stderr, "atrans\t%6d\t%6d\t%6d\n",
	       apool, aallocs, afrees);
	fprintf(stderr, "gtrans\t%6d\t%6d\t%6d\n",
	       gpool, gallocs, gfrees);
	fprintf(stderr, "btrans\t%6d\t%6d\t%6d\n",
	       bpool, ballocs, bfrees);
}
