#ifndef SIMPLE_ARENA
#define SIMPLE_ARENA

typedef struct arena_s
{
	const char *tag;
	char *data;
	size_t reserved;
	size_t used;
} arena_t;

static int arena_create(arena_t **arena_out, const char *tag, size_t n)
{
	*arena_out = NULL;
	char *ptr = malloc(n + sizeof(arena_t));
	if(!ptr)
		return 1;
	arena_t *a = (arena_t*)ptr;
	ptr += sizeof(arena_t);
	a->tag = tag;
	a->data = ptr;
	a->reserved = n;
	a->used = 0;
	*arena_out = a;
	return 0;
}

static char *arena_alloc(arena_t *a, size_t n)
{
	if(a->used + n > a->reserved)
	{
		printf("can't allocate %d bytes, out of memory for arena '%s' size: %d bytes / %d KB\n", n, a->tag, a->reserved, a->reserved / 1000);
		return NULL;
	}
	a->used += n;
	return &a->data[a->used - n];
}

static void arena_destroy(arena_t **a)
{
	if(!a)
		return;
	free(*a);
	*a = NULL;
}

#endif