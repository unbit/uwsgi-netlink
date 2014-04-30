#include <uwsgi.h>
extern struct uwsgi_server uwsgi;

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#ifndef NETLINK_SOCK_DIAG
#define NETLINK_SOCK_DIAG       4
#endif

#include <linux/sock_diag.h>
#include <linux/unix_diag.h>

/*

	netlink plugin exposing commodity functions and hooks

	(and some automatic improvement in monitoring)


*/

int uwsgi_netlink_new(int type) {
	int fd = socket(AF_NETLINK, SOCK_DGRAM, type);
	if (fd < 0) {
		uwsgi_error("uwsgi_netlink_new()/socket()");
	}
	return fd;
}

int uwsgi_netlink_sendmsg(int fd, uint16_t type, uint16_t flags, void *buf, size_t len) {
	struct sockaddr_nl sa;
	memset(&sa, 0, sizeof(struct sockaddr_nl));
	sa.nl_family = AF_NETLINK;

	struct nlmsghdr nlh;
	memset(&nlh, 0, sizeof(struct nlmsghdr));
	nlh.nlmsg_len = NLMSG_LENGTH(len);
	nlh.nlmsg_type = type;
	nlh.nlmsg_flags = flags;
	
	struct iovec iov[2];
	iov[0].iov_base = &nlh;
	iov[0].iov_len = sizeof(struct nlmsghdr);
	iov[1].iov_base = buf;
	iov[1].iov_len = len;

	struct msghdr msg;
	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_name = &sa;
	msg.msg_namelen = sizeof(struct sockaddr_nl);
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	ssize_t ret = sendmsg(fd, &msg, 0);
	if (ret < 0) {
		uwsgi_error("uwsgi_netlink_sendmsg()/sendmsg()");
	}	
	return ret;
}

static void netlink_socket_queue_unix_diag_run() {

	static time_t last_run = 0;

	// avoid netlink storming
	if (last_run > 0 && last_run == uwsgi.current_time) return;
	last_run = uwsgi.current_time;

	int fd = uwsgi_netlink_new(NETLINK_SOCK_DIAG);
	if (fd < 0) return;

	struct unix_diag_req udr;
	memset(&udr, 0, sizeof(struct unix_diag_req));

	udr.sdiag_family = AF_UNIX;
	udr.sdiag_protocol = SOCK_STREAM;
	udr.udiag_states = 1<<TCP_LISTEN;
	udr.udiag_show = UDIAG_SHOW_NAME|UDIAG_SHOW_RQLEN;

	if (uwsgi_netlink_sendmsg(fd, SOCK_DIAG_BY_FAMILY, NLM_F_DUMP|NLM_F_REQUEST, &udr, sizeof(struct unix_diag_req)) < 0) goto end;

	// now wait for the response
	uint8_t buf[8192];
		int ret = uwsgi_waitfd_event(fd, 1, POLLIN);
		if (ret <= 0) goto end;
		ssize_t rlen = recv(fd, buf, 8192, 0);
		if (rlen <= 0) goto end;
		struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
		while(NLMSG_OK(nlh, rlen)) {
			if (nlh->nlmsg_type == NLMSG_DONE) goto end;
			if (nlh->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *nle = (struct nlmsgerr *) NLMSG_DATA(nlh);
				uwsgi_log("[uwsgi-netlink] error: %d %s\n", nle->error, strerror(-nle->error));
				goto end;
			}	
			struct unix_diag_msg *udm = (struct unix_diag_msg *) NLMSG_DATA(nlh);
			size_t diag_len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(struct unix_diag_msg));
			struct rtattr *attr = (struct rtattr *) (udm+1);
			struct uwsgi_socket *found_sock = NULL;
			while (RTA_OK(attr, diag_len)) {
				if (attr->rta_type == UNIX_DIAG_NAME) {
					struct uwsgi_socket *uwsgi_sock = uwsgi.sockets;
					while(uwsgi_sock) {
						if (uwsgi_sock->family == AF_UNIX) {
							// the payload size includes the final 0
							if (!uwsgi_strncmp(RTA_DATA(attr), RTA_PAYLOAD(attr)-1, uwsgi_sock->name, uwsgi_sock->name_len)) {
								found_sock = uwsgi_sock;
							}
						}
						uwsgi_sock = uwsgi_sock->next;
					}
				}
				else if (attr->rta_type == UNIX_DIAG_RQLEN) {
					struct unix_diag_rqlen *rql = (struct unix_diag_rqlen *) RTA_DATA(attr);
					if (found_sock) {
						found_sock->queue = rql->udiag_rqueue;
						found_sock->max_queue = rql->udiag_wqueue;
						// overengineering
						found_sock = NULL;
					}
				}
				attr = RTA_NEXT(attr, diag_len);
			}
			
			nlh = NLMSG_NEXT(nlh, rlen);
		}

end:
	close(fd);
}

struct uwsgi_plugin netlink_plugin = {
	.name = "netlink",
	.master_cycle = netlink_socket_queue_unix_diag_run,
};
