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
 *   Controla el semáforo + shared memory para limitar a MAX_INSTANCES instancias simultáneas..
 */


#include "common.h"

int g_sem_id = -1; //id del semaforo
int g_shm_id = -1; //id de la memoria compartida
SharedState *g_shared = NULL; // puntero al estado compartido

static int sem_lock(int semid){
    struct sembuf sb = {0, -1, SEM_UNDO};
    return semop(semid, &sb, 1);
}
static int sem_unlock(int semid){
    struct sembuf sb = {0, +1, SEM_UNDO};
    return semop(semid, &sb, 1);
}



/**
 * Inicializa semáforo y shared memory.
 * @return 0 en éxito, -1 en error.
 */

int ipc_init(void){
    // ftok path ensure
    FILE *f = fopen(FTOK_PATH, "a"); if(f) fclose(f);
    key_t key = ftok(FTOK_PATH, FTOK_PROJ_ID);
    if(key == (key_t)-1){ perror("ftok"); return -1; }

    g_sem_id = semget(key, 1, IPC_CREAT | 0666);
    if(g_sem_id==-1){ perror("semget"); return -1; }
    // inicializar semáforo si aún no
    unsigned short arr[1];
    union semun { int val; struct semid_ds *buf; unsigned short *array; } arg;
    if(semctl(g_sem_id, 0, GETALL, arr)==-1){
        // try to init
        arg.val = 1;
        if(semctl(g_sem_id, 0, SETVAL, arg)==-1){
            perror("semctl SETVAL"); return -1;
        }
    }

    g_shm_id = shmget(key, sizeof(SharedState), IPC_CREAT | 0666);
    if(g_shm_id==-1){ perror("shmget"); return -1; }
    g_shared = (SharedState*)shmat(g_shm_id, NULL, 0);
    if(g_shared==(void*)-1){ perror("shmat"); g_shared=NULL; return -1; }

    // primera vez: asegurar consistencia
    sem_lock(g_sem_id);
    if(g_shared->instance_count < 0 || g_shared->instance_count > MAX_PIDS){
        g_shared->instance_count = 0;
        memset(g_shared->pids, 0, sizeof(g_shared->pids));
    }
    if (g_shared->notif_count < 0 || g_shared->notif_count > NOTIF_MAX) {
    	g_shared->notif_count = 0;
    }
    sem_unlock(g_sem_id);
    return 0;
}

/**
 * Intentar añadir la instancia actual.
 * @return 0 si hay espacio, 1 si excede el máximo, -1 en error.
 */


int instance_try_enter(void){
    if(ipc_init()!=0) return -1;
    int ok = 0;
    sem_lock(g_sem_id);
    // retirar pids zombies
    for(int i=0;i<g_shared->instance_count && i<MAX_PIDS;i++){
        if(g_shared->pids[i]!=0 && kill(g_shared->pids[i], 0)==-1 && errno==ESRCH){
            // limpiar
            g_shared->pids[i] = 0;
        }
    }
    // compactar
    int j=0;
    for(int i=0;i<MAX_PIDS;i++){
        if(g_shared->pids[i]) g_shared->pids[j++] = g_shared->pids[i];
    }
    g_shared->instance_count = j;

    if(g_shared->instance_count < g_cfg.max_instances){
        // entrar
        if(g_shared->instance_count < MAX_PIDS){
            g_shared->pids[g_shared->instance_count++] = getpid();
            ok = 1;
        } else {
            ok = 0;
        }
    } else {
        ok = 0;
    }
    sem_unlock(g_sem_id);
    return ok ? 0 : 1; // 0 ok, 1 excedido
}

void instance_leave(void){
    if(g_shared==NULL || g_sem_id==-1) return;
    sem_lock(g_sem_id);
    // eliminar PID
    int j=0;
    for(int i=0;i<g_shared->instance_count;i++){
        if(g_shared->pids[i]!=getpid())
            g_shared->pids[j++]=g_shared->pids[i];
    }
    g_shared->instance_count = j;
    sem_unlock(g_sem_id);
    // detach
    shmdt(g_shared);
    g_shared=NULL;
}
/** Forzar limpieza de semáforo y shared memory (rmid) */
void ipc_force_cleanup(void){
    if(g_shared){
        shmdt(g_shared);
        g_shared=NULL;
    }
    if(g_shm_id!=-1){
        shmctl(g_shm_id, IPC_RMID, NULL);
        g_shm_id=-1;
    }
    if(g_sem_id!=-1){
        semctl(g_sem_id, 0, IPC_RMID);
        g_sem_id=-1;
    }
}


int notif_push(pid_t to_pid, const char *msg) {
    if (!g_shared) return -1;
    if (!msg) return -1;

    struct sembuf sb = {0, -1, SEM_UNDO};
    semop(g_sem_id, &sb, 1);

    if (g_shared->notif_count >= NOTIF_MAX) {
        memmove(&g_shared->notif[0], &g_shared->notif[1],
                (NOTIF_MAX - 1) * sizeof(Notification));
        g_shared->notif_count = NOTIF_MAX - 1;
    }

    Notification *n = &g_shared->notif[g_shared->notif_count++];
    n->to_pid = to_pid;
    snprintf(n->text, sizeof(n->text), "%s", msg);

    sb.sem_op = +1;
    semop(g_sem_id, &sb, 1);
    return 0;
}

void notif_drain_for(pid_t pid) {
    if (!g_shared) return;

    struct sembuf sb = {0, -1, SEM_UNDO};
    semop(g_sem_id, &sb, 1);

    int i = 0;
    while (i < g_shared->notif_count) {
        Notification *n = &g_shared->notif[i];
        if (n->to_pid == 0 || n->to_pid == pid) {
            fprintf(stderr, "🔔 Notificación: %s\n", n->text);
            memmove(n, n + 1, (g_shared->notif_count - i - 1) * sizeof(Notification));
            g_shared->notif_count--;
            continue; /* no incrementes i: ahora hay un nuevo elemento en i */
        }
        i++;
    }

    sb.sem_op = +1;
    semop(g_sem_id, &sb, 1);
}

