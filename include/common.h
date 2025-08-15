/*
 * Proyecto: uamashell - Simulador de una herramienta de administración de UNIX
 * Equipo: Kernel Force
 * Autores:
 *  - Enrique Hernández Mauricio – 2223030397
 *  - Garrido Velázquez Iván – 2203025425
 *  - Loaeza Sánchez Wendy Maritza – 2193042056
 *  - Robles Pérez Luis Fernando – 2203031441
 *
 * Descripción:
 *  Cabecera común con estructuras, constantes y utilidades.
 *
 * Compilación:"Ver makefile
 */
#ifndef UAMASHELL_COMMON_H
#define UAMASHELL_COMMON_H

#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE



#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <signal.h>
#include <pwd.h>
#include <limits.h>
#include <curses.h>

/* ===== Config ===== */
typedef struct {
    char conf_path[PATH_MAX];
    char program_name[64];
    int  max_instances;
    char log_dir[PATH_MAX];
    char lock_dir[PATH_MAX];      /* NUEVO: directorio de locks por archivo */
    int  remote_port;              // puerto del servidor remoto
    char remote_allowed[1024];// CSV de IPs permitidas (lado servidor)
} Config;

#define PROGRAM_NAME     "uamashell"
#define DEFAULT_CONF     "./etc/uamashell.conf"
#define DEFAULT_LOG_DIR  "var/log"
#define ERROR_LOG_NAME   PROGRAM_NAME "_error.log"
#define CMD_LOG_NAME     PROGRAM_NAME ".log"
#define FTOK_PATH        "/tmp/uamashell_ftok"
#define FTOK_PROJ_ID     'K'

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef NOTIF_MAX
#define NOTIF_MAX 64
#endif

typedef struct {
    pid_t to_pid;         /* 0 = broadcast o PID destino */
    char  text[256];      /* mensaje a mostrar */
} Notification;

// ---------------- Memoria Compartida ----------------
#define MAX_PIDS 256
typedef struct {
    int instance_count;
    pid_t pids[MAX_PIDS];
    Notification notif[NOTIF_MAX];
    int notif_count;
} SharedState;

// IDs globales del IPC (sólo visibles en cada .c)
extern int g_sem_id;
extern int g_shm_id;
extern SharedState *g_shared;
extern Config g_cfg;


#ifndef DEFAULT_LOCK_DIR
#define DEFAULT_LOCK_DIR "var/locks"
#endif


/* === Versión III (ejecución remota) =============================== */
#ifndef UAMASHELL_REMOTE_ADDON
#define UAMASHELL_REMOTE_ADDON

#ifndef DEFAULT_REMOTE_PORT
#define DEFAULT_REMOTE_PORT 5050
#endif
#ifndef MAX_IP_STR
#define MAX_IP_STR 64
#endif




// ---------------- Prototipos ----------------
// config.c
int load_config(const char *path, Config *out);
int set_config_key(const char *path, const char *key, const char *value);
void ensure_dirs(const char *path);

// logging.c
void log_command(const char *fmt, ...);
void log_error(const char *fmt, ...);
void resolve_paths(char *cmd_log, size_t, char *err_log, size_t);
void get_user_context(char *user, size_t, char *tty, size_t, char *ip, size_t);

// instance.c
int ipc_init(void);
int instance_try_enter(void);
void instance_leave(void);
void ipc_force_cleanup(void);
/* notificaciones (definidas en instance.c) */
int  notif_push(pid_t to_pid, const char *msg);
void notif_drain_for(pid_t pid);

// pager.c
int curses_pager(const char *filepath, const char *title);

// utils
static inline void trim(char *s)
{
    /* elimina espacios en ambos extremos + '\r' + '\n' */
    char *p = s;
    while(*p && strchr(" \t\r\n", *p)) ++p;   /* adelante */
    memmove(s, p, strlen(p) + 1);

    size_t n = strlen(s);
    while(n && strchr(" \t\r\n", s[n-1])) s[--n] = '\0';
}

/* Servidor */
int  run_server(void);

/* Cliente */
int  remote_connect(const char *ip, int port);
int  remote_send_line(const char *line, FILE *out);
void remote_disconnect(void);
int  remote_is_active(void);
const char* remote_current_ip(void);


#endif /* UAMASHELL_REMOTE_ADDON */   // ← añade esta línea

#endif /* UAMASHELL_COMMON_H */
