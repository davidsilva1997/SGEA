#ifndef COMUM_H
#define COMUM_H

#include <Windows.h>

#define DIM			10
#define BUFFERSMALL	64
#define BUFFERSIZE	256
#define BUFFERLARGE	512

// sincronização do buffer circular
#define SEMAPHORE_SYNC_PRODUCER TEXT("SEMAPHORE_SYNC_PRODUCER")
#define SEMAPHORE_SYNC_CONSUMER TEXT("SEMAPHORE_SYNC_CONSUMER")
#define SEMAPHORE_SYNC_ITEMS	TEXT("SEMAPHORE_SYNC_ITEMS")
#define SEMAPHORE_SYNC_EMPTY	TEXT("SEMAPHORE_SYNC_EMPTY")

// sincronização de acesso dos aviões
#define SEMAPHORE_SYNC_BLOCK_ACCESS	TEXT("SEMAPHORE_SYNC_BLOCK_ACCESS")
#define SEMAPHORE_SYNC_MAX_PLANES	TEXT("SEMAPHORE_SYNC_MAX_PLANES")

#define MAP_FILE		TEXT("MAP_FILE")
#define EVENT_SHUTDOWN	TEXT("EVENT_SHUTDOWN")
#define UNDEFINED		TEXT("UNDEFINED")
#define SEMAPHORE_MOVEMENT	TEXT("SEMAPHORE_MOVEMENT")

// comandos disponíveis para o controlador e aviões
#define CMD_END			TEXT("FIM")		
#define CMD_HELP		TEXT("HELP")		// obtem ajuda acerca dos comandos disponíveis
#define CMD_CLEAR		TEXT("CLEAR")		// limpa a consola
#define CMD_CLS			TEXT("CLS")			// limpa a consola

// comandos disponíveis para os aviões
#define CMD_IDENTIFICATION	TEXT("CMD_IDENTIFICATION")
#define CMD_DEFINE			TEXT("DEFINE DESTINATION")	
#define CMD_BOARDING		TEXT("BOARDING")
#define CMD_START_FLIGHT	TEXT("START FLIGHT")
#define CMD_DETAILS			TEXT("DETAILS")


// respostas do controlador ao avião
#define VALID_IDENTIFICATION	TEXT("VALID_IDENTIFICATION")
#define INVALID_IDENTIFICATION	TEXT("INVALID_IDENTIFICATION")
#define VALID_DESTINATION		TEXT("VALID_DESTINATION")
#define INVALID_DESTINATION		TEXT("INVALID_DESTINATION")
#define VALID_MOVEMENT			TEXT("VALID_MOVEMENT")
#define INVALID_MOVEMENT		TEXT("INVALID_MOVEMENT")
#define ARRIVED_AT_DESTINATION	TEXT("ARRIVED_AT_DESTINATION")
#define VALID_BOARDING			TEXT("VALID_BOARDING")
#define INVALID_BOARDING		TEXT("INVALID_BOARDING")

// pipes
#define PIPE_NAME			TEXT("\\\\.\\pipe\\mynamedpipe")
#define PIPE_TIMEOUT		5000
#define CONNECTING_STATE	0
#define READING_STATE		1
#define WRITING_STATE		2

// comandos disponíveis para os passageiros
#define CMD_IDENTIFICATION_PASSENGER TEXT("CMD_IDENTIFICATION_PASSENGER")


// sincronização do buffer circular
typedef struct
{
	HANDLE semaphore_mutex_producer;
	HANDLE semaphore_mutex_consumer;
	HANDLE semaphore_items;			// número de itens por consumir
	HANDLE semaphore_empty;			// número de itens por produzir
} sync_circular_buffer;

// sincronização de acesso dos aviões
typedef struct
{
	HANDLE semaphore_block_access;	// semáforo de exclusão mútua que controla a aceitação de novos aviões
	HANDLE semaphore_access;		// controla o acesso dos aviões ao controlador
	BOOL   isBlocked;				// impede que o semáforo de aceitação de aviões fique preso
} sync_plane_access;

// posição no mapa
typedef struct
{
	int x;	// posição no eixo x
	int y;	// posição no eixo y
} position;

// estrutura de um aeroporto
typedef struct
{
	TCHAR name[BUFFERSMALL];// nome
	position position;		// posição no mapa
} airport;

// estado de um avião e passageiro
typedef enum
{
	GROUND = 0,
	FLYING = 1
} state;

// estrutura de um avião
typedef struct
{
	DWORD pid;			// identificador do avião
	int capacity;		// número máximo de passageiros
	int num_passenger;	// número de passageiros que embarcaram
	int speed;			// posições/segundo percorridas
	TCHAR origin[BUFFERSMALL];		// aeroporto de origem
	TCHAR destination[BUFFERSMALL];	// aeroporto de destino
	position pos_actual;		// posição atual do avião
	position pos_destination;	// posição do destino
	state state;		// estado do avião
} plane;

// item passado no buffer circular
typedef struct
{
	DWORD pidSender;	// processo que vai produzir (escrita)
	DWORD pidReceiver;	// processo que vai consumir (leitura)
	TCHAR command[BUFFERSIZE];	// comando a executar
	plane plane;
	int movementResult;	
} item;

// buffer circular
typedef struct
{
	item buffer[DIM];	// array de itens 
	int in;				// posição da próxima produção (escrita)
	int out;			// posição do próximo consumo (leitura)
} circular_buffer;

// item passado no pipe
typedef struct
{
	TCHAR command[BUFFERSIZE];	// comando a ser executado
	TCHAR extra[BUFFERSIZE];	// dados extra
} item_pipe;


// estrutura de uma passageiro
typedef struct
{
	DWORD pid;							// identificador do passageiro
	TCHAR name[BUFFERSIZE];			// nome do passageiro
	TCHAR origin[BUFFERSMALL];		// aeroporto de origem
	TCHAR destination[BUFFERSMALL];	// aeroporto de destino
	int	  waiting_time;					// tempo que espera até embarcar
	int	  pipe_index;					// index do pipe que lhe pertence
	enum state state;					// estado do passageiro
	DWORD pidPlane;					// avião onde embarcaram
} passenger;

/*
* createEvent()
* -------------------
* Cria um objeto evento manual-reset
*/
HANDLE createEvent(TCHAR*);

/*
* createMutex()
* -------------------
* Criar um objeto mutex
*/
HANDLE createMutex(TCHAR*);

/*
* createSemaphore()
* -------------------
* Cria um objeto semáforo
*/
HANDLE createSemaphore(TCHAR*, int);

/*
* createSemaphoreEmpty()
* -------------------
* Cria um objeto semáforo com 0 elementos 
*/
HANDLE createEmptySemaphore(TCHAR*, int);

/*
* createThread()
* -------------------
* Cria uma thread pendente (não inicia logo a sua execução)
*/
HANDLE createThread(LPTHREAD_START_ROUTINE, LPVOID data);

#endif