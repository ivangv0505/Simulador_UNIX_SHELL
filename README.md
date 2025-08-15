# uamashell – Simulador de Administración de UNIX

**uamashell** es un shell educativo que implementa:
- Límite de **instancias concurrentes** con **SysV IPC** (semáforo + memoria compartida).
- **Interfaz ncurses** (modo opcional texto plano).
- **Bitácoras** de comandos y errores.
- **Bloqueo por archivo** (Versión 2).
- **Ejecución remota** cliente/servidor con bitácoras en ambos lados (Versión 3).

---

## Tabla de Contenidos
- [Requisitos](#requisitos)
- [Compilación](#compilación)
- [Instalación](#instalación)
- [Uso](#uso)
  - [Modos de ejecución](#modos-de-ejecución)
  - [Comandos internos](#comandos-internos)
- [Configuración](#configuración)
- [Bitácoras](#bitácoras)
- [Limpieza de recursos](#limpieza-de-recursos)
- [Empacar para entrega](#empacar-para-entrega)
- [Consideraciones](#consideraciones)
- [Versión 2 — Bloqueo por archivo (concurrencia)](#versión-2--bloqueo-por-archivo-concurrencia)
  - [Qué se registra](#qué-se-registra)
  - [Comandos opcionales](#comandos-opcionales)
  - [Guía de prueba rápida](#guía-de-prueba-rápida)
- [Versión 3 — Ejecución remota](#versión-3--ejecución-remota)
  - [Servidor](#servidor)
  - [Cliente](#cliente)
  - [Bitácoras (cliente y servidor)](#bitácoras-cliente-y-servidor)
  - [Bloqueos por archivo en remoto](#bloqueos-por-archivo-en-remoto)
- [Autores](#autores)

---

## Requisitos
- **gcc**, **make**
- **ncurses** (paquete `libncurses-dev` o equivalente)
- **CVS**

---

## Compilación
Desde el directorio del proyecto:

```bash
cd /uamashell/
make clean && make
# Genera: bin/uamashell
```

---

## Instalación
```bash
./scripts/install.sh
# Instala en: $HOME/.local/uamashell
```

---

## Uso

Ejecuta el binario instalado:

```bash
~/.local/uamashell/bin/uamashell
```

### Modos de ejecución
1) **Texto plano** (sin ncurses)
```bash
export UAMASHELL_PLAIN=1
./bin/uamashell
```

2) **Interfaz ncurses**
```bash
./bin/uamashell
```

### Comandos internos
- `ayuda` → Muestra el menú de ayuda.  
- `terminar` → Finaliza la sesión.  
- `bitacora_comandos` → Pagina la bitácora de **comandos**.  
- `bitacora_error` → Pagina la bitácora de **errores**.  
- `showconf` → Muestra valores actuales cargados.  
- `setconf CLAVE=VALOR` → Cambia parámetros en `etc/uamashell.conf` **en caliente**.  
- `cd RUTA`  
- *(cualquier otro texto)* se ejecuta con `/bin/sh -c "<texto>"`.

---

## Configuración
**Archivo:** `etc/uamashell.conf`

Claves disponibles:
- `PROGRAM_NAME` (por defecto `uamashell`)
- `MAX_INSTANCES` (límite de instancias simultáneas; por defecto `3`)
- `LOG_DIR` (directorio de bitácoras; por defecto `var/log`)
- `LOCK_DIR` (directorio de lockfiles)
- `REMOTE_PORT` (puerto del servidor remoto)
- `REMOTE_ALLOWED` (IPs permitidas, separadas por coma)

**Ejemplo:**
```ini
PROGRAM_NAME=uamashell
MAX_INSTANCES=3
LOG_DIR=var/log
LOCK_DIR=var/locks
REMOTE_PORT=5050
REMOTE_ALLOWED=127.0.0.1
```

---

## Bitácoras
*(acumulativas)*

- `var/log/uamashell.log` — Historial de **comandos** (fecha, hora, comando, pid, usuario, tty, IP/SSH_CLIENT).  
- `var/log/uamashell_error.log` — **Errores** e **intentos de concurrencia** (dueño/competidor, archivo, pid/user/tty/ip, comando).

---

## Limpieza de recursos
Los recursos **SysV IPC** se liberan al salir.  
Si alguna instancia termina de forma anormal y deja recursos, ejecuta otra instancia para recompactar o reinicia el servidor (los IPC son globales del sistema).

---

## Empacar para entrega
```bash
./scripts/package.sh
# Genera: KERNEL_FORCE_JULIO_COMUNICACIÓN_PROCESOS_III_VERSION_I.tgz
```

---

## Consideraciones
- Se usa **SysV IPC** (semaforización + memoria compartida) para limitar **N instancias**.  
- Interfaz **ncurses** con paginación de bitácoras.  
- Las bitácoras registran: **fecha, hora, comando, pid, usuario, tty, IP**.  
- `setconf` modifica `etc/uamashell.conf` **sin reiniciar**.  
- No se usan variables globales salvo manejadores internos de IPC.

---

## Versión 2 — Bloqueo por archivo (concurrencia)

Antes de ejecutar un comando externo **modificador** (editores, `cp`/`mv`/`rm`, redirecciones `>`/`>>`, etc.), uamashell intenta **extraer la ruta objetivo**.

1. Si hay objetivo, crea un **lockfile** en `LOCK_DIR` y toma un `fcntl(F_WRLCK)` **no bloqueante** sobre ese lockfile.  
2. Si el **lock** falla (otro proceso lo mantiene):  
   - Muestra en la instancia *competidora* los datos del **dueño** (pid, usuario, tty, IP, comando).  
   - Envía una **notificación** al dueño.  
   - Registra el intento en **uamashell_error.log**.  
   - **No** ejecuta el comando del competidor.

### Qué se registra
- **`uamashell.log` (comandos):** cada comando (éxitos y rechazos) con metadatos.  
- **`uamashell_error.log` (errores):** además de errores, los **intentos de concurrencia** con:
  - archivo, pid/user/tty/ip del **dueño** y del **competidor**, y el **comando** del competidor.

### Comandos opcionales
- `notificaciones` — Muestra y limpia avisos pendientes.  
- `dueno <archivo>` — Indica si está bloqueado: imprime datos del dueño si hay lock; “libre” en caso contrario.

### Guía de prueba rápida
- `showconf` → verifica `MAX_INSTANCES`, `LOG_DIR`, `LOCK_DIR`.  
- **Terminal A:** `nano pruebaU.txt` (déjalo abierto).  
- **Terminal B:** `echo "X" >> pruebaU.txt` → ver **aviso y rechazo**.  
- **Terminal A:** `notificaciones` (ver el aviso).  
- `bitacora_error` → comprobar línea de “Acceso concurrente…”.  
- Cerrar **A** → repetir **B** (ahora debe **permitir** ejecutar).

---

## Versión 3 — Ejecución remota

### Servidor
En la máquina “servidor”:
```bash
./bin/uamashell --server
# Lee REMOTE_PORT y REMOTE_ALLOWED de etc/uamashell.conf
# Registra en: var/log/uamashell.log y var/log/uamashell_error.log
```

### Cliente
Dentro del shell:
```text
IP <direccion>     # abre sesión remota con el servidor (usa REMOTE_PORT)
desconectar        # cierra la sesión remota (no sales del programa)
```

### Bitácoras (cliente y servidor)
- `var/log/uamashell.log`
  - Comandos ejecutados (incluye `remote@<IP>: <cmd>` en el cliente).
  - Conexiones y desconexiones remotas (lado servidor).
- `var/log/uamashell_error.log`
  - Errores de ejecución.
  - Intentos de conexión **no autorizados**.
  - Intentos de **concurrencia** (dos instancias sobre el mismo archivo), con datos de dueño/competidor.  
  - **Formato:** fecha/hora, comando, pid, login, tty, IP (o `SSH_CLIENT`).

### Bloqueos por archivo en remoto
El servidor reutiliza el mismo **pipeline** que en local:
1. Detecta el **archivo objetivo**.  
2. Toma `fcntl(F_WRLCK)` sobre un **lockfile** en `LOCK_DIR`.  
3. Si el lock falla:  
   - Notifica al **competidor** con los datos del **dueño**.  
   - **No** ejecuta el comando competidor.  
   - Registra el evento en `uamashell_error.log`.

---

## Autores
- Enrique Hernández Mauricio – 2223030397  
- Garrido Velázquez Iván – 2203025425  
- Loaeza Sánchez Wendy Maritza – 2193042056  
- Robles Pérez Luis Fernando – 2203031441
