/*	$NetBSD: svc.h,v 1.17 2000/06/02 22:57:56 fvdl Exp $	*/

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
 *
 *	from: @(#)svc.h 1.35 88/12/17 SMI
 *	from: @(#)svc.h      1.27    94/04/25 SMI
 * $FreeBSD: src/include/rpc/svc.h,v 1.24 2003/06/15 10:32:01 mbr Exp $
 */

/*
 * svc.h, Server-side remote procedure call interface.
 *
 * Copyright (C) 1986-1993 by Sun Microsystems, Inc.
 */

#ifndef _TIRPC_SVC_H
#define _TIRPC_SVC_H
#include <sys/cdefs.h>
#include "reentrant.h"

/*
 * This interface must manage two items concerning remote procedure calling:
 *
 * 1) An arbitrary number of transport connections upon which rpc requests
 * are received.  The two most notable transports are TCP and UDP;  they are
 * created and registered by routines in svc_tcp.c and svc_udp.c, respectively;
 * they in turn call xprt_register and xprt_unregister.
 *
 * 2) An arbitrary number of locally registered services.  Services are
 * described by the following four data: program number, version number,
 * "service dispatch" function, a transport handle, and a boolean that
 * indicates whether or not the exported program should be registered with a
 * local binder service;  if true the program's number and version and the
 * port number from the transport handle are registered with the binder.
 * These data are registered with the rpc svc system via svc_register.
 *
 * A service's dispatch function is called whenever an rpc request comes in
 * on a transport.  The request's program and version numbers must match
 * those of the registered service.  The dispatch function is passed two
 * parameters, struct svc_req * and SVCXPRT *, defined below.
 */

/* Package init flags */
#define SVC_INIT_DEFAULT        0x0000
#define SVC_INIT_XPORTS         0x0001
#define SVC_INIT_EPOLL          0x0002
#define SVC_INIT_WARNX          0x0004

/*
 *      Service control requests
 */
#define SVCGET_VERSQUIET	1
#define SVCSET_VERSQUIET	2
#define SVCGET_CONNMAXREC	3
#define SVCSET_CONNMAXREC	4
#define SVCGET_XP_RECV		5
#define SVCSET_XP_RECV		6
#define SVCGET_XP_FLAGS		7
#define SVCSET_XP_FLAGS		8

/*
 * Operations for rpc_control().
 */
#define RPC_SVC_CONNMAXREC_SET  0	/* set max rec size, enable nonblock */
#define RPC_SVC_CONNMAXREC_GET  1
#define RPC_SVC_XPRTS_GET       2
#define RPC_SVC_XPRTS_SET       3
#define RPC_SVC_FDSET_GET       4
#define RPC_SVC_FDSET_SET       5

/*
 * Flags for svc_fd_create2
 */

#define SVC_VCCR_NONE             0x0000
#define SVC_VCCR_RADDR            0x0001
#define SVC_VCCR_RADDR_INET       0x0002
#define SVC_VCCR_RADDR_INET6      0x0004
#define SVC_VCCR_RADDR_LOCAL      0x0008
#define SVC_VCCR_MAP6_V1          0x0010


/* Svc event strategy */
enum svc_event_type {
    SVC_EVENT_FDSET /* trad. using select and poll */,
    SVC_EVENT_EPOLL /* Linux epoll interface */ 
};

typedef struct svc_init_params
{
    u_int version;
    u_long flags;
    u_int max_connections; /* xprts */
    u_int max_events;      /* epoll events */
    warnx_t warnx;
} svc_init_params;

/* this won't work yet.  threading fdsets around is annoying */
typedef struct svc_params
{
    enum svc_event_type ev_type;
    union {
        struct {
            int epoll_fd;
            struct epoll_event *events;
            u_int max_events;      /* epoll events */
        } epoll;
        struct {
            fd_set set; /* XXX future svc_fdset */
        } fd;
    } ev_u;

    u_int max_connections;
    
    struct __svc_ops {
        bool_t (*svc_clean_idle)(fd_set *fds, int timeout, bool_t cleanblock);
        void (*svc_run)(void);
        void (*svc_getreq)(int rdfds);
        void (*svc_getreqset)(fd_set *readfds);
        void (*svc_exit)(void);
    } *svc_ops;

} svc_params;

/*
 * SVCXPRT xp_flags
 */

#define SVC_XPORT_FLAG_NONE       0x0000
#define SVC_XPORT_FLAG_SETNEWFDS  0x0001
#define SVC_XPORT_FLAG_DONTCLOSE  0x0002

enum xprt_stat {
	XPRT_DIED,
	XPRT_MOREREQS,
	XPRT_IDLE
};

struct cf_rendezvous { /* kept in xprt->xp_p1 for rendezvouser */
	u_int sendsize;
	u_int recvsize;
	int maxrec;
};

struct cf_conn {  /* kept in xprt->xp_p1 for actual connection */
	enum xprt_stat strm_stat;
	u_int32_t x_id;
	XDR xdrs;
	char verf_body[MAX_AUTH_BYTES];
	u_int sendsize;
	u_int recvsize;
	int maxrec;
	bool_t nonblock;
	struct timeval last_recv_time;
};

/*
 * Server side transport handle
 */
typedef struct __rpc_svcxprt {
	int		xp_fd;
	u_short		xp_port;	 /* associated port number */
	struct xp_ops {
	    /* receive incoming requests */
	    bool_t	(*xp_recv)(struct __rpc_svcxprt *, struct rpc_msg *);
	    /* get transport status */
	    enum xprt_stat (*xp_stat)(struct __rpc_svcxprt *);
	    /* get arguments */
	    bool_t	(*xp_getargs)(struct __rpc_svcxprt *, xdrproc_t,
				void *);
	    /* send reply */
	    bool_t	(*xp_reply)(struct __rpc_svcxprt *, struct rpc_msg *);
	    /* free mem allocated for args */
	    bool_t	(*xp_freeargs)(struct __rpc_svcxprt *, xdrproc_t,
				void *);
	    /* destroy this struct */
	    void	(*xp_destroy)(struct __rpc_svcxprt *);
	} *xp_ops;
	int		xp_addrlen;	 /* length of remote address */
	struct sockaddr_in6 xp_raddr;	 /* remote addr. (backward ABI compat) */
	/* XXX - fvdl stick this here for ABI backward compat reasons */
	struct xp_ops2 {
		/* catch-all function */
		bool_t  (*xp_control)(struct __rpc_svcxprt *, const u_int,
				void *);
	} *xp_ops2;
	char		*xp_tp;		 /* transport provider device name */
	char		*xp_netid;	 /* network token */
	struct netbuf	xp_ltaddr;	 /* local transport address */
	struct netbuf	xp_rtaddr;	 /* remote transport address */
	struct opaque_auth xp_verf;	 /* raw response verifier */
	SVCAUTH		*xp_auth;	 /* auth handle of current req */
#if defined(TIRPC_EPOLL)
        struct epoll_event xp_epoll_ev;  /* event handle */
#endif
	void		*xp_p1;		 /* private: for use by svc ops */
	void		*xp_p2;		 /* private: for use by svc ops */
	void		*xp_p3;		 /* private: for use by svc lib */
	void		*xp_p4;		 /* private: for use by svc lib */
	int		xp_type;	 /* transport type */
	u_int		xp_flags;	 /* flags */
	rwlock_t lock;                   /* xprt lock */
} SVCXPRT;

/* functions which can be installed using a control function, e.g., 
 * xp_ops2->xp_control */
typedef bool_t (*xp_recv_t)(struct __rpc_svcxprt *, struct rpc_msg *);

/*
 * Service request
 */
struct svc_req {
	/* ORDER: compatibility with legacy RPC */
	u_int32_t	rq_prog;	/* service program number */
	u_int32_t	rq_vers;	/* service protocol version */
	u_int32_t	rq_proc;	/* the desired procedure */
	struct opaque_auth rq_cred;	/* raw creds from the wire */
	void		*rq_clntcred;	/* read only cooked cred */
	SVCXPRT		*rq_xprt;	/* associated transport */

	/* New with TI-RPC */
	caddr_t		rq_clntname;	/* read only client name */
	caddr_t		rq_svcname;	/* read only cooked service cred */
};

/*
 *  Approved way of getting address of caller
 */
#define svc_getrpccaller(x) (&(x)->xp_rtaddr)

/*
 * Operations defined on an SVCXPRT handle
 *
 * SVCXPRT		*xprt;
 * struct rpc_msg	*msg;
 * xdrproc_t		 xargs;
 * void *		 argsp;
 */
#define SVC_RECV(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))
#define svc_recv(xprt, msg)				\
	(*(xprt)->xp_ops->xp_recv)((xprt), (msg))

#define SVC_STAT(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)
#define svc_stat(xprt)					\
	(*(xprt)->xp_ops->xp_stat)(xprt)

#define SVC_GETARGS(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))
#define svc_getargs(xprt, xargs, argsp)			\
	(*(xprt)->xp_ops->xp_getargs)((xprt), (xargs), (argsp))

#define SVC_REPLY(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))
#define svc_reply(xprt, msg)				\
	(*(xprt)->xp_ops->xp_reply) ((xprt), (msg))

#define SVC_FREEARGS(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))
#define svc_freeargs(xprt, xargs, argsp)		\
	(*(xprt)->xp_ops->xp_freeargs)((xprt), (xargs), (argsp))

#define SVC_DESTROY(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)
#define svc_destroy(xprt)				\
	(*(xprt)->xp_ops->xp_destroy)(xprt)

#define SVC_CONTROL(xprt, rq, in)			\
	(*(xprt)->xp_ops2->xp_control)((xprt), (rq), (in))

/*
 * Service init (optional).
 */

__BEGIN_DECLS
void
svc_init (struct svc_init_params *);
__END_DECLS


/*
 * Service registration
 *
 * svc_reg(xprt, prog, vers, dispatch, nconf)
 *	const SVCXPRT *xprt;
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 *	const void (*dispatch)();
 *	const struct netconfig *nconf;
 */

__BEGIN_DECLS
extern bool_t	svc_reg(SVCXPRT *, const rpcprog_t, const rpcvers_t,
			void (*)(struct svc_req *, SVCXPRT *),
			const struct netconfig *);
__END_DECLS

/*
 * Service un-registration
 *
 * svc_unreg(prog, vers)
 *	const rpcprog_t prog;
 *	const rpcvers_t vers;
 */

__BEGIN_DECLS
extern void	svc_unreg(const rpcprog_t, const rpcvers_t);
__END_DECLS

/*
 * Transport registration.
 *
 * xprt_register(xprt)
 *	SVCXPRT *xprt;
 */
__BEGIN_DECLS
extern void	xprt_register(SVCXPRT *);
__END_DECLS

/*
 * Transport un-register
 *
 * xprt_unregister(xprt)
 *	SVCXPRT *xprt;
 */
__BEGIN_DECLS
extern void	xprt_unregister(SVCXPRT *);
__END_DECLS

/*
 * Create transport from file descriptor
 *
 * makefd_xprt(fd, sendsize, recvsize)
 *	int fd;
 *	u_int sendsize;
 *	u_int recvsize;
 */
__BEGIN_DECLS
SVCXPRT *makefd_xprt(int, u_int, u_int);
__END_DECLS

/*
 * This is used to set xprt->xp_raddr in a way legacy
 * apps can deal with
 *
 * __xprt_set_raddr(xprt, ss)
 *	SVCXPRT *xprt;
 *      const struct sockaddr_storage *ss;
 */
__BEGIN_DECLS
extern void __xprt_set_raddr(SVCXPRT *, const struct sockaddr_storage *);
__END_DECLS

/*
 * When the service routine is called, it must first check to see if it
 * knows about the procedure;  if not, it should call svcerr_noproc
 * and return.  If so, it should deserialize its arguments via
 * SVC_GETARGS (defined above).  If the deserialization does not work,
 * svcerr_decode should be called followed by a return.  Successful
 * decoding of the arguments should be followed the execution of the
 * procedure's code and a call to svc_sendreply.
 *
 * Also, if the service refuses to execute the procedure due to too-
 * weak authentication parameters, svcerr_weakauth should be called.
 * Note: do not confuse access-control failure with weak authentication!
 *
 * NB: In pure implementations of rpc, the caller always waits for a reply
 * msg.  This message is sent when svc_sendreply is called.
 * Therefore pure service implementations should always call
 * svc_sendreply even if the function logically returns void;  use
 * xdr.h - xdr_void for the xdr routine.  HOWEVER, tcp based rpc allows
 * for the abuse of pure rpc via batched calling or pipelining.  In the
 * case of a batched call, svc_sendreply should NOT be called since
 * this would send a return message, which is what batching tries to avoid.
 * It is the service/protocol writer's responsibility to know which calls are
 * batched and which are not.  Warning: responding to batch calls may
 * deadlock the caller and server processes!
 */

__BEGIN_DECLS
extern bool_t	svc_sendreply(SVCXPRT *, xdrproc_t, void *);
extern void	svcerr_decode(SVCXPRT *);
extern void	svcerr_weakauth(SVCXPRT *);
extern void	svcerr_noproc(SVCXPRT *);
extern void	svcerr_progvers(SVCXPRT *, rpcvers_t, rpcvers_t);
extern void	svcerr_auth(SVCXPRT *, enum auth_stat);
extern void	svcerr_noprog(SVCXPRT *);
extern void	svcerr_systemerr(SVCXPRT *);
extern int	rpc_reg(rpcprog_t, rpcvers_t, rpcproc_t,
			char *(*)(char *), xdrproc_t, xdrproc_t,
			char *);
__END_DECLS

/*
 * Lowest level dispatching -OR- who owns this process anyway.
 * Somebody has to wait for incoming requests and then call the correct
 * service routine.  The routine svc_run does infinite waiting; i.e.,
 * svc_run never returns.
 * Since another (co-existant) package may wish to selectively wait for
 * incoming calls or other events outside of the rpc architecture, the
 * routine svc_getreq is provided.  It must be passed readfds, the
 * "in-place" results of a select system call (see select, section 2).
 */

/*
 * Global keeper of rpc service descriptors in use
 * dynamic; must be inspected before each call to select
 */
extern int svc_maxfd;
#ifdef FD_SETSIZE
extern fd_set svc_fdset;
#define svc_fds svc_fdset.fds_bits[0]	/* compatibility */
#else
extern int svc_fds;
#endif /* def FD_SETSIZE */

/*
 * a small program implemented by the svc_rpc implementation itself;
 * also see clnt.h for protocol numbers.
 */
__BEGIN_DECLS
extern void rpctest_service(void);
__END_DECLS

__BEGIN_DECLS
extern void	svc_getreq(int);
extern void	svc_getreqset(fd_set *);
extern void	svc_getreq_common(int);
struct pollfd;
extern void	svc_getreq_poll(struct pollfd *, int);

extern void	svc_run(void);
extern void	svc_exit(void);
__END_DECLS

/*
 * Socket to use on svcxxx_create call to get default socket
 */
#define	RPC_ANYSOCK	-1
#define RPC_ANYFD	RPC_ANYSOCK

/*
 * These are the existing service side transport implementations
 */

__BEGIN_DECLS
/*
 * Transport independent svc_create routine.
 */
extern int svc_create(void (*)(struct svc_req *, SVCXPRT *),
			   const rpcprog_t, const rpcvers_t, const char *);
/*
 *      void (*dispatch)();             -- dispatch routine
 *      const rpcprog_t prognum;        -- program number
 *      const rpcvers_t versnum;        -- version number
 *      const char *nettype;            -- network type
 */


/*
 * Generic server creation routine. It takes a netconfig structure
 * instead of a nettype.
 */

extern SVCXPRT *svc_tp_create(void (*)(struct svc_req *, SVCXPRT *),
				   const rpcprog_t, const rpcvers_t,
				   const struct netconfig *);
        /*
         * void (*dispatch)();            -- dispatch routine
         * const rpcprog_t prognum;       -- program number
         * const rpcvers_t versnum;       -- version number
         * const struct netconfig *nconf; -- netconfig structure
         */


/*
 * Generic TLI create routine
 */
extern SVCXPRT *svc_tli_create(const int, const struct netconfig *,
			       const struct t_bind *, const u_int,
			       const u_int);
/*
 *      const int fd;                   -- connection end point
 *      const struct netconfig *nconf;  -- netconfig structure for network
 *      const struct t_bind *bindaddr;  -- local bind address
 *      const u_int sendsz;             -- max sendsize
 *      const u_int recvsz;             -- max recvsize
 */

/*
 * Connectionless and connectionful create routines
 */

extern SVCXPRT *svc_vc_create(const int, const u_int, const u_int);
/*
 *      const int fd;                           -- open connection end point
 *      const u_int sendsize;                   -- max send size
 *      const u_int recvsize;                   -- max recv size
 */

__END_DECLS

#define SVC_VC_CLNT_CREATE_NONE           0x0000
#define SVC_VC_CLNT_CREATE_DEDICATED      0x0001

__BEGIN_DECLS

/*
 * Create a client handle from an active service transport handle.
 */
extern CLIENT *svc_vc_clnt_create(SVCXPRT *, const rpcprog_t, const rpcvers_t,
				  const uint32_t);
/*
 *      SVCXPRT *xprt;                          -- active service xprt
 *      const rpcprog_t prog;                   -- RPC program number
 *      const rpcvers_t vers;                   -- RPC program version
 */

__END_DECLS

#define SVC_VC_CREATE_CL_FLAG_NONE        0x0000
#define SVC_VC_CREATE_CL_FLAG_DEDICATED   0x0001

__BEGIN_DECLS

/*
 * Create an RPC SVCXPRT handle from an active client transport
 * handle, i.e., to service RPC requests 
 */
extern SVCXPRT *svc_vc_create_cl(CLIENT *, u_int, u_int, const uint32_t);
        /*
	 * 
         * CLIENT *cl;                                  -- connected client
         * const u_int sendsize;                        -- max send size
         * const u_int recvsize;                        -- max recv size
         * const uint32_t flags;                        -- flags
         */


/*
 * Destroy a transport handle.  Do not alter connected transport state.
 */
extern void svc_vc_destroy_handle(SVCXPRT * xprt);

/*
 * Construct a service transport, unassociated with any transport
 * connection.
 */
extern SVCXPRT *svc_vc_create_xprt(u_long sendsz, u_long recvsz);

/*
 * Duplicate xprt from original to copy.
 */
extern void svc_vc_copy_xprt(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig);


/*
 * Added for compatibility to old rpc 4.0. Obsoleted by svc_vc_create().
 */
extern SVCXPRT *svcunix_create(int, u_int, u_int, char *);

extern SVCXPRT *svc_dg_create(const int, const u_int, const u_int);
        /*
         * const int fd;                                -- open connection
         * const u_int sendsize;                        -- max send size
         * const u_int recvsize;                        -- max recv size
         */


/*
 * the routine takes any *open* connection
 * descriptor as its first input and is used for open connections.
 */
extern SVCXPRT *svc_fd_create(const int, const u_int, const u_int);
/*
 *      const int fd;                           -- open connection end point
 *      const u_int sendsize;                   -- max send size
 *      const u_int recvsize;                   -- max recv size
 */

/*
 * Added for compatibility to old rpc 4.0. Obsoleted by svc_fd_create().
 */
extern SVCXPRT *svcunixfd_create(int, u_int, u_int);

/*
 * Memory based rpc (for speed check and testing)
 */
extern SVCXPRT *svc_raw_create(void);

/*
 * svc_dg_enable_cache() enables the cache on dg transports.
 */
int svc_dg_enablecache(SVCXPRT *, const u_int);

int __rpc_get_local_uid(SVCXPRT *_transp, uid_t *_uid);

__END_DECLS


/* for backward compatibility */
#include <rpc/svc_soc.h>



#endif /* !_TIRPC_SVC_H */
