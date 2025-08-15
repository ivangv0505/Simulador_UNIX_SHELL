/*
 * Proyecto: uamashell
 * Módulo: remote_client.c — Cliente para ejecución remota (Versión III)
 * Protocolo textual simple:
 *   Cliente -> Servidor:
 *       HELLO user=<u> pid=<p> tty=<t> ip=<i>\n
 *       CMD <n>\n<bytes...>
 *       QUIT\n
 *   Servidor -> Cliente:
 *       OK\n | ERR NOT_ALLOWED\n
 *       OUT <m>\n<bytes...>\nSTATUS <code>\n
 */

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#ifndef MAX_IP_STR
#define MAX_IP_STR 64
#endif
#ifndef DEFAULT_REMOTE_PORT
#define DEFAULT_REMOTE_PORT 5050
#endif

/* Estado de la sesión remota */
static int  g_remote_fd = -1;
static char g_remote_ip[MAX_IP_STR] = {0};

/* ---------------- helpers de E/S ---------------- */

static ssize_t write_all(int fd, const void *buf, size_t n){
    const char *p = (const char*)buf; size_t left = n;
    while(left){
        ssize_t w = send(fd, p, left, 0);
        if(w < 0){
            if(errno == EINTR) continue;
            return -1;
        }
        p += w; left -= (size_t)w;
    }
    return (ssize_t)n;
}

static int read_line(int fd, char *out, size_t max){
    size_t i = 0;
    while(i + 1 < max){
        char c; ssize_t r = recv(fd, &c, 1, 0);
        if(r == 0) return 0;
        if(r < 0){
            if(errno == EINTR) continue;
            return -1;
        }
        if(c == '\n'){ out[i] = '\0'; return 1; }
        out[i++] = c;
    }
    out[i] = '\0';
    return 1;
}

static int read_exact(int fd, void *buf, size_t n){
    char *p = (char*)buf; size_t got = 0;
    while(got < n){
        ssize_t r = recv(fd, p + got, n - got, 0);
        if(r == 0) return 0;
        if(r < 0){
            if(errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return 1;
}

/* ---------------- API pública ---------------- */

int remote_is_active(void){ return g_remote_fd >= 0; }

const char* remote_current_ip(void){
    return g_remote_ip[0] ? g_remote_ip : NULL;
}

int remote_connect(const char *ip, int port){
    if(!ip || !*ip){ errno = EINVAL; return -1; }
    if(g_remote_fd >= 0){ errno = EISCONN; return -1; }

    /* resolver y conectar */
    char portstr[16]; snprintf(portstr, sizeof(portstr), "%d", port>0?port:DEFAULT_REMOTE_PORT);
    struct addrinfo hints; memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_UNSPEC;

    struct addrinfo *res = NULL, *it = NULL;
    int err = getaddrinfo(ip, portstr, &hints, &res);
    if(err){ errno = ECONNREFUSED; return -1; }

    int fd = -1;
    for(it = res; it; it = it->ai_next){
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(fd < 0) continue;
        if(connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if(fd < 0) return -1;

    /* HELLO */
    char user[64] = {0}, tty[64] = {0}, lip[64] = {0};
    const char *u = getenv("USER"); if(u) strncpy(user, u, sizeof(user)-1); else strcpy(user, "unknown");

    const char *t = ttyname(STDIN_FILENO);
    if(t) strncpy(tty, t, sizeof(tty)-1); else strcpy(tty, "tty-unknown");

    const char *sc = getenv("SSH_CLIENT");
    if(sc){ strncpy(lip, sc, sizeof(lip)-1); char *sp=strchr(lip,' '); if(sp)*sp='\0'; }
    else strcpy(lip, "0.0.0.0");

    char hello[256];
    snprintf(hello, sizeof(hello), "HELLO user=%s pid=%d tty=%s ip=%s\n",
             user, (int)getpid(), tty, lip);

    if(write_all(fd, hello, strlen(hello)) < 0){ close(fd); return -1; }

    char line[256];
    int rl = read_line(fd, line, sizeof(line));
    if(rl <= 0){ close(fd); return -1; }
    if(strcmp(line, "OK") != 0){ close(fd); errno = EACCES; return -1; }

    /* listo */
    g_remote_fd = fd;
    strncpy(g_remote_ip, ip, sizeof(g_remote_ip)-1);
    return 0;
}

int remote_send_line(const char *line, FILE *out)
{
    if(g_remote_fd < 0){ errno = ENOTCONN; return -1; }
    if(!line) line = "";

    /* 1) enviar encabezado + payload */
    size_t n = strlen(line);
    char hdr[64];
    int hm = snprintf(hdr, sizeof(hdr), "CMD %zu\n", n);
    if(write_all(g_remote_fd, hdr, (size_t)hm) < 0) return -1;
    if(n && write_all(g_remote_fd, line, n) < 0) return -1;

    /* 2) leer "OUT <m>\n" */
    char l1[64];
    if(read_line(g_remote_fd, l1, sizeof(l1)) <= 0) return -1;

    size_t mlen = 0;
    if(sscanf(l1, "OUT %zu", &mlen) != 1){ errno = EPROTO; return -1; }

    /* 3) leer <m> bytes de salida y pasarlos a 'out' */
    if(mlen > 0){
        char *buf = (char*)malloc(mlen + 1);
        if(!buf){ errno = ENOMEM; return -1; }
        if(read_exact(g_remote_fd, buf, mlen) <= 0){ free(buf); return -1; }
        buf[mlen] = '\0';
        if(out) fwrite(buf, 1, mlen, out);
        free(buf);
    }

    /* 4) leer "STATUS <code>\n".
       Nota: algunos servidores envían un salto de línea entre OUT y STATUS. */
    char l2[64];
    if(read_line(g_remote_fd, l2, sizeof(l2)) <= 0) return -1;
    if(l2[0] == '\0'){                  /* línea en blanco opcional */
        if(read_line(g_remote_fd, l2, sizeof(l2)) <= 0) return -1;
    }

    int status = 0;
    if(sscanf(l2, "STATUS %d", &status) != 1){ errno = EPROTO; return -1; }
    return status;
}

void remote_disconnect(void){
    if(g_remote_fd >= 0){
        /* cierre amable; si falla no pasa nada */
        write_all(g_remote_fd, "QUIT\n", 5);
        close(g_remote_fd);
        g_remote_fd = -1;
        g_remote_ip[0] = '\0';
    }
}

