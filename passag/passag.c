#include <Windows.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include "../control/comum.h"

#define NUM_THREADS 3

typedef struct
{
	HANDLE hPipe;			// handle para o pipe
	passenger passenger;	// passageiro
	HANDLE hEventShutdown;	// evento para o comando shutdown
	HANDLE hMutex;			// handle objeto mutex
} thread_data;

void initPassenger(passenger*, TCHAR*, TCHAR*, TCHAR*, int);
HANDLE initPipe();
HANDLE createUnnamedMutex();
HANDLE openEvent(TCHAR*);
BOOL initIdentification(thread_data*);
BOOL sendRequest(HANDLE, item_pipe*);
BOOL isInteger(TCHAR*);
void cmdDetails(passenger);


// threads
HANDLE createThread(LPTHREAD_START_ROUTINE, LPVOID);
DWORD WINAPI threadShutdown(LPVOID);
DWORD WINAPI threadReadPipe(LPVOID);
DWORD WINAPI threadExecuteCommands(LPVOID);
DWORD WINAPI threadWaitingTime(LPVOID);





int _tmain(int argc, TCHAR* argv[])
{
	thread_data data;	// dados passados para as threads
	HANDLE hThread[NUM_THREADS];	// handle paras as threads
	HANDLE hThreadWaitingTime;		// handle para a thread que verifica se o tempo de espera até embarcar esgotou


#ifdef UNICODE
	(void)_setmode(_fileno(stdin), _O_WTEXT);
	(void)_setmode(_fileno(stdout), _O_WTEXT);
#endif

	_tprintf(TEXT("Inicialização do programa \'passag.exe\'...\n"));

	// verifica o número de argumentos passados
	if (argc < 4 || argc > 5)
	{
		_tprintf(TEXT("O número de argumentos passados pela linha de comandos está inválido!\nFormato exigido: passag.exe origem destino nome (tempo de espera em segundos)\nExemplo: passag.exe OPO LIS David 60\n"));
		return -1;
	}

	// verifica se o tempo de espera foi passado pela linha de comandos e se é um número
	if (argc == 5 && (!isInteger(argv[4])))
	{
		_tprintf(TEXT("O formato do tempo de espera está incorreto! O programa vai terminar!\n"));
		return -1;
	}

	if (argc == 4)
		initPassenger(&data.passenger, argv[1], argv[2], argv[3], 0);
	else
		initPassenger(&data.passenger, argv[1], argv[2], argv[3], _tstoi(argv[4]));

		//initPassenger(&data.passenger, TEXT("OPO"), TEXT("LIS"), TEXT("David"), 5000);

	// abre o pipe 
	if ((data.hPipe = initPipe()) == NULL)
	{
		_tprintf(TEXT("Erro ao tentar abrir o pipe.\n"));
		return -1;
	}

	// cria o objeto mutex
	if ((data.hMutex = createUnnamedMutex()) == NULL)
	{
		_tprintf(TEXT("Não foi possível criar o objeto mutex.\n"));
		return -1;
	}

	// abre o objeto evento shutdown
	if ((data.hEventShutdown = openEvent(EVENT_SHUTDOWN)) == NULL)
	{
		_tprintf(TEXT("Erro ao tentar abrir o objeto evento shutdown!\n"));
		return -1;
	}

	// identificação ao controlador
	if (initIdentification(&data) == FALSE)
		return -1;

	// cria a thread que lê as respostas do controlador
	if ((hThread[0] = createThread(threadReadPipe, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na criação da thread que lê as respostas do controlador!\n"));
		return -1;
	}

	// cria a thread que verifica se o comando shutdown foi executado
	if ((hThread[1] = createThread(threadShutdown, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na criação da thread que verifica se o comando shutdown foi executado!\n"));
		return -1;
	}

	// cria a thread que permite executar comandos
	if ((hThread[2] = createThread(threadExecuteCommands, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na criação da thread que permite executar comandos!\n"));
		return -1;
	}

	// cria a thread que verifica se o tempo de espera até embarcar esgotou
	if ((hThreadWaitingTime = createThread(threadWaitingTime, &data)) == NULL)
	{
		_tprintf(TEXT("Erro na criação da thread que verifica se o tempo de espera até embarcar esgotou!\n"));
		return -1;
	}	

	// inicia a execução das threads
	for (int i = 0; i < NUM_THREADS; i++)
		ResumeThread(hThread[i]);

	ResumeThread(hThreadWaitingTime);

	// Espera até que uma das threads termine
	WaitForMultipleObjects(
		NUM_THREADS,	// the number of object handles in the array 
		hThread,		// an array of object handles
		FALSE,			// does not wait for all threads
		INFINITE);		// waiting time

	CloseHandle(data.hMutex);
	CloseHandle(data.hEventShutdown);
	CloseHandle(data.hPipe);
	return 0;
}

/*
* initPassenger()
* -------------
* Inicializa os dados do passageiro
*/
void initPassenger(passenger* p, TCHAR* origin, TCHAR* destination, TCHAR* name, int waiting)
{
	p->pid = GetCurrentProcessId();
	_tcscpy_s(p->name, BUFFERSIZE, name);
	_tcscpy_s(p->origin, BUFFERSMALL, origin);
	_tcscpy_s(p->destination, BUFFERSMALL, destination);
	p->waiting_time = waiting;
	p->pipe_index = -1;	// ainda não foi atribuído
	p->state = GROUND;
	p->pidPlane = 0;
}

/*
* initPipe()
* -------------
* Abre o pipe
*/
HANDLE initPipe()
{
	HANDLE hPipe = NULL;
	DWORD dwMode;
	BOOL fSuccess;

	while (1)
	{
		// abre o pipe
		hPipe = CreateFile(
			PIPE_NAME,	// pipe name
			GENERIC_READ |	// read/write access
			GENERIC_WRITE,
			0,			// no sharing
			NULL,		// default security attributes
			OPEN_EXISTING,	// open pipe
			FILE_FLAG_OVERLAPPED,	// overlapped attribute
			NULL);		// no template file

		// se obteve um pipe válido sai da função
		if (hPipe != INVALID_HANDLE_VALUE)
			break;

		// erro outro que não PIPE_BUSY -> desiste
		if (GetLastError() != ERROR_PIPE_BUSY)
		{
			return NULL;
		}

		// se o pipe não estiver disponível dentro de 20 segundos -> desiste
		if (!WaitNamedPipe(PIPE_NAME, 20000))
		{
			_tprintf(TEXT("Nenhuma instância do pipe se libertou em 20 segundos! O programa vai terminar\n"));
			return NULL;
		}
	}

	dwMode = PIPE_READMODE_MESSAGE;
	fSuccess = SetNamedPipeHandleState(
		hPipe,		// handle para o pipe
		&dwMode,	// novo pipe mode
		NULL,		// max bytes, NULL = não mudar
		NULL);		// max tiime, NULL = não mudar

	if (!fSuccess)
	{
		_tprintf(TEXT("Houve um erro na alteração do modo do pipe!\n"));
		return NULL;
	}

	return hPipe;
}

/*
* initMutex()
* -------------
* Inicializa o objeto mutex
*/
HANDLE createUnnamedMutex()
{
	// Cria o mutex
	HANDLE mutex = CreateMutex(
		NULL,			// security attributes
		FALSE,			// calling thread obtains initial ownership of mutex object
		NULL);			// unname mutex object

	return mutex;
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
* createThread()
* -------------------
* Cria uma thread pendente (não inicia logo a sua execução)
*/
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

/*
* threadReadSharedMemory()
* -------------
* Thread que permite ler da memória partilhada
*/
DWORD WINAPI threadShutdown(LPVOID args)
{
	thread_data* data = (thread_data*)args;

	// espera até que o objeto evento seja assinalado
	WaitForSingleObject(data->hEventShutdown, INFINITE);

	_tprintf(TEXT("O controlador efetuou o comando shutdown!\nO programa vai terminar!\n"));


	return 0;
}

/*
* threadReadSharedMemory()
* -------------
* Thread que permite ler da memória partilhada
*/
DWORD WINAPI threadReadPipe(LPVOID args)
{
	thread_data* data = (thread_data*)args;
	DWORD cbRead, dwErr;
	item_pipe answer;
	BOOL fSuccess;

	while (1)
	{
		fSuccess = ReadFile(
			data->hPipe,	// handle para o pipe
			&answer,		// buffer para a reposta
			sizeof(item_pipe),	// tamanho do buffer
			&cbRead,		// bytes lidos
			NULL);			// não overlapped

		// a operação de leitura foi concluída com sucesso
		if (fSuccess && cbRead != 0)
		{
			// trata da resposta do controlador
			if (!handleAnswerFromController(data, answer))
				break;
			continue;
		}

		// a operação de leitura ainda está pendente
		dwErr = GetLastError();
		if (!fSuccess && (dwErr == ERROR_IO_PENDING))
		{
			_tprintf(TEXT("A resposta do controlador ainda está pendente...\n"));
			continue;
		}

		// um erro aconteceu
		//_tprintf(TEXT("Não foi possível ler a resposta do controlador. Erro [%d]\n"), dwErr);
		break;
	}

	return 0;
}

/*
* initIdentification()
* -------------
* Identifica-se ao controlador
*/
BOOL initIdentification(thread_data* data)
{
	item_pipe request;
	passenger* p = &data->passenger;

	// limpa o "lixo"
	ZeroMemory(&request, sizeof(item_pipe));

	// inicializa o item que vai enviar ao controlador
	_tcscpy_s(request.command, BUFFERSIZE, CMD_IDENTIFICATION_PASSENGER);
	_stprintf_s(request.extra, BUFFERSIZE, TEXT("%d|%s|%s|%s|%d"), p->pid, p->name, p->origin, p->destination, p->waiting_time);

	// envia o pedido
	if (!sendRequest(data->hPipe, &request))
	{
		_tprintf(TEXT("Erro ao enviar o pedido ao controlador.\n"));
		return FALSE;
	}
	_tprintf(TEXT("O pedido de identificação foi enviado ao controlador.\n"));

	return TRUE;
}

/*
* initIdentification()
* -------------
* Identifica-se ao controlador
*/
BOOL sendRequest(HANDLE hPipe, item_pipe* request)
{
	BOOL fSuccess;
	DWORD cbWritten;	// bytes escritos

	// escreve no pipe
	fSuccess = WriteFile(
		hPipe,				// handle para o pipe
		request,			// pedido
		sizeof(item_pipe),	// tamanho em bytes do pedido
		&cbWritten,			// bytes escritos
		NULL);

	return fSuccess;	
}

/*
* isInteger()
* -------------
* Verifica se o input é um número
*/
BOOL isInteger(TCHAR* input)
{
	// verifica se o input é 0
	if (_tcscmp(input, TEXT("0")) == 0)
	{
		return TRUE;
	}

	// verifica se é um número
	if (_tstoi(input) != 0)
		return TRUE;

	return FALSE;
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

	// fica em ciclo até escrever 'fim'
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

		if (_tcscmp(cmd, CMD_DETAILS) == 0)
		{
			// imprime os dados do passageiro
			cmdDetails(data->passenger);
		}
		else
		{
			_tprintf(TEXT("Comando [%s] inválido!\n"), cmd);
		}
	}
	return 0;
}

/*
* cmdDetails()
* -------------
* Imprime os dados do passageiro
*/
void cmdDetails(passenger p)
{
	_tprintf(TEXT("%5s\t%10s\t%10s\t%10s\t%6s\t%10s\t%10s\n"), TEXT("PID"), TEXT("Nome"), TEXT("Origem"), TEXT("Destino"), TEXT("Estado"), TEXT("Tempo Espera"), TEXT("Avião #"));
	_tprintf(TEXT("%5d\t%10s\t%10s\t%10s\t%6d\t%10d\t%10d\n"), p.pid, p.name, p.origin, p.destination, p.state, p.waiting_time, p.pidPlane); 
}

/*
* cmdDetails()
* -------------
* Imprime os dados do passageiro
*/
BOOL handleAnswerFromController(thread_data* data, item_pipe answer)
{
	TCHAR cmd[BUFFERSIZE];
	_tcscpy_s(cmd, BUFFERSIZE, answer.command);

	// trata o comando recebido
	if (_tcscmp(cmd, VALID_IDENTIFICATION) == 0)
	{
		_tprintf(TEXT("A identificação foi validada pelo controlador.\n%s\n"), answer.extra);
	}
	else if (_tcscmp(cmd, INVALID_IDENTIFICATION) == 0)
	{
		_tprintf(TEXT("A identificação não foi validada pelo controlador.\n%s\n"), answer.extra);
		return FALSE;
	}
	else if (_tcscmp(cmd, VALID_BOARDING) == 0)
	{
		data->passenger.state = FLYING;
		data->passenger.pidPlane = _tstoi(answer.extra);
		_tprintf(TEXT("Embarcou no avião %s\n"), answer.extra);
	}
	else if (_tcscmp(cmd, ARRIVED_AT_DESTINATION) == 0)
	{
		data->passenger.state = GROUND;
		_tprintf(TEXT("%s\n"), answer.extra);
		return FALSE;
	}
	else if (_tcscmp(cmd, VALID_MOVEMENT) == 0)
	{
		_tprintf(TEXT("%s\n"), answer.extra);
	}
	return TRUE;
}

/*
* threadWaitingTime()
* -------------
* Thread que verifica se o tempo de espera até embarcar esgotou
*/
DWORD WINAPI threadWaitingTime(LPVOID args)
{
	thread_data* data = (thread_data*)args;

	if (data->passenger.waiting_time > 0)
	{
		Sleep(data->passenger.waiting_time * 1000);

		// espera ter acesso aos dados
		WaitForSingleObject(data->hMutex, INFINITE);

		if (data->passenger.state == GROUND)
		{
			_tprintf(TEXT("Fartei-me de esperar. Vou sair!\n"));
			CloseHandle(data->hPipe);
			exit(-1);
		}

		// liberta o mutex
		ReleaseMutex(data->hMutex);

		
		
	}

	return 0;
}

