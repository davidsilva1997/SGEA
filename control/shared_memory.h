#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <Windows.h>
#include "comum.h"


/*
* initMapFile()
* -------------------
* Inicializa o ficheiro de mapeamento
*/
HANDLE initMapFile();

/*
* initMapViewOfFile()
* -------------------
* Abre a vista da memória partilhada
*/
circular_buffer* initMapViewOfFile(HANDLE);



#endif 