#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <windows.h>
#include <ctime>
#include <iostream>
#include <fstream>
#include <string>
#include <process.h>
#include <time.h>

// Коды клавиш 
#define KEY_Q 0x51
#define KEY_C 0x43
#define KEY_1 0x30
#define KEY_2 0x31
#define KEY_3 0x32


using namespace std;

const TCHAR szWinClass[] = _T("My Beautiful App");
const TCHAR szWinName[] = _T("My Beautiful Window");
HWND hwnd;               // дескриптор окна
HBRUSH hBrush;           // кисть
HDC hdc;
PAINTSTRUCT ps;
RECT clientRect;
UINT WM_GridChange;
UINT WM_GameOver;
HANDLE player_sem;       // семафор для посчёта количества игроков
HANDLE sl_X;             // событие для блокировки ходов крестика
HANDLE sl_O;             // событие для блокировки ходов нолика
HANDLE animate_thread;   // дескриптор потока анимации фона
bool animate = true;     // работает ли поток анимации фона
char player;             // символ игрока для данного потока ('o' или 'x')

int N; // Число ячеек

// Mapping для межпроцессного взаимодействия
HANDLE IPCMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 255, _T("GlobalMapping"));
char* game_field = (char*)MapViewOfFile(IPCMapping, FILE_MAP_ALL_ACCESS, 0, 0, 255);

// Параметры окна (размер, положение, цвет)
struct {
	int width = 320;
	int height = 240;
	int wnd_y = CW_USEDEFAULT;
	int wnd_x = CW_USEDEFAULT;
	int color[3] = { 0, 0, 0 };
} wnd_info;


/* Случайное целое число меду min_n и max_n (включительно) */
int GetRandomInt(int min_n, int max_n)
{
	return rand() % (max_n + 1) + min_n;
}


// Почистить память
void CleanUp()
{
	DestroyWindow(hwnd);
	DeleteObject(hBrush);
	UnmapViewOfFile(game_field);
	CloseHandle(IPCMapping);
}


// Поток анимации фона
DWORD WINAPI AnimateBG(void*)
{
	while (1)
	{
		wnd_info.color[0] += GetRandomInt(0, 10);
		if (wnd_info.color[0] >= 230) wnd_info.color[0] = 0;
		wnd_info.color[1] += GetRandomInt(0, 10);
		if (wnd_info.color[1] >= 230) wnd_info.color[1] = 0;
		wnd_info.color[2] += GetRandomInt(0, 10);
		if (wnd_info.color[2] >= 230) wnd_info.color[2] = 0;

		hBrush = CreateSolidBrush(RGB(wnd_info.color[0], wnd_info.color[1], wnd_info.color[2]));
		DeleteObject((HGDIOBJ)SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG)hBrush));
		InvalidateRect(hwnd, NULL, TRUE);
		Sleep(40);
	}
}


// Проверка на [w - победа] [n - ничего] [d - ничья]
char CheckForWin(char c)
{
	int i, j;

	for (i = 0; i < N; i++) { // строки
		for (j = 0; j < N; j++)
			if (game_field[i * N + j] != c) break;
		if (j == N) return 'w';
	}

	for (i = 0; i < N; i++) { // столбцы
		for (j = 0; j < N; j++)
			if (game_field[j * N + i] != c) break;
		if (j == N) return 'w';
	}

	for (i = 0; i < N; i++)  // главная диагональ
		if (game_field[i * N + i] != c) break;
	if (i == N) return 'w';

	for (i = 0; i < N; i++) // побочная диагональ
		if (game_field[(i + 1) * (N - 1)] != c) break;
	if (i == N) return 'w';

	for (i = 0; i < N * N; i++)
		if (game_field[i] == ' ') break;
	if (i == N * N) return 'd';

	return 'n';
}


/* Прочитать информацию из конфигурационного файла*/
void ReadConfig()
{
	ifstream cfile;
	cfile.open("conf.txt");
	if (cfile)
		cfile >> N;
	else N = 3;
	cfile.close();
}


void SaveWndData_fstream()
{
	ofstream wfile;
	wfile.open("s.txt");

	wfile << wnd_info.width << endl;
	wfile << wnd_info.height << endl;
	wfile << wnd_info.wnd_x << endl;
	wfile << wnd_info.wnd_y << endl;
	wfile << wnd_info.color[0] << endl;
	wfile << wnd_info.color[1] << endl;
	wfile << wnd_info.color[2] << endl;

	wfile.close();
}


void SaveWndData_mapping()
{
	HANDLE hFile = CreateFile(TEXT("s.txt"), GENERIC_WRITE | GENERIC_READ, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, 255, NULL);
		if (hMapping != NULL)
		{
			char* dataPtr = (char*)MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, 255);
			if (dataPtr != NULL)
			{
				//sscanf(buffer, format, var1, var2);   buffer -> var1, var2
				//sprintf(buffer, format, var1, var2);  var1, var2 -> buffer

				sprintf_s(dataPtr, 255, "%d\n%d\n%d\n%d\n%d\n%d\n%d\n",
					wnd_info.width, wnd_info.height, wnd_info.wnd_x, wnd_info.wnd_y,
					wnd_info.color[0], wnd_info.color[1], wnd_info.color[2]);
				UnmapViewOfFile(dataPtr);
			}
			CloseHandle(hMapping);
		}
	}
	CloseHandle(hFile);
}


void SaveWndData_filevar()
{
	FILE* wfile;
	fopen_s(&wfile, "s.txt", "w");

	if (wfile != NULL)
	{
		fprintf(wfile, "%d\n", wnd_info.width);
		fprintf(wfile, "%d\n", wnd_info.height);
		fprintf(wfile, "%d\n", wnd_info.wnd_x);
		fprintf(wfile, "%d\n", wnd_info.wnd_y);
		fprintf(wfile, "%d\n", wnd_info.color[0]);
		fprintf(wfile, "%d\n", wnd_info.color[1]);
		fprintf(wfile, "%d\n", wnd_info.color[2]);
		fclose(wfile);
	}
}


void SaveWndData_winapi()
{

	DWORD dBytesWritten;
	HANDLE hFile = CreateFile(TEXT("s.txt"), GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		string s = to_string(wnd_info.width) + "\n" + to_string(wnd_info.height) + "\n" + to_string(wnd_info.wnd_x) +
			"\n" + to_string(wnd_info.wnd_y) + "\n" + to_string(wnd_info.color[0]) + "\n" +
			to_string(wnd_info.color[1]) + "\n" + to_string(wnd_info.color[2]);
		WriteFile(hFile, s.c_str(), sizeof(char) * s.size(), &dBytesWritten, NULL);
	}

	CloseHandle(hFile);
}


void LoadWndData_fstream()
{
	ifstream rfile;
	rfile.open("s.txt");
	if (rfile)
	{
		rfile >> wnd_info.width;
		rfile >> wnd_info.height;
		rfile >> wnd_info.wnd_x;
		rfile >> wnd_info.wnd_y;
		rfile >> wnd_info.color[0];
		rfile >> wnd_info.color[1];
		rfile >> wnd_info.color[2];
	}
	rfile.close();
}


void LoadWndData_mapping()
{
	HANDLE hFile = CreateFile(TEXT("s.txt"), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
		if (hMapping != NULL)
		{
			char* dataPtr = (char*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
			if (dataPtr != NULL)
			{
				sscanf_s(dataPtr, "%d%d%d%d%d%d%d", &wnd_info.width, &wnd_info.height, &wnd_info.wnd_x, &wnd_info.wnd_y,
					&wnd_info.color[0], &wnd_info.color[1], &wnd_info.color[2]);
				UnmapViewOfFile(dataPtr);
			}
			CloseHandle(hMapping);
		}
	}
	CloseHandle(hFile);
}


void LoadWndData_filevar()
{
	FILE* rfile;
	fopen_s(&rfile, "s.txt", "rt");

	if (rfile != NULL)
	{
		fscanf_s(rfile, "%d", &wnd_info.width);
		fscanf_s(rfile, "%d", &wnd_info.height);
		fscanf_s(rfile, "%d", &wnd_info.wnd_x);
		fscanf_s(rfile, "%d", &wnd_info.wnd_y);
		fscanf_s(rfile, "%d", &wnd_info.color[0]);
		fscanf_s(rfile, "%d", &wnd_info.color[1]);
		fscanf_s(rfile, "%d", &wnd_info.color[2]);
		fclose(rfile);
	}
}


void LoadWndData_winapi()
{
	DWORD dBytesRead;
	char buffer[80];
	HANDLE hFile = CreateFile(TEXT("s.txt"), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		ReadFile(hFile, &buffer, sizeof(buffer), &dBytesRead, NULL);
		sscanf_s(buffer, "%d%d%d%d%d%d%d", &wnd_info.width, &wnd_info.height, &wnd_info.wnd_x, &wnd_info.wnd_y,
			&wnd_info.color[0], &wnd_info.color[1], &wnd_info.color[2]);
	}
	CloseHandle(hFile);
}


// Запустить блокнот
void RunNotepad(void)
{
	STARTUPINFO sInfo;
	PROCESS_INFORMATION pInfo;

	ZeroMemory(&sInfo, sizeof(STARTUPINFO));

	puts("Starting Notepad...");
	CreateProcess(_T("C:\\Windows\\Notepad.exe"),
		NULL, NULL, NULL, FALSE, 0, NULL, NULL, &sInfo, &pInfo);

	//WaitForSingleObject(pInfo.hProcess, INFINITE);
	CloseHandle(pInfo.hProcess);
	CloseHandle(pInfo.hThread);
}


// Обработка сообщений окна
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_GridChange) { // изменение игрового поля
		InvalidateRect(hwnd, NULL, TRUE);
		return 0;
	}
	if (message == WM_GameOver) { // конец игры
		DestroyWindow(hwnd);
		return 0;
	}

	switch (message)
	{
	case WM_HOTKEY:
		if (wParam == 1) DestroyWindow(hwnd); // ctrl+q
		if (wParam == 2) RunNotepad(); // shift+c
		return 0;

	case WM_SIZE:
		InvalidateRect(hwnd, NULL, TRUE);
		return 0;

	case WM_DESTROY:
		GetWindowRect(hwnd, &clientRect);
		wnd_info.width = clientRect.right - clientRect.left;
		wnd_info.height = clientRect.bottom - clientRect.top;
		wnd_info.wnd_x = clientRect.left;
		wnd_info.wnd_y = clientRect.top;
		PostQuitMessage(0);
		PostMessage(HWND_BROADCAST, WM_GameOver, NULL, NULL);
		return 0;

	case WM_KEYUP:
	{
		if (wParam == VK_RETURN) {
			wnd_info.color[0] = GetRandomInt(0, 255);
			wnd_info.color[1] = GetRandomInt(0, 255);
			wnd_info.color[2] = GetRandomInt(0, 255);
			hBrush = CreateSolidBrush(RGB(wnd_info.color[0], wnd_info.color[1], wnd_info.color[2]));
			DeleteObject((HGDIOBJ)SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG)hBrush));
			InvalidateRect(hwnd, NULL, TRUE);
		}
		if (wParam == VK_ESCAPE)
			DestroyWindow(hwnd);
		if (wParam == VK_SPACE) {
			if (animate) {
				animate = false;
				SuspendThread(animate_thread);
			}
			else {
				animate = true;
				ResumeThread(animate_thread);
			}
		}
		if (wParam == KEY_1) SetThreadPriority(animate_thread, 1);
		if (wParam == KEY_2) SetThreadPriority(animate_thread, 2);
		if (wParam == KEY_3) SetThreadPriority(animate_thread, 3);
		return 0;
	}

	case WM_PAINT:
	{
		POINT pt;
		GetClientRect(hwnd, &clientRect);

		LONG win_width = clientRect.right;
		LONG win_height = clientRect.bottom;
		int grid_size_x = win_width / N;
		int grid_size_y = win_height / N;

		hdc = BeginPaint(hwnd, &ps);
		HPEN grid_pen = CreatePen(PS_SOLID, 4, RGB(0, 0, 0));
		HPEN x_pen = CreatePen(PS_SOLID, 4, RGB(255, 0, 0));
		HBRUSH mybrush = CreateSolidBrush(RGB(255, 0, 0));
		SelectObject(hdc, grid_pen);
		SelectObject(hdc, mybrush);

		for (int i = 1; i < N; i++) { // Горизонтальные полосы
			int y = i * (win_height / N);
			MoveToEx(hdc, 0, y, &pt);
			LineTo(hdc, win_width, y);
		}
		for (int i = 1; i < N; i++) { // Вертикальные полосы
			int x = i * (win_width / N);
			MoveToEx(hdc, x, 0, &pt);
			LineTo(hdc, x, win_height);
		}


		SelectObject(hdc, x_pen);
		for (int i = 0; i < N; i++)
			for (int j = 0; j < N; j++) {
				if (game_field[i * N + j] == 'o') // Рисуем нолик
					Ellipse(hdc, grid_size_x * j, grid_size_y * i, grid_size_x * (j + 1), grid_size_y * (i + 1));

				else if (game_field[i * N + j] == 'x') { // Рисуем крестик
					MoveToEx(hdc, grid_size_x * j, grid_size_y * i, &pt);
					LineTo(hdc, grid_size_x * (j + 1), grid_size_y * (i + 1));
					MoveToEx(hdc, grid_size_x * (j + 1), grid_size_y * i, &pt);
					LineTo(hdc, grid_size_x * j, grid_size_y * (i + 1));
				}
			}

		DeleteObject(grid_pen);
		DeleteObject(x_pen);
		DeleteObject(mybrush);
		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_LBUTTONUP:
	{
		if (player == 'x') { // не даём сходить х если ходит о
			if (WaitForSingleObject(sl_X, 10) != WAIT_TIMEOUT) return 0;
		}
		else { // не даём сходить о если ходит х
			if (WaitForSingleObject(sl_O, 10) != WAIT_TIMEOUT) return 0;
		}

		GetClientRect(hwnd, &clientRect);
		int x = LOWORD(lParam) / (clientRect.right / N);
		int y = HIWORD(lParam) / (clientRect.bottom / N);

		if (game_field[y * N + x] == ' ') { // даём сходить только в пустую клетку
			game_field[y * N + x] = player;
			PostMessage(HWND_BROADCAST, WM_GridChange, NULL, NULL);
		}
		else {
			MessageBox(hwnd, _T("Сюда ходить нельзя"), _T("ОИШБКА!!!"), MB_OK | MB_ICONERROR);
			return 0;
		}

		char result = CheckForWin(player);
		if (result == 'w') { // победа
			MessageBox(hwnd, _T("Ура, победа!"), _T("Игра окончена"), MB_OK | MB_ICONINFORMATION);
			PostMessage(HWND_BROADCAST, WM_GameOver, NULL, NULL);
		}
		else if (result == 'd') { // ничья 
			MessageBox(hwnd, _T("Ничья"), _T("Игра окончена"), MB_OK | MB_ICONINFORMATION);
			PostMessage(HWND_BROADCAST, WM_GameOver, NULL, NULL);
		}

		if (player == 'x') {
			ResetEvent(sl_O);
			SetEvent(sl_X);
		}
		else {
			ResetEvent(sl_X);
			SetEvent(sl_O);
		}

		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}
	}

	return DefWindowProc(hwnd, message, wParam, lParam);
}


int main(int argc, char** argv)
{
	srand(time(NULL));

	ReadConfig();


	if (argc > 1) {
		if (strcmp(argv[1], "s") == 0)
			LoadWndData_fstream();
		else if (strcmp(argv[1], "m") == 0)
			LoadWndData_mapping();
		else if (strcmp(argv[1], "f") == 0)
			LoadWndData_filevar();
		else if (strcmp(argv[1], "w") == 0)
			LoadWndData_winapi();
	}

	BOOL bMessageOk;
	MSG message;
	WNDCLASS wincl = { 0 };

	int nCmdShow = SW_SHOW;
	HINSTANCE hThisInstance = GetModuleHandle(NULL);

	// Структура окна
	wincl.hInstance = hThisInstance;
	wincl.lpszClassName = szWinClass;
	wincl.lpfnWndProc = WindowProcedure;

	// Кисть для раскраски фона
	hBrush = CreateSolidBrush(RGB(wnd_info.color[0], wnd_info.color[1], wnd_info.color[2]));
	wincl.hbrBackground = hBrush;

	if (!RegisterClass(&wincl))
		return 0;

	// Создать окно
	hwnd = CreateWindow(
		szWinClass,            /* Classname */
		szWinName,             /* Title Text */
		WS_OVERLAPPEDWINDOW,   /* default window */
		wnd_info.wnd_x,        /* Windows decides the position */
		wnd_info.wnd_y,        /* where the window ends up on the screen */
		wnd_info.width,        /* The programs width */
		wnd_info.height,       /* and height in pixels */
		HWND_DESKTOP,          /* The window is a child-window to desktop */
		NULL,                  /* No menu */
		hThisInstance,         /* Program Instance handler */
		NULL                   /* No Window Creation data */
	);

	// определение очерёдности ходов, распределение "ролей"
	sl_O = CreateEvent(NULL, TRUE, FALSE, _T("GlobalO"));
	sl_X = CreateEvent(NULL, TRUE, FALSE, _T("GlobalX"));
	player_sem = CreateSemaphore(NULL, 2, 2, _T("GlobalPlayerSem"));
	if (GetLastError() != ERROR_ALREADY_EXISTS)
	{
		player = 'x'; // если это первый запуск, значит крестик
		SetWindowText(hwnd, _T("TicTacToe | Player:X"));
		for (int i = 0; i < N * N; i++) game_field[i] = ' '; // заодно обнулим поле, заполнив его пустыми клетками
		SetEvent(sl_O);
	}
	else
	{
		player = 'o'; // если второй, то крестик
		SetWindowText(hwnd, _T("TicTacToe | Player:O"));
	}

	if (WaitForSingleObject(player_sem, 100) == WAIT_TIMEOUT) // если попытаться открыть третье окно
	{
		MessageBox(hwnd, _T("Вожможно одновременное открытие не более двух окон"), _T("ОИШБКА!!!"), MB_OK | MB_ICONERROR);
		UnregisterClass(szWinClass, hThisInstance);
		CleanUp();
		return 0;
	}


	ShowWindow(hwnd, nCmdShow);
	animate_thread = CreateThread(NULL, 0, AnimateBG, NULL, THREAD_SUSPEND_RESUME, NULL);

	// Комбинации клавиш
	RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_NOREPEAT, KEY_Q); // ctrl+Q
	RegisterHotKey(hwnd, 2, MOD_SHIFT | MOD_NOREPEAT, KEY_C); // shift+C

	WM_GridChange = RegisterWindowMessage(_T("GridChange")); // сообщение об изменении игрового поля
	WM_GameOver = RegisterWindowMessage(_T("GameOver")); // сообщение об окончании игры


	// Цикл обработки сообщений
	while ((bMessageOk = GetMessage(&message, NULL, 0, 0)) != 0)
	{
		if (bMessageOk == -1)
		{
			puts("Suddenly, GetMessage failed! You can call GetLastError() to see what happend");
			break;
		}
		TranslateMessage(&message);
		DispatchMessage(&message);
	}


	if (argc > 1)
	{
		if (strcmp(argv[1], "s") == 0)
			SaveWndData_fstream();
		else if (strcmp(argv[1], "m") == 0)
			SaveWndData_mapping();
		else if (strcmp(argv[1], "f") == 0)
			SaveWndData_filevar();
		else if (strcmp(argv[1], "w") == 0)
			SaveWndData_winapi();
	}

	UnregisterClass(szWinClass, hThisInstance);
	CleanUp();
	CloseHandle(player_sem);
	CloseHandle(sl_O);
	CloseHandle(sl_X);
	CloseHandle(animate_thread);

	return 0;
}
