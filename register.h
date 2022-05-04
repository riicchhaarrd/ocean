#ifndef REGISTER_H
#define REGISTER_H

typedef enum
{
	/* VRU_GENERAL_PURPOSE, */
	/* VRU_FLOATING_POINT, */
	VRU_MAX
} vregister_usage_t;

typedef struct
{
	/* vregister_usage_t usage; */
	int index;
} vregister_t;
#endif
