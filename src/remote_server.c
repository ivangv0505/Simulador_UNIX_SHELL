/*
 * Proyecto: uamashell
 * Módulo: remote_server.c — Servidor para ejecución remota (Versión III)
 * Ejecuta: ./uamashell --server
 * Lee REMOTE_PORT y REMOTE_ALLOWED de g_cfg.
*Equipo: Kernel Force 
*Autores:
*  - Enrique Hernández Mauricio – 2223030397 
*  - Garrido Velázquez Iván – 2203025425
*  - Loaeza Sánchez Wendy  Maritza – 2193042056 
*  - Robles Pérez Luis Fernando – 2203031441 
	
*Grito de batalla: "¡Kernel Force, control total, sistema listo para el rival!"*/
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#ifndef MAX_IP_STR
#define MAX_IP_STR 64
#endif
#ifndef DEFAULT_REMOTE_PORT
#define DEFAULT_REMOTE_PORT 5050
#endif

static int srv_fd = -1;

static void on_sigint(int s){
    (void)s;
    if(srv_fd>=0) close(srv_fd);
    srv_fd = -1;
}

/* --- helpers parse/IO --- */

static int ip_in_allowed(const char *ip)
{
    /* REMOTE_ALLOWED: CSV de IPs exactas (simplificado) */
    if(!g_cfg.remote_allowed[0]) return 0; /* nadie permitido */
    char work[1024]; strncpy(work, g_cfg.remote_allowed, sizeof(work)-1); work[sizeof(work)-1]='\0';
    char *tok = strtok(work, ",");
    while(tok){
        while(*tok==' '||*tok=='\t') ++tok;
        const char *end = tok + strlen(tok);
        while(end>tok && (end[-1]==' '||end[-1]=='\t'||end[-1]=='\n'||end[-1]=='\r')) --end;
        char saved = *end; *((char*)end) = '\0';
        if(strcmp(tok, ip)==0){ *((char*)end)=saved; return 1; }
        *((char*)end)=saved;
        tok = strtok(NULL, ",");
    }
    return 0;
}

static ssize_t write_all(int fd, const void *buf, size_t n){
    const char *p = (const char*)buf; size_t left = n;
    while(left){
        ssize_t w = send(fd, p, left, 0);
        if(w<0){ if(errno==EINTR) continue; return -1; }
        p += w; left -= (size_t)w;
    }
    return (ssize_t)n;
}

static int read_line(int fd, char *out, size_t max){
    size_t i = 0;
    while (i + 1 < max) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) {
            out[i] = '\0';
            return 0;             /* conexión cerrada */
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') {
            out[i] = '\0';
            return 1;
        }
        out[i++] = c;
    }
    out[i] = '\0';
    return 1;
}


static int read_exact(int fd, void *buf, size_t n){
    char *p = (char*)buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r == 0) {
            return 0;             /* conexión cerrada */
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return 1;
}

extern int shell_execute_line(const char *line, FILE *out);

static void handle_client(int cfd, const char *peer_ip)
{
    char line[256];

    /* HELLO */
    if(read_line(cfd, line, sizeof(line))<=0){ close(cfd); return; }
    if(strncmp(line, "HELLO ", 6)!=0){ close(cfd); return; }
    if(!ip_in_allowed(peer_ip)){
        log_error("REMOTO: intento NO AUTORIZADO desde ip=%s ; hello=%s", peer_ip, line);
        printf("[server] NO AUTORIZADO: %s\n", peer_ip); fflush(stdout);

        write_all(cfd, "ERR NOT_ALLOWED\n", 16);
        close(cfd);
        return;
    } else{
        printf("[server] Conexion de %s\n", peer_ip); fflush(stdout);
    }

    log_command("REMOTO: conexion aceptada desde ip=%s ; %s", peer_ip, line);
    write_all(cfd, "OK\n", 3);

    for(;;){
        if(read_line(cfd, line, sizeof(line))<=0) break;
        if(strcmp(line, "QUIT")==0){
            log_command("REMOTO: desconexion desde ip=%s", peer_ip);
            printf("[server] Desconexion (socket cerrado) de %s\n", peer_ip);
            break;
        }
        size_t n=0;
        if(sscanf(line, "CMD %zu", &n)!=1){
            log_error("REMOTO: protocolo invalido desde %s: %s", peer_ip, line);
            break;
        }

        char *payload = (char*)malloc(n+1);
        if(!payload){ log_error("REMOTO: sin memoria para %zu bytes", n); break; }
        if(read_exact(cfd, payload, n)<=0){ free(payload); break; }
        payload[n]='\0';
	log_command("REMOTO: ip=%s cmd='%s'", peer_ip, payload);



        /* Ejecutar y capturar salida usando un pipe */
        int pfd[2]; if(pipe(pfd)<0){ free(payload); break; }
        FILE *out = fdopen(pfd[1], "w");
        int status = shell_execute_line(payload, out);
        fclose(out);

        /* leer salida */
        char buf[4096]; ssize_t r; size_t accsz=0; char *acc=NULL;
        while( (r = read(pfd[0], buf, sizeof(buf))) > 0 ){
            char *tmp = realloc(acc, accsz + (size_t)r);
            if(!tmp){ free(acc); acc=NULL; accsz=0; break; }
            acc = tmp; memcpy(acc+accsz, buf, (size_t)r); accsz += (size_t)r;
        }
        close(pfd[0]);

        /* OUT + STATUS */
        char hdr[64]; int hm = snprintf(hdr, sizeof(hdr), "OUT %zu\n", accsz);
        write_all(cfd, hdr, (size_t)hm);
        if(accsz) write_all(cfd, acc, accsz);
        char st[64]; int sm = snprintf(st, sizeof(st), "\nSTATUS %d\n", status);
        write_all(cfd, st, (size_t)sm);

        free(acc);
        free(payload);
    }

    close(cfd);
}

int run_server(void)
{
    /* señales */
    struct sigaction sa = {0}; sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    /* bind */
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", g_cfg.remote_port>0 ? g_cfg.remote_port : DEFAULT_REMOTE_PORT);

    struct addrinfo hints = {0}, *res=NULL, *it=NULL;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_PASSIVE;

    int err = getaddrinfo(NULL, portstr, &hints, &res);
    if(err){ log_error("REMOTO: getaddrinfo fallo: %s", gai_strerror(err)); return 1; }

    for(it=res; it; it=it->ai_next){
        srv_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if(srv_fd<0) continue;
        int on=1; setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if(bind(srv_fd, it->ai_addr, it->ai_addrlen)==0) break;
        close(srv_fd); srv_fd=-1;
    }
    freeaddrinfo(res);

    if(srv_fd<0){ log_error("REMOTO: no se pudo ligar al puerto %s", portstr); return 1; }
    if(listen(srv_fd, 16)<0){ log_error("REMOTO: listen fallo: %s", strerror(errno)); close(srv_fd); srv_fd=-1; return 1; }

    printf("Servidor remoto escuchando en puerto %s; allowed='%s'\n",
       		portstr, g_cfg.remote_allowed);
    fflush(stdout);

    log_command("REMOTO: servidor escuchando en puerto %s ; allowed='%s'", portstr, g_cfg.remote_allowed);

    while(srv_fd>=0){
        struct sockaddr_storage ss; socklen_t slen = sizeof(ss);
        int cfd = accept(srv_fd, (struct sockaddr*)&ss, &slen);
        if(cfd<0){ if(errno==EINTR) continue; log_error("REMOTO: accept fallo: %s", strerror(errno)); break; }

        char ipstr[MAX_IP_STR]="?";
        if(ss.ss_family==AF_INET){
            struct sockaddr_in *sin = (struct sockaddr_in*)&ss;
            inet_ntop(AF_INET, &sin->sin_addr, ipstr, sizeof(ipstr));
        } else if(ss.ss_family==AF_INET6){
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)&ss;
            inet_ntop(AF_INET6, &sin6->sin6_addr, ipstr, sizeof(ipstr));
        }
        handle_client(cfd, ipstr);
    }

    if(srv_fd>=0){ close(srv_fd); srv_fd=-1; }
    return 0;
}

