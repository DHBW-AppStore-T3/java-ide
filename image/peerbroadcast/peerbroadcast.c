/*
 * peerbroadcast: makes a UDP "broadcast" work between the environments of one deployment.
 *
 * The platform has no layer-2 broadcast between pods: 255.255.255.255, multicast and ff02::1 would reach the pods of
 * every deployment on the node, so the network policy drops them. What it does give a deployment is the headless
 * Service "peers": its DNS name resolves to the address of every pod of the deployment.
 *
 * This library is loaded with LD_PRELOAD. When a program sends a datagram (sendto, sendmsg) to a broadcast or
 * multicast address, it sends the same datagram to every address of "peers" instead, from the same socket and to the
 * same port. The receiver needs no change: it binds to the port as it would for a real broadcast. Every other send
 * (unicast, TCP, a name server) goes straight to libc and is not touched.
 *
 * Group addresses: 255.255.255.255, 224.0.0.0/4, the directed broadcast address of a local IPv4 subnet, ff00::/8 and
 * the IPv4 forms of these inside ::ffff:0:0/96 (what a Java DatagramSocket sends on a dual-stack socket).
 *
 * Not covered: send/write on a socket that was connected to a group address, sendmmsg, and programs that do not use
 * the C library for networking (statically linked ones, Go). If "peers" does not resolve (the pod network is off), the
 * send goes out unchanged, so a program behaves as it would without this library.
 *
 * Environment: APPSTORE_PEER_BROADCAST=off disables it, APPSTORE_PEERS_NAME replaces the name "peers".
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define MAX_PEERS 256
#define MAX_BROADCASTS 32
/* How long a lookup of "peers" and of the local subnets is reused. Pods come and go; a few seconds is fresh enough. */
#define CACHE_SECONDS 2
#define NEGATIVE_CACHE_SECONDS 5

typedef ssize_t (*sendto_fn)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
typedef ssize_t (*sendmsg_fn)(int, const struct msghdr *, int);

static sendto_fn real_sendto;
static sendmsg_fn real_sendmsg;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static int enabled = 1;
static const char *peers_name = "peers";

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
/* Set while this library itself does a lookup (the resolver sends datagrams, too). */
static __thread int in_shim;

static struct in_addr peers4[MAX_PEERS];
static size_t peers4_n;
static struct in6_addr peers6[MAX_PEERS];
static size_t peers6_n;
static time_t peers_valid_until;

static uint32_t broadcasts[MAX_BROADCASTS]; /* host byte order */
static size_t broadcasts_n;
static time_t broadcasts_valid_until;

static void init(void) {
    real_sendto = (sendto_fn)dlsym(RTLD_NEXT, "sendto");
    real_sendmsg = (sendmsg_fn)dlsym(RTLD_NEXT, "sendmsg");
    const char *off = getenv("APPSTORE_PEER_BROADCAST");
    if (off && (strcmp(off, "off") == 0 || strcmp(off, "0") == 0)) enabled = 0;
    const char *name = getenv("APPSTORE_PEERS_NAME");
    if (name && *name) peers_name = name;
}

static time_t now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

/* The directed broadcast addresses of the local IPv4 subnets (10.0.0.255 for 10.0.0.7/24). Caller holds the lock. */
static void refresh_broadcasts(time_t t) {
    if (t < broadcasts_valid_until) return;
    broadcasts_n = 0;
    struct ifaddrs *list;
    in_shim++;
    if (getifaddrs(&list) == 0) {
        for (struct ifaddrs *a = list; a && broadcasts_n < MAX_BROADCASTS; a = a->ifa_next) {
            if (!a->ifa_addr || !a->ifa_netmask || a->ifa_addr->sa_family != AF_INET) continue;
            uint32_t addr = ntohl(((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
            uint32_t mask = ntohl(((struct sockaddr_in *)a->ifa_netmask)->sin_addr.s_addr);
            /* A /32 (or /31) has no broadcast address of its own. */
            if (mask == 0 || mask >= 0xFFFFFFFEu) continue;
            broadcasts[broadcasts_n++] = addr | ~mask;
        }
        freeifaddrs(list);
    }
    in_shim--;
    broadcasts_valid_until = t + CACHE_SECONDS;
}

static int v4_is_group(uint32_t host_order) {
    if (host_order == 0xFFFFFFFFu) return 1;
    if ((host_order & 0xF0000000u) == 0xE0000000u) return 1;
    pthread_mutex_lock(&lock);
    refresh_broadcasts(now());
    int hit = 0;
    for (size_t i = 0; i < broadcasts_n && !hit; i++) hit = broadcasts[i] == host_order;
    pthread_mutex_unlock(&lock);
    return hit;
}

/* Whether the destination is a broadcast or multicast address; fills in its port (network byte order). */
static int is_group(const struct sockaddr *sa, socklen_t len, in_port_t *port) {
    if (!sa) return 0;
    if (sa->sa_family == AF_INET && len >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        *port = in->sin_port;
        return v4_is_group(ntohl(in->sin_addr.s_addr));
    }
    if (sa->sa_family == AF_INET6 && len >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        *port = in6->sin6_port;
        if (IN6_IS_ADDR_MULTICAST(&in6->sin6_addr)) return 1;
        if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
            uint32_t v4;
            memcpy(&v4, &in6->sin6_addr.s6_addr[12], sizeof v4);
            return v4_is_group(ntohl(v4));
        }
    }
    return 0;
}

/* Looks up "peers" if the cached answer is old. Caller holds the lock. */
static void refresh_peers(time_t t) {
    if (t < peers_valid_until) return;
    peers4_n = peers6_n = 0;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    in_shim++;
    int rc = getaddrinfo(peers_name, NULL, &hints, &res);
    in_shim--;
    if (rc != 0) {
        peers_valid_until = t + NEGATIVE_CACHE_SECONDS;
        return;
    }
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        if (a->ai_family == AF_INET && peers4_n < MAX_PEERS) {
            peers4[peers4_n++] = ((struct sockaddr_in *)a->ai_addr)->sin_addr;
        } else if (a->ai_family == AF_INET6 && peers6_n < MAX_PEERS) {
            const struct in6_addr *addr = &((struct sockaddr_in6 *)a->ai_addr)->sin6_addr;
            if (!IN6_IS_ADDR_LINKLOCAL(addr)) peers6[peers6_n++] = *addr;
        }
    }
    freeaddrinfo(res);
    peers_valid_until = t + (peers4_n + peers6_n ? CACHE_SECONDS : NEGATIVE_CACHE_SECONDS);
}

/*
 * The destinations for a group send on this socket, one per pod. "peers" lists a pod once per address family, so a
 * dual-stack cluster must not use both: an IPv6 socket takes the IPv6 addresses (or the IPv4 ones as ::ffff:a.b.c.d if
 * there are none), an IPv4 socket the IPv4 ones.
 */
struct targets {
    struct sockaddr_in6 addr[MAX_PEERS]; /* also holds a sockaddr_in */
    socklen_t len[MAX_PEERS];
    size_t n;
};

static void build_targets(int fd, in_port_t port, struct targets *out) {
    int domain = 0;
    socklen_t dl = sizeof domain;
    out->n = 0;
    if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &dl) != 0) return;

    pthread_mutex_lock(&lock);
    refresh_peers(now());
    if (domain == AF_INET) {
        for (size_t i = 0; i < peers4_n; i++, out->n++) {
            struct sockaddr_in *s = (struct sockaddr_in *)&out->addr[out->n];
            memset(s, 0, sizeof *s);
            s->sin_family = AF_INET;
            s->sin_port = port;
            s->sin_addr = peers4[i];
            out->len[out->n] = sizeof *s;
        }
    } else if (domain == AF_INET6) {
        size_t count = peers6_n ? peers6_n : peers4_n;
        for (size_t i = 0; i < count; i++, out->n++) {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&out->addr[out->n];
            memset(s, 0, sizeof *s);
            s->sin6_family = AF_INET6;
            s->sin6_port = port;
            if (peers6_n) {
                s->sin6_addr = peers6[i];
            } else {
                s->sin6_addr.s6_addr[10] = s->sin6_addr.s6_addr[11] = 0xff;
                memcpy(&s->sin6_addr.s6_addr[12], &peers4[i], sizeof peers4[i]);
            }
            out->len[out->n] = sizeof *s;
        }
    }
    pthread_mutex_unlock(&lock);
}

/* True when the send was replaced; *result is then what the caller returns (with errno set on -1). */
static int fan_out_sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *dest, socklen_t dlen,
                          ssize_t *result) {
    in_port_t port;
    if (in_shim || !enabled || !is_group(dest, dlen, &port)) return 0;
    struct targets t;
    build_targets(fd, port, &t);
    if (t.n == 0) return 0; /* no peers (pod network off): behave as without the library */
    ssize_t first_ok = -1;
    int err = 0;
    for (size_t i = 0; i < t.n; i++) {
        ssize_t r = real_sendto(fd, buf, n, flags, (struct sockaddr *)&t.addr[i], t.len[i]);
        if (r >= 0) {
            if (first_ok < 0) first_ok = r;
        } else {
            err = errno;
        }
    }
    if (first_ok >= 0) {
        *result = first_ok;
    } else {
        errno = err;
        *result = -1;
    }
    return 1;
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *dest, socklen_t dlen) {
    pthread_once(&init_once, init);
    ssize_t result;
    if (fan_out_sendto(fd, buf, n, flags, dest, dlen, &result)) return result;
    return real_sendto(fd, buf, n, flags, dest, dlen);
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
    pthread_once(&init_once, init);
    in_port_t port;
    if (!in_shim && enabled && msg && is_group((const struct sockaddr *)msg->msg_name, msg->msg_namelen, &port)) {
        struct targets t;
        build_targets(fd, port, &t);
        if (t.n > 0) {
            ssize_t first_ok = -1;
            int err = 0;
            for (size_t i = 0; i < t.n; i++) {
                struct msghdr copy = *msg;
                copy.msg_name = (void *)&t.addr[i];
                copy.msg_namelen = t.len[i];
                ssize_t r = real_sendmsg(fd, &copy, flags);
                if (r >= 0) {
                    if (first_ok < 0) first_ok = r;
                } else {
                    err = errno;
                }
            }
            if (first_ok >= 0) return first_ok;
            errno = err;
            return -1;
        }
    }
    return real_sendmsg(fd, msg, flags);
}
