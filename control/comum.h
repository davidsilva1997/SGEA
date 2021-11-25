#ifndef COMUM_H
#define COMUM_H

#include <Windows.h>

#define DIM			10
#define BUFFERSMALL	64
#define BUFFERSIZE	256
#define BUFFERLARGE	512

// sincroniza��o do buffer circular
#define SEMAPHORE_SYNC_PRODUCER TEXT("SEMAPHORE_SYNC_PRODUCER")
#define SEMAPHORE_SYNC_CONSUMER TEXT("SEMAPHORE_SYNC_CONSUMER")
#define SEMAPHORE_SYNC_ITEMS	TEXT("SEMAPHORE_SYNC_ITEMS")
#define SEMAPHORE_SYNC_EMPTY	TEXT("SEMAPHORE_SYNC_EMPTY")

// sincroniza��o de acesso dos avi�es
#define SEMAPHORE_SYNC_BLOCK_ACCESS	TEXT("SEMAPHORE_SYNC_BLOCK_ACCESS")
#define SEMAPHORE_SYNC_MAX_PLANES	TEXT("SEMAPHORE_SYNC_MAX_PLANES")

#define MAP_FILE		TEXT("MAP_FILE")
#define EVENT_SHUTDOWN	TEXT("EVENT_SHUTDOWN")
#define UNDEFINED		TEXT("UNDEFINED")
#define SEMAPHORE_MOVEMENT	TEXT("SEMAPHORE_MOVEMENT")

// comandos dispon�veis para o controlador e avi�es
#define CMD_END			TEXT("FIM")		
#define CMD_HELP		TEXT("HELP")		// obtem ajuda acerca dos comandos dispon�veis
#define CMD_CLEAR		TEXT("CLEAR")		// limpa a consola
#define CMD_CLS			TEXT("CLS")			// limpa a consola

// comandos dispon�veis para os avi�es
#define CMD_IDENTIFICATION	TEXT("CMD_IDENTIFICATION")
#define CMD_DEFINE			TEXT("DEFINE DESTINATION")	
#define CMD_BOARDING		TEXT("BOARDING")
#define CMD_START_FLIGHT	TEXT("START FLIGHT")
#define CMD_DETAILS			TEXT("DETAILS")


// respostas do controlador ao avi�o
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

// comandos dispon�veis para os passageiros
#define CMD_IDENTIFICATION_PASSENGER TEXT("CMD_IDENTIFICATION_PASSENGER")


// sincroniza��o do buffer circular
typedef struct
{
	HANDLE semaphore_mutex_producer;
	HANDLE semaphore_mutex_consumer;
	HANDLE semaphore_items;			// n�mero de itens por consumir
	HANDLE semaphore_empty;			// n�mero de itens por produzir
} sync_circular_buffer;

// sincroniza��o de acesso dos avi�es
typedef struct
{
	HANDLE semaphore_block_access;	// sem�foro de exclus�o m�tua que controla a aceita��o de novos avi�es
	HANDLE semaphore_access;		// controla o acesso dos avi�es ao controlador
	BOOL   isBlocked;				// impede que o sem�foro de aceita��o de avi�es fique preso
} sync_plane_access;

// posi��o no mapa
typedef struct
{
	int x;	// posi��o no eixo x
	int y;	// posi��o no eixo y
} position;

// estrutura de um aeroporto
typedef struct
{
	TCHAR name[BUFFERSMALL];// nome
	position position;		// posi��o no mapa
} airport;

// estado de um avi�o e passageiro
typedef enum
{
	GROUND = 0,
	FLYING = 1
} state;

// estrutura de um avi�o
typedef struct
{
	DWORD pid;			// identificador do avi�o
	int capacity;		// n�mero m�ximo de passageiros
	int num_passenger;	// n�mero de passageiros que embarcaram
	int speed;			// posi��es/segundo percorridas
	TCHAR origin[BUFFERSMALL];		// aeroporto de origem
	TCHAR destination[BUFFERSMALL];	// aeroporto de destino
	position pos_actual;		// posi��o atual do avi�o
	position pos_destination;	// posi��o do destino
	state state;		// estado do avi�o
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
	int in;				// posi��o da pr�xima produ��o (escrita)
	int out;			// posi��o do pr�ximo consumo (leitura)
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
	int	  waiting_time;					// tempo que espera at� embarcar
	int	  pipe_index;					// index do pipe que lhe pertence
	enum state state;					// estado do passageiro
	DWORD pidPlane;					// avi�o onde embarcaram
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
* Cria um objeto sem�foro
*/
HANDLE createSemaphore(TCHAR*, int);

/*
* createSemaphoreEmpty()
* -------------------
* Cria um objeto sem�foro com 0 elementos 
*/
HANDLE createEmptySemaphore(TCHAR*, int);

/*
* createThread()
* -------------------
* Cria uma thread pendente (n�o inicia logo a sua execu��o)
*/
HANDLE createThread(LPTHREAD_START_ROUTINE, LPVOID data);

#endif