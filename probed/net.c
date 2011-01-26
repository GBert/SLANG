/**
 * \file   net.c
 * \brief  Wrapped network functions, such as 'receive with timestamp'
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2010-12-01
 */

#include <time.h>
#include <errno.h>
#include <string.h>
#include "probed.h"

/**
 * Receive on socket 'sock' into struct pkt with timestamp
 *
 * Wraps the recv() function, but optimized for the struct pkt_t. The 
 * function receives DATALEN bytes, and places both address, data and
 * RX timestamp into the struct pkt.
 * \param[in]  sock  The socket to read from
 * \param[in]  flags Flags to recv(), usually for reading from error queue
 * \param[out] pkt   Pointer to pkt, where addr, data and tstamp is placed
 * \warning          This function only receices DATALEN bytes
 */

int recv_w_ts(int sock, int flags, /*@out@*/ pkt_t *pkt) {
	socklen_t addrlen;
	struct msghdr msg[1];
	struct iovec iov[1];
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;

	/* prepare message structure */
	memset(&(pkt->data), 0, DATALEN);
	memset(msg, 0, sizeof (*msg));
	addrlen = (socklen_t)sizeof pkt->addr;
	iov[0].iov_base = pkt->data;
	iov[0].iov_len = DATALEN;
	msg[0].msg_iov = iov;
	msg[0].msg_iovlen = 1;
	msg[0].msg_name = (caddr_t)&pkt->addr;
	msg[0].msg_namelen = addrlen;
	msg[0].msg_control = &control;
	msg[0].msg_controllen = sizeof control;

	if (recvmsg(sock, msg, flags) < 0) {
		/* Don't warn about err queue, send_w_ts will figure that out */
		if ((flags & MSG_ERRQUEUE) == 0)
			syslog(LOG_INFO, "recvmsg: %s", strerror(errno));
		return -1;
	} else {
		if ((flags & MSG_ERRQUEUE) != 0) {
			/* store kernel tx tstamp */
			if (tstamp_extract(msg, &pkt->ts) < 0) 
				return -1;
			/* tx timestamp packet, just save and bail */
			return 0;
		} else {
			/* store rx tstamp */
			if (tstamp_extract(msg, &pkt->ts) < 0)
				syslog(LOG_ERR, "RX timestamp error");
			return 0;
		}
	}
}

/**
 * Send 'data' to 'addr'  on socket 'sock' with timestamp
 *
 * Wraps the send() function, but optimized for SLA-NG. It sends DATALEN
 * bytes ('data' has to be that large) onto 'sock' and places the TX 
 * timestamp in 'ts'.
 * \param[in]  sock  The socket to read from
 * \param[in]  addr  Pointer to address where to send the data
 * \param[in]  data  Pointer to the data to send
 * \param[out] ts    Pointer to ts, where to put the TX timestamp
 * \warning          This function only send DATALEN bytes
 * \bug              Will report TX timestamp error if sending data to other 
 *                   interface than the one SO_TIMESTAMPING is active on.
 */

int send_w_ts(int sock, addr_t *addr, char *data, /*@out@*/ ts_t *ts) {
	socklen_t slen;

	slen = (socklen_t)sizeof *(addr);
	memset(ts, 0, sizeof (struct timespec));
	/* get userland tx timestamp (before send, hehe) */
	if (cfg.ts == 'u')   
		(void)clock_gettime(CLOCK_REALTIME, ts);
	/* do the send */
	if (sendto(sock, data, DATALEN, 0, (struct sockaddr*)addr, slen) < 0) {
		syslog(LOG_INFO, "sendto: %s", strerror(errno));
		return -1;
	}
	/* get kernel tx timestamp */
	if (cfg.ts != 'u') { 
		if (tstamp_fetch_tx(sock, ts) < 0) {
			syslog(LOG_ERR, "TX timestamp error");
			return -1;
		}
	}
	return 0;
}

/**
 * Bind two listening sockets, one UDP (ping/pong) and one TCP (timestamps)
 * 
 * \param[out] s_udp Pointer to UDP socket to create and bind
 * \param[out] s_tcp Pointer to TCP socket to create, bind and listen
 * \param[in]  port  The port number to use for binding
 * \warning          Should be run only once
 */

void bind_or_die(/*@out@*/ int *s_udp, /*@out@*/ int *s_tcp, uint16_t port) {
	int f = 0;
	socklen_t slen;
	struct sockaddr_in6 my;

	syslog(LOG_INFO, "Binding port %d\n", (int)port);
	my.sin6_family = (sa_family_t)AF_INET6;
	my.sin6_port = htons(port);
	my.sin6_addr = in6addr_any;
	/* UDP socket */
	*s_udp = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (*s_udp < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} 
	/* Give us a dual-stack (ipv4/6) socket */
	slen = (socklen_t)sizeof f;
	if (setsockopt(*s_udp, IPPROTO_IPV6, IPV6_V6ONLY, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));
	/* Bind port */
	slen = (socklen_t)sizeof (struct sockaddr_in6);
	if (bind(*s_udp, (struct sockaddr *)&my, slen) < 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* TCP socket */
	*s_tcp = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (*s_tcp < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} 
	/* Give us a dual-stack (ipv4/6) socket */
	f = 0;
	slen = (socklen_t)sizeof f;
	if (setsockopt(*s_tcp, IPPROTO_IPV6, IPV6_V6ONLY, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));
	f = 1;
	if (setsockopt(*s_tcp, SOL_SOCKET, SO_REUSEADDR, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: SO_REUSEADDR: %s", strerror(errno));
	/* Bind port */
	slen = (socklen_t)sizeof (struct sockaddr_in6);
	if (bind(*s_tcp, (struct sockaddr *)&my, slen) < 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (listen(*s_tcp, 10) == -1) {
		syslog(LOG_ERR, "listen: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
}