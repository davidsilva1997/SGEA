#include <Windows.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include "../control/comum.h"
#include "SO2_TP_DLL_2021.h"


// avi�o
#define MIN_CAPACITY 0
#define MAX_CAPACITY 600
#define MIN_SPEED 1
#define MAX_SPEED 5

#define NUM_THREADS 3

// dados passados para as threads
typedef struct
{
	plane plane;			// representa o avi�o
	circular_buffer* ptrSharedMemory;		// ponteiro para a mem�ria partilhada
	sync_plane_access sync_plane_access;	//  sincroniza��o do acesso ao sistema
	sync_circular_buffer sync_circular_buffer;	// sincroniza��o do buffer circular
	HANDLE hEventShutdown;	// evento para o comando shutdown
	HANDLE hMutexMovement;	// sem�foro de exclus�o m�tua que permite efetuar movimenta��es
} thread_data;


BOOL isInteger(TCHAR*);
void initPlaneData(plane*, TCHAR*, TCHAR*, TCHAR*);
HANDLE openEvent(TCHAR*);
HANDLE openSemaphore(TCHAR*);
HANDLE createSemaphore(TCHAR*, int);
BOOL openSyncPlaneAccess(sync_plane_access*);
void closeSyncPlaneAccess(sync_plane_access*);
BOOL openSyncSharedMemory(sync_circular_buffer*);
void closeSyncSharedMemory(sync_circular_buffer*);
void closeHandles(thread_data*);


// mem�ria partilhada
HANDLE openFileMapping(TCHAR*);
circular_buffer* openMapViewOfFile(HANDLE);
void initIdentification(thread_data*);
void writeInSharedMemory(thread_data*, item*);
BOOL handleCommandFromController(thread_data*, item);

// threads
HANDLE createThread(LPTHREAD_START_ROUTINE, LPVOID);
DWORD WINAPI threadReadSharedMemory(LPVOID);
DWORD WINAPI threadExecuteCommands(LPVOID);
DWORD WINAPI threadShutdown(LPVOID);

// comandos
void cmdDefine(thread_data*);
void cmdEnd(thread_data*);
void cmdDetails(plane);
void cmdBoarding(thread_data*);
void cmdHelp(void);
void cmdClear(void);
int cmdStartFlight(thread_data*);

BOOL isDestinationDefined(plane*);


int _tmain(int argc, TCHAR* argv[])
{
	HANDLE hMapFile;	// handle para o ficheiro de mapeamento
	thread_data data;	// dados passados para as threads
	HANDLE hThread[NUM_THREADS];	// handle para as threads

#ifdef UNICODE
	(void)_setmode(_fileno(stdin), _O_WTEXT);
	(void)_setmode(_fileno(stdout), _O_WTEXT);
#endif

	_tprintf(TEXT("Inicializa��o do programa \'aviao.exe\'...\n"));

	// verifica o n�mero de argumentos passados pela linha de comandos
	if (argc != 4)
	{
		_tprintf(TEXT("O n�mero de argumentos passados pela linha de comandos inv�lido!\nFormato exigido: aviao.exe capacidade velociade aeroporto de origem\nExemplo: aviao.exe 50 1 OPO\n"));
		return -1;
	}

	// verifica o formato dos argumentos passados (capacidade e velocidade)
	if (!isInteger(argv[1]) || !isInteger(argv[2]))
	{
		_tprintf(TEXT("Formato dos argumentos incorreto!\n"));
		return -1;
	}

	// verifica se o valor da capacidade � v�lido
	if (_tstoi(argv[1]) < MIN_CAPACITY || _tstoi(argv[1]) > MAX_CAPACITY)
	{
		_tprintf(TEXT("O valor da capacidade � inv�lido! Insira um valor entre %d e %d.\n"), MIN_CAPACITY, MAX_CAPACITY);
		return -1;
	}

	// verifica se o valor da velocidade � v�lido
	if (_tstoi(argv[2]) < MIN_SPEED || _tstoi(argv[2]) > MAX_SPEED)
	{
		_tprintf(TEXT("O valor da velocidade � inv�lido! Insira um valor entre %d e %d.\n"), MIN_SPEED, MAX_SPEED);
		return -1;
	}

	// inicializa os dados do avi�o
	initPlaneData(&data.plane, argv[1], argv[2], argv[3]);

	// abre o objeto evento shutdown
	if ((data.hEventShutdown = openEvent(EVENT_SHUTDOWN)) == NULL)
	{
		_tprintf(TEXT("Erro ao tentar abrir o objeto evento shutdown!\n"));
		return -1;
	}

	// cria o sem�foro de exclus�o m�tua que permite efetuar movimenta��es
	if ((data.hMutexMovement = createSemaphore(SEMAPHORE_MOVEMENT, 1)) == NULL)
	{
		_tprintf(TEXT("N�o foi poss�vel criar o objeto sem�foro para o movimento!\n"));
		return -1;
	}

	// abre o ficheiro de mapeamento
	if ((hMapFile = openFileMapping(MAP_FILE)) == NULL)
	{
		_tprintf(TEXT("N�o foi poss�vel abrir o ficheiro de mapeamento!\n"));
		return -1;
	}

	// abre a vista da mem�ria partilhada do buffer circular
	if ((data.ptrSharedMemory = openMapViewOfFile(hMapFile)) == NULL)
	{
		_tprintf(TEXT("N�o foi poss�vel abrir a vista da mem�ria partilhada!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// abre os objetos sem�foros que sincronizam o acesso ao sistema
	if (!openSyncPlaneAccess(&data.sync_plane_access))
	{
		_tprintf(TEXT("N�o foi poss�vel abrir os objetos de sincroniza��o de acesso ao sistema!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// abre os objetos sem�foros que sincronizam o buffer circular
	if (!openSyncSharedMemory(&data.sync_circular_buffer))
	{
		_tprintf(TEXT("N�o foi poss�vel abrir os objetos de sincroniza��o do buffer circular!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// espera at� ter a permiss�o para aceder ao sistema
	_tprintf(TEXT("A espera da permiss�o para aceder ao SGEA..."));
	WaitForSingleObject(data.sync_plane_access.semaphore_block_access, INFINITE);
	ReleaseSemaphore(data.sync_plane_access.semaphore_block_access, 1, NULL);
	_tprintf(TEXT("Permiss�o obtida.\n"));

	// espera at� ter espa�o para aceder ao sistema
	_tprintf(TEXT("A espera de ter espa�o para aceder ao SGEA..."));
	WaitForSingleObject(data.sync_plane_access.semaphore_access, INFINITE);
	_tprintf(TEXT("Entrei.\n"));

	// identifica��o ao controlador
	initIdentification(&data);

	// cria a thread que l� da mem�ria partilhada
	if ((hThread[0] = createThread(threadReadSharedMemory, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o da thread que permite ler a mem�ria partilhada!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// cria a thread que permite executar comandos
	if ((hThread[1] = createThread(threadExecuteCommands, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o da thread que permite executar comandos!\n"));
		CloseHandle(hMapFile);
		return -1;
	}

	// cria a thread que verifica se o comando shutdown foi executado
	if ((hThread[2] = createThread(threadShutdown, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na cria��o da thread que verifica se o comando shutdown foi executado!\n"));
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

	// liberta o espa�o para outro avi�o aceder ao sistema
	ReleaseSemaphore(data.sync_plane_access.semaphore_access, 1, NULL);

	closeHandles(&data);
	CloseHandle(hMapFile);
	return 0;
}

/*
* isInteger()
* -------------
* Verifica se o input � um n�mero
*/
BOOL isInteger(TCHAR* input)
{
	// verifica se o input � "0"
	if (_tcscmp(input, TEXT("0")) == 0)
		return TRUE;

	// verifica se � um n�mero inteiro
	if (_tstoi(input) != 0)
		return TRUE;

	return FALSE;
}

/*
* initPlaneData()
* -------------
* Preenche os dados do avi�o
*/
void initPlaneData(plane* p, TCHAR* capacity, TCHAR* speed, TCHAR* origin)
{
	p->pid = GetCurrentProcessId();
	p->capacity = _tstoi(capacity);
	p->num_passenger = 0;
	p->speed = _tstoi(speed);
	_tcscpy_s(p->origin, BUFFERSMALL, origin);
	_tcscpy_s(p->destination, BUFFERSMALL, UNDEFINED);
	p->pos_actual.x = p->pos_actual.y = 0;
	p->pos_destination.x = p->pos_destination.y = 0;
	p->state = GROUND;
}

/*
* openEvent()
* -------------------
* Abre um objeto evento manual-reset
*/
HANDLE openEvent(TCHAR* name)
{
	// abre o objeto evento
	HANDLE event = OpenEvent(
		SYNCHRONIZE,	// The right to use the object for synchronization.
		FALSE,	// inherit the handle
		name);	// name of the event object

	return event;
}

/*
* openEvent()
* -------------------
* Abre um objeto evento manual-reset
*/
HANDLE openSemaphore(TCHAR* name)
{
	HANDLE semaphore = OpenSemaphore(
		SYNCHRONIZE | SEMAPHORE_MODIFY_STATE,
		FALSE,				// does not inherit the handle
		name);				// the name of semaphore object

	if (semaphore == NULL)
		_tprintf(TEXT("Erro ao abrir o semaforo [%s]!\n"), name);

	return semaphore;
}


/*
* createSemaphore()
* -------------------
* Cria um objeto sem�foro
*/
HANDLE createSemaphore(TCHAR* name, int count)
{
	// cria o sem�foro
	HANDLE semaphore = CreateSemaphore(
		NULL,	// default security attributes
		count,	// initial count
		count,	// maximum count
		name);	// name of the semaphore

	return semaphore;
}

/*
* openFileMapping()
* -------------------
* Abre o ficheiro de mapeamento
*/
HANDLE openFileMapping(TCHAR* name)
{
	HANDLE hMapFile = OpenFileMapping(
		FILE_MAP_ALL_ACCESS,	// read/write access
		FALSE,					// do not inherit
		name);

	return hMapFile;
}

/*
* openMapViewOfFile()
* -------------------
* Abre a vista da mem�ria partilhada
*/
circular_buffer* openMapViewOfFile(HANDLE hMapFile)
{
	circular_buffer* ptr = MapViewOfFile(
		hMapFile,	// handle to map object
		FILE_MAP_READ |	// read/write access
		FILE_MAP_WRITE,
		0,			// offset high - where the view begins
		0,			// offset low  - where the view begins
		sizeof(circular_buffer));	// number of bytes of a file mapping to map to the view

	return ptr;
}

/*
* openSyncPlaneAccess()
* -------------------
* Abre os sem�foros que sincronizam o aceeso dos avi�es ao sistema
*/
BOOL openSyncPlaneAccess(sync_plane_access* sync)
{
	if ((sync->semaphore_block_access = openSemaphore(SEMAPHORE_SYNC_BLOCK_ACCESS)) == NULL)
		return FALSE;
	if ((sync->semaphore_access = openSemaphore(SEMAPHORE_SYNC_MAX_PLANES)) == NULL)
		return FALSE;
	return TRUE;
}

/*
* openSyncPlaneAccess()
* -------------------
* Abre os sem�foros que sincronizam o aceeso dos avi�es ao sistema
*/
BOOL openSyncSharedMemory(sync_circular_buffer* sync)
{
	if ((sync->semaphore_mutex_producer = openSemaphore(SEMAPHORE_SYNC_PRODUCER)) == NULL)
		return FALSE;
	if ((sync->semaphore_mutex_consumer = openSemaphore(SEMAPHORE_SYNC_CONSUMER)) == NULL)
		return FALSE;
	if ((sync->semaphore_items = openSemaphore(SEMAPHORE_SYNC_ITEMS)) == NULL)
		return FALSE;
	if ((sync->semaphore_empty = openSemaphore(SEMAPHORE_SYNC_EMPTY)) == NULL)
		return FALSE;
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
}

/*
* initIdentification()
* -------------------
* Identifica��o do avi�o ao controlador
*/
void initIdentification(thread_data* data)
{
	item item;	// item passado no buffer circular

	item.pidSender = data->plane.pid;
	item.pidReceiver = 0;	// para o controlador
	_tcscpy_s(item.command, BUFFERSIZE, CMD_IDENTIFICATION);
	item.plane = data->plane;

	// escreve os dados no buffer circular da mem�ria partilhada
	writeInSharedMemory(data, &item);
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
* createThread()
* -------------------
* Cria uma thread pendente (n�o inicia logo a sua execu��o)
*/
HANDLE createThread(LPTHREAD_START_ROUTINE function, LPVOID data)
{
	// cria a thread e inicia a execu��o da mesma
	HANDLE thread = CreateThread(
		NULL,		// default security attributes
		0,			// default size of the stack
		function,	// function to be executed
		data,		// data to be passed to the thread
		CREATE_SUSPENDED,	// flag that control the creation of the thread
		NULL);		// thread identifier is not returned

	return thread;
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
	BOOL run = TRUE;

	while (run)
	{
		// espera que haja um item para ser consumido
		WaitForSingleObject(sync.semaphore_items, INFINITE);

		// espera pelo mutex do consumidor
		WaitForSingleObject(sync.semaphore_mutex_consumer, INFINITE);

		// obtem o item a consumir
		itemParaConsumir = data->ptrSharedMemory->buffer[data->ptrSharedMemory->out];

		// verifica se � o avi�o que tem de consumir
		if (itemParaConsumir.pidReceiver == data->plane.pid)
		{
			// incrementa a posi��o do pr�ximo consumo
			data->ptrSharedMemory->out = (data->ptrSharedMemory->out + 1) % DIM;

			// liberta o mutex do consumidor
			ReleaseSemaphore(sync.semaphore_mutex_consumer, 1, NULL);

			// incrementa o n�mero de itens por produzir
			ReleaseSemaphore(sync.semaphore_empty, 1, NULL);

			// trata o pedido do controlador
			run = handleCommandFromController(data, itemParaConsumir);
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
* threadReadSharedMemory()
* -------------
* Thread que permite ler da mem�ria partilhada
*/
DWORD WINAPI threadExecuteCommands(LPVOID args)
{
	thread_data* data = (thread_data*)args;
	TCHAR cmd[BUFFERSMALL] = { '\0' };
	int cmdSize, movementResult = -2;

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

		if (_tcscmp(cmd, CMD_DEFINE) == 0)
		{
			// define o destino do avi�o
			cmdDefine(data);
		}
		else if (_tcscmp(cmd, CMD_BOARDING) == 0)
		{
			// embarca os passageiros
			cmdBoarding(data);
		}
		else if (_tcscmp(cmd, CMD_START_FLIGHT) == 0)
		{
			// inicia o voo
			do
			{
				// continua o voo at� chegar ao destino ou um erro occorer
				if ((movementResult = cmdStartFlight(data)) == -1)
					break;
			} while (movementResult != 0);
		}
		else if (_tcscmp(cmd, CMD_DETAILS) == 0)
		{
			// imprime os dados do avi�o
			cmdDetails(data->plane);
		}
		else if (_tcscmp(cmd, CMD_HELP) == 0)
		{
			// obtem ajuda acerca dos comandos existentes
			cmdHelp();
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
			cmdEnd(data);
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
* threadReadSharedMemory()
* -------------
* Thread que permite ler da mem�ria partilhada
*/
DWORD WINAPI threadShutdown(LPVOID args)
{
	thread_data* data = (thread_data*)args;

	// espera at� que o objeto evento seja assinalado
	WaitForSingleObject(data->hEventShutdown, INFINITE);

	_tprintf(TEXT("O controlador efetuou o comando shutdown!\nO programa vai terminar!\n"));


	return 0;
}

/*
* handleCommandFromController()
* -------------
* Trata os comandos recebidos pelo controlador
*/
BOOL handleCommandFromController(thread_data* data, item itemParaConsumir)
{
	TCHAR cmd[BUFFERSIZE];
	_tcscpy_s(cmd, BUFFERSIZE, itemParaConsumir.command);

	// trata o comando recebido
	if (_tcscmp(cmd, VALID_IDENTIFICATION) == 0)
	{
		data->plane = itemParaConsumir.plane;
		_tprintf(TEXT("A identifica��o foi validada pelo controlador.\n"));
	}
	else if (_tcscmp(cmd, INVALID_IDENTIFICATION) == 0)
	{
		_tprintf(TEXT("A identifica��o n�o foi validada pelo controlador.\nO programa vai terminar.\n"));
		return FALSE;
	}
	else if (_tcscmp(cmd, VALID_DESTINATION) == 0)
	{
		data->plane = itemParaConsumir.plane;
		_tprintf(TEXT("O aeroporto de destino foi definido para [%s].\n"), data->plane.destination);
	}
	else if (_tcscmp(cmd, INVALID_DESTINATION) == 0)
	{
		_tprintf(TEXT("A defini��o do aeroporto de destino falhou.\n"));
	}
	else if (_tcscmp(cmd, VALID_MOVEMENT) == 0)
	{
		data->plane = itemParaConsumir.plane;
		_tprintf(TEXT("Posi��o atual: [%d,%d]\n"), data->plane.pos_actual.x, data->plane.pos_actual.y);
		ReleaseSemaphore(data->hMutexMovement, 1, NULL);
	}
	else if (_tcscmp(cmd, INVALID_DESTINATION) == 0)
	{
		data->plane = itemParaConsumir.plane;
		_tprintf(TEXT("A movimenta��o do avi�o falhou!\n"));
		ReleaseSemaphore(data->hMutexMovement, 1, NULL);
	}
	else if (_tcscmp(cmd, ARRIVED_AT_DESTINATION) == 0)
	{
		data->plane = itemParaConsumir.plane;
		_tprintf(TEXT("O avi�o chegou ao destino!\n"));
		ReleaseSemaphore(data->hMutexMovement, 1, NULL);
	}
	else if (_tcscmp(cmd, VALID_BOARDING) == 0)
	{
		data->plane = itemParaConsumir.plane;
		_tprintf(TEXT("O embarque dos passageiros foi efetuado com sucesso.\n Embarcaram [%d] passageiros.\n"), data->plane.num_passenger);
	}
	else if (_tcscmp(cmd, INVALID_BOARDING) == 0)
	{
		data->plane = itemParaConsumir.plane;
		_tprintf(TEXT("O embarque dos passageiros falhou!\n"));
	}

	return TRUE;
}

/*
* cmdDefine()
* -------------
* Define o destino do avi�o
*/
void cmdDefine(thread_data* data)
{
	item item;

	// verifica se o avi�o est� em voo
	if (data->plane.state == FLYING)
	{
		_tprintf(TEXT("N�o foi poss�vel alterar o destino porque o avi�o est� em voo!\n"));
		return;
	}

	// dados passados ao controlador
	item.pidSender = data->plane.pid;
	item.pidReceiver = 0;		// para o controlador
	_tcscpy_s(item.command, BUFFERSIZE, CMD_DEFINE);
	item.plane = data->plane;

	// obtem o input
	_tprintf(TEXT("Insira o nome do aeroproto de destino: "));
	_fgetts(item.plane.destination, BUFFERSMALL, stdin);
	item.plane.destination[_tcslen(item.plane.destination) - 1] = '\0';

	// produz o item no buffer circular
	writeInSharedMemory(data, &item);
	_tprintf(TEXT("Pedido [%s] enviado ao controlador!"), item.command);
}

/*
* cmdDefine()
* -------------
* Embarca os passageiros
*/
void cmdBoarding(thread_data* data)
{
	item itemParaProduzir;

	// dados passados ao controlador
	itemParaProduzir.pidSender = data->plane.pid;
	itemParaProduzir.pidReceiver = 0;	// para o controlador
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, CMD_BOARDING);
	itemParaProduzir.plane = data->plane;

	// escreve os dados no buffer circular
	writeInSharedMemory(data, &itemParaProduzir);
}

/*
* cmdEnd()
* -------------
* Termina a execu��o do avi�o
*/
void cmdEnd(thread_data* data)
{
	item item;

	// dados passados ao controlador
	item.pidSender = data->plane.pid;
	item.pidReceiver = 0;		// para o controlador
	_tcscpy_s(item.command, BUFFERSIZE, CMD_END);

	// produz o item no buffer circular
	writeInSharedMemory(data, &item);
}

/*
* cmdDetails()
* -------------
* Imprime os dados do avi�o
*/
void cmdDetails(plane p)
{
	_tprintf(TEXT("%5s\t%6s\t%6s\t%10s\t%10s\t%10s\t%10s\t%10s\t%6s\n"), TEXT("PID"), TEXT("Capac."), TEXT("Speed"), TEXT("Origem"), TEXT("Destino"), TEXT("Pos. Atual"), TEXT("Pos.Destino"), TEXT("N�m. Passag."), TEXT("Estado"));
	_tprintf(TEXT("%5d\t%6d\t%6d\t%10s\t%10s\t[%4d,%4d]\t[%4d,%4d]\t%10d\t%6d\n"), p.pid, p.capacity, p.speed, p.origin, p.destination, p.pos_actual.x, p.pos_actual.y, p.pos_destination.x, p.pos_destination.y, p.num_passenger, p.state);
}

/*
* cmdHelp()
* -------------
* Obtem ajuda acerca dos comandos dispon�veis
*/
void cmdHelp()
{
	_tprintf(TEXT("\n\'%20s\'\t%s\n"), CMD_DEFINE, TEXT(" - Define o aeroporto de destino"));
	_tprintf(TEXT("\'%20s\'\t%s\n"), CMD_BOARDING, TEXT(" - Embarca os passageiros"));
	_tprintf(TEXT("\'%20s\'\t%s\n"), CMD_START_FLIGHT, TEXT(" - Inicia o voo"));
	_tprintf(TEXT("\'%20s\'\t%s\n"), CMD_DETAILS, TEXT(" - Imprime os dados do avi�o"));
	_tprintf(TEXT("\'%20s\'\t%s\n"), CMD_HELP, TEXT(" - Obtem ajuda acerca dos comandos existentes"));
	_tprintf(TEXT("\'%20s\'\t%s\n"), CMD_CLEAR, TEXT(" - Limpa a consola"));
	_tprintf(TEXT("\'%20s\'\t%s\n"), CMD_CLS, TEXT(" - Limpa a consola"));
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
* cmdStartFlight()
* -------------
* Inicia o voo
*/
int cmdStartFlight(thread_data* data)
{
	item itemParaProduzir;		// item que vai ser produzido no buffer circular
	plane* p;
	position next_position = { 0, 0 };
	int movementResult = -1;

	// limpa o lixo
	ZeroMemory(&itemParaProduzir, sizeof(item));

	// verifica se o destino foi definido
	if (!isDestinationDefined(&data->plane))
	{
		_tprintf(TEXT("N�o foi poss�vel iniciar o voo porque o destino n�o est� definido!\n"));
		return -1;
	}

	// Espera que o sem�foro de exclus�o m�tua para a movimenta��o se liberte
	// � libertado quando recebe uma reposta do controlador !
	WaitForSingleObject(data->hMutexMovement, INFINITE);

	p = &data->plane;

	// dados passados ao controlador
	itemParaProduzir.pidSender = data->plane.pid;
	itemParaProduzir.pidReceiver = 0;		// para o controlador
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, CMD_START_FLIGHT);
	itemParaProduzir.plane = data->plane;

	// movimenta��o 
	// 0 - chegou ao destino, 1 - movimenta��o correta, 2 - alguem erro ocorreu
	movementResult = move(p->pos_actual.x, p->pos_actual.y, p->pos_destination.x, p->pos_destination.y, &next_position.x, &next_position.y);

	itemParaProduzir.plane.pos_actual = p->pos_actual;		
	itemParaProduzir.plane.pos_destination = next_position;
	itemParaProduzir.movementResult = movementResult;
	
	writeInSharedMemory(data, &itemParaProduzir);

	// tem em considera��o a velociade do avi�o
	Sleep(1000/data->plane.speed);

	return movementResult;
}

/*
* isDestinationDefined()
* -------------
* Verifica se o defino foi definido
*/
BOOL isDestinationDefined(plane* p)
{
	if (_tcscmp(p->destination, UNDEFINED) == 0)
		return FALSE;
	return TRUE;
}