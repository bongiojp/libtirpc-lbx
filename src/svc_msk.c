/*
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc. 
 */

/*
 * Implements an msk connection server side RPC.
 */

#include <config.h>

#include <sys/cdefs.h>
#include <pthread.h>
#include <reentrant.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/poll.h>
#if defined(TIRPC_EPOLL)
#include <sys/epoll.h> /* before rpc.h */
#endif
#include <rpc/rpc.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netconfig.h>
#include <err.h>

#include "rpc_com.h"
#include "svc_internal.h"
#include "clnt_internal.h"
#include "svc_xprt.h"
#include "rpc_rdma.h"
#include <rpc/svc_rqst.h>

/*
 * kept in xprt->xp_p2 (sm_data(xprt))
 */
struct svc_msk_data {
	msk_trans_t    *trans;
	size_t          sm_iosz;                /* size of send.recv buffer */
	u_int32_t       sm_xid;                 /* transaction id */
	XDR             sm_xdrs;                        /* XDR handle */
	char            sm_verfbody[MAX_AUTH_BYTES];    /* verifier body */
	u_int32_t	sendsz;
	u_int32_t	recvsz;
	u_int32_t	credits;

	struct msghdr   sm_msghdr;              /* msghdr received from	clnt */
	unsigned char   sm_cmsg[64];            /* cmsghdr received from clnt */
};


extern struct svc_params __svc_params[1];

#define	sm_data(xprt)	((struct svc_msk_data *)(xprt->xp_p2))

static void svc_msk_ops(SVCXPRT *);
static enum xprt_stat svc_msk_stat(SVCXPRT *);
static bool svc_msk_recv(SVCXPRT *, struct rpc_msg *);
static bool svc_msk_reply(SVCXPRT *, struct rpc_msg *);
static bool svc_msk_getargs(SVCXPRT *, xdrproc_t, void *);
static bool svc_msk_freeargs(SVCXPRT *, xdrproc_t, void *);
static void svc_msk_destroy(SVCXPRT *);
static bool svc_msk_control(SVCXPRT *, const u_int, void *);

/*
 * svc_msk_create
 * like svc_vc_create, trans is expected to be a CHILD trans (after accept_one), but NOT finalized
 * credits is the number of buffers used
 */
SVCXPRT *
svc_msk_create(msk_trans_t *trans, u_int credits, void (*callback)(void*), void* callbackarg)
{
	SVCXPRT *xprt;
	struct svc_msk_data *sm = NULL;

	if (!trans || trans->state != MSK_CONNECT_REQUEST) {
		__warnx("svc_msk_create: could not get transport information");
		return (NULL);
	}

	/*
	 * Find the receive and the send size
	 */
	xprt = mem_alloc(sizeof (SVCXPRT));
	if (xprt == NULL)
		goto freedata;
	memset(xprt, 0, sizeof (SVCXPRT));

	sm = mem_alloc(sizeof (*sm));
	if (sm == NULL)
		goto freedata;

	xprt->xp_flags = SVC_XPRT_FLAG_NONE;
	xprt->xp_p2 = sm;
	xprt->xp_auth = NULL;
	xprt->xp_verf.oa_base = sm->sm_verfbody;
	sm->sendsz = 8*1024;
	sm->recvsz = 4*8*1024;
	sm->credits = credits ? credits : 10; //default value if credits = 0;
	sm->trans = trans;

	svc_msk_ops(xprt);

 	if (xdrmsk_create(&(sm->sm_xdrs), sm->trans, sm->sendsz, sm->recvsz, sm->credits, callback, callbackarg)) {
		abort();
		return FALSE;
	}

	if (msk_finalize_accept(sm->trans)) {
		abort();
		return FALSE;
	}

	__rpc_set_netbuf(&xprt->xp_rtaddr, &sm->trans->addr.sa_stor, sizeof (struct sockaddr_storage));

	return (xprt);
freedata:
	(void) __warnx("svc_msk_create: out of memory");
	if (xprt) {
            if (sm)
                (void) mem_free(sm, sizeof (*sm));
            svc_rqst_finalize_xprt(xprt);
            (void) mem_free(xprt, sizeof (SVCXPRT));
	}
	return (NULL);
}

/*ARGSUSED*/
static enum xprt_stat
svc_msk_stat(xprt)
	SVCXPRT *xprt;
{
	switch(sm_data(xprt)->trans->state) {
		case MSK_LISTENING:
		case MSK_CONNECTED:
			return (XPRT_IDLE);
			/* any point in adding a break? */
		default: /* suppose anything else means a problem */
		        return (XPRT_DIED);
	}
}

static bool
svc_msk_recv(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct svc_msk_data *sm = sm_data(xprt);
	XDR *xdrs = &(sm->sm_xdrs);

	printf("in recv, waiting for incoming buffer\n");

	rpcrdma_svc_setbuf(xdrs, 0, XDR_DECODE);

	if (! xdr_callmsg(xdrs, msg)) {
		return (FALSE);
	}

        /* XXX actually using sm->sm_xid !MT-SAFE */
	sm->sm_xid = msg->rm_xid;

	return (TRUE);
}

static bool
svc_msk_reply(SVCXPRT *xprt, struct rpc_msg *msg)
{
	struct svc_msk_data *sm = sm_data(xprt);
	XDR *xdrs = &(sm->sm_xdrs);

	xdrproc_t xdr_results;
	caddr_t xdr_location;
	bool has_args;

	if (msg->rm_reply.rp_stat == MSG_ACCEPTED &&
	    msg->rm_reply.rp_acpt.ar_stat == SUCCESS) {
            has_args = TRUE;
            xdr_results = msg->acpted_rply.ar_results.proc;
            xdr_location = msg->acpted_rply.ar_results.where;
            msg->acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;
            msg->acpted_rply.ar_results.where = NULL;
	} else {
            xdr_results = NULL;
            xdr_location = NULL;
            has_args = FALSE;
        }

	rpcrdma_svc_setbuf(xdrs, 0, XDR_ENCODE);

        /* MT-SAFE */
        if (! (msg->rm_flags & RPC_MSG_FLAG_MT_XID))
            msg->rm_xid = sm->sm_xid;

	if (xdr_replymsg(xdrs, msg) &&
	    (!has_args || (xprt->xp_auth &&
	     SVCAUTH_WRAP(xprt->xp_auth, xdrs, xdr_results, xdr_location)))) {

		return rpcrdma_svc_flushout(xdrs);

	}

	return (FALSE);
}

static bool
svc_msk_getargs(SVCXPRT *xprt, xdrproc_t xdr_args, void *args_ptr)
{
	if (! SVCAUTH_UNWRAP(xprt->xp_auth, &(sm_data(xprt)->sm_xdrs),
			     xdr_args, args_ptr)) {
		(void)svc_freeargs(xprt, xdr_args, args_ptr);
		return FALSE;
	}
	return TRUE;
}

static bool
svc_msk_freeargs(SVCXPRT *xprt, xdrproc_t xdr_args, void *args_ptr)
{
	XDR *xdrs = &(sm_data(xprt)->sm_xdrs);

	xdrs->x_op = XDR_FREE;
	return (*xdr_args)(xdrs, args_ptr);
}

static void
svc_msk_destroy(SVCXPRT *xprt)
{
	struct svc_msk_data *sm = sm_data(xprt);

	if (xprt->xp_auth != NULL) {
		SVCAUTH_DESTROY(xprt->xp_auth);
		xprt->xp_auth = NULL;
	}
	XDR_DESTROY(&(sm->sm_xdrs));
	msk_destroy_trans(&(sm->trans));
	(void) mem_free(sm, sizeof (*sm));

        /* call free hook */
        if (xprt->xp_ops2->xp_free_xprt)
            xprt->xp_ops2->xp_free_xprt(xprt);
	(void) mem_free(xprt, sizeof (SVCXPRT));
}

extern mutex_t ops_lock;

static bool
/*ARGSUSED*/
svc_msk_control(SVCXPRT *xprt, const u_int rq, void *in)
{
	switch (rq) {
	case SVCGET_XP_FLAGS:
	    *(u_int *)in = xprt->xp_flags;
	    break;
	case SVCSET_XP_FLAGS:
	    xprt->xp_flags = *(u_int *)in;
	    break;
	case SVCGET_XP_RECV:
            mutex_lock(&ops_lock);
	    *(xp_recv_t *)in = xprt->xp_ops->xp_recv;
            mutex_unlock(&ops_lock);
	    break;
	case SVCSET_XP_RECV:
            mutex_lock(&ops_lock);
	    xprt->xp_ops->xp_recv = *(xp_recv_t)in;
            mutex_unlock(&ops_lock);
	    break;
	case SVCGET_XP_GETREQ:
            mutex_lock(&ops_lock);
	    *(xp_getreq_t *)in = xprt->xp_ops2->xp_getreq;
            mutex_unlock(&ops_lock);
	    break;
	case SVCSET_XP_GETREQ:
            mutex_lock(&ops_lock);
	    xprt->xp_ops2->xp_getreq = *(xp_getreq_t)in;
            mutex_unlock(&ops_lock);
	    break;
	case SVCGET_XP_DISPATCH:
            mutex_lock(&ops_lock);
	    *(xp_dispatch_t *)in = xprt->xp_ops2->xp_dispatch;
            mutex_unlock(&ops_lock);
	    break;
	case SVCSET_XP_DISPATCH:
            mutex_lock(&ops_lock);
	    xprt->xp_ops2->xp_dispatch = *(xp_dispatch_t)in;
            mutex_unlock(&ops_lock);
	    break;
	case SVCGET_XP_FREE_XPRT:
            mutex_lock(&ops_lock);
	    *(xp_free_xprt_t *)in = xprt->xp_ops2->xp_free_xprt;
            mutex_unlock(&ops_lock);
	    break;
	case SVCSET_XP_FREE_XPRT:
            mutex_lock(&ops_lock);
	    xprt->xp_ops2->xp_free_xprt = *(xp_free_xprt_t)in;
            mutex_unlock(&ops_lock);
	    break;
	default:
	    return (FALSE);
	}
	return (TRUE);
}

static void
svc_msk_ops(SVCXPRT *xprt)
{
	static struct xp_ops ops;
	static struct xp_ops2 ops2;

        /* VARIABLES PROTECTED BY ops_lock: ops, xp_type */

	mutex_lock(&ops_lock);

	/* Fill in type of service */
        xprt->xp_type = XPRT_UDP;

	if (ops.xp_recv == NULL) {
		ops.xp_recv = svc_msk_recv;
		ops.xp_stat = svc_msk_stat;
		ops.xp_getargs = svc_msk_getargs;
		ops.xp_reply = svc_msk_reply;
		ops.xp_freeargs = svc_msk_freeargs;
		ops.xp_destroy = svc_msk_destroy;
		ops2.xp_control = svc_msk_control;
		ops2.xp_getreq = svc_getreq_default;
                ops2.xp_dispatch = svc_dispatch_default;
                ops2.xp_rdvs = NULL; /* no default */
                ops2.xp_free_xprt = NULL; /* no default */
	}
	xprt->xp_ops = &ops;
	xprt->xp_ops2 = &ops2;
	mutex_unlock(&ops_lock);
}
