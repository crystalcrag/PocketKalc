/*
 * symtable.h : public function to manage a list of symbols.
 *
 * written by T.Pierron, june 2022.
 */


#ifndef CALC_SYMTABLE_H
#define CALC_SYMTABLE_H

#include "parse.h"

typedef struct SymTable_t       SymTable_t;
typedef struct SymTable_t *     SymTable;

struct SymTable_t
{
	SymTable next;
	Result   symbols;
	int      count;
};

Result symTableAdd(SymTable, STRPTR name, Variant);
void   symTableFree(SymTable);
Result symTableFindByName(SymTable, STRPTR varName);
Result symTableFindByValue(SymTable, Variant);
void   symTableAssign(Result assignTo, Variant value);


#endif

