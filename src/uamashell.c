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
 *   El main(): maneja señales, carga config, detecta modo (plain vs. ncurses), y dispatch de comandos internos (ayuda, showconf, setconf, etc) o
 *   externos.
 */

#include "common.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>
#include <ncurses.h>
extern Config g_cfg; //configuracion global
static volatile sig_atomic_t g_running = 1; //bandera de ejecucion

static void cmd_notificaciones(void);
static void cmd_dueno(const char *arg);



/* --- tipos y prototipos para que no haya declaraciones implícitas --- */
typedef struct {
    int  fd;
    char path[PATH_MAX];
} LockInfo;

static void sanitize(const char *in, char *out, size_t n);
static char *extract_target(const char *cmd);
static int  acquire_file_lock(const char *target_path, const char *cmd, LockInfo *info);
static void release_file_lock(LockInfo *info);
static void run_external(const char *cmd);


/* Manejador SIGINT/SIGTERM */
static void on_signal(int sig) {
    // Registrar y detener bucles
    log_error("Señal recibida %d, liberando recursos", sig);
    g_running = 0;
}

/* Ayuda en modo texto */
static void help_plain(void) {
    puts("Comandos internos:");
    puts("  ayuda               - Muestra esta ayuda");
    puts("  terminar            - Termina la ejecución");
    puts("  bitacora_comandos   - Muestra bitácora de comandos");
    puts("  bitacora_error      - Muestra bitácora de errores");
    puts("  showconf            - Muestra configuración");
    puts("  setconf k=v         - Modifica configuración");
    puts("  cd <ruta>           - Cambia de directorio");
    puts("  notificaciones       - Muestra avisos pendientes por conflictos");
    puts("  dueno <archivo>      - Muestra quién tiene el lock de <archivo>");
    puts("  IP <direccion>      - Conecta a servidor remoto");
    puts("  desconectar         - Termina la sesión remota");
    puts("Cualquier otro texto → /bin/sh -c …");
}

/* Mostrar bitácoras */
static void show_log_plain(bool err) {
    char cmdp[PATH_MAX], errp[PATH_MAX];
    resolve_paths(cmdp,sizeof cmdp,errp,sizeof errp);
    const char *f = err ? errp : cmdp;
    FILE *fp = fopen(f,"r");
    if (!fp) { printf("No se pudo abrir %s\n", f); return; }
    char ln[1024];
    while (fgets(ln,sizeof ln,fp)) fputs(ln,stdout);
    fclose(fp);
}

/* Ejecutar comando */
static void exec_plain(const char *cmd) {
    log_command("exec: %s", cmd);
    FILE *p = popen(cmd,"r");
    if (!p) { log_error("popen: %s", strerror(errno)); return; }
    char ln[1024];
    while (fgets(ln,sizeof ln,p)) fputs(ln,stdout);
    int st = pclose(p);
    if (st==-1) log_error("pclose: %s", strerror(errno));
    else if (WIFEXITED(st) && WEXITSTATUS(st))
        log_error("Comando terminó %d: %s", WEXITSTATUS(st), cmd);
}

/* Bucle texto puro*/


static void loop_plain(void) {
    char buf[1024];
    ssize_t n;
    fprintf(stderr, "→ [loop_plain] conf_path='%s'\n", g_cfg.conf_path);
    while (g_running) {
        
        static int first = 1;
        if (first) {
            puts("\n===== UAMASHELL KERNEL FORCE ACTIVO (modo texto) =====");
            first = 0;
        }

        /* prompt */
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) {
            perror("getcwd");
            strcpy(cwd, "?");
        }
        printf("\n[%s] %s\n> ", g_cfg.program_name, cwd);
        fflush(stdout);

        /* 1) leer carácter a carácter hasta \n */
        int pos = 0;
        for (;;) {
	notif_drain_for(getpid());

            unsigned char c;
            n = read(STDIN_FILENO, &c, 1);
            if (n <= 0) goto salir;           /* EOF o error */
            if (c == '\r' || c == '\n') break;
            if (pos < (int)sizeof(buf) - 1) buf[pos++] = c;
        }
        buf[pos] = '\0';

        /* 2) comandos internos */
        if (strcmp(buf, "terminar") == 0) {
            break;
        }
        else if (strcmp(buf, "ayuda") == 0) {
            help_plain();
            continue;
        }
        else if (strcmp(buf, "showconf") == 0) {
	   printf("PROGRAM_NAME=%s\n"
           "MAX_INSTANCES=%d\n"
           "LOG_DIR=%s\n"
           "LOCK_DIR=%s\n",
            g_cfg.program_name,
            g_cfg.max_instances,
            g_cfg.log_dir,
            g_cfg.lock_dir[0] ? g_cfg.lock_dir : "(no configurado)");
            continue;
        }
        else if (strcmp(buf, "bitacora_comandos") == 0) {
            show_log_plain(false);
            continue;
        }
        else if (strcmp(buf, "bitacora_error") == 0) {
            show_log_plain(true);
            continue;
        }
        else if (strncmp(buf, "cd ", 3) == 0) {
            char *d = buf + 3;
            trim(d);
            if (chdir(d) < 0)
                log_error("cd %s: %s", d, strerror(errno));
            else
                log_command("cd %s", d);
            continue;
        }
        else if (strncmp(buf, "setconf ", 8) == 0) {
            char *kv = buf + 8;
            char *eq = strrchr(kv, '=');
            if (!eq) {
                puts("Uso: setconf clave=valor");
                continue;
            }
            *eq = '\0';
            char *k = kv;
            char *v = eq + 1;
            trim(k); trim(v);
	    fprintf(stderr,"DEBUG setconf: archivo=%s, clave=%s, valor=%s\n",
            g_cfg.conf_path, k, v);
            if (set_config_key(g_cfg.conf_path, k, v) != 0) {
                perror("set_config_key");
		puts("No se pudo modificar config");
            } else {
                load_config(g_cfg.conf_path, &g_cfg);
                log_command("setconf %s=%s", k, v);
            }
            continue;
        } else if (strcmp(buf, "notificaciones") == 0) {
    	cmd_notificaciones();
    	continue;
	} else if (strncmp(buf, "dueno ", 6) == 0) {
    	cmd_dueno(buf + 6);
    	continue;
	} else if (strcmp(buf, "dueno") == 0) {
        cmd_dueno(NULL);         // sin argumento -> imprime "Uso: ..."
        continue;
	}else if (strncmp(buf, "IP ", 3) == 0) {
    		char ip[128] = {0};
    		sscanf(buf+3, "%127s", ip);
    		if (ip[0] == '\0') {
       		 	printf("Uso: IP <direccion>\n");
    		} else if (remote_is_active()) {
        	printf("Ya hay una sesión remota activa con %s. Usa 'desconectar' primero.\n", remote_current_ip());
    		}else {
        		int port = g_cfg.remote_port > 0 ? g_cfg.remote_port : DEFAULT_REMOTE_PORT;
        		if (remote_connect(ip, port) == 0) {
           	 		log_command("Remoto: conectado a %s:%d", ip, port);
            			printf("Conectado a %s:%d\n", ip, port);
        		} else {
            			log_error("Remoto: fallo de conexion a %s:%d (%s)", ip, port, strerror(errno));
            			printf("Conexion fallida: %s\n", strerror(errno));
        		}
    		}
    		continue;	
	} else if (strcmp(buf, "desconectar") == 0) {
    		if (!remote_is_active()) {
       			 printf("No hay sesion remota activa.\n");
    		} else {
        		 log_command("Remoto: desconectando de %s", remote_current_ip());
        		 remote_disconnect();
        		  printf("Sesion remota cerrada. De vuelta a local.\n");
    		}
    		continue;
	}else if (buf[0] == '\0') {
   	 /* línea vacía: solo repinta prompt */
    	continue;
	} else {
    	/* ← cualquier otro texto: comando externo */
    	run_external(buf);
    	continue;
	}

        /* 3) ningún interno: lo envío al shell */
        exec_plain(buf);
    }

salir:
    ;  /* etiqueta para EOF o error de lectura */
}

static void sanitize(const char *in, char *out, size_t n) {
    size_t i = 0;
    while (*in && i + 1 < n) {
        out[i++] = (*in == '/') ? '_' : *in;
        in++;
    }
    out[i] = '\0';
}

/* Regla mínima: tomamos el PRIMER argumento que exista en disco como “archivo objetivo” */
static char *extract_target(const char *cmd) {
    if (!cmd) return NULL;
    char *copy = strdup(cmd);
    if (!copy) return NULL;

    char *save = NULL;
    char *tok = strtok_r(copy, " \t", &save); /* comando */
    tok = strtok_r(NULL, " \t", &save);       /* primer argumento */

    char *out = NULL;
    if (tok && access(tok, F_OK) == 0) {
        out = strdup(tok);
    }
    free(copy);
    return out;
}

static void run_external(const char *cmd) {
    LockInfo lock;
    memset(&lock, 0, sizeof(lock));
    lock.fd = -1;
    lock.path[0] = '\0';

    char *target = extract_target(cmd);

    if (target) {
        int l = acquire_file_lock(target, cmd, &lock);
        if (l < 0) {
            /* Ya está bloqueado: lee los datos del dueño desde el lockfile y notifica */
            int rfd = open(lock.path, O_RDONLY);
            char buf[512] = {0};
            if (rfd >= 0) {
                read(rfd, buf, sizeof(buf) - 1);
                close(rfd);
            }

            /* Aviso en la instancia que choca */
            fprintf(stderr, "⚠️ Archivo en uso. Detalles del dueño:\n%s\n", buf);

            /* Extraer PID dueño */
            pid_t owner = 0;
            sscanf(buf, "pid=%d", &owner);

            /* Mensaje para el dueño con datos del segundo */
            char me_user[64], me_tty[64], me_ip[64];
            get_user_context(me_user, sizeof(me_user), me_tty, sizeof(me_tty), me_ip, sizeof(me_ip));


	    /* Limitar lo que mostramos como “target” para evitar warnings de truncation */
	    char tshort[128];
	    sanitize(target ? target : "(n/a)", tshort, sizeof(tshort));

	    char msg_owner[512];
	    int off = snprintf(msg_owner, sizeof(msg_owner),
                   "Conflicto sobre '%s': ", tshort);
	    if (off < 0 || off >= (int)sizeof(msg_owner)) off = (int)sizeof(msg_owner) - 1;
	
	    /* Bounded %s para que el compilador conozca el máximo posible */
	    snprintf(msg_owner + off, sizeof(msg_owner) - off,
        	 "competidor pid=%d user=%.32s tty=%.32s ip=%.64s cmd=%.128s",
	         getpid(), me_user, me_tty, me_ip, cmd ? cmd : "(n/a)");


            /* Notificar al dueño (si lo conocemos) y registrar en bitácora de errores */
            if (owner > 0) {
                notif_push(owner, msg_owner);
            }
            log_error("Acceso concurrente a %s :: dueño{%s} :: competidor{pid=%d user=%s tty=%s ip=%s cmd=%s}",
                      target, buf, getpid(), me_user, me_tty, me_ip, cmd ? cmd : "(n/a)");

            free(target);
            return; /* No ejecutamos el comando si el archivo está en uso */
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (status != 0) {
        log_error("Comando terminó con código %d: %s", WEXITSTATUS(status), cmd);
    }
    log_command("Comando terminó con código %d: %s", WEXITSTATUS(status), cmd);

    if (lock.fd >= 0) {
        release_file_lock(&lock);
    }
    free(target);
}


/* Crea lockfile en g_cfg.lock_dir y toma fcntl(F_WRLCK) no bloqueante */
static int acquire_file_lock(const char *target_path, const char *cmd, LockInfo *info) {
    if (!target_path || !info) return -1;

    ensure_dirs(g_cfg.lock_dir);

    char name[PATH_MAX];
    sanitize(target_path, name, sizeof(name));
    int nn = snprintf(info->path, sizeof(info->path), "%s/%s.lock", g_cfg.lock_dir, name);
     if (nn < 0 || nn >= (int)sizeof(info->path)) {
    	errno = ENAMETOOLONG;
    	return -1;
      }


    int fd = open(info->path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return -1;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = getpid();

    if (fcntl(fd, F_SETLK, &fl) == -1) {
        close(fd);
        return -2; /* ya bloqueado por otra instancia */
    }

    /* Guardar contexto del dueño dentro del lockfile */
    char user[64], tty[64], ip[64];
    get_user_context(user, sizeof(user), tty, sizeof(tty), ip, sizeof(ip));
    ftruncate(fd, 0);
    dprintf(fd,
            "pid=%d\nuser=%s\ntty=%s\nip=%s\ncmd=%s\nfile=%s\n",
            getpid(), user, tty, ip, cmd ? cmd : "(n/a)", target_path);

    info->fd = fd;
    return fd;
}

static void release_file_lock(LockInfo *info) {
    if (!info) return;
    if (info->fd >= 0) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_UNLCK;
        fl.l_whence = SEEK_SET;

        fcntl(info->fd, F_SETLK, &fl);
        close(info->fd);
        unlink(info->path);  /* lockfile ya no necesario */
        info->fd = -1;
    }
}

/* Muestra y vacía notificaciones para esta instancia */
static void cmd_notificaciones(void) {
    printf("(notificaciones)\n");
    /* Reutiliza el drenado que ya tienes */
    notif_drain_for(getpid());
}

/* Muestra el dueño (si lo hay) del lock de un archivo */
static void cmd_dueno(const char *arg) {
    if (!arg || !*arg) {
        puts("Uso: dueno <archivo>");
        return;
    }

    char name[PATH_MAX];
    char path[PATH_MAX];
    sanitize(arg, name, sizeof(name));
    if (snprintf(path, sizeof(path), "%s/%s.lock", g_cfg.lock_dir, name) >= (int)sizeof(path)) {
        puts("Ruta de lock demasiado larga.");
        return;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("Archivo libre (no hay lock): %s\n", arg);
        return;
    }

    char buf[512] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        printf("Lock encontrado pero vacío: %s\n", path);
        return;
    }

    /* Esperamos líneas con pid=, user=, tty=, ip=, cmd= que nosotros mismos escribimos al crear el lock */
    printf("Dueño de '%s':\n%s\n", arg, buf);
}

/*
 * Ejecuta una línea exactamente con el mismo motor que se usa en el loop
 * interactivo (internos, locks, externos) pero escribiendo en 'out'.
 *
 * Adapta el cuerpo para llamar a tus funciones reales de dispatch;
 * un esquema típico:
 *  - if (strcmp(cmd, "ayuda")==0) { print_help(out); return 0; }
 *  - else if (...) ...
 *  - else { status = run_external(cmd, out); }
 */
int shell_execute_line(const char *line, FILE *out)
{
    int saved = dup(STDOUT_FILENO);
    fflush(stdout);
    dup2(fileno(out), STDOUT_FILENO);

    /* usa tu propio pipeline */
    extern int process_one_line(const char *); 
    int status = process_one_line(line ? line : "");

    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    return status;
}



int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* 1) convertir al proceso en líder y ponerlo en primer plano */
    pid_t self = getpid();
    if (setpgid(0, self) == -1 && errno != EEXIST) perror("setpgid");
    if (tcsetpgrp(STDIN_FILENO, self) == -1) perror("tcsetpgrp");

    /* 2) diagnóstico inicial */
    fprintf(stderr, "A) entro a main()\n");
    fprintf(stderr, "*** UAMASHELL ha arrancado (diagnóstico) ***\n");
    fflush(stderr);

    /* 3) desactivar buffering para evitar pantallas vacías */
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    /* 4) ignorar SIGHUP/SIGTSTP accidentales */
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    /* 5) capturar SIGINT/SIGTERM para g_running=0 */
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* 6) carga de configuración */
    if (load_config(DEFAULT_CONF, &g_cfg) != 0) {
        fprintf(stderr, "error al cargar config\n");
        return 1;
    }

    ensure_dirs(g_cfg.lock_dir);

/* --- Versión III: modo servidor remoto --- */
    if (load_config(DEFAULT_CONF, &g_cfg) != 0) {
    }

    if (argc > 1 && strcmp(argv[1], "--server") == 0) {
    	 return run_server();
    }





    char realp[PATH_MAX];
    if (!realpath(g_cfg.conf_path, realp)) {
        perror("realpath conf_path");
    } else {
        fprintf(stderr,"Archivo de config real: %s\n", realp);
    }

    fprintf(stderr,
        "B) config cargada: program_name=\"%s\", max_instances=%d, log_dir=\"%s\"\n",
       g_cfg.program_name, g_cfg.max_instances, g_cfg.log_dir);

    /* 7) comprobar modo texto forzado */
    const char *plain = getenv("UAMASHELL_PLAIN");
    fprintf(stderr, "C) getenv(\"UAMASHELL_PLAIN\") = %s\n",
            plain ? plain : "(null)");
    if (plain && strcmp(plain, "1") == 0) {
	fprintf(stderr, "D) entro al modo PLAIN\n");
        loop_plain();    /* tu bucle de texto puro */

        instance_leave();
        return 0;
    }

    /* Intento ncurses */
    if (!initscr()) {
        loop_plain();
        instance_leave();
        return 0;
    }
    cbreak(); noecho(); keypad(stdscr,TRUE); curs_set(0);
    clear();
    mvprintw(0,0,
        "Interfaz ncurses no lista.\n"
        "Usa 't' o 'T' para texto, 'terminar' o Ctrl-C para salir.\n");
    refresh();
    
    while (g_running) {
	fprintf(stderr, "→ loop_plain: conf_path='%s'\n", g_cfg.conf_path);
        int ch=getch();
        if (ch=='t'||ch=='T') break;
    }
    endwin();
    instance_leave();
    return 0;
}

