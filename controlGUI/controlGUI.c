#include <Windows.h>
#include <windowsx.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <Psapi.h>
#include <commctrl.h>    
#include "../control/registry.h"
#include "../control/comum.h"
#include "resource.h"

#ifdef UNICODE
#define _tWinMain wWinMain
#else
#define _tWinMain WinMain
#endif // UNICODE


#define NUM_THREADS		3
#define WAITING_TIME	3000

// GUI
#define APP_NAME TEXT("Control GUI - SGEA")
#define CLASS_NAME TEXT("CLASS_NAME")
#define WINDOW_HEIGHT	900
#define WINDOW_WIDTH	900

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
#define CMD_ADD_AIRPORTS	TEXT("ADD AIRPORTS")	// adiciona 4 aeroporto ao mapa
#define CMD_PAUSE		TEXT("PAUSE")		// bloqueia a aceita��o de novos avi�es
#define CMD_UNPAUSE		TEXT("UNPAUSE")		// desbloqueia a aceita��o de novos avi�es
#define CMD_HELP		TEXT("HELP")		// lista os comandos dispon�veis

// identificador dos controls
#define BTN_COMMANDS	1
#define WND_LOGS		2

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
	circular_buffer* ptrSharedMemory;		// ponteiro para a mem�ria partilhada
	map_data				map;					// representa o mapa
	sync_circular_buffer	sync_circular_buffer;	// sincroniza��o do buffer circular
	sync_plane_access		sync_plane_access;		// sincroniza��o de acesso dos avi�es
	HANDLE					hEventShutdown;			// evento assinalado quando � executado o comando shutdown
	PIPE_INSTANCE* pipe;					// ponteiros para array de inst�ncias do pipe
	HANDLE					hEvents[MAX_PASSAG];	// objetos evento para as inst�ncias do pipe
	HWND					hWnd;			// handle para a janela do programa
} thread_data;

// GUI
ATOM registerClass(HINSTANCE, TCHAR*);
HWND createWindow(HINSTANCE, TCHAR*, thread_data*);
LRESULT CALLBACK handleEvents(HWND, UINT, WPARAM, LPARAM);
BOOL addControls(HWND);
BOOL CALLBACK dlgAddAirport(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK dlgExecuteCommands(HWND, UINT, WPARAM, LPARAM);
void imprimeBordasMapa(HWND, HDC);
void imprimeAeroportos(HWND, HDC, thread_data*);
void mostraDadosAeroporto(HWND, LPARAM, map_data*);
void imprimeAvioes(HWND, HDC, thread_data*);
void mostraDadosAviao(HWND, LPARAM, map_data*);


// inicializa��o dos dados
HANDLE initMapFile();
circular_buffer* initMapViewOfFile(HANDLE);
DWORD readValueInRegistry(TCHAR*);
DWORD handleValueReadInRegistry(TCHAR*);
BOOL initMap(map_data*);
HANDLE createEvent(TCHAR*);
HANDLE createMutex(TCHAR*);
HANDLE createSemaphore(TCHAR*, int);
HANDLE createEmptySemaphore(TCHAR*, int);
BOOL initSyncPlaneAccess(thread_data*);
void closeSyncPlaneAccess(sync_plane_access*);
BOOL initSyncCircularBuffer(thread_data*);
void closeSyncCircularBuffer(sync_circular_buffer*);
void initCircularBuffer(thread_data*);

// comandos do controlador
void cmdShutdown(thread_data*);
BOOL cmdAddAirport(map_data*, airport); 
BOOL cmdPause(thread_data*);
BOOL cmdUnpause(thread_data*);
BOOL cmdAddAirports(map_data*);


// mapa
BOOL isAirportFull(map_data*);
BOOL isValidAirportData(airport);
BOOL isAirportNameRepeated(map_data*, TCHAR*);
BOOL isPositionEmpty(map_data*, position);
BOOL isAnotherAirportAround(map_data*, airport);
BOOL isPlanesFull(map_data*);
position getPositionOfAirport(map_data*, TCHAR*);
BOOL isValidPosition(map_data*, position);
passenger getPassengerFromIdentification(TCHAR*);
BOOL isValidPassenger(thread_data*, passenger*);
BOOL isInteger(TCHAR*);

// comandos recebidos pelos avi�es
void cmdAddPlane(thread_data*, item*);
void cmdDefine(thread_data*, item*);
void cmdBoarding(thread_data*, item*);
void cmdStartFlight(thread_data*, item*);



// threads
DWORD WINAPI threadReadSharedMemory(LPVOID);
DWORD WINAPI threadStillAlive(LPVOID);
DWORD WINAPI threadReadPipeInstances(LPVOID);

void handleCommandFromPlane(thread_data*, item);


// altera��o dos dados do mapa
void addAirport(map_data*, airport*);
void addPlane(map_data*, plane*);
void writeInSharedMemory(thread_data*, item*);
void updatePlane(thread_data*, plane*);
BOOL removePlane(thread_data*, int);
void updatePlanePositions(thread_data*, plane*);
void removePassenger(map_data*, int);
void addPassenger(map_data*, passenger*);


// pipes
BOOL initPipeInstances(thread_data*);
BOOL ConnectToNewClient(HANDLE, OVERLAPPED*);
void DisconnectAndReconnect(PIPE_INSTANCE*, DWORD);
void getAnswerForPassenger(thread_data*, PIPE_INSTANCE*, int);
BOOL sendMessageToPassenger(PIPE_INSTANCE*, item_pipe*);






HWND hWndLogs;				// caixa de texto para logs


int WINAPI _tWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nWinMode)
{
	HWND hWnd;
	MSG msg;
	HANDLE hMapFile;	// handle para o ficheiro de mapeamento
	thread_data data;	// dados passados para as threads

	// ************************DADOS*******************************
	// inicializa o ficheiro de mapeamento
	hMapFile = initMapFile();
	if (hMapFile == NULL)
	{
		MessageBoxEx(NULL, TEXT("Erro na inicializa��o do ficheiro de mapeamento!"), TEXT("Inicializa��o do ficheiro de mapeamento"), MB_OK | MB_ICONERROR, 0);
		return -1;
	}

	// abre a vista da mem�ria partilhada do buffer circular
	data.ptrSharedMemory = initMapViewOfFile(hMapFile);
	if (data.ptrSharedMemory == NULL)
	{
		MessageBoxEx(NULL, TEXT("Erro no mapeamento da vista!"), TEXT("Mapeamento da vista"), MB_OK | MB_ICONERROR, 0);
		CloseHandle(hMapFile);
		return -1;
	}

	// obtem o n�mero m�ximo de aeroportos e avi�es do registry
	data.map.maxAirports = handleValueReadInRegistry(AIRPORT_SUBKEY);
	data.map.maxPlanes = handleValueReadInRegistry(PLANE_SUBKEY);

	// inicializa o mapa
	if (!initMap(&data.map))
	{
		MessageBoxEx(NULL, TEXT("Erro na inicializa��o do mapa!"), TEXT("Inicializa��o do mapa"), MB_OK | MB_ICONERROR, 0);
		CloseHandle(hMapFile);
		return -1;
	}

	// inicializa a estrutura de sincroniza��o de acesso dos avi�es
	if (!initSyncPlaneAccess(&data))
	{
		MessageBoxEx(NULL, TEXT("Erro na inicializa��o da estrutura de sincroniza��o de acesso dos avi�es!"), TEXT("Sincroniza��o dos avi�es"), MB_OK | MB_ICONERROR, 0);
		CloseHandle(hMapFile);
		return -1;
	}

	// inicializa a estrutura de sincroniza��o do buffer circular
	if (!initSyncCircularBuffer(&data))
	{
		MessageBoxEx(NULL, TEXT("Erro na inicializa��o da estrutura de sincroniza��o do buffer circular!"), TEXT("Sincroniza��o do buffer circular"), MB_OK | MB_ICONERROR, 0);
		CloseHandle(hMapFile);
		return -1;
	}

	// inicializa o evento que controla se o comando shutdown � executado
	if ((data.hEventShutdown = createEvent(EVENT_SHUTDOWN)) == NULL)
	{
		MessageBoxEx(NULL, TEXT("Erro na cria��o do objeto evento que controla se o comando shutdown � executado!!"), TEXT("Cria��o de objeto evento"), MB_OK | MB_ICONERROR, 0);
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
	
	// ************************GUI*******************************
	// regista a classe da janela
	if (!registerClass(hInstance, CLASS_NAME))
	{
		MessageBoxEx(NULL, TEXT("N�o foi poss�vel registar a classe da janela principal."), TEXT("Erro no registo da classe da janela"), MB_OK | MB_ICONERROR, 0);
		return 0;
	}

	// cria a janela
	if ((hWnd = createWindow(hInstance, CLASS_NAME, &data)) == NULL)
	{
		MessageBoxEx(NULL, TEXT("N�o foi poss�vel criar a janela principal."), TEXT("Erro"), MB_OK | MB_ICONERROR, 0);
		return 0;
	}

	// apresenta a janela no ecr�
	ShowWindow(hWnd, nWinMode);
	UpdateWindow(hWnd);

	// ciclo de mensagens
	while (GetMessage(&msg, NULL, 0, 0))
	{
		// traduz mensagens de teclas virtuais em mensagens de caracteres
		TranslateMessage(&msg);
		// reencaminha a mensagem para a janela alvo
		DispatchMessage(&msg);
	}

	return msg.wParam;
}

/*
* registerClass()
* ---------------
* Regista a classe da janela
*/
ATOM registerClass(HINSTANCE hInstance, TCHAR* className)
{
	WNDCLASSEX wc;

	HICON icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_SGEA));

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.hInstance = hInstance;		// handle para esta inst�ncia
	wc.lpszClassName = className;	// nome da classe
	wc.lpfnWndProc = handleEvents;	// fun��o da janela
	wc.style = CS_HREDRAW;			// estilo default
	wc.hIcon = icon;	// icone std
	wc.hIconSm = icon;	// icone menor
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);	// estilo do cursor
	wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU);			// com menu
	wc.cbClsExtra = 0;				// sem info extra
	wc.cbWndExtra = sizeof(thread_data);				// sem info extra
	wc.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);	// cor de fundo

	return RegisterClassEx(&wc);
}

/*
* createWindow()
* -------------
* Cria a janela
*/
HWND createWindow(HINSTANCE hInstance, TCHAR* className, thread_data* data)
{
	HWND hWndDesktop;	// handle para a janela desktop
	RECT rectDesktop;	// dimens�es da janela
	int posX, posY;		// coordenadas X e Y onde deve ser criada a janela do programa

	hWndDesktop = GetDesktopWindow();			// obtem o handle da janela desktop
	GetClientRect(hWndDesktop, &rectDesktop);	// obtem as dimens�es da janela desktop
	
	posX = (rectDesktop.right / 2) - WINDOW_WIDTH/2;	// calcula a coordenada X
	posY = (rectDesktop.bottom / 2) - WINDOW_WIDTH / 2;	// calcula a coordenada Y	

	HWND hWnd = CreateWindowEx(
		WS_EX_WINDOWEDGE,
		className,				// nome da classe desta janela
		APP_NAME,				// t�tulo
		WS_OVERLAPPEDWINDOW,	// estilo de janela normal
		posX,						// coordenada X = escolhida pelo Windows
		posY,						// coordenada y = escolhida pelo Windows
		WINDOW_WIDTH,					// largura = escolhida pelo Windows
		WINDOW_HEIGHT,					// altura = escolhida pelo Windows
		HWND_DESKTOP,			// sem janela pai
		NULL,					// com menu - define NULL para que o sistema use o menu da classe
		hInstance,				// handle para esta inst�ncia do programa
		data);					// com argumentos (informa��o adicional)

	return hWnd;
}

/*
* handleEvents()
* -------------
* Trata os eventos enviados � janela
*/
LRESULT CALLBACK handleEvents(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static CREATESTRUCT* cs;
	static thread_data* data;
	PAINTSTRUCT ps;
	HDC hdc;
	HANDLE hThread[NUM_THREADS];
	TCHAR buffer[1024] = { '\0' };


	switch (msg)
	{
	case WM_CREATE:
		cs = (CREATESTRUCT*)lParam;
		data = cs->lpCreateParams;	// obtem um ponteiro para os dados	
		data->hWnd = hWnd;

		// adiciona os controls � janela
		if (!addControls(hWnd))
		{
			MessageBoxEx(hWnd, TEXT("N�o foi poss�vel adicionar os controls � janela."), TEXT("Erro de janela"), MB_OK | MB_ICONERROR, 0);
			PostQuitMessage(0);
		}

		// cria a thread que l� a mem�ria partilhada
		if ((hThread[0] = createThread(threadReadSharedMemory, data)) == NULL)
		{
			MessageBoxEx(hWnd, TEXT("N�o foi poss�vel criar a thread que l� � mem�ria partilhada"), TEXT("Erro na cria��o de thread"), MB_OK | MB_ICONERROR, 0);
			PostQuitMessage(0);
		}

		// cria a thread que verifica se os processos avi�o ainda est�o em execu��o
		if ((hThread[1] = createThread(threadStillAlive, data)) == NULL)
		{
			MessageBoxEx(hWnd, TEXT("N�o foi poss�vel criar a thread que verifica a execu��o dos processos avi�o"), TEXT("Erro na cria��o de thread"), MB_OK | MB_ICONERROR, 0);
			PostQuitMessage(0);
		}

		// cria a thread que l� os pedidos das inst�ncias do pipe
		if ((hThread[2] = createThread(threadReadPipeInstances, data)) == NULL)
		{
			MessageBoxEx(hWnd, TEXT("N�o foi poss�vel criar a thread que l� os pedidos dos pipes"), TEXT("Erro na cria��o de thread"), MB_OK | MB_ICONERROR, 0);
			PostQuitMessage(0);
		}
		break;

		// paint
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);

		// borda do mapa
		imprimeBordasMapa(hWnd, hdc);

		// imprime os aeroportos no mapa
		imprimeAeroportos(hWnd, hdc, data);

		// imprime os avi�es em voo
		imprimeAvioes(hWnd, hdc, data);	

		EndPaint(hWnd, &ps);
		break;

	case WM_LBUTTONDOWN:
		mostraDadosAeroporto(hWnd, lParam, &data->map);
		break;

	case WM_RBUTTONDOWN:
		mostraDadosAviao(hWnd, lParam, &data->map);
		break;

		// menus
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			// menu controlador > shutdown
		case ID_CONTROLADOR_SHUTDOWN:
			if (MessageBoxEx(hWnd, TEXT("Deseja mesmo efetuar shutdown ao sistema?"), TEXT("Shutdown"), MB_YESNO | MB_ICONWARNING, 0) == IDYES)
			{
				cmdShutdown(data);
				MessageBoxEx(hWnd, TEXT("Comando shutdown efetuado! Os avi�es e passageiros foram informados."), TEXT("Shutdown"), MB_OK | MB_ICONINFORMATION, 0);
				PostQuitMessage(0);
			}
			break;

			//  menu aeroportos > adicionar
		case ID_AEROPORTOS_ADICIONAR:
			DialogBoxParam(
				NULL,
				MAKEINTRESOURCE(DLG_ADD_AIRPORT),	// template
				hWnd,			// janela pai
				dlgAddAirport,	// fun��o da dialog
				(LPARAM)data);	// parametro
			// gerar WS_PAINT
			InvalidateRect(hWnd, NULL, TRUE);
			break;

			// menu aeroportos > listar
		case ID_AEROPORTOS_LISTAR:			
			// entra no mutex
			WaitForSingleObject(data->map.hMutex, INFINITE);

			// verifica se h� aeroportos
			if (data->map.numAirports == 0)
			{
				ReleaseMutex(data->map.hMutex);
				SetWindowText(hWndLogs, TEXT("Nenhum aeroporto inserido no sistema!"));
				break;
			}

			// insere os aeroportos no buffer
			_tcscpy_s(buffer, 1024, TEXT("Lista de aeroportos:\r\n"));
			for (int i = 0; i < data->map.numAirports; i++)
			{
				_stprintf_s(buffer, 1024, TEXT("%s %s [%d,%d]\r\t"), buffer, data->map.airports[i].name, data->map.airports[i].position.x, data->map.airports[i].position.y);
			}

			// liberta o mutex
			ReleaseMutex(data->map.hMutex);
			
			SetWindowText(hWndLogs, buffer);
			break;

			// menu avi�es > listar
		case ID_AVIOES_LISTAR:
			// entra no mutex
			WaitForSingleObject(data->map.hMutex, INFINITE);

			// verifica se h� aeroportos
			if (data->map.numPlanes == 0)
			{
				ReleaseMutex(data->map.hMutex);
				SetWindowText(hWndLogs, TEXT("Nenhum avi�o inserido no sistema!"));
				break;
			}

			// insere os aeroportos no buffer			
			_stprintf_s(buffer, 1024, TEXT("%5s\r\t%6s\r\t%6s\r\t%10s\r\t%10s\r\t%10s\r\t%10s\r\t%10s\r\t%6s\r\n"), TEXT("PID"), TEXT("Capac."), TEXT("Speed"), TEXT("Origem"), TEXT("Destino"), TEXT("Pos. Atual"), TEXT("Pos.Destino"), TEXT("N�m. Passag."), TEXT("Estado"));

			for (int i = 0; i < data->map.numPlanes; i++)			
				_stprintf_s(buffer, 1024, TEXT("%s%5d\r\t%6d\r\t%6d\r\t%10s\r\t%10s\r\t[%4d,%4d]\r\t[%4d,%4d]\r\t%10d\r\t%6d\r\n"), buffer, data->map.planes[i].pid, data->map.planes[i].capacity, data->map.planes[i].speed, data->map.planes[i].origin, data->map.planes[i].destination, data->map.planes[i].pos_actual.x, data->map.planes[i].pos_actual.y, data->map.planes[i].pos_destination.x, data->map.planes[i].pos_destination.y, data->map.planes[i].num_passenger, data->map.planes[i].state);
			

			// liberta o mutex
			ReleaseMutex(data->map.hMutex);

			SetWindowText(hWndLogs, buffer);			
			break;

			// menu avi�es > bloquear
		case ID_AVIOES_BLOCK:
			if (cmdPause(data))
			{
				SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es foi bloqueada!"));
				MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es foi bloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONINFORMATION, 0);
			}
			else
			{
				SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es j� estava bloqueada!"));
				MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es j� estava bloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONWARNING, 0);
			}
			break;

			// menu avi�es > desbloquear
		case ID_AVIOES_UNBLOCK:
			if (cmdUnpause(data))
			{
				SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es foi desbloqueada!"));
				MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es foi desbloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONINFORMATION, 0);
			}
			else
			{
				SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es j� estava desbloqueada!"));
				MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es j� estava desbloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONWARNING, 0);
			}
			break;

			//  menu passageiros > listar
		case ID_PASSAGEIROS_LISTAR:
			// entra no mutex
			WaitForSingleObject(data->map.hMutex, INFINITE);

			// verifica se h� aeroportos
			if (data->map.numPassengers == 0)
			{
				ReleaseMutex(data->map.hMutex);
				SetWindowText(hWndLogs, TEXT("Nenhum passageiro inserido no sistema!"));
				break;
			}

			// insere os aeroportos no buffer			
			_stprintf_s(buffer, 1024, TEXT("%5s\r\t%10s\r\t%10s\r\t%10s\r\t%6s\r\t%10s\r\t%10s\r\n"), TEXT("PID"), TEXT("Nome"), TEXT("Origem"), TEXT("Destino"), TEXT("Estado"), TEXT("Tempo Esp."), TEXT("Avi�o"));

			for (int i = 0; i < data->map.numPassengers; i++)
				_stprintf_s(buffer, 1024, TEXT("%s%5d\r\t%10s\r\t%10s\r\t%10s\r\t%6d\r\t%10d\r\t%10d\r\n"), buffer, data->map.passengers[i].pid, data->map.passengers[i].name, data->map.passengers[i].origin, data->map.passengers[i].destination, data->map.passengers[i].state, data->map.passengers[i].waiting_time, data->map.passengers[i].pidPlane);


			// liberta o mutex
			ReleaseMutex(data->map.hMutex);

			SetWindowText(hWndLogs, buffer);
			break;

			// bot�o executar comandos
		case BTN_COMMANDS:
			DialogBoxParam(
				NULL,
				MAKEINTRESOURCE(DLG_EXECUTE_CMD),	// template
				hWnd,					// janela pai
				dlgExecuteCommands,		// fun��o da dialog
				(LPARAM)data);			// parametro
			InvalidateRect(hWnd, NULL, TRUE);

			break;
		}
		break;

		// Destroi a janela e termina o programa
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

		// antes de fechar a janela
	case WM_CLOSE:
		if (MessageBoxEx(hWnd, TEXT("Deseja mesmo sair e terminar o programa?"), TEXT("Terminar o programa"), MB_YESNO | MB_ICONWARNING, 0) == IDYES)
			PostQuitMessage(0);
		break;

	default:
		// chama o procedimento default da janela
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}

	return 0;
}

/*
* addControls()
* -------------
* Adiciona os controlos � janela
*/
BOOL addControls(HWND hWnd)
{
	HWND hButtonCommands;	// handle para o bot�o que permite executar comandos
	

	// cria o bot�o que permite executar comandos
	hButtonCommands = CreateWindowEx(
		WS_EX_LEFT,	// estilo "extended" da janela
		WC_BUTTON,			// classe do control
		TEXT("Executar comando"),	// texto do control
		WS_CHILD |			// estilo do control
		WS_VISIBLE |		// inicialmente vis�vel
		WS_BORDER |			// control com borda ligeira
		BS_MULTILINE,		// texto em v�rias linhas se necess�rio
		10,					// coordenada x
		10,					// coordenada y
		80,					// largura
		80,					// altura
		hWnd,				// janela pai
		(HMENU)BTN_COMMANDS,// identificador do control
		NULL,				// handle para a ins�ncia do m�dulo a ser associado com a janela
		NULL);				// sem parametros passados
	
	if (hButtonCommands == NULL)
		return FALSE;

	// cria o control que mostra os logs
	hWndLogs = CreateWindowEx(
		WS_EX_LEFT,	// estilo "extended" da janela
		WC_EDIT,			// classe do control
		TEXT(""),	// texto do control
		WS_CHILD |			// estilo do control
		WS_VISIBLE |		// inicialmente vis�vel
		WS_BORDER |			// control com borda ligeira
		ES_MULTILINE |		// texto em v�rias linhas se necess�rio
		ES_READONLY |		// n�o permite ao utilizador escrever
		WS_VSCROLL,			// scroll vertical
		100,				// coordenada x
		10,					// coordenada y
		WINDOW_WIDTH - 130,		// largura
		80,					// altura
		hWnd,				// janela pai
		(HMENU)WND_LOGS,// identificador do control
		NULL,				// handle para a ins�ncia do m�dulo a ser associado com a janela
		NULL);				// sem parametros passados

	if (hWndLogs == NULL)
		return FALSE;

	return TRUE;
}

/*
* dlgAddAirport()
* -------------
* Fun��o da dialog que permite adicionar aeroportos
*/
BOOL CALLBACK dlgAddAirport(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static thread_data* data;
	airport airport;
	BOOL isTranslated;
	TCHAR input[BUFFERSMALL];	// texto a adicionar � edit control
	HWND hWndDesktop;
	RECT rectDesktop, rectDlg;
	int posX, posY;

	switch (msg)
	{
		// inicializa��o da dialog
	case WM_INITDIALOG:
		data = (thread_data*)lParam;
		// centra a dialog no meio do ecr�
		hWndDesktop = GetDesktopWindow();			// obtem o handle da janela desktop
		GetClientRect(hWndDesktop, &rectDesktop);	// obtem as dimens�es da janela desktop
		GetClientRect(dlg, &rectDlg);	// obtem as dimens�es da dialog

		posX = (rectDesktop.right / 2) - rectDlg.right / 2;		// calcula a coordenada X
		posY = (rectDesktop.bottom / 2) - rectDlg.bottom / 2;	// calcula a coordenada Y	

		SetWindowPos(dlg, HWND_TOP, posX, posY, 0, 0, SWP_NOSIZE);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			// bot�o OK clicado
		case IDOK:
			// obtem o input
			GetDlgItemText(dlg, ecAirportName, airport.name, BUFFERSMALL);
			airport.position.x = GetDlgItemInt(dlg, ecAirportX, &isTranslated, FALSE);
			if (!isTranslated)
				airport.position.x = -1;
			airport.position.y = GetDlgItemInt(dlg, ecAirportY, &isTranslated, FALSE);
			if (!isTranslated)
				airport.position.y = -1;

			// adiciona o aeroporto
			if (cmdAddAirport(&data->map, airport))
			{
				_stprintf_s(input, BUFFERSMALL, TEXT("O aeroporto [%s] foi adicionado na posi��o [%d,%d]."), airport.name, airport.position.x, airport.position.y);
				SetWindowText(hWndLogs, input);
				MessageBoxEx(dlg, input, TEXT("Aeroporto"), MB_OK | MB_ICONINFORMATION, 0);
			}

			EndDialog(dlg, IDOK);
			return TRUE;

			// bot�o CANCEL clicado
		case IDCANCEL:
			EndDialog(dlg, IDOK);
			return TRUE;

		default:
			return TRUE;
		}

		// fecho da dialog
	case WM_CLOSE:
		EndDialog(dlg, IDOK);
		return TRUE;
	}

	return FALSE;
}

/*
* dlgAddAirport()
* -------------
* Fun��o da dialog que permite adicionar aeroportos
*/
BOOL CALLBACK dlgExecuteCommands(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static thread_data* data;
	HWND hWndDesktop;
	RECT rectDesktop, rectDlg;
	int posX, posY;
	TCHAR buffer[BUFFERSIZE];
	int cmdSize;

	switch (msg)
	{
		// inicializa��o da dialog
	case WM_INITDIALOG:
		data = (thread_data*)lParam;
		// centra a dialog no meio do ecr�
		hWndDesktop = GetDesktopWindow();			// obtem o handle da janela desktop
		GetClientRect(hWndDesktop, &rectDesktop);	// obtem as dimens�es da janela desktop
		GetClientRect(dlg, &rectDlg);	// obtem as dimens�es da dialog

		posX = (rectDesktop.right / 2) - rectDlg.right / 2;		// calcula a coordenada X
		posY = (rectDesktop.bottom / 2) - rectDlg.bottom / 2;	// calcula a coordenada Y	

		SetWindowPos(dlg, HWND_TOP, posX, posY, 0, 0, SWP_NOSIZE);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
			// bot�o OK clicado
		case IDOK:
			// obtem o input
			GetDlgItemText(dlg, ecCmd, buffer, BUFFERSIZE);

			// maiusculas
			cmdSize = _tcslen(buffer);
			for (int i = 0; i < cmdSize; i++)
				buffer[i] = _totupper(buffer[i]);

			if (_tcscmp(buffer, CMD_SHUTDOWN) == 0)
			{
				cmdShutdown(data);
				MessageBoxEx(dlg, TEXT("Comando shutdown efetuado! Os avi�es e passageiros foram informados."), TEXT("Shutdown"), MB_OK | MB_ICONINFORMATION, 0);
				PostQuitMessage(0);
			}
			else if (_tcscmp(buffer, CMD_ADD_AIRPORTS) == 0)
			{
				if (cmdAddAirports(&data->map))
				{
					SetWindowText(hWndLogs, TEXT("Os aeroportos foram adicionados com sucesso."));
					MessageBoxEx(dlg, TEXT("Os aeroportos foram adicionados com sucesso."), TEXT("Aeroportos adicionados"), MB_OK | MB_ICONINFORMATION, 0);
				}
			}
			else if (_tcscmp(buffer, CMD_HELP) == 0)
			{
				TCHAR data[1024] = { '\0' };

				_stprintf_s(data, 1024, TEXT("\'%s\'\r\n\r\t%s\r\n\'%s\'\r\n\r\t%s\r\n\'%s\'\r\n\r\t%s\r\n\'%s\'\r\n\r\t%s\r\n\'%s\'\r\n\r\t%s\r\n\'%s\'\r\n\r\t%s\r\n"),
					CMD_SHUTDOWN, TEXT("-- Encerra o sistema"),
					CMD_ADD_AIRPORTS, TEXT("-- Adiciona 4 aeroportos j� definidos por defeito"),
					CMD_PAUSE, TEXT("-- Bloqueia a aceita��o de novos avi�es"),
					CMD_UNPAUSE, TEXT("-- Desbloqueia a aceita��o de novos avi�es"),
					CMD_CLEAR, TEXT("-- Limpa a consola"),
					CMD_CLS, TEXT("-- Limpa a consola"));
				MessageBoxEx(dlg, data, TEXT("Ajuda"), MB_OK | MB_ICONINFORMATION, 0);
				SetWindowText(hWndLogs, data);
			}
			else if (_tcscmp(buffer, CMD_PAUSE) == 0)
			{
				if (cmdPause(data))
				{
					SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es foi bloqueada!"));
					MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es foi bloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONINFORMATION, 0);
				}
				else
				{
					SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es j� estava bloqueada!"));
					MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es j� estava bloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONWARNING, 0);
				}
			}
			else if (_tcscmp(buffer, CMD_UNPAUSE) == 0)
			{
				if (cmdUnpause(data))
				{
					SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es foi desbloqueada!"));
					MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es foi desbloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONINFORMATION, 0);
				}
				else
				{
					SetWindowText(hWndLogs, TEXT("A aceita��o de novos avi�es j� estava desbloqueada!"));
					MessageBoxEx(NULL, TEXT("A aceita��o de novos avi�es j� estava desbloqueada!"), TEXT("Aceita��o de avi�es"), MB_OK | MB_ICONWARNING, 0);
				}
			}
			else if (_tcscmp(buffer, CMD_CLEAR) == 0)
			{
				SetWindowText(hWndLogs, TEXT(""));
			}
			else if (_tcscmp(buffer, CMD_CLS) == 0)
			{
				SetWindowText(hWndLogs, TEXT(""));
			}
			else
			{
				MessageBoxEx(dlg, TEXT("O comando inserido n�o � v�lido!"), TEXT("Comando inv�lido"), MB_OK | MB_ICONERROR, 0);
			}
			
			EndDialog(dlg, IDOK);
			return TRUE;

			// bot�o CANCEL clicado
		case IDCANCEL:
			EndDialog(dlg, IDOK);
			return TRUE;

		default:
			return TRUE;
		}

		// fecho da dialog
	case WM_CLOSE:
		EndDialog(dlg, IDOK);
		return TRUE;
	}

	return FALSE;
}

/*
* initMapFile()
* -------------------
* Inicializa o ficheiro de mapeamento
*/
HANDLE initMapFile()
{
	HANDLE hMapFile;	// handle para o ficheiro de mapeamento

	// tenta abrir o ficheiro de mapeamento
	hMapFile = OpenFileMapping(
		FILE_MAP_ALL_ACCESS,	// access to the file mapping object
		FALSE,					// handle cannot be inherited
		MAP_FILE);				// name of the file mapping object

	// o ficheiro j� existe ent�o o programa tem de terminar
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

/*
* initMapViewOfFile()
* -------------------
* Abre a vista da mem�ria partilhada
*/
circular_buffer* initMapViewOfFile(HANDLE hMapFile)
{
	circular_buffer* ptr;

	// obtem o ponteiro para a vista da mem�ria partilhada
	ptr = (circular_buffer*)MapViewOfFile(
		hMapFile,	// handle to map object
		FILE_MAP_READ |	// read/write permission
		FILE_MAP_WRITE,
		0,				// offset high - where the view begins
		0,				// offset low  - where the view begins
		sizeof(circular_buffer));	// number of bytes of a file mapping to map to the view

	return ptr;
}

/*
* readValueInRegistry()
* -------------------
* Obtem o valor de um par chave-valor do registry
*/
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
		(LPBYTE)&dwData,		// a pointer that receive the value's data
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
* createEvent()
* -------------------
* Cria um objeto evento manual-reset
*/
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

/*
* createMutex()
* -------------------
* Criar um objeto mutex
*/
HANDLE createMutex(TCHAR* name)
{
	// cria o objeto mutex
	HANDLE mutex = CreateMutex(
		NULL,	// security attributes
		FALSE,	// calling thread obtains initial ownership 
		name);

	return mutex;
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
* createSemaphoreEmpty()
* -------------------
* Cria um objeto sem�foro com 0 elementos
*/
HANDLE createEmptySemaphore(TCHAR* name, int count)
{
	// cria o sem�foro
	HANDLE semaphore = CreateSemaphore(
		NULL,	// default security attributes
		0,		// initial count
		count,	// maximum count
		name);	// name of the semaphore

	return semaphore;
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
* initCircularBuffer()
* -------------
* Inicializa o buffer circular
*/
void initCircularBuffer(thread_data* data)
{
	data->ptrSharedMemory->in = data->ptrSharedMemory->out = 0;
}

/*
* cmdShutdown()
* -------------
* Encerra o sistema, avisa os avi�es e passageiros
*/
void cmdShutdown(thread_data* data)
{
	SetEvent(data->hEventShutdown);
	Sleep(500);
}

/*
* cmdAddAirport()
* -------------
* Adiciona um aeroporto ao sistema
*/
BOOL cmdAddAirport(map_data* map, airport newAirport)
{
	airport* airports = map->airports;	// ponteiro para o array de aeroportos

	// verifica se o n�mero m�ximo de aeroportos n�o foi atingido
	if (isAirportFull(map))
	{
		MessageBoxEx(NULL, TEXT("O n�mero m�ximo de aeroportos j� foi atingido!"), TEXT("Cria��o de aeroporto"), MB_OK | MB_ICONERROR, 0);
		return FALSE;
	}

	// verifica se os dados do aeroporto s�o validos
	if (!isValidAirportData(newAirport))
	{
		MessageBoxEx(NULL, TEXT("Os dados do aeroporto n�o s�o v�lidos!"), TEXT("Cria��o de aeroporto"), MB_OK | MB_ICONERROR, 0);
		return FALSE;
	}

	// verifica se existe um aeroporto com o mesmo nome
	if (isAirportNameRepeated(map, newAirport.name))
	{
		MessageBoxEx(NULL, TEXT("Existe um aeroporto com o mesmo nome!"), TEXT("Cria��o de aeroporto"), MB_OK | MB_ICONERROR, 0);
		return FALSE;
	}
	// verifica se a posi��o est� ocupada por outro aeroporto
	if (!isPositionEmpty(map, newAirport.position))
	{
		MessageBoxEx(NULL, TEXT("Existe um aeroporto nessa posi��o!"), TEXT("Cria��o de aeroporto"), MB_OK | MB_ICONERROR, 0);
		return FALSE;
	}

	// verifica se existe um aeroporto a menos de 10 posi��es
	if (isAnotherAirportAround(map, newAirport))
	{
		MessageBoxEx(NULL, TEXT("Existe um aeroporto a menos de 10 posi��es!"), TEXT("Cria��o de aeroporto"), MB_OK | MB_ICONERROR, 0);
		return FALSE;
	}

	// adiciona o aeroporto
	addAirport(map, &newAirport);

	return TRUE;
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
	// verifica se a aceita��o de novos avi�es j� n�o est� bloqueada
	if (!data->sync_plane_access.isBlocked)
		return FALSE;

	// desbloqueia a aceita��o de novos avi�es
	data->sync_plane_access.isBlocked = FALSE;
	ReleaseSemaphore(data->sync_plane_access.semaphore_block_access, 1, NULL);

	return TRUE;
}

/*
* cmdAddAirports()
* -------------
* Adiciona 4 aeroportos j� definidos pelo sistema
*/
BOOL cmdAddAirports(map_data* map)
{
	airport newAirport[4];

	// limpa o lixo 
	ZeroMemory(&newAirport, sizeof(airport) * 4);

	_tcscpy_s(newAirport[0].name, BUFFERSMALL, TEXT("OPO"));
	newAirport[0].position.x = 125;
	newAirport[0].position.y = 125;
	_tcscpy_s(newAirport[1].name, BUFFERSMALL, TEXT("LIS"));
	newAirport[1].position.x = 250;
	newAirport[1].position.y = 125;
	_tcscpy_s(newAirport[2].name, BUFFERSMALL, TEXT("PAR"));
	newAirport[2].position.x = 125;
	newAirport[2].position.y = 250;
	_tcscpy_s(newAirport[3].name, BUFFERSMALL, TEXT("GVA"));
	newAirport[3].position.x = 250;
	newAirport[3].position.y = 250;

	for (int i = 0; i < 4; i++)
	{
		// verifica se o n�mero m�ximo de aeroportos n�o foi atingido
		if (isAirportFull(map))
		{
			SetWindowText(hWndLogs, TEXT("O n�mero m�ximo de aeroportos j� foi atingido!"));
			return FALSE;
		}

		// verifica se existe um aeroporto com o mesmo nome
		if (isAirportNameRepeated(map, newAirport[i].name))
		{
			SetWindowText(hWndLogs, TEXT("Existe um aeroporto com o mesmo nome!"));
			return FALSE;
		}

		// verifica se a posi��o est� ocupada por outros aeroportos
		if (!isPositionEmpty(map, newAirport[i].position))
		{
			SetWindowText(hWndLogs, TEXT("J� existe um aeroporto nessa posi��o!"));
			return FALSE;
		}

		// verifica se exite outro aeroporto a menos de 10 posi��es
		if (isAnotherAirportAround(map, newAirport[i]))
		{
			SetWindowText(hWndLogs, TEXT("Existe outro aeroporto num raio de 10 posi��es!"));
			return FALSE;
		}

		addAirport(map, &newAirport[i]);

		//_tprintf(TEXT("O aeroporto [%s] foi adicionado na posi��o [%d,%d]!\n"), newAirport[i].name, newAirport[i].position.x, newAirport[i].position.y);
	}
	return TRUE;
}


/*
* imprimeBordasMapa()
* -------------
* Imprime as bordas do mapa
*/
void imprimeBordasMapa(HWND hWnd, HDC hdc)
{
	RECT rect;			// dimens�es da janela pai
	int posX, posY;		// posi��o onde ver ser desenhado o mapa
	int posX2, posY2;		// posi��o onde ver ser desenhado o mapa
	GetClientRect(hWnd, &rect);	// obtem as dimens�es da janela pai

	//posX = (rect.right / 2) - ((rect.right / 10) * 3.75);		// calcula a coordenada X
	//posY = (rect.bottom / 2) - ((rect.bottom / 10) * 3.75);	// calcula a coordenada Y	
	
	//posX2 = (rect.right / 2) + ((rect.right / 10) * 4.5);		// calcula a coordenada X
	//posY2 = (rect.bottom / 2) + ((rect.bottom / 10) * 4.5);	// calcula a coordenada Y	

	posX = 10;
	posY = 100;
	posX2 = rect.right - 10;		// calcula a coordenada X
	posY2 = rect.bottom - 10;	// calcula a coordenada Y	

	/*posX = WINDOW_WIDTH  - (WINDOW_WIDTH - 100);
	posY = WINDOW_HEIGHT - (WINDOW_HEIGHT - 100);
	posX2 = WINDOW_WIDTH - 100;
	posY2 = WINDOW_HEIGHT - 100;*/

	if (posX2 > posX + 1000)
		posX2 = posX + 1000;

	if (posY2 > posY + 1000)
		posY2 = posY + 1000;


	Rectangle(
		hdc,	// handle para o device context
		posX,	// coordenada X
		posY,	// coordenada Y,
		posX2,
		posY2);
}

/*
* imprimeAeroportos()
* -------------
* Imprime os aeroportos no mapa
*/
void imprimeAeroportos(HWND hWnd, HDC hdc, thread_data* data)
{
	RECT rect;
	int posX, posY;		// posi��o onde ver ser desenhado o aeroporto

	GetClientRect(hWnd, &rect);	// obtem as dimens�es da janela pai

	for (int i = 0; i < data->map.numAirports; i++)
	{
		posX = 10;		// calcula a coordenada X
		posY = 100;		// calcula a coordenada Y	
		posX += data->map.airports[i].position.x;
		posY += data->map.airports[i].position.y;
		Rectangle(hdc, posX, posY, posX + 4, posY + 4);
	}
}

/*
* imprimeAvioes()
* -------------
* Imprime os avi�es em voo no mapa
*/
void imprimeAvioes(HWND hWnd, HDC hdc, thread_data* data)
{
	RECT rect;
	int posX, posY;		// posi��o onde ver ser desenhado o avi�o

	GetClientRect(hWnd, &rect);	// obtem as dimens�es da janela pai

	for (int i = 0; i < data->map.numPlanes; i++)
	{
		if (data->map.planes[i].state == FLYING)
		{
			posX = 10;		// calcula a coordenada X
			posY = 100;		// calcula a coordenada Y	
			posX += data->map.planes[i].pos_actual.x;
			posY += data->map.planes[i].pos_actual.y;
			Ellipse(hdc, posX, posY, posX + 4, posY + 4);
			//Rectangle(hdc, posX, posY, posX + 4, posY + 4);
		}
		
	}
}


/*
* mostraDadosAeroporto()
* -------------
* Mostra os dados do aeroporto quando� utilizador carrega num aeroporto
*/
void mostraDadosAeroporto(HWND hWnd, LPARAM lParam, map_data* map)
{
	HDC hdc;		// handle para o device context
	int x, y;		// coordenadas
	int dif = 10;
	int numPlanes = 0, numPassengers = 0;
	TCHAR buffer[BUFFERSMALL];
	position posAirport;
	RECT rect;

	hdc = GetDC(hWnd);
	x = GET_X_LPARAM(lParam);
	y = GET_Y_LPARAM(lParam);
	GetClientRect(hWnd, &rect);	// obtem as dimens�es da janela pai

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	for (int i = 0; i < map->numAirports; i++)
	{
		posAirport = map->airports[i].position;
		posAirport.x += 10;		// o mapa come�a na coord. x 10
		posAirport.y += 100;	// o mapa come�a na coord. y 100

		//if (posAirport.x < x + dif || posAirport.x > x - dif && posAirport.y < y + dif || posAirport.y > y - dif)
		if ((x < posAirport.x + dif && x > posAirport.x - dif) && (y < posAirport.y + dif && y > posAirport.y - dif))
		{

			// contabiliza o n�mero de aerooportos nesse aeroporto
			for (int j = 0; j < map->numPlanes; j++)
			{
				if (map->planes[j].pos_actual.x == map->airports[i].position.x && map->planes[j].pos_actual.y == map->airports[i].position.y)
					if (map->planes[j].state == GROUND)
						numPlanes++;
			}

			// contabiliza o n�mero de passageiros nesse aeroporto
			for (int k = 0; k < map->numPassengers; k++)
			{
				if (_tcscmp(map->passengers[k].origin, map->airports[i].name) == 0)
					if (map->passengers[k].state == GROUND)
						numPassengers++;
			}

			_stprintf_s(buffer, BUFFERSMALL, TEXT("Aeroporto: %s\r\nN�mero avi�es: %d\r\nN�mero passageiros: %d"), map->airports[i].name, numPlanes, numPassengers);


			// nome, n�m de avi�es e passageiros
			MessageBoxEx(hWnd, buffer, TEXT("Informa��o sobre o aeroporto"), MB_OK | MB_ICONINFORMATION, 0);
			
			break;

		}
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	ReleaseDC(hWnd, hdc);
}

/*
* mostraDadosAviao()
* -------------
* Mostra os dados do avi�o quando h� um evento mousehover
*/
void mostraDadosAviao(HWND hWnd, LPARAM lParam, map_data* map)
{
	HDC hdc;		// handle para o device context
	int x, y;		// coordenadas
	int dif = 10;
	int numPlanes = 0, numPassengers = 0;
	TCHAR buffer[BUFFERSIZE];
	position posPlane;
	RECT rect;

	hdc = GetDC(hWnd);
	x = GET_X_LPARAM(lParam);
	y = GET_Y_LPARAM(lParam);
	GetClientRect(hWnd, &rect);	// obtem as dimens�es da janela pai

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	for (int i = 0; i < map->numPlanes; i++)
	{
		posPlane = map->planes[i].pos_actual;
		posPlane.x += 10;		// o mapa come�a na coord. x 10
		posPlane.y += 100;	// o mapa come�a na coord. y 100

		// identifica qual � o avi�o
		if ((x < posPlane.x + dif && x > posPlane.x - dif) && (y < posPlane.y + dif && y > posPlane.y - dif))
		{
			_stprintf_s(buffer, BUFFERSIZE, TEXT("Avi�o: [%d]\r\n[%s] -> [%s]\r\nN�mero passageiros: %d/%d\r\nVelocidade: %d"), map->planes[i].pid, map->planes[i].origin, map->planes[i].destination, map->planes[i].num_passenger, map->planes[i].capacity, map->planes[i].speed);
			// nome, n�m de avi�es e passageiros
			MessageBoxEx(hWnd, buffer, TEXT("Informa��o sobre o avi�o"), MB_OK | MB_ICONINFORMATION, 0);

			break;

		}
	}

	// liberta o mutex
	ReleaseMutex(map->hMutex);

	ReleaseDC(hWnd, hdc);
}

/*
* createThread()
* -------------
* Cria uma thread
*/
HANDLE createThread(LPTHREAD_START_ROUTINE function, LPVOID data)
{
	// cria a thread e inicia a execu��o da mesma
	HANDLE thread = CreateThread(
		NULL,		// default security attributes
		0,			// default size of the stack
		function,	// function to be executed
		data,		// data to be passed to the thread
		0,			// flag that control the creation of the thread
		NULL);		// thread identifier is not returned

	return thread;
}

/*
*threadReadSharedMemory()
* ------------ -
*Thread que permite ler da mem�ria partilhada
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
	TCHAR buffer[BUFFERSIZE] = { '\0' };


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
		{
			MessageBoxEx(NULL, TEXT("A enumera��o dos processos falhou"), TEXT("Erro de thread"), MB_OK | MB_ICONERROR, 0);
			return -1;
		}

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
				_stprintf_s(buffer, BUFFERSIZE, TEXT("O avi�o [%d] terminou a sua execu��o."), data->map.planes[i].pid);
				SetWindowText(hWndLogs, buffer);
				if (removePlane(data, data->map.planes[i].pid))
				{
					_stprintf_s(buffer, BUFFERSIZE, TEXT("O avi�o [%d] foi removido com sucesso porque j� n�o estava em execu��o."), data->map.planes[i].pid);
					SetWindowText(hWndLogs, buffer);
				}					
				else
				{
					_stprintf_s(buffer, BUFFERSIZE, TEXT("Erro na remo��o do avi�o [%d]."), data->map.planes[i].pid);
					SetWindowText(hWndLogs, buffer);
				}
			}
		}


		// liberta o mutex
		ReleaseMutex(data->map.hMutex);
	}

	return 0;
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
	TCHAR buffer[BUFFERSIZE] = { '\0' };


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
			_stprintf_s(buffer, BUFFERSIZE, TEXT("Erro durante a leitura do pedido de um passageiro. O index n�o � v�lido."));
			SetWindowText(hWndLogs, buffer);
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
					//_tprintf(TEXT("Um cliente ligou-se ao pipe [%d].\n"), index);
					_stprintf_s(buffer, BUFFERSIZE, TEXT("Um cliente ligou-se ao pipe [%d]."), index);
					SetWindowText(hWndLogs, buffer);
					pipe[index].dwState = READING_STATE;
					break;
				}
				else
				{
					//_tprintf(TEXT("Erro durante a opera��o de liga��o no pipe [%d].\n"), index);
					_stprintf_s(buffer, BUFFERSIZE, TEXT("Erro durante a opera��o de liga��o no pipe [%d]."), index);
					SetWindowText(hWndLogs, buffer);
					return 0;
				}

				// opera��o de leitura pendente
			case READING_STATE:
				if (fSuccess || cbRead > 0)
				{
					//_tprintf(TEXT("O pedido do pipe [%d] �: [%s].\n"), index, pipe[index].request.command);
					_stprintf_s(buffer, BUFFERSIZE, TEXT("O pedido do pipe [%d] �: [%s]."), index, pipe[index].request.command);
					SetWindowText(hWndLogs, buffer);
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
					//_tprintf(TEXT("Erro durante a opera��o de escrita no pipe [%d].\n"), index);
					_stprintf_s(buffer, BUFFERSIZE, TEXT("Erro durante a opera��o de escrita no pipe [%d]."), index);
					SetWindowText(hWndLogs, buffer);
					removePassenger(&data->map, index);
					DisconnectAndReconnect(data->pipe, index);
					continue;
				}

				// default
			default:
				//_tprintf(TEXT("O estado do pipe [%d] � inv�lido.\n"), index);
				_stprintf_s(buffer, BUFFERSIZE, TEXT("O estado do pipe [%d] � inv�lido."), index);
				SetWindowText(hWndLogs, buffer);
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
				//_tprintf(TEXT("O pedido do pipe [%d] �: [%s].\n"), index, pipe[index].request.command);
				_stprintf_s(buffer, BUFFERSIZE, TEXT("O pedido do pipe [%d] �: [%s]."), index, pipe[index].request.command);
				SetWindowText(hWndLogs, buffer);
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
				//_tprintf(TEXT("A resposta para o pipe #%d �: [%s]!\n"), index, pipe[index].answer.extra);
				_stprintf_s(buffer, BUFFERSIZE, TEXT("A resposta para o pipe #%d �: [%s]."), index, pipe[index].answer.extra);
				SetWindowText(hWndLogs, buffer);
				pipe[index].fPendingIO = FALSE;
				pipe[index].dwState = READING_STATE;
				//pipe[index].dwState = WRITING_STATE;
				continue;
			}

			// a opera��o de leitura ainda est� pendente
			dwErr = GetLastError();
			if (!fSuccess && (dwErr == ERROR_IO_PENDING))
			{
				//_tprintf(TEXT("A resposta para o pipe #%d ainda est� pendente...\n"), index);
				_stprintf_s(buffer, BUFFERSIZE, TEXT("A resposta para o pipe #%d ainda est� pendente..."), index);
				SetWindowText(hWndLogs, buffer);
				pipe[index].fPendingIO = TRUE;
				continue;
			}

			// um erro aconteceu, o cliente deve ser desligado
			removePassenger(&data->map, index);
			DisconnectAndReconnect(data->pipe, index);
			break;

			// default
		default:
			//_tprintf(TEXT("Estado do pipe #%d inv�lido!\n"), index);
			_stprintf_s(buffer, BUFFERSIZE, TEXT("Estado do pipe #%d inv�lido."), index);
			SetWindowText(hWndLogs, buffer);
			return 0;
		}
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
		InvalidateRect(data->hWnd, NULL, TRUE);	// gera um evento WS_PAINT
	}
	else if (_tcscmp(cmd, CMD_BOARDING) == 0)
	{
		// embarca os passageiros
		cmdBoarding(data, &itemParaConsumir);
	}
	else if (_tcscmp(cmd, CMD_END) == 0)
	{
		// remove o avi�o do sistema
		removePlane(data, itemParaConsumir.pidSender);
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
	TCHAR buffer[BUFFERSMALL] = { '\0' };

	// limpa o lixo
	ZeroMemory(&itemParaProduzir, sizeof(item));

	// preenche os dados do item a ser produzido
	itemParaProduzir.pidSender = 0;
	itemParaProduzir.pidReceiver = itemParaConsumir->pidSender;
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, INVALID_IDENTIFICATION);

	// verifica se h� espa�o para o novo avi�o
	if (isPlanesFull(&data->map))
	{
		_stprintf_s(buffer, BUFFERSMALL, TEXT("N�o h� espa�o para adicionar o avi�o [%d]."), itemParaConsumir->plane.pid);
		//MessageBoxEx(NULL, buffer, TEXT("Erro na adi��o do avi�o"), MB_OK | MB_ICONERROR, 0);
		SetWindowText(hWndLogs, buffer);
		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}

	// verifica se o aeroporto de origem do novo avi�o existe
	if (!isAirportNameRepeated(&data->map, itemParaConsumir->plane.origin))
	{
		_stprintf_s(buffer, BUFFERSMALL, TEXT("O aeroporto de origem [%s] do avi�o [%d] n�o existe."), itemParaConsumir->plane.origin, itemParaConsumir->plane.pid);
		//MessageBoxEx(NULL, buffer, TEXT("Erro na adi��o do avi�o"), MB_OK | MB_ICONERROR, 0);
		SetWindowText(hWndLogs, buffer);
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
	_stprintf_s(buffer, BUFFERSMALL, TEXT("Avi�o [%d] adicionado com sucesso."), itemParaConsumir->plane.pid);
	//MessageBoxEx(NULL, buffer, TEXT("Avi�o adicionado com sucesso"), MB_OK | MB_ICONINFORMATION, 0);
	SetWindowText(hWndLogs, buffer);

	// escreve na mem�ria partilhada
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, VALID_IDENTIFICATION);

	writeInSharedMemory(data, &itemParaProduzir);
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
* cmdDefine()
* -------------
* Define o aeroporto de destino
*/
void cmdDefine(thread_data* data, item* itemParaConsumir)
{
	item itemParaProduzir;	// item que vai ser enviado como resposta ao aviao
	TCHAR buffer[BUFFERSIZE] = { '\0' };


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
		_stprintf_s(buffer, BUFFERSIZE, TEXT("A defini��o do aeroporto de destino do avi�o [%d] falhou porque o aeroporto de destino [%s] n�o existe!"), itemParaProduzir.plane.pid, itemParaProduzir.plane.destination);
		SetWindowText(hWndLogs, buffer);
		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}

	// verifica se o aeroporto de destino � igual ao aeroporto de origem do avi�o
	if (_tcscmp(itemParaProduzir.plane.origin, itemParaProduzir.plane.destination) == 0)
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("A defini��o do aeroporto de destino do avi�o [%d] falhou porque � igual ao aeroporto de origem."), itemParaProduzir.plane.pid);
		SetWindowText(hWndLogs, buffer);
		writeInSharedMemory(data, &itemParaProduzir);
		return;
	}

	// atualiza os dados do avi�o
	updatePlane(data, &itemParaProduzir.plane);

	_stprintf_s(buffer, BUFFERSIZE, TEXT("O avi�o [%d] atualizou o destino para [%s]."), itemParaProduzir.plane.pid, itemParaProduzir.plane.destination);
	SetWindowText(hWndLogs, buffer);


	// escreve na mem�ria partilhada
	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, VALID_DESTINATION);
	itemParaProduzir.plane.pos_destination = getPositionOfAirport(&data->map, itemParaProduzir.plane.destination);
	writeInSharedMemory(data, &itemParaProduzir);


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
* removePlane()
* -------------------
* Remove o avi�o no sistema
*/
BOOL removePlane(thread_data* data, int pid)
{
	TCHAR buffer[BUFFERSIZE] = { '\0' };
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
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("O avi�o [%d] teve um acidente durante o voo."), pid);
		SetWindowText(hWndLogs, buffer);
		MessageBoxEx(NULL, buffer, TEXT("Acidente"), MB_OK | MB_ICONWARNING, 0);

	}
	else
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("O piloto do avi�o [%d] reformou-se."), pid);
		SetWindowText(hWndLogs, buffer);
		MessageBoxEx(NULL, buffer, TEXT("Piloto"), MB_OK | MB_ICONWARNING, 0);
	}

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
	TCHAR buffer[BUFFERLARGE] = { '\0' };


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
		_stprintf_s(buffer, BUFFERLARGE, TEXT("N�o foi poss�vel embarcar os passageiros no avi�o [%d] porque o destino n�o foi definido."), itemParaConsumir->plane.pid);
		SetWindowText(hWndLogs, buffer);
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

			_stprintf_s(buffer, BUFFERLARGE, TEXT("%sO passageiro [%s] embarcou no avi�o [%d] com origem [%s] e destino [%s].\r\n"), buffer, passengers[i].name, itemParaProduzir.plane.pid, itemParaProduzir.plane.origin, itemParaProduzir.plane.destination);
			SetWindowText(hWndLogs, buffer);

		}
	}

	// liberta o mutex
	ReleaseMutex(data->map.hMutex);

	_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, VALID_BOARDING);
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
	TCHAR buffer[BUFFERLARGE] = { '\0' };


	// posi��o destino do avi�o
	position pos_destination = getPositionOfAirport(&data->map, itemParaConsumir->plane.destination);
	position pos_temp;	// caso haja um obstaculo sera calculada outra posi��o

	_stprintf_s(buffer, BUFFERSIZE, TEXT("O avi�o [%d] est� na posi��o [%d,%d]."), itemParaConsumir->pidSender, itemParaConsumir->plane.pos_actual.x, itemParaConsumir->plane.pos_actual.y);
	SetWindowText(hWndLogs, buffer);

	// preenche os dados do item a ser produzido
	itemParaProduzir.pidSender = 0;
	itemParaProduzir.pidReceiver = itemParaConsumir->pidSender;
	itemParaProduzir.plane = itemParaConsumir->plane;
	/*_tcscpy_s(itemParaProduzir.command, BUFFERSIZE, INVALID_MOVEMENT);
	itemParaProduzir.plane = itemParaConsumir->plane;*/

	// verifica se a posi��o calculada n�o colide com outros avi�es em voo
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
				_stprintf_s(buffer, BUFFERLARGE, TEXT("O avi�o [%d] desviou-se para a posi��o [%d,%d]."), itemParaConsumir->pidSender, pos_temp.x, pos_temp.y);
				SetWindowText(hWndLogs, buffer);
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
					_stprintf_s(buffer, BUFFERLARGE, TEXT("O avi�o [%d] desviou-se para a posi��o [%d,%d]."), itemParaConsumir->pidSender, pos_temp.x, pos_temp.y);
					SetWindowText(hWndLogs, buffer);
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
		// escreve na consola
		_stprintf_s(buffer, BUFFERLARGE, TEXT("O avi�o [%d] chegou ao destino."), itemParaProduzir.plane.pid);
		SetWindowText(hWndLogs, buffer);

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
		// atualiza o n�mero de passageiros
		for (int i = 0; i < data->map.numPlanes; i++)
		{
			if (data->map.planes[i].pid == itemParaProduzir.plane.pid)
			{
				data->map.planes[i].num_passenger = 0;
				break;
			}
		}
	}
	else if (itemParaConsumir->movementResult == 1)
	{
		// boa movimenta��o
		itemParaProduzir.plane.pos_actual = itemParaProduzir.plane.pos_destination;
		itemParaProduzir.plane.pos_destination = pos_destination;
		itemParaProduzir.plane.state = FLYING;
		itemParaProduzir.plane.num_passenger = itemParaConsumir->plane.num_passenger;
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
		/*for (int i = 0; i < data->map.numPlanes; i++)
		{
			if (data->map.planes[i].pid == itemParaProduzir.plane.pid)
			{
				data->map.planes[i].num_passenger = 0;
				break;
			}
		}*/


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
* removePassenger()
* -------------
* Remove o passageiro do array de passageiros
*/
void removePassenger(map_data* map, int index)
{
	TCHAR buffer[BUFFERSIZE] = { '\0' };

	// entra no mutex
	WaitForSingleObject(map->hMutex, INFINITE);

	// verifica se o index � v�lido
	if (index < 0 || index > map->numPassengers)
	{
		ReleaseMutex(map->hMutex);
		return;
	}

	// mensagem
	if (map->passengers[index].state == FLYING)
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("O passageiro [%s] morreu num acidente durante o voo."), map->passengers[index].name);
		SetWindowText(hWndLogs, buffer);
	}
	else
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("O passageiro [%s] saiu do sistema"), map->passengers[index].name);
		SetWindowText(hWndLogs, buffer);
	}

	// reoganiza os elementos do array
	for (int i = index; i < map->numPassengers - 1; i++)
		map->passengers[i] = map->passengers[i + 1];

	// decrementa o n�mero de passageiros
	map->numPassengers--;

	// liberta o mutex
	ReleaseMutex(map->hMutex);
}

/*
* isValidPosition()
* -------------------
* Inicializa as inst�ncias do pipe
*/
BOOL initPipeInstances(thread_data* data)
{
	TCHAR buffer[BUFFERSIZE] = { '\0' };

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
			_stprintf_s(buffer, BUFFERSIZE, TEXT("Erro na cria��o do objeto evento para uma inst�ncia de pipe."));
			SetWindowText(hWndLogs, buffer);
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
			_stprintf_s(buffer, BUFFERSIZE, TEXT("Erro na cria��o da inst�ncia do pipe."));
			SetWindowText(hWndLogs, buffer);
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
			//_tprintf(TEXT("O pipe [%d] est� dispon�vel para ler o pedido do cliente...\n"), i);
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
	TCHAR buffer[BUFFERSIZE] = { '\0' };

	BOOL fConnect, fPendingIO = FALSE;

	// inicia uma opera��o de liga��o  overlapped
	fConnect = ConnectNamedPipe(hPipe, overlap);

	// devolve FALSE por norma
	if (fConnect)
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("Houve um erro no ConnectNamedPipe."));
		SetWindowText(hWndLogs, buffer);
	}

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
		_stprintf_s(buffer, BUFFERSIZE, TEXT("Houve um erro no ConnectNamedPipe."));
		SetWindowText(hWndLogs, buffer);
		return FALSE;
	}

	return fPendingIO;
}

/*
* DisconnectAndReconnect()
* -------------------
* Quando h� um erro ou um passageiro fecha o handle do pipe:
* Desliga o pipe do passageiro e chama a subrotina ConnectNamedPipe para esperar que outro cliente se ligue
*/
void DisconnectAndReconnect(PIPE_INSTANCE* pipe, DWORD index)
{
	TCHAR buffer[BUFFERSIZE] = { '\0' };

	// desliga a inst�ncia do pipe
	if (DisconnectNamedPipe(pipe[index].hPipeInstance))
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("A inst�ncia do pipe [%d] foi desligada."), index);
		SetWindowText(hWndLogs, buffer);
	}
	else
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("Erro ao desligar a inst�ncia do pipe [%d]."), index);
		SetWindowText(hWndLogs, buffer);
	}

	// chama a sobrotina ConnectNamedPipe
	pipe[index].fPendingIO = ConnectToNewClient(pipe[index].hPipeInstance, &pipe[index].overlap);

	if (pipe[index].fPendingIO)
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("A inst�ncia do pipe [%d] voltou a ficar dispon�vel para um novo cliente."), index);
		SetWindowText(hWndLogs, buffer);
		pipe[index].dwState = CONNECTING_STATE;
	}
	else
	{
		_stprintf_s(buffer, BUFFERSIZE, TEXT("A inst�ncia do pipe [%d] est� no estado de leitura."), index);
		SetWindowText(hWndLogs, buffer);
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
		return TRUE;
	}
	else
	{
		// a opera��o de escrita est� pendente
		if (!fSuccess && (GetLastError() == ERROR_IO_PENDING))
		{
			hPipe->fPendingIO = TRUE;
		}
		return FALSE;
	}
}