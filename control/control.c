#include <Windows.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <Psapi.h>
#include "shared_memory.h"
#include "comum.h"
#include "registry.h"

#define NUM_THREADS		4
#define WAITING_TIME	3000

// mapa
#define MAX_AIRPORTS	20
#define MAX_PLANES		20
#define MAP_SIZE		1000
#define DISTANCE		10
#define MAP_EVENT		TEXT("MAP_EVENT")
#define MAP_MUTEX		TEXT("MAP_MUTEX")

#define MAX_PASSAG		10

// comandos dispon�veis para o controlador
#define CMD_SHUTDOWN	TEXT("SHUTDOWN")	// encerra o sistema todo
#define CMD_ADD_AIRPORT	TEXT("ADD AIRPORT")	// adiciona um aeroporto ao mapa
#define CMD_ADD_AIRPORTS	TEXT("ADD AIRPORTS")	// adiciona 4 aeroporto ao mapa
#define CMD_PAUSE		TEXT("PAUSE")		// bloqueia a aceita��o de novos avi�es
#define CMD_UNPAUSE		TEXT("UNPAUSE")		// desbloqueia a aceita��o de novos avi�es
#define CMD_LIST		TEXT("LIST")		// lista os comandos dispon�veis


// estrutura de uma int�ncia do pipe
typedef struct
{
	HANDLE hPipeInstance;	// inst�ncia do pipe
	OVERLAPPED overlap;
	item_pipe request;		// pedido do passageiro
	DWORD cbReadRequest;	// bytes lidos
	item_pipe answer;		// resposta do controlador
	DWORD cbToWriteAnswer;	// bytes para escrever
	DWORD dwState;			// estado do pipe
	BOOL fPendingIO;		// indica se a opera��o (liga��o, leitura, escrita) est� pendente
} PIPE_INSTANCE;


// estrutura do mapa
typedef struct
{
	int* map;			// mapa (array bidimensional)
	airport* airports;	// ponteiro para array de aeroportos
	int numAirports;	// n�mero de aeroportos inseridos
	int maxAirports;	// n�mero m�ximo de aeroportos
	plane* planes;		// ponteiro para array de avi�es
	int numPlanes;		// n�mero de avi�es inseridos
	int maxPlanes;		// n�mero m�ximo de avi�es
	passenger* passengers;	// ponteiro para array de passageiros
	int numPassengers;	// n�mero de passageiros inseridos
	HANDLE hEvent;		// quando ocorre um evento para atualizar o mapa
	HANDLE hMutex;		// exclus�o m�tua dos dados
} map_data;

// dados passados para as threads
typedef struct
{
	circular_buffer*		ptrSharedMemory;		// ponteiro para a mem�ria partilhada
	map_data				map;					// representa o mapa
	sync_circular_buffer	sync_circular_buffer;	// sincroniza��o do buffer circular
	sync_plane_access		sync_plane_access;		// sincroniza��o de acesso dos avi�es
	HANDLE					hEventShutdown;			// evento assinalado quando � executado o comando shutdown
	PIPE_INSTANCE*			pipe;					// ponteiros para array de inst�ncias do pipe
	HANDLE					hEvents[MAX_PASSAG];	// objetos evento para as inst�ncias do pipe
} thread_data;

DWORD handleValueReadInRegistry(TCHAR* lpValueName);


// threads
DWORD WINAPI threadExecuteCommands(LPVOID);
DWORD WINAPI threadReadSharedMemory(LPVOID);
DWORD WINAPI threadStillAlive(LPVOID);
DWORD WINAPI threadReadPipeInstances(LPVOID);

// mem�ria partilhada
BOOL initMap(map_data*);
void initCircularBuffer(thread_data*);
void writeInSharedMemory(thread_data*, item*);
BOOL initSyncPlaneAccess(thread_data*);
void closeSyncPlaneAccess(sync_plane_access*);
BOOL initSyncCircularBuffer(thread_data*);
void closeSyncCircularBuffer(sync_circular_buffer*);
void closeHandles(thread_data*);

// pipes
BOOL initPipeInstances(thread_data*);
BOOL initPipeInstances(thread_data*);
BOOL ConnectToNewClient(HANDLE, OVERLAPPED*);
void DisconnectAndReconnect(PIPE_INSTANCE*, DWORD);
void getAnswerForPassenger(thread_data*, PIPE_INSTANCE*, int);
passenger getPassengerFromIdentification(TCHAR*);
BOOL sendMessageToPassenger(PIPE_INSTANCE*, item_pipe*);



// comandos dispon�veis para o controlador
void cmdShutdown(thread_data*);
BOOL cmdAddAirport(map_data*);
void cmdAddAirports(map_data*);
BOOL cmdPause(thread_data*);
BOOL cmdUnpause(thread_data*);
void cmdList(map_data*);
const TCHAR* cmdHelp(void);
void cmdClear(void);

// comandos enviados pelos avi�es
void handleCommandFromPlane(thread_data*, item);
void cmdAddPlane(thread_data*, item*);
void cmdDefine(thread_data*, item*);
void cmdStartFlight(thread_data*, item*);
void cmdBoarding(thread_data*, item*);

BOOL isAirportFull(map_data*);
BOOL isValidAirportData(airport);
BOOL isAirportNameRepeated(map_data*, TCHAR*);
BOOL isPositionEmpty(map_data*, position);
BOOL isAnotherAirportAround(map_data*, airport);
BOOL isPlanesFull(map_data*);
void getNewAirportData(airport*);
void getIntegerFromUser(int*);
void addAirport(map_data*, airport*);
void addPlane(map_data*, plane*);
void addPassenger(map_data*, passenger*);
BOOL removePlane(thread_data*, int);
void removePassenger(map_data*, int);
void updatePlane(thread_data*, plane*);
void updatePlanePositions(thread_data*, plane*);
void printAirportsData(map_data*);
void printPlanesData(map_data*);
void printPassengersData(map_data*);
position getPositionOfAirport(map_data*, TCHAR*);
BOOL isValidPosition(map_data*, position);
BOOL isInteger(TCHAR*);
BOOL isValidPassenger(thread_data*, passenger*);




int _tmain(int argc, TCHAR* argv[])
{
	HANDLE hMapFile;	// handle para o ficheiro de mapeamento
	thread_data data;	// dados passados para as threads
	HANDLE hThread[NUM_THREADS];	// handle para as threads

#ifdef UNICODE
	(void)_setmode(_fileno(stdin), _O_WTEXT);
	(void)_setmode(_fileno(stdout), _O_WTEXT);
#endif

	_tprintf(TEXT("Inicializa��o do programa \'control.exe\'...\n"));

	// inicializa o ficheiro de mapeamento
	hMapFile = initMapFile();
	if (hMapFile == NULL)
	{
		_tprintf(TEXT("Erro na inicializa��o do ficheiro de mapeamento!\n"));
		return -1;
	}

	// abre a vista da mem�ria partilhada do buffer circular
	data.ptrSharedMemory = initMapViewOfFile(hMapFile);
	if (data.ptrSharedMemory == NULL)
	{
		_tprintf(TEXT("Erro no mapeamento da vista!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// obtem o n�mero m�ximo de aeroportos e avi�es do registry
	data.map.maxAirports = handleValueReadInRegistry(AIRPORT_SUBKEY);
	data.map.maxPlanes = handleValueReadInRegistry(PLANE_SUBKEY);
	_tprintf(TEXT("N�mero m�ximo de aeroportos [%d]\nN�mero m�ximo de avi�es [%d]\nN�mero m�ximo de passageiros [%d]\n"), data.map.maxAirports, data.map.maxPlanes, MAX_PASSAG);

	// inicializa o mapa
	if (!initMap(&data.map))
	{
		_tprintf(TEXT("Erro na inicializa��o do mapa!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// inicializa a estrutura de sincroniza��o de acesso dos avi�es
	if (!initSyncPlaneAccess(&data))
	{
		_tprintf(TEXT("Erro na inicializa��o da estrutura de sincroniza��o de acesso dos avi�es!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// inicializa a estrutura de sincroniza��o do buffer circular
	if (!initSyncCircularBuffer(&data))
	{
		_tprintf(TEXT("Erro na inicializa��o da estrutura de sincroniza��o do buffer circular!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// inicializa o evento que controla se o comando shutdown � executado
	if ((data.hEventShutdown = createEvent(EVENT_SHUTDOWN)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o do objeto evento que controla se o comando shutdown � executado!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// inicializa o buffer circular
	initCircularBuffer(&data);

	// inicializa os pipes
	if (!initPipeInstances(&data))
	{
		_tprintf(TEXT("Erro na inicializa��o das inst�ncias dos pipes.\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// cria a thread que permite executar comandos
	if ((hThread[0] = createThread(threadExecuteCommands, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o da thread que permite executar comandos!\n"));
		closeHandles(&data);
		CloseHandle(hMapFile);
		return -1;
	}

	// cria a thread que l� a mem�ria partilhada
	if ((hThread[1] = createThread(threadReadSharedMemory, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o da thread que permite ler a mem�ria partilhada!\n"));
		closeHandles(&data);
		CloseHandle(hMapFile);
		return -1;
	}

	// cria a thread que verifica se os processos avi�o ainda est�o em execu��o
	if ((hThread[2] = createThread(threadStillAlive, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o da thread que verifica a execu��o dos processos avi�o!\n"));
		closeHandles(&data);
		CloseHandle(hMapFile);
		return -1;
	}

	// cria a thread que l� os pedidos das inst�ncias do pipe
	if ((hThread[3] = createThread(threadReadPipeInstances, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o da thread l� os pedidos dos pipes!\n"));
		closeHandles(&data);
		CloseHandle(hMapFile);
		return -1;
	}

	// inicia a execu��o das threads
	for (int i = 0; i < NUM_THREADS; i++)
		ResumeThread(hThread[i]);

	// Espera at� que uma das threads termine
	WaitForMultipleObjects(
		NUM_THREADS,	// the number of object handles in the array 
		hThread,		// an array of object handles
		FALSE,			// does not wait for all threads
		INFINITE);		// waiting time

	// fecha os handles dos objetos evento, mutex e sem�foro
	closeHandles(&data);
	UnmapViewOfFile(data.ptrSharedMemory);
	CloseHandle(hMapFile);
	return 0;
}

/*
* handleValueReadInRegistry()
* -------------
* Trata o valor lido no registry
*/
DWORD handleValueReadInRegistry(TCHAR* lpValueName)
{
	DWORD dwValue = readValueInRegistry(lpValueName);

	switch (dwValue)
	{
	case -3:
		_tprintf(TEXT("N�o foi poss�vel abrir a chave [%s]!\n"), lpValueName); break;
	case -2:
		_tprintf(TEXT("N�o foi poss�vel ler o valor da chave [%s]!\n"), lpValueName); break;
	case -1:
		_tprintf(TEXT("A chave [%s] tem um formato incorreto!\n"), lpValueName); break;
	case 0:
		_tprintf(TEXT("O valor da chave [%s] � igual a 0. Vai ser assumido o valor por defeito [%d]!\n"), lpValueName, MAX_AIRPORTS); break;
	}

	if (_tcscmp(lpValueName, AIRPORT_SUBKEY) == 0)
	{
		if (dwValue > MAX_AIRPORTS || dwValue == 0)
			dwValue = MAX_AIRPORTS;
	}
	else if (_tcscmp(lpValueName, PLANE_SUBKEY) == 0)
	{
		if (dwValue > MAX_PLANES || dwValue == 0)
			dwValue = MAX_PLANES;
	}

	return dwValue;
}

/*
* initMap()
* -------------
* Inicializa o mapa
*/
BOOL initMap(map_data* map)
{
	// aloca��o de mem�ria
	map->map = (int*)malloc(MAP_SIZE * MAP_SIZE * sizeof(int));
	map->airports = (airport*)malloc(map->maxAirports * sizeof(airport));
	map->planes = (plane*)malloc(map->maxPlanes * sizeof(plane));
	map->passengers = (passenger*)malloc(MAX_PASSAG * sizeof(passenger));

	// verifica se a aloca��o de mem�ria deu certo
	if (map->map == NULL)
		return FALSE;
	if (map->airports == NULL)
		return FALSE;
	if (map->planes == NULL)
		return FALSE;
	if (map->passengers == NULL)
		return FALSE;

	map->numAirports = 0;
	map->numPlanes = 0;
	map->numPassengers = 0;

	// cria o objeto evento
	if ((map->hEvent = createEvent(MAP_EVENT)) == NULL)
		return FALSE;

	// cria o object mutex
	if ((map->hMutex = createMutex(MAP_MUTEX)) == NULL)
		return FALSE;

	// preenche o mapa com zeros
	ZeroMemory(map->map, sizeof(map->map));

	// limpa o lixo 
	ZeroMemory(map->airports, sizeof(airport) * map->maxAirports);
	ZeroMemory(map->planes, sizeof(plane) * map->maxPlanes);
	ZeroMemory(map->passengers, sizeof(passenger) * MAX_PASSAG);

	return TRUE;
}

/*
* initSyncPlaneAccess()
* -------------
* Inicializa a estrutura de sincroniza��o de acesso dos avi�es
*/
BOOL initSyncPlaneAccess(thread_data* data)
{
	sync_plane_access* sync = &data->sync_plane_access;

	// cria o sem�foro que permite bloquear/desbloquear a aceita��o de novos avi�es
	sync->semaphore_block_access = createSemaphore(SEMAPHORE_SYNC_BLOCK_ACCESS, 1);
	if (sync->semaphore_block_access == NULL)
		return FALSE;

	// cria o sem�foro que controla o n�mero de avi�es em execu��o
	sync->semaphore_access = createSemaphore(SEMAPHORE_SYNC_MAX_PLANES, data->map.maxPlanes);
	if (sync->semaphore_access == NULL)
		return FALSE;

	// gere o bloqueio/desbloqueio da aceita��o dos avi�es
	sync->isBlocked = FALSE;

	return TRUE;
}

/*
* closeSyncSharedMemory()
* -------------
* Fecha os handles dos sem�foros
*/
void closeSyncPlaneAccess(sync_plane_access* data)
{
	CloseHandle(data->semaphore_block_access);
	CloseHandle(data->semaphore_access);
}

/*
* initSyncCircularBuffer()
* -------------
* Inicializa a estrutura de sincroniza��o do buffer circular
*/
BOOL initSyncCircularBuffer(thread_data* data)
{
	sync_circular_buffer* sync = &data->sync_circular_buffer;

	// cria o sem�foro de exlus�o m�tua de produtor
	sync->semaphore_mutex_producer = createSemaphore(SEMAPHORE_SYNC_PRODUCER, 1);
	if (sync->semaphore_mutex_producer == NULL)
		return FALSE;

	// cria o sem�foro de exlus�o m�tua de consumidor
	sync->semaphore_mutex_consumer = createSemaphore(SEMAPHORE_SYNC_CONSUMER, 1);
	if (sync->semaphore_mutex_consumer == NULL)
		return FALSE;

	// cria o sem�foro que contabiliza os itens por consumir
	sync->semaphore_items = createEmptySemaphore(SEMAPHORE_SYNC_ITEMS, DIM);
	if (sync->semaphore_items == NULL)
		return FALSE;

	// cria o sem�foro que contabiliza os itens que podem ser produzidos
	sync->semaphore_empty = createSemaphore(SEMAPHORE_SYNC_EMPTY, DIM);
	if (sync->semaphore_empty == NULL)
		return FALSE;

	return TRUE;
}

/*
* closeSyncSharedMemory()
* -------------
* Fecha os handles dos sem�foros
*/
void closeSyncCircularBuffer(sync_circular_buffer* data)
{
	CloseHandle(data->semaphore_mutex_producer);
	CloseHandle(data->semaphore_mutex_consumer);
	CloseHandle(data->semaphore_items);
	CloseHandle(data->semaphore_empty);
}

/*
* closeHandles()
* -------------
* Fecha os handles dos objetos evento, mutex e sem�foro
*/
void closeHandles(thread_data* data)
{
	closeSyncPlaneAccess(&data->sync_plane_access);
	closeSyncCircularBuffer(&data->sync_circular_buffer);
	CloseHandle(data->hEventShutdown);
	CloseHandle(data->map.hEvent);
	CloseHandle(data->map.hMutex);
}

/*
* initCircularBuffer()
* -------------
* Inicializa o buffer circular
*/
void initCircularBuffer(thread_data* data)
{
	data->ptrSharedMemory->in = data->ptrSharedMemory->out = 0;
}

/*
* threadExecuteCommands()
* -------------
* Thread que permite executar comandos
*/
DWORD WINAPI threadExecuteCommands(LPVOID args)
{
	thread_data* data = (thread_data*)args;
	TCHAR cmd[BUFFERSMALL] = { '\0' };
	int cmdSize;

	// fica em ciclo at� escrever 'fim'
	while (_tcscmp(cmd, CMD_END) != 0)
	{
		// obtem o input
		_tprintf(TEXT("\n\n> "));
		_fgetts(cmd, BUFFERSMALL, stdin);
		cmd[_tcslen(cmd) - 1] = '\0';

		// maiusculas
		cmdSize = _tcslen(cmd);
		for (int i = 0; i < cmdSize; i++)
			cmd[i] = _totupper(cmd[i]);

		if (_tcscmp(cmd, CMD_SHUTDOWN) == 0)
		{
			// encerra o sistema
			cmdShutdown(data);
			break;
		}
		else if (_tcscmp(cmd, CMD_ADD_AIRPORT) == 0)
		{
			// adiciona um aeroporto
			cmdAddAirport(&data->map);
		}
		else if (_tcscmp(cmd, CMD_ADD_AIRPORTS) == 0)
		{
			// adiciona 4 aeroportos j� definidos por defeito
			cmdAddAirports(&data->map);
		}
		else if (_tcscmp(cmd, CMD_PAUSE) == 0)
		{
			// bloqueia a aceita��o de novos avi�es
			if (cmdPause(data))
				_tprintf(TEXT("A aceita��o de novos avi�es foi bloqueada!\n"));
			else
				_tprintf(TEXT("A aceita��o de novos avi�es j� estava bloqueada!\n"));

		}
		else if (_tcscmp(cmd, CMD_UNPAUSE) == 0)
		{
			// desbloqueia a aceita��o de novos avi�es
			if (cmdUnpause(data))
				_tprintf(TEXT("A aceita��o de novos avi�es foi desbloqueada!\n"));
			else
				_tprintf(TEXT("A aceita��o de novos avi�es j� estava desbloqueada!\n"));
		}
		else if (_tcscmp(cmd, CMD_LIST) == 0)
		{
			// lista os aeroportos, avi�es e passageiros
			cmdList(&data->map);
		}
		else if (_tcscmp(cmd, CMD_HELP) == 0)
		{
			// obtem ajuda acerca dos comandos dispon�veis
			_tprintf(TEXT("%s\n"), cmdHelp());
		}
		else if (_tcscmp(cmd, CMD_CLEAR) == 0)
		{
			// limpa a consola
			cmdClear();
		}
		else if (_tcscmp(cmd, CMD_CLS) == 0)
		{
			// limpa a consola
			cmdClear();
		}
		else if (_tcscmp(cmd, CMD_END) == 0)
		{
			// termina o programa
			_tprintf(TEXT("O programa vai encerrar!\n"));
			break;
		}
		else
		{
			_tprintf(TEXT("Comando [%s] inv�lido!\n"), cmd);
		}
	}
	
	return 0;
}

/*
* cmdShutdown()
* -------------
* Encerra o sistema, avisa os avi�es e passageiros
*/
void cmdShutdown(thread_data* data)
{
	_tprintf(TEXT("O comando [SHUTDOWN] foi executado! O sistema vai encerrar dentro de 2 segundos!\n"));

	// assinala o evento
	SetEvent(data->hEventShutdown);
	Sleep(2000);
}

/*
* cmdAddAirport()
* -------------
* Adiciona um aeroporto ao sistema
*/
BOOL cmdAddAirport(map_data* map)
{
	airport* airports = map->airports;	// ponteiro para o array de aeroportos
	airport newAirport;

	// verifica se o n�mero m�ximo de aeroportos n�o foi atingido
	if (isAirportFull(map))
	{
		_tprintf(TEXT("O n�mero m�ximo de aeroportos j� foi atingido!\n"));
		return FALSE;
	}

	// obtem os dados do novo aeroporto
	getNewAirportData(&newAirport);

	// verifica se os dados do aeroporto s�o validos
	if (!isValidAirportData(newAirport))
	{
		_tprintf(TEXT("Os dados do aeroporto n�o s�o v�lidos!\n"));
		return FALSE;
	}

	// verifica se existe um aeroporto com o mesmo nome
	if (isAirportNameRepeated(map, newAirport.name))
	{
		_tprintf(TEXT("Existe um aeroporto com o mesmo nome!\n"));
		return FALSE;
	}

	// verifica se a posi��o est� ocupada por outro aeroporto
	if (!isPositionEmpty(map, newAirport.position))
	{
		_tprintf(TEXT("Existe um aeroporto nessa posi��o!\n"));
		return FALSE;
	}

	// verifica se existe um aeroporto a menos de 10 posi��es
	if (isAnotherAirportAround(map, newAirport))
	{
		_tprintf(TEXT("Existe um aeroporto a menos de %d posi��es!\n"), DISTANCE);
		return FALSE;
	}

	// adiciona o aeroporto
	addAirport(map, &newAirport);

	_tprintf(TEXT("O aeroporto [%s] foi adicionado na posi��o [%d,%d]!\n"), newAirport.name, newAirport.position.x, newAirport.position.y);

	return TRUE;
}

/*
* cmdAddAirports()
* -------------
* Adiciona 4 aeroportos j� definidos pelo sistema
*/
void cmdAddAirports(map_data* map)
{
	airport newAirport[4];

	// limpa o lixo 
	ZeroMemory(&newAirport, sizeof(airport) * 4);

	_tcscpy_s(newAirport[0].name, BUFFERSMALL, TEXT("OPO"));
	newAirport[0].position.x = 0;
	newAirport[0].position.y = 0;
	_tcscpy_s(newAirport[1].name, BUFFERSMALL, TEXT("LIS"));
	newAirport[1].position.x = 50;
	newAirport[1].position.y = 50;
	_tcscpy_s(newAirport[2].name, BUFFERSMALL, TEXT("PAR"));
	newAirport[2].position.x = 100;
	newAirport[2].position.y = 100;
	_tcscpy_s(newAirport[3].name, BUFFERSMALL, TEXT("GVA"));
	newAirport[3].position.x = 150;
	newAirport[3].position.y = 150;

	for (int i = 0; i < 4; i++)
	{
		// verifica se o n�mero m�ximo de aeroportos n�o foi atingido
		if (isAirportFull(map))
		{
			_tprintf(TEXT("O n�mero m�ximo de aeroportos j� foi atingido!\n"));
			return;
		}

		// verifica se existe um aeroporto com o mesmo nome
		if (isAirportNameRepeated(map, newAirport[i].name))
		{
			_tprintf(TEXT("Existe um aeroporto com o mesmo nome!\n"));
			return;
		}

		// verifica se a posi��o est� ocupada por outros aeroportos
		if (!isPositionEmpty(map, newAirport[i].position))
		{
			_tprintf(TEXT("J� existe um aeroporto nessa posi��o!\n"));
			return;
		}

		// verifica se exite outro aeroporto a menos de 10 posi��es
		if (isAnotherAirportAround(map, newAirport[i]))
		{
			_tprintf(TEXT("Existe outro aeroporto num raio de 10 posi��es!\n"));
			return;
		}

		addAirport(map, &newAirport[i]);

		_tprintf(TEXT("O aeroporto [%s] foi adicionado na posi��o [%d,%d]!\n"), newAirport[i].name, newAirport[i].position.x, newAirport[i].position.y);
	}
}

/*
* cmdPause()
* -------------
* Bloqueia a aceita��o de novos avi�es
*/
BOOL cmdPause(thread_data* data)
{
	// verifica se a aceita��o de novos avi�es j� n�o est� bloqueada
	if (data->sync_plane_access.isBlocked)
		return FALSE;

	// bloqueia a aceita��o de novos avi�es
	data->sync_plane_access.isBlocked = TRUE;
	WaitForSingleObject(data->sync_plane_access.semaphore_block_access, INFINITE);

	return TRUE;
}

/*
* cmdUnpause()
* -------------
* Desbloqueia a aceita��o de novos avi�es
*/
BOOL cmdUnpause(thread_data* data)
{
	// verifica se a aceita��o de novos avi�es j� n�o est� desbloqueada
	if (!data->sync_plane_access.isBlocked)
		return FALSE;

	// desbloqueia a aceita��o de novos avi�es
	data->sync_plane_access.isBlocked = FALSE;
	ReleaseSemaphore(data->sync_plane_access.semaphore_block_access, 1, NULL);

	return TRUE;
}

/*
* cmdList()
* -------------
* Lista os aeroportos, avi�es e passageiros
*/
void cmdList(map_data* map)
{
	// imprime os aeroportos
	printAirportsData(map);

	// imprime os avi�es
	printPlanesData(map);

	// imprime os passageiros
	printPassengersData(map);	
}

/*
* cmdHelp()
* -------------
* Obtem ajuda acerca dos comandos dispon�veis
*/
const TCHAR* cmdHelp(void)
{
	TCHAR data[2048] = { '\0' };

	_stprintf_s(data, 2048, TEXT("\n\'%s\'\t%s\n\'%s\'\t%s\n\'%s\'\t%s\n\'%s\'\t\t%s\n\'%s\'\t%s\n\'%s\'\t\t%s\n\'%s\'\t\t%s\n\'%s\'\t\t%s\n\'%s\'\t\t%s\n\'%s\'\t\t%s\n"),
		CMD_SHUTDOWN, TEXT("-- Encerra o sistema"),
		CMD_ADD_AIRPORT, TEXT("-- Adiciona um aeroporto"),
		CMD_ADD_AIRPORTS, TEXT("-- Adiciona 4 aeroportos j� definidos por defeito"),
		CMD_PAUSE, TEXT("-- Bloqueia a aceita��o de novos avi�es"),
		CMD_UNPAUSE, TEXT("-- Desbloqueia a aceita��o de novos avi�es"),
		CMD_LIST, TEXT("-- Lista os aeroportos, avi�es e passageiros"),
		CMD_HELP, TEXT("-- Obtem ajuda acerca dos comandos dispon�veis"),
		CMD_CLEAR, TEXT("-- Limpa a consola"),
		CMD_CLS, TEXT("-- Limpa a consola"),
		CMD_END, TEXT("-- Termina o programa"));

	return data;
}

/*
* cmdClear()
* -------------
* Limpa a consola
*/
void cmdClear(void)
{
	system("cls");
}

/*
* isAirportFull()
* -------------
* Verifica se o aeroporto est� cheio
*/
BOOL isAirportFull(map_data* map)
{
	BOOL isFull = FALSE;

	// espera pelo mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	// verifica se o aeroporto est� cheio
	if (map->numAirports >= map->maxAirports)
		isFull = TRUE;

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	return isFull;
}

/*
* getNewAirportData()
* -------------
* Pede ao utilizador os dados do novo aeroporto
*/
void getNewAirportData(airport* airport)
{
	// obtem o nome
	_tprintf(TEXT("Nome do aeroporto: "));
	_fgetts(airport->name, BUFFERSMALL, stdin);
	airport->name[_tcslen(airport->name) - 1] = '\0';

	// obtem a posi��o no eixo x
	_tprintf(TEXT("Insira a posi��o no eixo X: "));
	getIntegerFromUser(&airport->position.x);

	// obtem a posi��o no eixo x
	_tprintf(TEXT("Insira a posi��o no eixo Y: "));
	getIntegerFromUser(&airport->position.y);
}

/*
* getIntegerFromUser()
* -------------
* Obtem um inteiro (n�mero)
*/
void getIntegerFromUser(int* position)
{
	TCHAR temp[16] = { '\0' };

	// fica em ciclo at� introduzir um n�mero
	do
	{
		// obtem o input
		_fgetts(temp, 16, stdin);
		temp[_tcslen(temp) - 1] = '\0';

		// sai so ciclo se o input for 0
		if (_tcscmp(temp, TEXT("0")) == 0)
			break;
	} while (_tstoi(temp) == 0);

	*position = _tstoi(temp);
}

/*
* isValidAirportData()
* -------------
* Verifica a integridade dos dados do aeroporto
*/
BOOL isValidAirportData(airport airport)
{
	int x = airport.position.x;
	int y = airport.position.y;
	int max = MAP_SIZE - 1;

	if (x < 0 || y < 0 || x > max || y > max || _tcslen(airport.name) < 3)
		return FALSE;

	return TRUE;
}

/*
* isAirportNameRepeated()
* -------------
* Verifica se existe um aeroporto com o mesmo nome j� inserido no sistemaa
*/
BOOL isAirportNameRepeated(map_data* map, TCHAR* name)
{
	BOOL isRepeated = FALSE;

	// espera pelo mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	// verifica se existe um aeroporto com o mesmo nome
	for (int i = 0; i < map->numAirports; i++)
	{
		if (_tcscmp(map->airports[i].name, name) == 0)
		{
			isRepeated = TRUE;
			break;
		}
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	return isRepeated;
}

/*
* isPositionEmpty()
* -------------
* Verifica se a posi��o n�o est� ocupada por outro aeroporto
*/
BOOL isPositionEmpty(map_data* map, position position)
{
	BOOL isEmpty = TRUE;

	// espera pelo mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	// verifica se existe um aeroporto com o mesmo nome
	for (int i = 0; i < map->numAirports; i++)
	{
		if (map->airports[i].position.x == position.x && map->airports[i].position.y == position.y)
		{
			isEmpty = FALSE;
			break;
		}
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	return isEmpty;
}

/*
* isAnotherAirportAround()
* -------------
* Verifica se h� um aeroporto a menos de 10 posi��es
*/
BOOL isAnotherAirportAround(map_data* map, airport newAirport)
{
	BOOL airportAround = FALSE;
	int x = newAirport.position.x;	// posi��o do aeroporto a ser criado
	int y = newAirport.position.y;	// posi��o do aeroporto a ser criado
	int num_posicoes;				// n�mero de posi��es a percorrer no eixo X
	int num_linha = 1;				// indica a linha atual no eixo Y
	int step;						// n�mero de posi��es a percorrer em cada sentido no eixo X

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);


	for (int yy = y - DISTANCE; yy <= y; yy++)
	{
		num_posicoes = num_linha * 2 - 1;	// obtem o n�mero de posi��es a percorrer no eixo X

		if (num_posicoes == 1)
			step = 0;
		else
			step = num_posicoes / 2;

		for (int xx = x - step; xx <= x + step; xx++)
		{
			// verifica se xx e yy s�o posi��es validas
			if (yy >= 0 && yy < MAP_SIZE && xx >= 0 && xx < MAP_SIZE)
			{
				// verifica se h� um aeroporto nesse raio
				for (int k = 0; k < map->numAirports; k++)
				{
					if (map->airports->position.x == xx && map->airports->position.y == yy)
					{
						airportAround = TRUE;
						break;
					}
				}
				if (airportAround)
					break;
			}
		}
		if (airportAround)
			break;

		num_linha++;
	}

	if (!airportAround)
	{
		num_linha = 1;
		for (int yy = y + DISTANCE; yy > y; yy--)
		{
			num_posicoes = num_linha * 2 - 1;	// obtem o n�mero de posi��es a percorrer no eixo X

			if (num_posicoes == 1)
				step = 0;
			else
				step = num_posicoes / 2;

			for (int xx = x - step; xx <= x + step; xx++)
			{
				// verifica se xx e yy s�o posi��es validas
				if (yy >= 0 && yy < MAP_SIZE && xx >= 0 && xx < MAP_SIZE)
				{
					// verifica se h� um aeroporto nesse raio
					for (int k = 0; k < map->numAirports; k++)
					{
						if (map->airports->position.x == xx && map->airports->position.y == yy)
						{
							airportAround = TRUE;
							break;
						}
					}
					if (airportAround)
						break;
				}
			}
			if (airportAround)
				break;

			num_linha++;
		}
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	return airportAround;
}

/*
* addAirport()
* -------------
* Adiciona o aeroporto no array de aeroportos
*/
void addAirport(map_data* map, airport* airport)
{
	int index; 
	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	index = map->numAirports;
	_tcscpy_s(map->airports[index].name, BUFFERSMALL, airport->name);
	map->airports[index].position.x = airport->position.x;
	map->airports[index].position.y = airport->position.y;
	map->numAirports++;

	// liberta o mutex
	ReleaseMutex(map->hMutex);
}

/*
* printAirportsData()
* -------------
* Imprime os dados dos aeroportos
*/
void printAirportsData(map_data* map)
{
	airport* airports = map->airports;	// ponteiro para o array de aeroportos
	int count = map->numAirports;		// n�mero de aeroportos existentes

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	_tprintf(TEXT("\nAeroportos\n"));

	_tprintf(TEXT("%10s\t%10s\n"), TEXT("Nome"), TEXT("Posi��o"));
	for (int i = 0; i < count; i++)
	{
		_tprintf(TEXT("%10s\t[%4d,%4d]\n"), airports[i].name, airports[i].position.x, airports[i].position.y);
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);
}

/*
* printPlanesData()
* -------------
* Imprime os dados dos avi�es
*/
void printPlanesData(map_data* map)
{
	plane* planes = map->planes;		// ponteiro para o array de avi�es
	int count = map->numPlanes;			// n�mero de avi�es existentes

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	_tprintf(TEXT("\nAvi�es\n"));

	_tprintf(TEXT("%5s\t%6s\t%6s\t%10s\t%10s\t%10s\t%10s\t%10s\t%6s\n"), TEXT("PID"), TEXT("Capac."), TEXT("Speed"), TEXT("Origem"), TEXT("Destino"), TEXT("Pos. Atual"), TEXT("Pos.Destino"), TEXT("N�m. Passag."), TEXT("Estado"));
	for (int i = 0; i < count; i++)
		_tprintf(TEXT("%5d\t%6d\t%6d\t%10s\t%10s\t[%4d,%4d]\t[%4d,%4d]\t%10d\t%6d\n"), planes[i].pid, planes[i].capacity, planes[i].speed, planes[i].origin, planes[i].destination, planes[i].pos_actual.x, planes[i].pos_actual.y, planes[i].pos_destination.x, planes[i].pos_destination.y, planes[i].num_passenger, planes[i].state);

	
	// liberta o mutex
	ReleaseMutex(map->hMutex);
}

/*
* printPassengersData()
* -------------
* Imprime os dados dos avi�es
*/
void printPassengersData(map_data* map)
{
	passenger* passengers = map->passengers;	// ponteiro para o array de passageiros
	int count = map->numPassengers;				// n�mero de passageiros existentes

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	_tprintf(TEXT("\nPassageiros\n"));

	_tprintf(TEXT("%5s\t%10s\t%10s\t%10s\t%6s\t%10s\t%10s\n"), TEXT("PID"), TEXT("Nome"), TEXT("Origem"), TEXT("Destino"), TEXT("Estado"), TEXT("Tempo Esp."), TEXT("Avi�o"));
	for (int i = 0; i < count; i++)
		_tprintf(TEXT("%5d\t%10s\t%10s\t%10s\t%6d\t%10d\t%10d\n"), passengers[i].pid, passengers[i].name, passengers[i].origin, passengers[i].destination, passengers[i].state, passengers[i].waiting_time, passengers[i].pidPlane);


	// liberta o mutex
	ReleaseMutex(map->hMutex);
}


/*
* threadReadSharedMemory()
* -------------
* Thread que permite ler da mem�ria partilhada
*/
DWORD WINAPI threadReadSharedMemory(LPVOID args)
{
	thread_data* data = (thread_data*)args;
	sync_circular_buffer sync = data->sync_circular_buffer;
	item itemParaConsumir;		

	while (1)
	{
		// espera que haja um item para ser consumido
		WaitForSingleObject(sync.semaphore_items, INFINITE);

		// espera pelo mutex do consumidor
		WaitForSingleObject(sync.semaphore_mutex_consumer, INFINITE);

		// obtem o item a consumir
		itemParaConsumir = data->ptrSharedMemory->buffer[data->ptrSharedMemory->out];

		// verifica se � o controlador que tem de consumir
		if (itemParaConsumir.pidReceiver == 0)
		{
			// incrementa a posi��o do pr�ximo consumo
			data->ptrSharedMemory->out = (data->ptrSharedMemory->out + 1) % DIM;

			// liberta o mutex do consumidor
			ReleaseSemaphore(sync.semaphore_mutex_consumer, 1, NULL);

			// incrementa o n�mero de itens por produzir
			ReleaseSemaphore(sync.semaphore_empty, 1, NULL);

			// trata o pedido do avi�o
			handleCommandFromPlane(data, itemParaConsumir);
		}
		else
		{
			// liberta o mutex do consumidor
			ReleaseSemaphore(sync.semaphore_mutex_consumer, 1, NULL);

			// incrementa o n�mero de itens por consumir
			ReleaseSemaphore(sync.semaphore_items, 1, NULL);
		}

	}

	return 0;
}


/*
* threadStillAlive()
* -------------
* Thread que verifica se os processos avi�o ainda est�o em execu��o
*/
DWORD WINAPI threadStillAlive(LPVOID args)
{
	thread_data* data = (thread_data*)args;
	DWORD pProcessIds[2048] = { 0 };
	BOOL bEnumProcesses = FALSE, bInExecution = FALSE;
	DWORD bytesNeeded = 0;
	DWORD numProcesses = 0;

	// limpa o lixo
	ZeroMemory(&pProcessIds, sizeof(DWORD) * 2048);

	while (1)
	{
		// espera o tempo definido
		Sleep(WAITING_TIME);

		// enumera��o dos processos em execu��o
		bEnumProcesses = EnumProcesses(
			pProcessIds,			// ponteiro para um array que recebe a lista de pids
			sizeof(pProcessIds),	// tamanho do array de pids em bytes
			&bytesNeeded);			// n�mero de bytes devolvido no array de pids

		if (!bEnumProcesses)
			_tprintf(TEXT("A enumera��o dos processos falhou!\n"));

		// calcula o n�mero de processos em execu��o
		numProcesses = bytesNeeded / sizeof(DWORD);

		// entra no mutex
		WaitForSingleObject(data->map.hMutex, INFINITE);

		// verifica se os processos avi�o ainda est�o em execu��o
		for (int i = 0; i < data->map.numPlanes; i++)
		{
			bInExecution = FALSE;
			for (DWORD j = 0; j < numProcesses; j++)
			{
				// o processo avi�o ainda est� em execu��o
				if (data->map.planes[i].pid == pProcessIds[j])
				{
					bInExecution = TRUE;
					break;
				}
			}
			if (!bInExecution)
			{
				_tprintf(TEXT("O avi�o [%d] terminou a sua execu��o!\n"), data->map.planes[i].pid);
				if (removePlane(data, data->map.planes[i].pid))
					_tprintf(TEXT("O avi�o [%d] foi removido com sucesso!\n"), data->map.planes[i].pid);
				else
					_tprintf(TEXT("Erro na remo��o do avi�o [%d]!\n"), data->map.planes[i].pid);
			}
		}


		// liberta o mutex
		ReleaseMutex(data->map.hMutex);
	}

	return 0;
}

/*
* handleCommandFromPlane()
* -------------
* Trata os comandos recebidos por parte dos avi�es
*/
void handleCommandFromPlane(thread_data* data, item itemParaConsumir)
{
	TCHAR cmd[BUFFERSIZE];
	_tcscpy_s(cmd, BUFFERSIZE, itemParaConsumir.command);

	// trata o comando recebido
	if (_tcscmp(cmd, CMD_IDENTIFICATION) == 0)
	{
		// adiciona um avi�o
		cmdAddPlane(data, &itemParaConsumir);
	}
	else if (_tcscmp(cmd, CMD_DEFINE) == 0)
	{
		// define o aeroporto de destino
		cmdDefine(data, &itemParaConsumir);
	}
	else if (_tcscmp(cmd, CMD_START_FLIGHT) == 0)
	{
		// inicia a movimenta��o do avi�o
		cmdStartFlight(data, &itemParaConsumir);
	}
	else if (_tcscmp(cmd, CMD_BOARDING) == 0)
	{
		// embarca os passageiros
		cmdBoarding(data, &itemParaConsumir);
	}
	else if (_tcscmp(cmd, CMD_END) == 0)
	{
		// remove o avi�o do sistema
		removePlane(data, itemParaConsumir.plane.pid);
	}
}

/*
* cmdAddPlane()
* -------------
* Adiciona um avi�o no sistema
*/
void cmdAddPlane(thread_data* data, item* itemParaConsumir)
{
	item itemParaProduzir;	// item que vai ser enviado como resposta ao aviao

	// limpa o lixo
	ZeroMemory(&itemParaProduzir, sizeof(item));

	// preenche os dados do item a ser produzido
	itemParaProduzir.pidSender = 0;
	itemParaProduzir.pidReceiver = itemParaConsumir->pidSender;
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, INVALID_IDENTIFICATION);

	// verifica se h� espa�o para o novo avi�o
	if (isPlanesFull(&data->map))
	{
		_tprintf(TEXT("N�o h� espa�o para adicionar o avi�o [%d].\n"), itemParaConsumir->plane.pid);
		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}

	// verifica se o aeroporto de origem do novo avi�o existe
	if (!isAirportNameRepeated(&data->map, itemParaConsumir->plane.origin))
	{
		_tprintf(TEXT("O aeroporto de origem [%s] do avi�o [%d] n�o existe.\n"), itemParaConsumir->plane.origin, itemParaConsumir->plane.pid);
		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}
	
	// preenche os dados do avi�o
	itemParaProduzir.plane.pid = itemParaConsumir->plane.pid;
	itemParaProduzir.plane.capacity = itemParaConsumir->plane.capacity;
	itemParaProduzir.plane.num_passenger = itemParaConsumir->plane.num_passenger;
	itemParaProduzir.plane.speed = itemParaConsumir->plane.speed;
	_tcscpy_s(itemParaProduzir.plane.origin, BUFFERSMALL, itemParaConsumir->plane.origin);
	_tcscpy_s(itemParaProduzir.plane.destination, BUFFERSMALL, UNDEFINED);
	itemParaProduzir.plane.pos_actual = getPositionOfAirport(&data->map, itemParaProduzir.plane.origin);
	itemParaProduzir.plane.pos_destination.x = itemParaProduzir.plane.pos_destination.y = -1;
	itemParaProduzir.plane.state = itemParaConsumir->plane.state;

	// adiciona o avi�o no sistema
	addPlane(&data->map, &itemParaProduzir.plane);
	_tprintf(TEXT("O avi�o [%d] foi adicionado com sucesso!\n"), itemParaProduzir.plane.pid);

	// escreve na mem�ria partilhada
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, VALID_IDENTIFICATION);
	writeInSharedMemory(data, &itemParaProduzir);
}

/*
* cmdDefine()
* -------------
* Define o aeroporto de destino
*/
void cmdDefine(thread_data* data, item* itemParaConsumir)
{
	item itemParaProduzir;	// item que vai ser enviado como resposta ao aviao

	// limpa o lixo
	ZeroMemory(&itemParaProduzir, sizeof(item));

	// preenche os dados do item a ser produzido
	itemParaProduzir.pidSender = 0;
	itemParaProduzir.pidReceiver = itemParaConsumir->pidSender;
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, INVALID_DESTINATION);
	itemParaProduzir.plane = itemParaConsumir->plane;

	// verifica se o aeroporto de destino existe
	if (!isAirportNameRepeated(&data->map, itemParaProduzir.plane.destination))
	{
		_tprintf(TEXT("A defini��o do aeroporto de destino do avi�o [%d] falhou porque o aeroporto de destino [%s] n�o existe!\n"), itemParaProduzir.plane.pid, itemParaProduzir.plane.destination);

		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}

	// verifica se o aeroporto de destino � igual ao aeroporto de origem do avi�o
	if (_tcscmp(itemParaProduzir.plane.origin, itemParaProduzir.plane.destination) == 0)
	{
		_tprintf(TEXT("A defini��o do aeroporto de destino do avi�o [%d] falhou porque � igual ao aeroporto de origem!\n"), itemParaProduzir.plane.pid);
		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}

	// atualiza os dados do avi�o
	updatePlane(data, &itemParaProduzir.plane);
	_tprintf(TEXT("O avi�o [%d] atualizou o destino para [%s]!\n"), itemParaProduzir.plane.pid, itemParaProduzir.plane.destination);

	// escreve na mem�ria partilhada
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, VALID_DESTINATION);
	itemParaProduzir.plane.pos_destination = getPositionOfAirport(&data->map, itemParaProduzir.plane.destination);
	writeInSharedMemory(data, &itemParaProduzir);
}

/*
* cmdStartFlight()
* -------------
* Inicia o voo
*/
void cmdStartFlight(thread_data* data, item* itemParaConsumir)
{
	item itemParaProduzir;		// item que vai ser enviado como resposta ao aviao
	item_pipe itemParaPassageiro;	// item enviado ao passageiro

	// posi��o destino do avi�o
	position pos_destination = getPositionOfAirport(&data->map, itemParaConsumir->plane.destination);
	position pos_temp;	// caso haja um obstaculo sera calculada outra posi��o

	_tprintf(TEXT("\nO avi�o [%d] movimenta��o [%d] est� na posi��o [%d,%d] e a posi��o calculada � [%d,%d].\n"), itemParaConsumir->pidSender, itemParaConsumir->movementResult, itemParaConsumir->plane.pos_actual.x, itemParaConsumir->plane.pos_actual.y, itemParaConsumir->plane.pos_destination.x, itemParaConsumir->plane.pos_destination.y);

	// preenche os dados do item a ser produzido
	itemParaProduzir.pidSender = 0;
	itemParaProduzir.pidReceiver = itemParaConsumir->pidSender;
	itemParaProduzir.plane = itemParaConsumir->plane;
	/*_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, INVALID_MOVEMENT);
	itemParaProduzir.plane = itemParaConsumir->plane;*/

	// verifica se a posi��o calculada n�p colide com outros avi�es em voo
	if (itemParaConsumir->movementResult == 1)
	{
		// se n�o for uma posi��o v�lida, desvia-se dessa posi��o
		if (!isValidPosition(&data->map, itemParaConsumir->plane.pos_destination))
		{
			// tenta virar para a esquerda ou para a direita
			if (itemParaConsumir->plane.pos_destination.x > 0)
				pos_temp.x = itemParaConsumir->plane.pos_destination.x - 1; // vira para a esquerda
			else if (itemParaConsumir->plane.pos_destination.x < MAP_SIZE - 1)
				pos_temp.x = itemParaConsumir->plane.pos_destination.x + 1; // vira para a direita
			pos_temp.y = itemParaConsumir->plane.pos_destination.y;
			// verifica se a nova posi��o calculada � v�lida
			if (isValidPosition(&data->map, pos_temp))
			{
				_tprintf(TEXT("O avi�o [%d] desviou-se para a posi��o [%d,%d].\n"), itemParaConsumir->pidSender, pos_temp.x, pos_temp.y);
				itemParaProduzir.plane.pos_destination = pos_temp;
			}
			else
			{
				// tenta virar para cima ou para baixo
				if (itemParaConsumir->plane.pos_destination.y > 0)
					pos_temp.y = itemParaConsumir->plane.pos_destination.y - 1; // vira para cima
				else if (itemParaConsumir->plane.pos_destination.y < MAP_SIZE - 1)
					pos_temp.y = itemParaConsumir->plane.pos_destination.y + 1; // vira para baixo
				pos_temp.x = itemParaConsumir->plane.pos_destination.x;
				// verifica se a nova posi��o calculada � v�lida
				if (isValidPosition(&data->map, pos_temp))
				{
					_tprintf(TEXT("O avi�o [%d] desviou-se para a posi��o [%d,%d].\n"), itemParaConsumir->pidSender, pos_temp.x, pos_temp.y);
					itemParaProduzir.plane.pos_destination = pos_temp;
				}
				else
				{
					itemParaConsumir->movementResult = 2; // ocorreu um erro e fica na mesma posi��o
				}
			}

		}
	}

	
	if (itemParaConsumir->movementResult == 0)
	{
		// o avi�o chegou ao destino
		_tcscpy_s(itemParaProduzir.plane.origin, BUFFERSMALL, itemParaProduzir.plane.destination);
		itemParaProduzir.plane.pos_actual = pos_destination;
		itemParaProduzir.plane.pos_destination.x = itemParaProduzir.plane.pos_destination.y = -1;
		itemParaProduzir.plane.state = GROUND;
		itemParaProduzir.plane.num_passenger = 0;
		_tcscpy_s(itemParaProduzir.plane.destination, BUFFERSMALL, UNDEFINED);		
		_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, ARRIVED_AT_DESTINATION);
		_tprintf(TEXT("O avi�o [%d] chegou ao destino.\n"), itemParaProduzir.plane.pid);

		// informa o passageiro que chegou ao destino
		_tcscpy_s(itemParaPassageiro.command, BUFFERSIZE, ARRIVED_AT_DESTINATION);
		_stprintf_s(itemParaPassageiro.extra, BUFFERSIZE, TEXT("Chegou ao destino."));

		// escreve no pipe do passageiro
		for (int i = 0; i < data->map.numPassengers; i++)
		{
			if (data->map.passengers[i].pidPlane == itemParaProduzir.plane.pid)
			{
				sendMessageToPassenger(&data->pipe[i], &itemParaPassageiro);
				//removePassenger(&data->map, i);
			}
		}
	}
	else if (itemParaConsumir->movementResult == 1)
	{
		// boa movimenta��o
		itemParaProduzir.plane.pos_actual = itemParaProduzir.plane.pos_destination;
		itemParaProduzir.plane.pos_destination = pos_destination;
		itemParaProduzir.plane.state = FLYING;
		_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, VALID_MOVEMENT);
		//_tprintf(TEXT("O avi�o [%d] efetuou uma boa movimenta��o.\n"), itemParaProduzir.plane.pid);

		// informa o passageiro que chegou ao destino
		_tcscpy_s(itemParaPassageiro.command, BUFFERSIZE, VALID_MOVEMENT);
		_stprintf_s(itemParaPassageiro.extra, BUFFERSIZE, TEXT("Posi��o atual [%d,%d]"), itemParaProduzir.plane.pos_actual.x, itemParaProduzir.plane.pos_actual.y);

		// escreve no pipe do passageiro
		for (int i = 0; i < data->map.numPassengers; i++)
		{
			if (data->map.passengers[i].pidPlane == itemParaProduzir.plane.pid)
			{
				sendMessageToPassenger(&data->pipe[i], &itemParaPassageiro);
			}
		}

		// atualiza o n�mero de passageiros para 0
		for (int i = 0; i < data->map.numPlanes; i++)
		{
			if (data->map.planes[i].pid == itemParaProduzir.plane.pid)
			{
				data->map.planes[i].num_passenger = 0;
				break;
			}
		}
		

	}
	else if (itemParaConsumir->movementResult == 2)
	{
		// ocorreu um erro
		itemParaProduzir.plane.pos_destination = pos_destination;

		_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, INVALID_MOVEMENT);
	}

	// atualiza a posi��o do avi�o
	updatePlanePositions(data, &itemParaProduzir.plane);

	// escreve o item produzido no buffer circular
	writeInSharedMemory(data, &itemParaProduzir);
}

/*
* cmdBoarding()
* -------------
* Embarca os passageiros
*/
void cmdBoarding(thread_data* data, item* itemParaConsumir)
{
	item itemParaProduzir;	// item que vai ser enviado como resposta ao aviao
	plane* planes = data->map.planes;	// ponteiro para o array de avi�es
	passenger* passengers = data->map.passengers;	// ponteiro para o array de passageiros
	item_pipe itemParaPassageiro = { VALID_BOARDING, TEXT("") };

	// limpa o lixo
	ZeroMemory(&itemParaProduzir, sizeof(item));

	// preenche os dados do item a ser produzido
	itemParaProduzir.pidSender = 0;
	itemParaProduzir.pidReceiver = itemParaConsumir->pidSender;
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, INVALID_BOARDING);
	itemParaProduzir.plane = itemParaConsumir->plane;

	// verifica se o aeroporto de destino do avi�o foi definido
	if (_tcscmp(itemParaConsumir->plane.destination, UNDEFINED) == 0)
	{
		_tprintf(TEXT("N�o foi poss�vel embarcar os passageiros no avi�o [%d] porque o destino n�o foi definido!\n"), itemParaConsumir->plane.pid);
		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}

	// entra no mutex
	WaitForSingleObject(data->map.hMutex, INFINITE);

	// embarca os passageiros
	for (int i = 0; i < data->map.numPassengers; i++)
	{
		// verifica se os aeroportos coincidem
		if ((_tcscmp(passengers[i].origin, itemParaProduzir.plane.origin) == 0) && (_tcscmp(passengers[i].destination, itemParaProduzir.plane.destination) == 0))
		{
			// verifica se h� espa�o no avi�o
			if (itemParaProduzir.plane.num_passenger >= itemParaProduzir.plane.capacity)
				break;

			// incrementa o n�mero de passageiros embarcados no avi�o
			itemParaProduzir.plane.num_passenger++;
			for (int j = 0; j < data->map.numPlanes; i++)
			{
				if (planes[j].pid == itemParaProduzir.plane.pid)
				{
					planes[j].num_passenger++;
					break;
				}
			}

			// altera o estado do passageiro
			passengers[i].state = FLYING;
			passengers[i].pidPlane = itemParaProduzir.plane.pid;

			// envia a mensagem ao passageiro
			_stprintf_s(itemParaPassageiro.extra, BUFFERSIZE, TEXT("%d"), itemParaProduzir.plane.pid);
			sendMessageToPassenger(&data->pipe[i], &itemParaPassageiro);

			_tprintf(TEXT("O passageiro [%s] embarcou no avi�o [%d] com origem [%s] e destino [%s].\n"), passengers[i].name, itemParaProduzir.plane.pid, itemParaProduzir.plane.origin, itemParaProduzir.plane.destination);

		}
	}

	// liberta o mutex
	ReleaseMutex(data->map.hMutex);

	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, VALID_BOARDING);
	writeInSharedMemory(data, &itemParaProduzir);
}

/*
* writeInSharedMemory()
* -------------------
* Escreve na mem�ria partilhada
*/
void writeInSharedMemory(thread_data* data, item* item)
{
	sync_circular_buffer sync = data->sync_circular_buffer;

	// espera que haja espa�o para produzir
	WaitForSingleObject(sync.semaphore_empty, INFINITE);

	// espera pelo mutex do produtor
	WaitForSingleObject(sync.semaphore_mutex_producer, INFINITE);

	// escreve o item na mem�ria partilhada
	data->ptrSharedMemory->buffer[data->ptrSharedMemory->in] = *item;

	// incrementa a posi��o da pr�xima produ��o
	data->ptrSharedMemory->in = (data->ptrSharedMemory->in + 1) % DIM;

	// liberta o mutex do produtor
	ReleaseSemaphore(sync.semaphore_mutex_producer, 1, NULL);

	// liberta o n�mero de itens por consumir
	ReleaseSemaphore(sync.semaphore_items, 1, NULL);
}

/*
* isPlanesFull()
* -------------------
* Verifica se o sistema j� atingiu o n�mero m�ximo de avi�es
*/
BOOL isPlanesFull(map_data* map)
{
	BOOL isFull = FALSE;

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	// verifica se h� espa�o para o avi�o
	if (map->numPlanes >= map->maxPlanes)
		isFull = TRUE;

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	return isFull;
}

/*
* getPositionOfAirport()
* -------------------
* Devolve a posi��o do aeroporto pelo nome
*/
position getPositionOfAirport(map_data* map, TCHAR* name)
{
	airport* airports = map->airports;	// ponteiro para o array de aeroportos
	position position = { -1, -1 };

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	for (int i = 0; i < map->numAirports; i++)
	{
		if (_tcscmp(airports[i].name, name) == 0)
		{
			position = airports[i].position;
			break;
		}
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	return position;
}

/*
* addPlane()
* -------------------
* Adiciona o avi�o no sistema
*/
void addPlane(map_data* map, plane* p)
{
	int index;

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	// index onde deve inserir o avi�o
	index = map->numPlanes;

	// adiciona o avi�o
	map->planes[index].pid = p->pid;
	map->planes[index].capacity = p->capacity;
	map->planes[index].num_passenger = p->num_passenger;
	map->planes[index].speed = p->speed;
	_tcscpy_s(map->planes[index].origin, BUFFERSMALL, p->origin);
	_tcscpy_s(map->planes[index].destination, BUFFERSMALL, p->destination);
	map->planes[index].pos_actual = p->pos_actual;
	map->planes[index].pos_destination = p->pos_destination;
	map->planes[index].state = p->state;

	// incrementa o n�mero de avi�es inseridos no sistema
	map->numPlanes++;	

	// liberta o mutex
	ReleaseMutex(map->hMutex);
}

/*
* removePlane()
* -------------------
* Remove o avi�o no sistema
*/
BOOL removePlane(thread_data* data, int pid)
{
	int index = -1;

	// entra no mutex
	WaitForSingleObject(data->map.hMutex, INFINITE);

	// index a remover
	for (int i = 0; i < data->map.numPlanes; i++)
	{
		if (data->map.planes[i].pid == pid)
		{
			index = i;
			break;
		}
	}

	// verifica se o index � v�lido
	if (index < 0 || index > data->map.numPlanes - 1)
	{
		// liberta o mutex
		ReleaseMutex(data->map.hMutex);
		return FALSE;
	}

	// mensagem
	if (data->map.planes[index].state == FLYING)
		_tprintf(TEXT("O avi�o [%d] teve um acidente durante o voo!\n"), pid);
	else
		_tprintf(TEXT("O piloto do avi�o [%d] reformou-se!\n"), pid);

	// reorganiza o array de avi�es
	for (int i = index; i < data->map.numPlanes - 1; i++)
		data->map.planes[i] = data->map.planes[i + 1];

	// decrementa o n�mero de avi�es
	data->map.numPlanes--;

	// liberta o mutex
	ReleaseMutex(data->map.hMutex);

	// liberta uma posi��o do sem�foro que contabiliza o n�mero de avi�es em execu��o
	ReleaseSemaphore(data->sync_plane_access.semaphore_access, 1, NULL);

	return TRUE;
}

/*
* updatePlane()
* -------------------
* Atualiza os dados do avi�o
*/
void updatePlane(thread_data* data, plane* p)
{
	// obtem a posi��o do aeroporto de destino
	position destination = getPositionOfAirport(&data->map, p->destination);

	// entra no mutex
	WaitForSingleObject(data->map.hMutex, INFINITE);

	for (int i = 0; i < data->map.numPlanes; i++)
	{
		if (data->map.planes[i].pid == p->pid)
		{
			_tcscpy_s(data->map.planes[i].destination, BUFFERSMALL, p->destination);
			data->map.planes[i].pos_destination = destination;
			break;
		}
	}

	// liberta o mutex
	ReleaseMutex(data->map.hMutex);
}

/*
* updatePlanePositions()
* -------------------
* Atualiza os aeroportos de origem e destino e as posi��es dos mesmos e o estado
*/
void updatePlanePositions(thread_data* data, plane* p)
{
	// entra no mutex
	WaitForSingleObject(data->map.hMutex, INFINITE);

	for (int i = 0; i < data->map.numPlanes; i++)
	{
		if (data->map.planes[i].pid == p->pid)
		{
			_tcscpy_s(data->map.planes[i].origin, BUFFERSMALL, p->origin);
			_tcscpy_s(data->map.planes[i].destination, BUFFERSMALL, p->destination);
			data->map.planes[i].pos_actual = p->pos_actual;
			data->map.planes[i].pos_destination = p->pos_destination;
			data->map.planes[i].state = p->state;
			break;
		}
	}

	// liberta o mutex
	ReleaseMutex(data->map.hMutex);
}

/*
* isValidPosition()
* -------------------
* Verifica se a posi��o est� vazia
*/
BOOL isValidPosition(map_data* map, position p)
{
	BOOL isValid = TRUE;

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	for (int i = 0; i < map->numPlanes; i++)
	{
		// verifica se existe outro avi�o em voo nessa posi��o
		if (map->planes[i].pos_actual.x == p.x && map->planes[i].pos_actual.y == p.y)
		{
			if (map->planes[i].state == FLYING)
				isValid = FALSE;
			break;
		}			
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	return isValid;
}

/*
* isValidPosition()
* -------------------
* Inicializa as inst�ncias do pipe
*/
BOOL initPipeInstances(thread_data* data)
{
	// aloca��o de mem�ria para o pipe
	data->pipe = (PIPE_INSTANCE*)malloc(MAX_PASSAG * sizeof(PIPE_INSTANCE));

	// verifica se a aloca��o de mem�ria deu certo
	if (data->pipe == NULL)
		return FALSE;

	// limpa o lixo
	ZeroMemory(data->pipe, MAX_PASSAG * sizeof(PIPE_INSTANCE));

	PIPE_INSTANCE* pipe = data->pipe;	// ponteiro para o array de inst�ncias do pipe
	
	// criar v�rias inst�ncias do pipe com o objeto evento
	// uma opera��o overlapped ConnectNamedPipe � iniciada para cada inst�ncia
	for (int i = 0; i < MAX_PASSAG; i++)
	{
		// cria o objeto evento
		data->hEvents[i] = CreateEvent(
			NULL,	// default security attributes
			TRUE,	// manual-reset event
			TRUE,	// initial state is signaled
			NULL);

		if (data->hEvents[i] == NULL)
		{
			_tprintf(TEXT("Erro na cria��o do objeto evento para uma inst�ncia de pipe.\n"));
			return FALSE;
		}

		// inicializa a estrutura overlapped
		pipe[i].overlap.hEvent = data->hEvents[i];

		// cria o pipe
		pipe[i].hPipeInstance = CreateNamedPipe(
			PIPE_NAME,				// pipe name
			PIPE_ACCESS_DUPLEX |	// read/write access
			FILE_FLAG_OVERLAPPED,	// overlapped mode
			PIPE_TYPE_MESSAGE |		// message-type mode
			PIPE_READMODE_MESSAGE |	// message-read mode
			PIPE_WAIT,				// blocking mode
			MAX_PASSAG,				// number of instances
			sizeof(item_pipe),		// output buffer size
			sizeof(item_pipe),		// input buffer size
			PIPE_TIMEOUT,			// client time-out
			NULL);

		if (pipe[i].hPipeInstance == INVALID_HANDLE_VALUE)
		{
			_tprintf(TEXT("Erro na cria��o da inst�ncia do pipe.\n"));
			return FALSE;
		}

		// chama a subrotina para efetuar a liga��o com um novo cliente
		// em princ�cio devolve sempre true
		pipe[i].fPendingIO = ConnectToNewClient(pipe[i].hPipeInstance, &pipe[i].overlap);

		if (pipe[i].fPendingIO)
		{
			//_tprintf(TEXT("O pipe [%d] est� dispon�vel para efetuar uma liga��o com um cliente...\n"), i);
			pipe[i].dwState = CONNECTING_STATE;
		}
		else
		{
			_tprintf(TEXT("O pipe [%d] est� dispon�vel para ler o pedido do cliente...\n"), i);
			pipe[i].dwState = READING_STATE;
		}

	}

	return TRUE;
}

/*
* ConnectToNewClient()
* -------------------
* Inicia uma opera��o de liga��o overlapped
* Devolve TRUE se a opera��o estiver pendente ou FALSE se a opera��o foi conclu�da
*/
BOOL ConnectToNewClient(HANDLE hPipe, OVERLAPPED* overlap)
{
	BOOL fConnect, fPendingIO = FALSE;

	// inicia uma opera��o de liga��o  overlapped
	fConnect = ConnectNamedPipe(hPipe, overlap);

	// devolve FALSE por norma
	if (fConnect)
		_tprintf(TEXT("Houve um erro no ConnectNamedPipe.\n"));

	switch (GetLastError())
	{
		// a opera��o overlapped est� em progresso
	case ERROR_IO_PENDING:
		fPendingIO = TRUE;
		break;

		// o cliente j� efetuou a liga��o, entao o objeto evento deve ser assinalado
	case ERROR_PIPE_CONNECTED:
		if (SetEvent(overlap->hEvent))
			break;

		// erro durante o processo de liga��o
	default:
		_tprintf(TEXT("Houve um erro no ConnectNamedPipe.\n"));
		return FALSE;
	}

	return fPendingIO;
}

/*
* threadReadPipeInstances()
* -------------------
* L� os pedidos dos passageiros nos pipes
*/
DWORD WINAPI threadReadPipeInstances(LPVOID args)
{
	thread_data* data = (thread_data*)args;
	PIPE_INSTANCE* pipe = data->pipe;		// ponteiro para o array de inst�ncias do pipe
	DWORD dwWait, index, cbRead, dwErr;
	BOOL fSuccess;

	while (1)
	{
		// espera que um objeto evento seja assinalado, indicando que 
		// uma opera��o overlapped de leitura, escrita ou liga��o foi conclu�da
		dwWait = WaitForMultipleObjects(
			MAX_PASSAG,		// number of event objects
			data->hEvents,	// array of event objects
			FALSE,			// does not wait for all
			INFINITE);		// wait indefinitely

		// dwWait mostra qual pipe concluiu a opera��o
		index = dwWait - WAIT_OBJECT_0;

		// verifica se o index � v�lido
		if (index < 0 || index > MAX_PASSAG - 1)
		{
			_tprintf(TEXT("Erro durante a leitura do pedido de um passageiro.\nO index n�o � v�lido!\n"));
			break;
		}

		// obtem o estado do pipe se a opera��o estava pendente
		if (pipe[index].fPendingIO)
		{
			// devolve o resultado de uma opera��o overlapped no pipe espec�fico
			fSuccess = GetOverlappedResult(
				pipe[index].hPipeInstance,	// handle to pipe
				&pipe[index].overlap,		// overlapped structure
				&cbRead,					// bytes transferred
				FALSE);						// do not wait

			switch (pipe[index].dwState)
			{
				// opera��o de liga��o pendente
			case CONNECTING_STATE:
				if (fSuccess)
				{
					_tprintf(TEXT("Um cliente ligou-se ao pipe [%d].\n"), index);
					pipe[index].dwState = READING_STATE;
					break;
				}
				else
				{
					_tprintf(TEXT("Erro durante a opera��o de liga��o no pipe [%d].\n"), index);
					return 0;
				}

				// opera��o de leitura pendente
			case READING_STATE:
				if (fSuccess || cbRead > 0)
				{
					_tprintf(TEXT("O pedido do pipe [%d] �: [%s].\n"), index, pipe[index].request.command);
					pipe[index].cbReadRequest = cbRead;
					pipe[index].dwState = WRITING_STATE;
					break;
				}
				else
				{
					//_tprintf(TEXT("Erro durante a opera��o de leitura no pipe [%d].\n"), index);
					removePassenger(&data->map, index);
					DisconnectAndReconnect(data->pipe, index);
					continue;
				}

				// opera��o de escrita pendente
			case WRITING_STATE:
				if (fSuccess && cbRead == pipe[index].cbReadRequest)
				{
					pipe[index].dwState = READING_STATE;
					break;
				}
				else
				{
					_tprintf(TEXT("Erro durante a opera��o de escrita no pipe [%d].\n"), index);
					removePassenger(&data->map, index);
					DisconnectAndReconnect(data->pipe, index);
					continue;
				}

				// default
			default:
				_tprintf(TEXT("O estado do pipe [%d] � inv�lido.\n"), index);
				return 0;
			}
		}

		// o estado do pipe determina qual � a pr�xima opera��o a efetuar
		switch (pipe[index].dwState)
		{
			// a inst�ncia do pipe est� ligada ao passageiro e pronta para ler o pedido do cliente
		case READING_STATE:
			fSuccess = ReadFile(
				pipe[index].hPipeInstance,
				&pipe[index].request,
				sizeof(item_pipe),
				&pipe[index].cbReadRequest,
				&pipe[index].overlap);

			// a opera��o de leitura foi conclu�da com sucesso
			if (fSuccess && pipe[index].cbReadRequest != 0)
			{
				_tprintf(TEXT("O pedido do pipe [%d] �: [%s].\n"), index, pipe[index].request.command);
				pipe[index].fPendingIO = FALSE;
				pipe[index].dwState = WRITING_STATE;
				continue;
			}

			// a opera��o de leitura ainda est� pendente
			dwErr = GetLastError();
			if (!fSuccess && (dwErr == ERROR_IO_PENDING))
			{
				//_tprintf(TEXT("O pedido do pipe #%d ainda est� pendente...\n"), index);
				pipe[index].fPendingIO = TRUE;
				continue;
			}

			// um erro aconteceu, o cliente deve ser desligado
			removePassenger(&data->map, index);
			DisconnectAndReconnect(data->pipe, index);
			break;

			// o pedido do cliente foi lido com sucesso, o servidor est� pronto para enviar uma resposta
		case WRITING_STATE:
			getAnswerForPassenger(data, &pipe[index], index);
			fSuccess = WriteFile(
				pipe[index].hPipeInstance,
				&pipe[index].answer,
				sizeof(item_pipe),
				&cbRead,
				&pipe[index].overlap);

			// a opera��o de escrita foi conclu�da com sucesso
			if (fSuccess && cbRead == pipe[index].cbToWriteAnswer)
			{
				_tprintf(TEXT("A resposta para o pipe #%d �: [%s]!\n"), index, pipe[index].answer.extra);
				pipe[index].fPendingIO = FALSE;
				pipe[index].dwState = READING_STATE;
				//pipe[index].dwState = WRITING_STATE;
				continue;
			}

			// a opera��o de leitura ainda est� pendente
			dwErr = GetLastError();
			if (!fSuccess && (dwErr == ERROR_IO_PENDING))
			{
				_tprintf(TEXT("A resposta para o pipe #%d ainda est� pendente...\n"), index);
				pipe[index].fPendingIO = TRUE;
				continue;
			}

			// um erro aconteceu, o cliente deve ser desligado
			removePassenger(&data->map, index);
			DisconnectAndReconnect(data->pipe, index);
			break;

			// default
		default:
			_tprintf(TEXT("Estado do pipe #%d inv�lido!\n"), index);
			return 0;
		}
	}

	return 0;
}

/*
* DisconnectAndReconnect()
* -------------------
* Quando h� um erro ou um passageiro fecha o handle do pipe:
* Desliga o pipe do passageiro e chama a subrotina ConnectNamedPipe para esperar que outro cliente se ligue
*/
void DisconnectAndReconnect(PIPE_INSTANCE* pipe, DWORD index)
{
	// desliga a inst�ncia do pipe
	if (DisconnectNamedPipe(pipe[index].hPipeInstance))
		_tprintf(TEXT("A inst�ncia do pipe [%d] foi desligada.\n"), index);
	else
		_tprintf(TEXT("Erro ao desligar a inst�ncia do pipe [%d].\n"), index);

	// chama a sobrotina ConnectNamedPipe
	pipe[index].fPendingIO = ConnectToNewClient(pipe[index].hPipeInstance, &pipe[index].overlap);

	if (pipe[index].fPendingIO)
	{
		_tprintf(TEXT("A inst�ncia do pipe [%d] voltou a ficar dispon�vel para um novo cliente.\n"), index);
		pipe[index].dwState = CONNECTING_STATE;
	}
	else
	{
		_tprintf(TEXT("A inst�ncia do pipe [%d] est� no estado de leitura.\n"), index);
		pipe[index].dwState = READING_STATE;
	}
}

/*
* getAnswerForPassenger()
* -------------------
* Obtem a resposta para o passageiro
*/
void getAnswerForPassenger(thread_data* data, PIPE_INSTANCE* pipe, int index)
{
	if (_tcscmp(pipe->request.command, CMD_IDENTIFICATION_PASSENGER) == 0)
	{
		// obtem os dados do passageiro passados pelo campo extra do item_pipe
		passenger p = getPassengerFromIdentification(pipe->request.extra);
		p.pipe_index = index;
		pipe->cbToWriteAnswer = sizeof(item_pipe);

		if (isValidPassenger(data, &p))
		{
			// adiciona o passageiro ao sistema
			addPassenger(&data->map, &p);
			// resposta ao passageiros
			_tcscpy_s(pipe->answer.command, BUFFERSIZE, VALID_IDENTIFICATION);
			_tcscpy_s(pipe->answer.extra, BUFFERSIZE, TEXT("Bem-vindo ao Sistema de Gest�o de Espa�o A�reo!"));
		}
		else
		{
			// dados inv�lidos
			_tcscpy_s(pipe->answer.command, BUFFERSIZE, INVALID_IDENTIFICATION);
			_tcscpy_s(pipe->answer.extra, BUFFERSIZE, TEXT("Os dados do passageiro s�o inv�lidos!"));
		}
	}
}

/*
* getPassengerFromIdentification()
* -------------------
* Obtem os dados do passageiro passados pelo campo extra do item_pipe
*/
passenger getPassengerFromIdentification(TCHAR* extra)
{
	TCHAR temp[BUFFERSIZE];
	TCHAR* token = NULL;
	TCHAR* next_token = NULL;
	TCHAR seps[] = TEXT("|");
	int i = 0;
	passenger p;

	// limpa o lixo
	ZeroMemory(&p, sizeof(passenger));

	// pid|nome|origem|destino|wait
	_tcscpy_s(temp, BUFFERSIZE, extra);

	// obtem o primeiro token
	token = _tcstok_s(temp, seps, &next_token);

	// enquanto houver tokens no temp
	while (token != NULL)
	{
		if (token != NULL)
		{
			switch (i)
			{
				// pid
			case 0:
				if (isInteger(token))
					p.pid = _tstoi(token);
				break;

				// nome
			case 1:
				_tcscpy_s(p.name, BUFFERSIZE, token);
				break;

				// origem
			case 2:
				_tcscpy_s(p.origin, BUFFERSMALL, token);
				break;

				// destino
			case 3:
				_tcscpy_s(p.destination, BUFFERSMALL, token);
				break;

				// tempo de espera
			case 4:
				if (isInteger(token))
					p.waiting_time = _tstoi(token);
				break;
			}
			i++;
			token = _tcstok_s(NULL, seps, &next_token);
		}
	}

	return p;
}

/*
* isInteger()
* -------------------
* Verifica se o input � um n�mero
*/
BOOL isInteger(TCHAR* input)
{
	// verifica se o input � 0
	if (_tcscmp(input, TEXT("0")) == 0)
	{
		return TRUE;
	}

	// verifica se � um n�mero
	if (_tstoi(input) != 0)
		return TRUE;

	return FALSE;
}

/*
* isValidPassenger()
* -------------------
* Verifica se os dados do passageiro s�o v�lidos
*/
BOOL isValidPassenger(thread_data* data, passenger* p)
{
	// verifica se h� espa�o para adicionar um passageiro
	if (data->map.numPassengers >= MAX_PASSAG)
		return FALSE;

	// verifica se o aeroporto de origem do passageiro existe
	if (!isAirportNameRepeated(&data->map, p->origin))
		return FALSE;

	// verifica se o aeroporto de destino do passageiro existe
	if (!isAirportNameRepeated(&data->map, p->destination))
		return FALSE;

	return TRUE;
}

/*
* addPassenger()
* -------------
* Adiciona o passageiro no array de passageiros
*/
void addPassenger(map_data* map, passenger* p)
{
	int index;

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	index = map->numPassengers;

	map->passengers[index].pid = p->pid;
	_tcscpy_s(map->passengers[index].name, BUFFERSIZE, p->name);
	_tcscpy_s(map->passengers[index].origin, BUFFERSMALL, p->origin);
	_tcscpy_s(map->passengers[index].destination, BUFFERSMALL, p->destination);
	map->passengers[index].waiting_time = p->waiting_time;
	map->passengers[index].pipe_index = p->pipe_index;
	map->passengers[index].state = p->state;
	
	// incrementa o n�mero de passageiros inseridos no sistema
	map->numPassengers++;

	// liberta o mutex
	ReleaseMutex(map->hMutex);
}

/*
* removePassenger()
* -------------
* Remove o passageiro do array de passageiros
*/
void removePassenger(map_data* map, int index)
{

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	// verifica se o index � v�lido
	if (index < 0 || index > map->numPassengers - 1)
	{
		ReleaseMutex(map->hMutex);
		return;
	}

	// mensagem
	if (map->passengers[index].state == FLYING)
		_tprintf(TEXT("O passageiro [%s] morreu num acidente durante o voo!\n"), map->passengers[index].name);
	else
		_tprintf(TEXT("O passageiro [%s] saiu do sistema!\n"), map->passengers[index].name);

	// reoganiza os elementos do array
	for (int i = index; i < map->numPassengers - 1; i++)
		map->passengers[i] = map->passengers[i + 1];

	// decrementa o n�mero de passageiros
	map->numPassengers--;

	// liberta o mutex
	ReleaseMutex(map->hMutex);
}

/*
* sendMessageToPassenger()
* -------------
* Remove o passageiro do array de passageiros
*/
BOOL sendMessageToPassenger(PIPE_INSTANCE* hPipe, item_pipe* request)
{
	BOOL fSuccess;
	DWORD cbWritten;

	// escreve no pipe
	fSuccess = WriteFile(
		hPipe->hPipeInstance,
		request,
		sizeof(item_pipe),
		&cbWritten,
		&hPipe->overlap);

	// a opera��o de escrita foi conclu�da com sucesso
	if (fSuccess && cbWritten == sizeof(item_pipe))
	{
		hPipe->fPendingIO = FALSE;
		hPipe->dwState = READING_STATE;
	}
	else
	{
		// a opera��o de escrita est� pendente
		if (!fSuccess && (GetLastError() == ERROR_IO_PENDING))
		{
			hPipe->fPendingIO = TRUE;
		}
	}
}