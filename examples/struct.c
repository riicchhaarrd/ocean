#include <stdio.h>

struct entity
{
    char name[64];
    int health;
    int maxhealth;
};

void test(entity* p)
{
    p->maxhealth = 100;
    p->health = p->maxhealth;
}

int main()
{
    entity e;
    test(&e);

    printf("health = %d\n", e.health);
	return 0;
}
