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
 *   – Implementa load_config() (parsing de clave=valor) y set_config_key() (reescritura del .conf).
 */

#include "common.h"

Config g_cfg;
// ** Auxiliar ** parsear entero de cadena o devolver valor por defecto
static int parse_int(const char *s, int defv) {
    if (!s) return defv;
    char *end=NULL;
    long v = strtol(s, &end, 10);
    if (end==s || *end!='\0') return defv;
    if (v<0) v=0;
    return (int)v;
}
/*
void trim(char *s){
    if(!s) return;
    char *p=s, *q=s;
    while(*q && isspace((unsigned char)*q)) q++;
    while(*q) *p++ = *q++;
    *p='\0';
    // rtrim
    int n=strlen(s);
    while(n>0 && isspace((unsigned char)s[n-1])) s[--n]='\0';
}*/

// Crear directorio si no existe (para log_dir, etc.)
void ensure_dirs(const char *path){
    if(!path || !*path) return;
    struct stat st;
    if (stat(path, &st)==-1){
        mkdir(path, 0775);
    }
}


/**
 * Carga archivo de configuración de formato clave=valor.
 * Rellena `out` con valores por defecto + overrides del archivo.
 * @return 0 en éxito (incluso si no existía el archivo), -1 si `out` es NULL.
 */
int load_config(const char *path, Config *out) {
    if (!out) return -1;

    /* valores por defecto */
    snprintf(out->conf_path,   sizeof(out->conf_path),   "%s", path);
    snprintf(out->program_name,sizeof(out->program_name),"%s", PROGRAM_NAME);
    out->max_instances = 3;
    snprintf(out->log_dir,     sizeof(out->log_dir),     "%s", DEFAULT_LOG_DIR);
    snprintf(out->lock_dir, sizeof(out->lock_dir), "%s", "var/lock");
    out->remote_port = DEFAULT_REMOTE_PORT;
    out->remote_allowed[0] = '\0';   /* vacío = nadie autorizado */

    FILE *f = fopen(path, "r");
    if (!f) {
        /* si no existe, quedamos con defaults */
        return 0;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* ignorar comentarios o líneas muy cortas */
        if (line[0] == '#' || strlen(line) < 3) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = line;
        char *val = eq + 1;

        /* quita espacios y saltos */
        trim(key);
        trim(val);

        /* claves conocidas */
        if (strcmp(key, "MAX_INSTANCES") == 0) {
            out->max_instances = parse_int(val, out->max_instances);
        } else if (strcmp(key, "LOG_DIR") == 0) {
            snprintf(out->log_dir, sizeof(out->log_dir), "%s", val);
        } else if (strcmp(key, "PROGRAM_NAME") == 0) {
            snprintf(out->program_name, sizeof(out->program_name), "%s", val);
        } else if (strcmp(key, "LOCK_DIR") == 0) {
            snprintf(out->lock_dir, sizeof(out->lock_dir), "%s", val);
        } else if (strcmp(key, "REMOTE_PORT") == 0) {
            out->remote_port = parse_int(val, out->remote_port);
	} else if (strcmp(key, "REMOTE_ALLOWED") == 0) {
   	    strncpy(out->remote_allowed, val, sizeof(out->remote_allowed)-1);
	    out->remote_allowed[sizeof(out->remote_allowed)-1] = '\0';
	}
    }

    fclose(f);
    return 0;
}

/**
 * Modifica o añade una clave=valor en el archivo de configuración.
 * @return 0 en éxito, -1 en error de escritura/lectura.
 */

int set_config_key(const char *path, const char *key, const char *value){
    if(!path) path = DEFAULT_CONF;
    // cargar todo y reescribir
    FILE *f = fopen(path, "r");
    bool found=false;
    char buf[8192] = {0};
    if(f){
        char line[1024];
        while(fgets(line, sizeof(line), f)){
            char work[1024]; snprintf(work, sizeof(work), "%s", line);
            char *eq = strchr(work,'=');
            if(eq){
                *eq='\0';
                trim(work);
                if(strcmp(work, key)==0){
                    found=true;
                    snprintf(line, sizeof(line), "%s=%s\n", key, value);
                }
            }
            strncat(buf, line, sizeof(buf)-strlen(buf)-1);
        }
        fclose(f);
    }
    if(!found){
        char nl[256]; snprintf(nl, sizeof(nl), "%s=%s\n", key, value);
        strncat(buf, nl, sizeof(buf)-strlen(buf)-1);
    }
    ensure_dirs("etc");
    FILE *w = fopen(path, "w");
    if(!w) return -1;
    fwrite(buf, 1, strlen(buf), w);
    fclose(w);
    return 0;
}
