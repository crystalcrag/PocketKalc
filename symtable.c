/*
 * symtable.c : store variables names and results into a simple hash table. A bit overkill for a
 *              simple calc, but it does not require that much code over a standard linked list.
 *
 * written by T.Pierron, june 2022.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "UtilityLibLite.h"
#include "symtable.h"

/* hash function */
uint32_t crc32(uint32_t crc, DATA8 buf, int max)
{
	static uint32_t crctable[256];

	crc = crc ^ 0xffffffffL;
	if (crctable[0] == 0)
	{
		int i, k, c;
		for (i = 0; i < 256; i++)
		{
			for (k = 0, c = i; k < 8; k++)
				c = c & 1 ? 0xedb88320 ^ (c >> 1) : c >> 1;
			crctable[i] = c;
		}
	}

	if (max > 0)
	{
		while (max > 0)
		{
			crc = crctable[(crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
			max --;
		}
	}
	else /* string */
	{
		while (buf[0])
			crc = crctable[(crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
	}
	return crc ^ 0xffffffffL;
}

void symTableAssign(Result var, Variant v)
{
	if (var->bin.type == TYPE_STR)
	{
		if (var->bin.string == v->string)
			/* already done */
			return;
		free(var->bin.string);
	}
	var->bin = *v;
	if (v->type == TYPE_STR)
		var->bin.string = strdup(v->string);
}

#define MAX_HASH_CAPA      19

/* note: suppose that a previoius call to symTableFindByName(name) returned NULL */
Result symTableAdd(SymTable syms, STRPTR name, Variant v)
{
	SymTable slot, prev;

	if (syms->symbols == NULL)
		syms->symbols = calloc(sizeof (struct Result_t), MAX_HASH_CAPA);

	for (slot = syms, prev = NULL; slot->count == MAX_HASH_CAPA; prev = slot, slot = slot->next);

	if (slot == NULL)
	{
		/* all hash are full: add a new one (symbols cannot be relocated: reference on them will be all over the place) */
		slot = calloc(sizeof *slot + sizeof (struct Result_t) * MAX_HASH_CAPA, 1);
		slot->symbols = (Result) (slot + 1);
		prev->next = slot;
	}

	Result var = slot->symbols + crc32(0, name, strlen(name)) % MAX_HASH_CAPA;

	/* check for collision */
	if (var->name[0])
	{
		Result eof = slot->symbols + MAX_HASH_CAPA;
		Result next = var;
		int i;
		for (i = 0; i < MAX_HASH_CAPA; i ++)
		{
			next ++;
			if (next == eof) next = slot->symbols;
			if (next->name[0] == 0)
			{
				/* need a linked list of all entriese with the same hash (to avoid scaning whole table if there is a collision) */
				next->name[MAX_VAR_NAME-1] = var->name[MAX_VAR_NAME-1];
				var->name[MAX_VAR_NAME-1] = next - slot->symbols;
				var = next;
				break;
			}
		}
	}

	// fprintf(stderr, "new var %s at index %d\n", name, var - slot->symbols);

	slot->count ++;
	CopyString(var->name, name, sizeof var->name - 1);
	symTableAssign(var, v);

	return var;
}

void symTableFree(SymTable syms)
{
	SymTable list, next;

	for (list = syms; list; list = next)
	{
		/* need to free strings though */
		int i;
		for (i = 0; i < MAX_HASH_CAPA; i ++)
		{
			Result res = list->symbols + i;
			if (res->bin.type == TYPE_STR)
				free(res->bin.string);
		}

		next = list->next;
		if (list == syms) free(list->symbols);
		else free(list);
	}

	memset(syms, 0, sizeof *syms);
}

void synTableDump(SymTable syms)
{
	SymTable list;
	for (list = syms; list; list = list->next)
	{
		int i;
		for (i = 0; i < MAX_HASH_CAPA; i ++)
		{
			Result res = list->symbols + i;
			if (res->bin.type == TYPE_STR)
				fprintf(stderr, "%s = %s\n", res->name, res->bin.string);
		}
	}
}

/* check if variable <name> is already defined */
Result symTableFindByName(SymTable syms, STRPTR varName)
{
	while (syms && syms->symbols)
	{
		Result var = syms->symbols + crc32(0, varName, strlen(varName)) % MAX_HASH_CAPA;

		if (strcasecmp(var->name, varName))
		{
			while (var->name[MAX_VAR_NAME-1])
			{
				/* linked list of symbols with same hash */
				var = syms->symbols + var->name[MAX_VAR_NAME-1];
				if (strcasecmp(var->name, varName) == 0)
					return var;
			}
		}
		else return var;

		syms = syms->next;
	}
	return NULL;
}

Result symTableFindByValue(SymTable syms, Variant v)
{
	/* need to scan all entries :-/ */
	while (syms && syms->symbols)
	{
		Result res, eof;
		for (res = syms->symbols, eof = res + MAX_HASH_CAPA; res < eof; res ++)
		{
			if (res->name[0] && res->bin.type == v->type)
			{
				switch (res->bin.type & 15) {
				case TYPE_DBL:   if (fabs(res->bin.real64 - v->real64) < 0.00001) return res; break;
				case TYPE_FLOAT: if (fabsf(res->bin.real32 - v->real32) < 0.00001) return res; break;
				case TYPE_STR:   if (strcmp(res->bin.string, v->string) == 0) return res; break;
				case TYPE_INT:   if (res->bin.int64 == v->int64) return res; break;
				case TYPE_INT32: if (res->bin.int32 == v->int32) return res; break;
				default:         break;
				}
			}
		}
		syms = syms->next;
	}
	return NULL;
}
