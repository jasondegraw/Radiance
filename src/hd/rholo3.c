/* Copyright (c) 1998 Silicon Graphics, Inc. */

#ifndef lint
static char SCCSid[] = "$SunId$ SGI";
#endif

/*
 * Routines for tracking beam compuatations
 */

#include "rholo.h"

#define abs(x)		((x) > 0 ? (x) : -(x))
#define sgn(x)		((x) > 0 ? 1 : (x) < 0 ? -1 : 0)

static PACKHEAD	*complist=NULL;	/* list of beams to compute */
static int	complen=0;	/* length of complist */
static int	listpos=0;	/* current list position for next_packet */
static int	lastin= -1;	/* last ordered position in list */


int
beamcmp(b0, b1)				/* comparison for compute order */
register PACKHEAD	*b0, *b1;
{
	return(	b1->nr*(b0->nc+1) - b0->nr*(b1->nc+1) );
}


int
beamidcmp(b0, b1)			/* comparison for beam searching */
register PACKHEAD	*b0, *b1;
{
	register int	c = b0->hd - b1->hd;

	if (c) return(c);
	return(b0->bi - b1->bi);
}


int
dispbeam(b, hb)				/* display a holodeck beam */
register BEAM	*b;
register HDBEAMI	*hb;
{
	static int	n = 0;
	static PACKHEAD	*p = NULL;

	if (b == NULL)
		return;
	if (b->nrm > n) {		/* (re)allocate packet holder */
		n = b->nrm;
		if (p == NULL) p = (PACKHEAD *)malloc(packsiz(n));
		else p = (PACKHEAD *)realloc((char *)p, packsiz(n));
		if (p == NULL)
			error(SYSTEM, "out of memory in dispbeam");
	}
					/* assign packet fields */
	bcopy((char *)hdbray(b), (char *)packra(p), b->nrm*sizeof(RAYVAL));
	p->nr = p->nc = b->nrm;
	for (p->hd = 0; hdlist[p->hd] != hb->h; p->hd++)
		if (hdlist[p->hd] == NULL)
			error(CONSISTENCY, "unregistered holodeck in dispbeam");
	p->bi = hb->b;
	disp_packet(p);			/* display it */
}


bundle_set(op, clist, nents)	/* bundle set operation */
int	op;
PACKHEAD	*clist;
int	nents;
{
	int	oldnr, n;
	HDBEAMI	*hbarr;
	register PACKHEAD	*csm;
	register int	i;
					/* search for common members */
	for (csm = clist+nents; csm-- > clist; )
		csm->nc = -1;
	qsort((char *)clist, nents, sizeof(PACKHEAD), beamidcmp);
	for (i = 0; i < complen; i++) {
		csm = (PACKHEAD *)bsearch((char *)(complist+i), (char *)clist,
				nents, sizeof(PACKHEAD), beamidcmp);
		if (csm == NULL)
			continue;
		oldnr = complist[i].nr;
		csm->nc = complist[i].nc;
		switch (op) {
		case BS_ADD:		/* add to count */
			complist[i].nr += csm->nr;
			csm->nr = 0;
			break;
		case BS_ADJ:		/* reset count */
			complist[i].nr = csm->nr;
			csm->nr = 0;
			break;
		case BS_DEL:		/* delete count */
			if (csm->nr == 0 || csm->nr >= complist[i].nr)
				complist[i].nr = 0;
			else
				complist[i].nr -= csm->nr;
			break;
		}
		if (complist[i].nr != oldnr)
			lastin = -1;	/* flag sort */
	}
				/* record computed rays for uncommon beams */
	for (csm = clist+nents; csm-- > clist; )
		if (csm->nc < 0)
			csm->nc = bnrays(hdlist[csm->hd], csm->bi);
				/* complete list operations */
	switch (op) {
	case BS_NEW:			/* new computation set */
		listpos = 0; lastin = -1;
		if (complen)		/* free old list */
			free((char *)complist);
		complist = NULL;
		if (!(complen = nents))
			return;
		complist = (PACKHEAD *)malloc(nents*sizeof(PACKHEAD));
		if (complist == NULL)
			goto memerr;
		bcopy((char *)clist, (char *)complist, nents*sizeof(PACKHEAD));
		break;
	case BS_ADD:			/* add to computation set */
	case BS_ADJ:			/* adjust set quantities */
		if (nents <= 0)
			return;
		sortcomplist();		/* sort updated list & new entries */
		qsort((char *)clist, nents, sizeof(PACKHEAD), beamcmp);
					/* what can't we satisfy? */
		for (i = nents, csm = clist; i-- && csm->nr > csm->nc; csm++)
			;
		n = csm - clist;
		if (op == BS_ADJ) {	/* don't regenerate adjusted beams */
			for (++i; i-- && csm->nr > 0; csm++)
				;
			nents = csm - clist;
		}
		if (n) {		/* allocate space for merged list */
			PACKHEAD	*newlist;
			newlist = (PACKHEAD *)malloc(
					(complen+n)*sizeof(PACKHEAD) );
			if (newlist == NULL)
				goto memerr;
						/* merge lists */
			mergeclists(newlist, clist, n, complist, complen);
			if (complen)
				free((char *)complist);
			complist = newlist;
			complen += n;
		}
		listpos = 0;
		lastin = complen-1;	/* list is now sorted */
		break;
	case BS_DEL:			/* delete from computation set */
		return;			/* already done */
	default:
		error(CONSISTENCY, "bundle_set called with unknown operation");
	}
	if (outdev == NULL)		/* nothing to display? */
		return;
					/* load and display beams we have */
	hbarr = (HDBEAMI *)malloc(nents*sizeof(HDBEAMI));
	for (i = nents; i--; ) {
		hbarr[i].h = hdlist[clist[i].hd];
		hbarr[i].b = clist[i].bi;
	}
	hdloadbeams(hbarr, nents, dispbeam);
	free((char *)hbarr);
	return;
memerr:
	error(SYSTEM, "out of memory in bundle_set");
}


double
beamvolume(hp, bi)	/* compute approximate volume of a beam */
HOLO	*hp;
int	bi;
{
	GCOORD	gc[2];
	FVECT	cp[4], edgeA, edgeB, cent[2];
	FVECT	v, crossp[2], diffv;
	double	vol[2];
	register int	i;
					/* get grid coordinates */
	if (!hdbcoord(gc, hp, bi))
		error(CONSISTENCY, "bad beam index in beamvolume");
	for (i = 0; i < 2; i++) {	/* compute cell area vectors */
		hdcell(cp, hp, gc+i);
		VSUM(edgeA, cp[1], cp[0], -1.0);
		VSUM(edgeB, cp[3], cp[1], -1.0);
		fcross(crossp[i], edgeA, edgeB);
					/* compute center */
		cent[i][0] = 0.5*(cp[0][0] + cp[2][0]);
		cent[i][1] = 0.5*(cp[0][1] + cp[2][1]);
		cent[i][2] = 0.5*(cp[0][2] + cp[2][2]);
	}
					/* compute difference vector */
	VSUM(diffv, cent[1], cent[0], -1.0);
	for (i = 0; i < 2; i++) {	/* compute volume contributions */
		vol[i] = 0.5*DOT(crossp[i], diffv);
		if (vol[i] < 0.) vol[i] = -vol[i];
	}
	return(vol[0] + vol[1]);	/* return total volume */
}


init_global()			/* initialize global ray computation */
{
	long	wtotal = 0;
	double	frac;
	int	i;
	register int	j, k;
					/* free old list and empty queue */
	if (complen > 0) {
		free((char *)complist);
		done_packets(flush_queue());
	}
					/* allocate beam list */
	complen = 0;
	for (j = 0; hdlist[j] != NULL; j++)
		complen += nbeams(hdlist[j]);
	complist = (PACKHEAD *)malloc(complen*sizeof(PACKHEAD));
	if (complist == NULL)
		error(SYSTEM, "out of memory in init_global");
					/* compute beam weights */
	k = 0;
	for (j = 0; hdlist[j] != NULL; j++) {
		frac = 512. * VLEN(hdlist[j]->wg[0]) *
				VLEN(hdlist[j]->wg[1]) *
				VLEN(hdlist[j]->wg[2]);
		for (i = nbeams(hdlist[j]); i > 0; i--) {
			complist[k].hd = j;
			complist[k].bi = i;
			complist[k].nr = frac*beamvolume(hdlist[j], i) + 0.5;
			complist[k].nc = bnrays(hdlist[j], i);
			wtotal += complist[k++].nr;
		}
	}
					/* adjust weights */
	if (vdef(DISKSPACE))
		frac = 1024.*1024.*vflt(DISKSPACE) / (wtotal*sizeof(RAYVAL));
	else
		frac = 1024.*1024.*16384. / (wtotal*sizeof(RAYVAL));
	while (k--)
		complist[k].nr = frac*complist[k].nr + 0.5;
	listpos = 0; lastin = -1;	/* perform initial sort */
	sortcomplist();
					/* no view vicinity */
	myeye.rng = 0;
}


mergeclists(cdest, cl1, n1, cl2, n2)	/* merge two sorted lists */
register PACKHEAD	*cdest;
register PACKHEAD	*cl1, *cl2;
int	n1, n2;
{
	register int	cmp;

	while (n1 | n2) {
		if (!n1) cmp = 1;
		else if (!n2) cmp = -1;
		else cmp = beamcmp(cl1, cl2);
		if (cmp > 0) {
			copystruct(cdest, cl2);
			cl2++; n2--;
		} else {
			copystruct(cdest, cl1);
			cl1++; n1--;
		}
		cdest++;
	}
}


sortcomplist()			/* fix our list order */
{
	PACKHEAD	*list2;
	register int	i;

	if (complen <= 0)	/* check to see if there is even a list */
		return;
	if (lastin < 0 || listpos*4 >= complen*3)
		qsort((char *)complist, complen, sizeof(PACKHEAD), beamcmp);
	else if (listpos) {	/* else sort and merge sublist */
		list2 = (PACKHEAD *)malloc(listpos*sizeof(PACKHEAD));
		if (list2 == NULL)
			error(SYSTEM, "out of memory in sortcomplist");
		bcopy((char *)complist,(char *)list2,listpos*sizeof(PACKHEAD));
		qsort((char *)list2, listpos, sizeof(PACKHEAD), beamcmp);
		mergeclists(complist, list2, listpos,
				complist+listpos, complen-listpos);
		free((char *)list2);
	}
					/* drop satisfied requests */
	for (i = complen; i-- && complist[i].nr <= complist[i].nc; )
		;
	if (i < 0) {
		free((char *)complist);
		complist = NULL;
		complen = 0;
	} else if (i < complen-1) {
		list2 = (PACKHEAD *)realloc((char *)complist,
				(i+1)*sizeof(PACKHEAD));
		if (list2 != NULL) {
			complist = list2;
			complen = i+1;
		}
	}
	listpos = 0; lastin = i;
}


/*
 * The following routine works on the assumption that the bundle weights are
 * more or less evenly distributed, such that computing a packet causes
 * a given bundle to move way down in the computation order.  We keep
 * track of where the computed bundle with the highest priority would end
 * up, and if we get further in our compute list than this, we resort the
 * list and start again from the beginning.  Since
 * a merge sort is used, the sorting costs are minimal.
 */
next_packet(p, n)		/* prepare packet for computation */
register PACKET	*p;
int	n;
{
	register int	i;

	if (listpos > lastin)		/* time to sort the list */
		sortcomplist();
	if (complen <= 0)
		return(0);
	p->hd = complist[listpos].hd;
	p->bi = complist[listpos].bi;
	p->nc = complist[listpos].nc;
	p->nr = complist[listpos].nr - p->nc;
	if (p->nr <= 0)
		return(0);
#ifdef DEBUG
	if (n < 1 | n > RPACKSIZ)
		error(CONSISTENCY, "next_packet called with bad n value");
#endif
	if (p->nr > n)
		p->nr = n;
	complist[listpos].nc += p->nr;	/* find where this one would go */
	while (lastin > listpos && 
			beamcmp(complist+lastin, complist+listpos) > 0)
		lastin--;
	listpos++;
	return(1);
}
