/*
 * Proyecto: uamashell - Simulador de una herramienta de administración de UNIX
 * Equipo: Kernel Force
 * Autores:
 *   - Enrique Hernández Mauricio — 2223030397
 *   - Garrido Velázquez Iván — 2203025425
 *   - Loaeza Sánchez Wendy Maritza — 2193042056
 *   - Robles Pérez Luis Fernando — 2203031441
 *
 * Descripción:
 *   log_command() y log_error(): envían registros a uamashell.log y a uamashell_error.log con timestamp, PID, usuario, etc.
 */


#include "common.h"
// Abrir (o crear) archivo de log en modo append

static FILE* open_log(const char *path){
    ensure_dirs(g_cfg.log_dir);
    FILE *f = fopen(path, "a");
    return f;
}
// Construir rutas completas para logs de comandos y errores

void resolve_paths(char *cmd_log, size_t cmd_sz, char *err_log, size_t err_sz){
    snprintf(cmd_log, cmd_sz, "%s/%s", g_cfg.log_dir, CMD_LOG_NAME);
    snprintf(err_log, err_sz, "%s/%s", g_cfg.log_dir, ERROR_LOG_NAME);
}
// Formatear timestamp actual "YYYY-MM-DD HH:MM:SS"
static void timestamp(char *buf, size_t n){
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

// Obtener contexto de usuario: nombre, tty e IP (si SSH)
void get_user_context(char *user, size_t u, char *tty, size_t t, char *ip, size_t i){
    const char *uenv = getenv("LOGNAME"); if(!uenv) uenv = getenv("USER");
    if(!uenv){ struct passwd *pw = getpwuid(getuid()); if(pw) uenv = pw->pw_name; }
    snprintf(user, u, "%s", uenv ? uenv : "unknown");
    // Dispositivo de entrada (tty)

    char *ttyname_ptr = ttyname(STDIN_FILENO);
    snprintf(tty, t, "%s", ttyname_ptr ? ttyname_ptr : "n/a");
    //ip
    const char *ssh = getenv("SSH_CLIENT");
    if(ssh){
        // formato: "IP_CLIENTE PUERTO_CLIENTE PUERTO_SERVIDOR"
        char ipbuf[64]={0};
        sscanf(ssh, "%63s", ipbuf);
        snprintf(ip, i, "%s", ipbuf);
    } else {
        snprintf(ip, i, "n/a");
    }
}
// Función común que escribe la línea de log y cierra el archivo

static void vlog_common(FILE *f, const char *kind, const char *fmt, va_list ap){
    if(!f) return;
    char ts[32]; timestamp(ts, sizeof(ts));
    char user[64], tty[64], ip[64];
    get_user_context(user, sizeof(user), tty, sizeof(tty), ip, sizeof(ip));
    fprintf(f, "[%s] %s pid=%d user=%s tty=%s ip=%s :: ", ts, kind, getpid(), user, tty, ip);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fclose(f);
}
// Registrar comando exitoso
void log_command(const char *fmt, ...){
    char cmd_path[PATH_MAX], err_path[PATH_MAX];
    resolve_paths(cmd_path, sizeof(cmd_path), err_path, sizeof(err_path));
    FILE *f = open_log(cmd_path);
    va_list ap; va_start(ap, fmt);
    vlog_common(f, "CMD", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...){
    char cmd_path[PATH_MAX], err_path[PATH_MAX];
    resolve_paths(cmd_path, sizeof(cmd_path), err_path, sizeof(err_path));
    FILE *f = open_log(err_path);
    va_list ap; va_start(ap, fmt);
    vlog_common(f, "ERR", fmt, ap);
    va_end(ap);
}
