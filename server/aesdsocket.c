#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <syslog.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define PORT_STR    "9000"
#define LISTEN_BACKLOG 50
#define BUFFER_SIZE 1024
#define BUFFER_SIZE_MAX (BUFFER_SIZE * 8)
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"

#define EXIT_ERR_CODE -1

static void handle_client(int client_fd, const char *client_ip);
static void signal_handler(int signo);
static int setup_signals(void);
static int send_file_to_client(int client_fd);

static volatile sig_atomic_t m_shutdown = 0;

static char    m_send_file_buf[BUFFER_SIZE];
static char    m_recv_from_client_buf[BUFFER_SIZE];
static char    m_pkt_buf[BUFFER_SIZE * 8];

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

    while(1)
    {
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

        handle_client(client_fd, client_ip);

        if (m_shutdown) break;
    }
 
    syslog(LOG_INFO, "Caught signal, exiting\n");

    close(server_fd);
    
    //delete file
    if (unlink(OUTPUT_FILE) == -1 && errno != ENOENT)
    {
        perror("delete file");
    }

    closelog();

    return EXIT_SUCCESS;
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

static void handle_client(int client_fd, const char* client_ip)
{
    /* Open (or create) the output file in append mode. */
    int file_fd = open(OUTPUT_FILE,
                       O_WRONLY | O_CREAT | O_APPEND,
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); /* 0644 */
    if (file_fd == -1) {
        perror("open(" OUTPUT_FILE ")");
        close(client_fd);
        return;
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
                    io_err = 1;
                    break;
                }
                pkt_len = 0;
            }
 
            m_pkt_buf[pkt_len++] = *p;
 
            if (*p == '\n') {
                if (write_to_file(file_fd, m_pkt_buf, pkt_len) == -1) {
                    perror("write(" OUTPUT_FILE ") [newline]");
                    io_err = 1;
                    break;
                }
                packets++;
                pkt_len = 0;
 
                close(file_fd);
                file_fd = -1;
 
                if (send_file_to_client(client_fd) == -1)
                    fprintf(stderr, "warning: send_file_to_client failed for fd=%d\n", client_fd);
 
                syslog(LOG_INFO, "Closed connection from %s\n", client_ip);
 
                close(client_fd);
                return;
            }
 
            p++;
        }
    }
 
    if (n == -1)
        perror("recv");
 
        
    if (file_fd != -1)
        close(file_fd);
    
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s\n", client_ip);
}
