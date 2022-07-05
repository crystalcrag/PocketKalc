/*
 * script.h : public functions for managing and running scripts.
 *
 * written by T.Pierron, june 2022
 */


#ifndef KALC_SCRIPT_H
#define KALC_SCRIPT_H

#include "parse.h"
#include "symtable.h"

#ifdef SITGLLIB_H
void scriptShow(SIT_Widget app);
int  scriptCheck(SIT_Widget, APTR, APTR);
#endif
Bool scriptCancelRename(void);
void scriptCommitChanges(void);
Bool scriptExecute(STRPTR prog, int argc, Variant argv);
void scriptTest(void);
void scriptResetStdout(void);


/* per compiled program, use sub-functions if you reach this limit */
#define MAX_SCRIPT_SIZE      65536

/*
 * private datatypes below that point
 */

typedef struct ProgByteCode_t *    ProgByteCode;
typedef struct ProgLabel_t *       ProgLabel;
typedef struct ProgState_t *       ProgState;
typedef struct ProgOutput_t        ProgOutput_t;
struct ProgByteCode_t
{
	struct ListNode_t  node;
	struct ListHead_t  labels;
	struct ByteCode_t  bc;
	struct SymTable_t  symbols;
	struct Variant_t * returnVal;

	TEXT name[16];
	int  crc32;
	int  curInst; /* STOKEN_* */
	int  errCode;
	int  errLine;
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

struct ProgOutput_t
{
	DATA8 buffer;
	int   usage, max;
};

enum /*  extra error codes from script */
{
	PERR_DuplicateLabel = PERR_LastError,
	PERR_MissingLabel,
	PERR_NotInsideLoop,
	PERR_MissingEnd,
	PERR_MissingSeparator,
	PERR_StdoutFull,
};

void scriptToByteCode(ProgByteCode prog, DATA8 source);

#endif
