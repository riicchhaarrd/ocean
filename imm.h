#ifndef IMM_H
#define IMM_H
#include "types.h"
#include <assert.h>

typedef struct
{
	int nbits;
	union
	{
		int64_t dq;
		int32_t dd;
		int16_t dw;
		int8_t db;
	};
	bool is_unsigned;
} imm_t;

#define DECLARE_IMM_CAST(TYPE)                                                                                         \
	static TYPE imm_cast_##TYPE(imm_t* imm)                                                                            \
	{                                                                                                                  \
		switch (imm->nbits)                                                                                            \
		{                                                                                                              \
			case 8:                                                                                                    \
				return (TYPE)imm->db;                                                                                  \
			case 16:                                                                                                   \
				return (TYPE)imm->dw;                                                                                  \
			case 32:                                                                                                   \
				return (TYPE)imm->dd;                                                                                  \
			case 64:                                                                                                   \
				return (TYPE)imm->dq;                                                                                  \
		}                                                                                                              \
		return 0;                                                                                                      \
	}

DECLARE_IMM_CAST(int8_t)
DECLARE_IMM_CAST(int16_t)
DECLARE_IMM_CAST(int32_t)
DECLARE_IMM_CAST(int64_t)

#endif
