
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
 *   Pruebas unitarias de funciones clave.
 */

// src/test_main.c
#include "common.h"    // donde está Config y DEFAULT_CONF
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Punto A: inicio de main()

    fprintf(stderr, "A) Tester: entro a main()\n");

    Config cfg;
    if (load_config(DEFAULT_CONF, &cfg) < 0) {
        fprintf(stderr, "ERROR: load_config falló\n");
        return 1;
    }
    fprintf(stderr, "B) Tester: config cargada: program_name=\"%s\", max_instances=%d, log_dir=\"%s\"\n",
            cfg.program_name, cfg.max_instances, cfg.log_dir);

    char *plain = getenv("UAMASHELL_PLAIN");
    fprintf(stderr, "C) Tester: getenv(\"UAMASHELL_PLAIN\") = %s\n", plain ? plain : "(null)");

    if (plain && strcmp(plain, "1") == 0) {
        fprintf(stderr, "D) Tester: entro al modo PLAIN\n");
    } else {
        fprintf(stderr, "D) Tester: NO entro al modo PLAIN\n");
    }

    return 0;
}

