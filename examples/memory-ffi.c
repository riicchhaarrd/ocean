//ocean.exe -d -bmemory -lmsvcrt.dll -lkernel32.dll -luser32.dll test.c test.exe
//last argument test.exe is unused atm, because we're running the code directly

int time(int t);
int printf(const char *format, ...);

int MessageBoxA(
  void*  hWnd,
  const char* lpText,
  const char* lpCaption,
  int   uType
);

int main()
{
	MessageBoxA(0, "hello world", 0, 0);
	printf("hello world\n");
	return time(0);
}