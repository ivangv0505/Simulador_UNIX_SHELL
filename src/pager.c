
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
 *   Encapsula la llamada a /usr/bin/less (o $PAGER) para mostrar páginas de logs con paginación.
 */

#include "common.h"


/**
 * Muestra el contenido de filepath, paginando en pantalla.
 * @param filepath Ruta al archivo de texto a mostrar.
 * @param title    Título para mostrar en el encabezado de cada página.
 * @return 0 en éxito, -1 si falla al abrir el archivo.
 */

int curses_pager(const char *filepath, const char *title){
   //abre el archivo en modo lectura
    FILE *f = fopen(filepath, "r");
    if(!f){
        log_error("No se pudo abrir %s: %s", filepath, strerror(errno));
        return -1;
    }
    // Limpiar pantalla y obtener dimensiones

    clear();
    int rows, cols; getmaxyx(stdscr, rows, cols);
    int lines_per_page = rows - 2;
    char line[1024];
    int line_no = 0;
    int page = 1;
    int ch;
    // Bucle principal de paginación

    while(true){
        clear();
        // Imprimir encabezado con título y número de página

        mvprintw(0,0,"%s - Página %d (q para salir, espacio/s para avanzar, b para atrás)", title, page);
        int printed = 0;
       // long pos = ftell(f);
        // Leer y mostrar hasta lines_per_page líneas
        while(printed < lines_per_page && fgets(line, sizeof(line), f)){
            line_no++;
	//asegura que no desborda el ancho de la terminal
            line[cols-1]='\0';
            mvprintw(1+printed, 0, "%s", line);
            printed++;
        }
        refresh();
        // Esperar pulsación de tecla

        ch = getch();
        if(ch=='q' || ch=='Q' || ch==27) break;
        if(ch=='b' || ch=='B'){
            // retroceder una página
            // estrategia: rebobinar al inicio y avanzar (simplifica)
            rewind(f); line_no=0;
            int target = (page-2)*lines_per_page;
            if(target<0) target=0;
            int cnt=0;
            while(cnt<target && fgets(line, sizeof(line), f)) cnt++;
            page = (page>1)? page-1 : 1;
        } else {
            if(printed < lines_per_page){
                // fin de archivo
                break;
            }
            page++;
        }
    }
    fclose(f);
    return 0;
}
