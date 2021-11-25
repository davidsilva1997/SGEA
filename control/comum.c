#include "comum.h"

HANDLE createEvent(TCHAR* name)
{
	// cria o objeto evento
	HANDLE event = CreateEvent(
		NULL,	// security attributes
		TRUE,	// manual-reset event
		FALSE,	// initial state not signaled
		name);	// name of the event object

	return event;
}

HANDLE createMutex(TCHAR* name)
{
	// cria o objeto mutex
	HANDLE mutex = CreateMutex(
		NULL,	// security attributes
		FALSE,	// calling thread obtains initial ownership 
		name);

	return mutex;
}

HANDLE createSemaphore(TCHAR* name, int count)
{
	// cria o semáforo
	HANDLE semaphore = CreateSemaphore(
		NULL,	// default security attributes
		count,	// initial count
		count,	// maximum count
		name);	// name of the semaphore

	return semaphore;
}

HANDLE createEmptySemaphore(TCHAR* name, int count)
{
	// cria o semáforo
	HANDLE semaphore = CreateSemaphore(
		NULL,	// default security attributes
		0,		// initial count
		count,	// maximum count
		name);	// name of the semaphore

	return semaphore;
}

HANDLE createThread(LPTHREAD_START_ROUTINE function, LPVOID data)
{
	// cria a thread e inicia a execução da mesma
	HANDLE thread = CreateThread(
		NULL,		// default security attributes
		0,			// default size of the stack
		function,	// function to be executed
		data,		// data to be passed to the thread
		CREATE_SUSPENDED,	// flag that control the creation of the thread
		NULL);		// thread identifier is not returned

	return thread;	
}
