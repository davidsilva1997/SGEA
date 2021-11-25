#include "shared_memory.h"

HANDLE initMapFile()
{
	HANDLE hMapFile;	// handle para o ficheiro de mapeamento

	// tenta abrir o ficheiro de mapeamento
	hMapFile = OpenFileMapping(
		FILE_MAP_ALL_ACCESS,	// access to the file mapping object
		FALSE,					// handle cannot be inherited
		MAP_FILE);				// name of the file mapping object

	// o ficheiro já existe então o programa tem de terminar
	if (hMapFile != NULL)
		return NULL;
	
	// cria o ficheiro de mapeamento
	hMapFile = CreateFileMapping(
		INVALID_HANDLE_VALUE,	// creates a file mapping object
		NULL,					// default security attributes
		PAGE_READWRITE,			// page protection
		0,						// high-order of the maximum size of the file mapping object
		sizeof(circular_buffer),// low-order of the maximum size of the file mapping object
		MAP_FILE);				// name of the file mapping object

	return hMapFile;
}

circular_buffer* initMapViewOfFile(HANDLE hMapFile)
{
	circular_buffer* ptr;

	// obtem o ponteiro para a vista da memória partilhada
	ptr = (circular_buffer*)MapViewOfFile(
		hMapFile,	// handle to map object
		FILE_MAP_READ |	// read/write permission
		FILE_MAP_WRITE,
		0,				// offset high - where the view begins
		0,				// offset low  - where the view begins
		sizeof(circular_buffer));	// number of bytes of a file mapping to map to the view

	return ptr;
}

