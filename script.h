/*
 * script.h : public functions for managing and running scripts.
 *
 * written by T.Pierron, june 2022
 */


#ifndef KALC_SCRIPT_H
#define KALC_SCRIPT_H

#include "parse.h"
#include "symtable.h"

void scriptShow(SIT_Widget app);
Bool scriptCancelRename(void);
void scriptCommitChanges(void);
void scriptTest(void);


/*
 * private datatypes below that point
 */

typedef struct ProgByteCode_t *    ProgByteCode;
typedef struct ProgLabel_t *       ProgLabel;
typedef struct ProgState_t *       ProgState;
struct ProgByteCode_t
{
	struct ListNode_t node;
	struct ListHead_t labels;
	struct ByteCode_t bc;
	struct SymTable_t symbols;

	TEXT name[16];
	int  errCode;
	int  line;
};

struct ProgState_t
{
	ListNode node;
	int      jumpIfFalse;
	int      jumpAtEnd;
	int      line;
	uint8_t  defState;
	uint8_t  grammar;
	uint8_t  pendingEnd;
};

struct ProgLabel_t
{
	ListNode node;
	int      jumpTo;
	int      writeTo;
	TEXT     name[1];
};

enum /*  extra error codes from script */
{
	PERR_DuplicateLabel = PERR_LastError,
	PERR_MissingLabel,
	PERR_NotInsideLoop,
	PERR_MissingEnd,
};

#endif
