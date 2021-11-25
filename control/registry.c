#include "registry.h"

DWORD readValueInRegistry(TCHAR* valueName)
{
	HKEY key;	// handle to the opened key
	DWORD dwResult, dwSize, dwType, dwData;

	// abre a chave
	dwResult = RegOpenKeyEx(
		REGISTRY_HKEY,		// handle to an open registry key
		REGISTRY_SUBKEY,	// the name of the registry subkey to be opened
		0,					// option to apply when opening the key
		KEY_READ,			// mask forr the desired access rights
		&key);				// pointer thar receives a handle to the opened key

	// erro ao abrir a chave
	if (dwResult != ERROR_SUCCESS)
		return -3;

	dwSize = sizeof(DWORD);	// size of data

	// obtem o tipo e o valor 
	dwResult = RegQueryValueEx(
		key,			// handle to an open registry key
		valueName,		// the name of the registry value
		NULL,			// reserved parameter
		&dwType,		// a pointer that receive a code indicating the type of data
		&dwData,		// a pointer that receive the value's data
		&dwSize);		// a pointer that specifies the size of the buffer

	// erro na leitura do valor
	if (dwResult != ERROR_SUCCESS)
	{
		RegCloseKey(key);
		return -2;
	}

	// erro no tipo de dado do valor
	if (dwType != REG_DWORD)
	{
		RegCloseKey(key);
		return -1;
	}

	RegCloseKey(key);

	return dwData;
}