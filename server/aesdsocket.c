#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <pthread.h>
#include <sys/queue.h>


// see BSD implementation in https://github.com/openembedded/openembedded-core/blob/master/meta/recipes-core/musl/bsd-headers/sys-queue.h#L132
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                      \
    for ((var) = SLIST_FIRST((head));                                   \
         (var) && ((tvar) = SLIST_NEXT((var), field), 1);               \
         (var) = (tvar))


#define PORT_STR    "9000"
#define LISTEN_BACKLOG 50
#define BUFFER_SIZE 1024
#define BUFFER_SIZE_MAX (BUFFER_SIZE * 8)
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"

#define EXIT_ERR_CODE -1

static void* handle_client(void* args);
static void signal_handler(int signo);
static int setup_signals(void);
static int write_to_file(int fd, const char *buf, size_t len);
static int send_file_to_client(int client_fd);

static volatile sig_atomic_t m_shutdown = 0;

static char    m_send_file_buf[BUFFER_SIZE];
static char    m_recv_from_client_buf[BUFFER_SIZE];
static char    m_pkt_buf[BUFFER_SIZE * 8];

static pthread_mutex_t m_file_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Each accepted connection gets one thread_node.
 * The node is allocated before pthread_create and freed after pthread_join.
 */
typedef struct thread_node {
    pthread_t                thread;
    int                      client_fd;
    SLIST_ENTRY(thread_node) entries;
} thread_node_t;

/* Head type for the singly-linked list. */
SLIST_HEAD(thread_list, thread_node);

/* The list and its protecting mutex are only accessed from the main thread,
 * so no additional lock is needed for the list itself. */
static struct thread_list g_threads = SLIST_HEAD_INITIALIZER(g_threads);

static pthread_t          m_timer_thread;

#define TIMESTAMP_INTERVAL_S 10

/*
 * Every 10 seconds wake up and append RFC 2822 timestamp to OUTPUT_FILE
 *
 * Format:
 *   timestamp:Tue, 19 May 2026 00:01:02 +0000
 *
 * The thread exits cleanly when m_shutdown is set.
 */

static void *timer_thread(void *arg);\

/*
 * Reap any threads that have already finished.
 * Called from the main thread before and after the accept loop.
 * Uses SLIST_FOREACH_SAFE so we can remove while iterating.
 */
static void reap_finished_threads(void);
 
/*
 * Join ALL remaining threads unconditionally.
 * Called during shutdown after closing the server socket.
 * Closing client_fd from the main thread is not done here — each thread
 * closes its own fd on exit, so we only need to wait.
 */
static void join_all_threads(void);

int main(int argc, char *argv[])
{
    struct addrinfo hints, *res;

    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        daemon_mode = 1;
    }

    openlog("server", LOG_PID | LOG_CONS, LOG_DAEMON);

    int sig_err = setup_signals();
    if(sig_err)
    {
        perror("setup_signals");
        return EXIT_ERR_CODE;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;      /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;    /* TCP */
    hints.ai_flags    = AI_PASSIVE;     /* wildcard bind address */

    int gai_err = getaddrinfo(NULL, PORT_STR, &hints, &res);
    if (gai_err != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_err));
        return EXIT_ERR_CODE;
    }

    int server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1)
    {
        perror("socket");
        return EXIT_ERR_CODE;
    }
 
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt");
        return EXIT_ERR_CODE;
    }
 
   if (bind(server_fd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("bind");
        return EXIT_ERR_CODE;
    }

    /* addrinfo list no longer needed */
    freeaddrinfo(res);

    if (listen(server_fd, LISTEN_BACKLOG) == -1)
    {
        perror("listen");
        return EXIT_ERR_CODE;
    }
 
    if (daemon_mode)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            return EXIT_ERR_CODE;
        }

        if (pid > 0) //parent
        {
            exit(EXIT_SUCCESS);
        }

        if (setsid() == -1)
        {
            perror("setsid");
            return EXIT_ERR_CODE;
        }
    
        if (chdir("/") == -1)
        {
            perror("chdir/");
            return EXIT_ERR_CODE;
        }
    
        umask(0);

        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd == -1)
        {
            perror("null_fd");
            return EXIT_ERR_CODE;
        }
    
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
    
        if (null_fd > STDERR_FILENO)
        {
            close(null_fd);
        }
    }

    int rc = pthread_create(&m_timer_thread, NULL, timer_thread, NULL);
    if (rc != 0) {
        errno = rc;
        perror("pthread_create(timer_thread)");
        return EXIT_ERR_CODE;
    }


    while(1)
    {
        /* Opportunistically reap threads that have already finished. */
        reap_finished_threads();

        if (m_shutdown) break;

        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
 
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1)
        {
            if (errno == EINTR)
            {
                /* Interrupted by a signal */
                if (m_shutdown) break;
                continue;
            }

            perror("accept");
            return EXIT_ERR_CODE;
        }
        
        char client_ip[INET6_ADDRSTRLEN];
        if (client_addr.ss_family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
            inet_ntop(AF_INET, &s->sin_addr, client_ip, sizeof(client_ip));

        }
        else
        {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;
            inet_ntop(AF_INET6, &s->sin6_addr, client_ip, sizeof(client_ip));
        }

        syslog(LOG_INFO, "Accepted connection from %s\n",  client_ip);

        /* Allocate a node; initialise it; add to list; spawn thread. */
        thread_node_t *node = malloc(sizeof(thread_node_t));
        if (node == NULL) {
            perror("malloc thread_node");
            close(client_fd);
            continue;
        }
 
        node->client_fd   = client_fd;
 
        int rc = pthread_create(&node->thread, NULL, handle_client, node);
        if (rc != 0) {
            errno = rc;
            perror("pthread_create(handle_client)");
            free(node);
            close(client_fd);
            continue;
        }
 
        SLIST_INSERT_HEAD(&g_threads, node, entries);

        if (m_shutdown) break;
    }
 
    syslog(LOG_INFO, "Caught signal, exiting\n");

    close(server_fd);
    
    /* Wait for the timer thread to notice m_shutdown and exit. */
    pthread_join(m_timer_thread, NULL);

    join_all_threads();
 
    pthread_mutex_destroy(&m_file_mutex);

    //delete file
    if (unlink(OUTPUT_FILE) == -1 && errno != ENOENT)
    {
        perror("delete file");
    }

    closelog();

    return EXIT_SUCCESS;
}

static void *timer_thread(void *arg)
{
    (void)arg;
 
    while (!m_shutdown) {
        /* Sleep in 1-second increments so we notice m_shutdown promptly. */
        for (int i = 0; i < TIMESTAMP_INTERVAL_S && !m_shutdown; i++)
        {
            sleep(1);
        }
 
        if (m_shutdown)
        {
            break;
        }
 
        /* Build the RFC 2822 timestamp string. */
        time_t     now = time(NULL);
        struct tm  tm_buf;
        char       ts[64];
 
        localtime_r(&now, &tm_buf); //re-entrant
 
        /* RFC 2822 date-time: "Tue, 19 May 2026 00:01:02 +0000" */
        if (strftime(ts, sizeof(ts), "%a, %d %b %Y %H:%M:%S %z", &tm_buf) == 0)
        {
            syslog(LOG_ERR, "timer_thread: strftime failed");
            continue;
        }
 
        /* Format the full line. */
        char line[128];
        int  len = snprintf(line, sizeof(line), "timestamp:%s\n", ts);
        if (len < 0 || (size_t)len >= sizeof(line))
        {
            syslog(LOG_ERR, "timer_thread: snprintf overflow");
            continue;
        }
 
        pthread_mutex_lock(&m_file_mutex);
 
        int file_fd = open(OUTPUT_FILE,
                           O_WRONLY | O_CREAT | O_APPEND,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (file_fd == -1)
        {
            syslog(LOG_ERR, "timer_thread: open(" OUTPUT_FILE "): %m");
            pthread_mutex_unlock(&m_file_mutex);
            continue;
        }
 
        if (write_to_file(file_fd, line, (size_t)len) == -1)
        {
            syslog(LOG_ERR, "timer_thread: write(" OUTPUT_FILE "): %m");
        }
 
        close(file_fd);
        pthread_mutex_unlock(&m_file_mutex);
 
        syslog(LOG_DEBUG, "Timestamp written: %s", ts);
    }
 
    return NULL;
}

/*
 * Reap any threads that have already finished.
 * Called from the main thread before and after the accept loop.
 * Uses SLIST_FOREACH_SAFE so we can remove while iterating.
 */
static void reap_finished_threads(void)
{
    thread_node_t *node, *tmp;
    SLIST_FOREACH_SAFE(node, &g_threads, entries, tmp) {
        /* pthread_tryjoin_np is Linux-specific; use it to avoid blocking. */
        if (pthread_tryjoin_np(node->thread, NULL) == 0) {
            SLIST_REMOVE(&g_threads, node, thread_node, entries);
            free(node);
        }
    }
}
 
/*
 * Join ALL remaining threads unconditionally.
 * Called during shutdown after closing the server socket.
 * Closing client_fd from the main thread is not done here — each thread
 * closes its own fd on exit, so we only need to wait.
 */
static void join_all_threads(void)
{
    thread_node_t *node, *tmp;
    SLIST_FOREACH_SAFE(node, &g_threads, entries, tmp) {
        /* Signal the client fd to unblock any blocking recv/send. */
        shutdown(node->client_fd, SHUT_RDWR);
        pthread_join(node->thread, NULL);
        SLIST_REMOVE(&g_threads, node, thread_node, entries);
        free(node);
    }
}

static void signal_handler(int signum)
{
    (void)signum;
    m_shutdown = 1;
}

static int setup_signals(void)
{
    struct sigaction sa;
 
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if ( sigaction(SIGINT,  &sa, NULL) == -1) 
    {
        perror("sigaction(SIGINT)");
    }

    if ( sigaction(SIGTERM, &sa, NULL) == -1)
    {
        perror("sigaction(SIGTERM)");
    }
    return EXIT_SUCCESS;
}
 
/* Write len bytes to fd
 * Returns 0 on success, -1 on error (errno set by write). */
static int write_to_file(int fd, const char *buf, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t w = write(fd, buf + written, len - written);
        if (w == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (size_t)w;
    }
    return EXIT_SUCCESS;
}
 
/*
 * Opens OUTPUT_FILE for reading and sends its entire contents to client_fd.
 * Returns 0 on success, -1 on any error.
 */
static int send_file_to_client(int client_fd)
{
    int rd_fd = open(OUTPUT_FILE, O_RDONLY);
    if (rd_fd == -1) {
        perror("send_file_to_client: open(" OUTPUT_FILE ")");
        return -1;
    }
 
    ssize_t n;
    int     ret = 0;
 
    while ((n = read(rd_fd, m_send_file_buf, sizeof(m_send_file_buf))) > 0) {
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t s = send(client_fd, m_send_file_buf + sent, (size_t)(n - sent), 0);
            if (s == -1) {
                if (errno == EINTR) continue;
                perror("send_file_to_client: send");
                ret = -1;
                goto done;
            }
            sent += s;
        }
    }
 
    if (n == -1) {
        perror("send_file_to_client: read");
        ret = -1;
    }
 
done:
    close(rd_fd);
    return ret;
}

static void* handle_client(void* arg)
{

    thread_node_t *node      = (thread_node_t *)arg;
    int client_fd = node->client_fd;

    pthread_mutex_lock(&m_file_mutex);
    /* Open (or create) the output file in append mode. */
    int file_fd = open(OUTPUT_FILE,
                       O_WRONLY | O_CREAT | O_APPEND,
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); /* 0644 */
    if (file_fd == -1) {
        perror("open(" OUTPUT_FILE ")");
        pthread_mutex_unlock(&m_file_mutex);
        close(client_fd);
        return NULL;
    }
 
    size_t  pkt_max_len = sizeof(m_pkt_buf);
    size_t  pkt_len = 0;
    ssize_t n;
    int     packets = 0;
    int     io_err  = 0;
 
    while (!io_err && (n = recv(client_fd, m_recv_from_client_buf, sizeof(m_recv_from_client_buf), 0)) > 0) {
 
        const char *p   = m_recv_from_client_buf;
        const char *end = m_recv_from_client_buf + n;
 
        while (p < end && !io_err) {
 
            /* flush before the incoming byte would overflow. */
            if (pkt_len >= pkt_max_len) {
                // flush buffer. full
                if (write_to_file(file_fd, m_pkt_buf, pkt_len) == -1) {
                    perror("write(" OUTPUT_FILE ") [buffer full]");
                    pthread_mutex_unlock(&m_file_mutex);
                    io_err = 1;
                    break;
                }
                pkt_len = 0;
            }
 
            m_pkt_buf[pkt_len++] = *p;
 
            if (*p == '\n') {
                if (write_to_file(file_fd, m_pkt_buf, pkt_len) == -1) {
                    perror("write(" OUTPUT_FILE ") [newline]");
                    pthread_mutex_unlock(&m_file_mutex);
                    io_err = 1;
                    break;
                }
                packets++;
                pkt_len = 0;
 
                close(file_fd);
                
                file_fd = -1;
                
                if (send_file_to_client(client_fd) == -1)
                {
                    fprintf(stderr, "warning: send_file_to_client failed for fd=%d\n", client_fd);
                }
                pthread_mutex_unlock(&m_file_mutex);
                
                close(client_fd);
                syslog(LOG_INFO, "Closed connection from %d\n", client_fd);
                return NULL;
            }
 
            p++;
        }
    }
 
    if (n == -1)
        perror("recv");
 
        
    if (file_fd != -1)
        close(file_fd);
    
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from client fd:%d\n", client_fd);

    return NULL;
}
