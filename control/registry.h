#ifndef REGISTRY_H
#define REGISTRY_H

#include <Windows.h>

#define REGISTRY_HKEY HKEY_CURRENT_USER
#define REGISTRY_SUBKEY	TEXT("TPSO2")
#define AIRPORT_SUBKEY	TEXT("AEROPORTOS")
#define PLANE_SUBKEY	TEXT("AVIOES")

/*
* readValueInRegistry()
* -------------------
* Obtem o valor de um par chave-valor do registry
*/
DWORD readValueInRegistry(TCHAR*);

#endif