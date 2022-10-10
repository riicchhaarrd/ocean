
int add(int a, int b)
{
	return a + b;
}

int main()
{
	int a = 1;
	int b = add(a, 2);
	int c = add(add(a, b), 3);
	int d = add(add(add(a, b), c), 4);
	int e = a + b + c + d + 5;
	int f = a + b + c + d + e + 6;
	int g = a + b + c + d + e + f + 7;
	int h = a + b + c + d + e + f + g + 8;
	return h + 9;
}
