/*
 * Lexer.h : private datatype used by Lexer.c only, pubic API is in extra.h
 */

#ifndef	LEXER_H
#define	LEXER_H

#include "UtilityLibLite.h"
#include <stdio.h>

typedef uint16_t               NFAState;
typedef NFAState *             NFAVector;
typedef struct NFA_t *         NFA;
typedef struct DFA_t *         DFA;
typedef struct DFA_t           DFABuf;
typedef struct CFA_t *         CFA;
typedef struct NFATrans_t *    NFATrans;
typedef struct NFARel_t *      NFARel;
typedef struct NFAStack_t *    NFAStack;
typedef struct NFAStack_t      NFAStackBuf;
typedef struct NFAFile_t *     NFAFile;
typedef struct DFAState_t *    DFAState;
typedef struct DFATrans_t *    DFATrans;
typedef struct HilitClass_t *  HilitClass;
typedef struct LexerExtra_t *  Lexer;

#define	TRASH_STATE            ((NFAState)~0)

struct NFATrans_t
{
	uint16_t chrtrans;         /* Character read for transition */
	NFAState state;            /* New state reached */
	int      next;
};

/* Special values for chrtrans */
#define	EPSILON_TRANSITION     256
#define SUBDFA_TRANSITION      257
#define EPSILON_CLOSURE(nfa, state)   ((NFAVector)(nfa->epsbuf.buffer + nfa->epsclosure[state]))
#define SZST                   sizeof (NFAState)

struct NFARel_t
{
	NFAState trash;            /* Default trashcan state */
	int      first, last;      /* First transition in vector (NFA.trans - linked list) */
	uint8_t  isFinal;          /* Final state */
	uint8_t  flag;             /* FLG_* */
	uint16_t visit;            /* eps. closure stage */
};

enum /* Bitfield values for 'flag' */
{
	FLG_BEG_OF_WORD = 0x01,
	FLG_END_OF_WORD = 0x02,
	FLG_BEG_OF_LINE = 0x04,
	FLG_NON_ADV_ST  = 0x08,
	FLG_SUBDFA      = 0x10
};

struct NFAStack_t
{
	NFAState state1;           /* Start of parenthesis */
	NFAState state2;           /* For union */
};

struct NFAFile_t
{
	APTR   nf_Next;
	STRPTR nf_Name;
	STRPTR nf_Old;
	ULONG  nf_Line;
	FILE * nf_Fh;
};

struct HilitClass_t            /* Custom highlight class */
{
	uint8_t  name[15];         /* Class name */
	uint8_t  attr;             /* bitfield ATTR_* */
	uint8_t  fg[4];            /* Foreground color code */
	uint8_t  bg[4];            /* Bg color code */
};

enum
{
	ATTR_BOLD  = 1,
	ATTR_LINE  = 2,
	ATTR_RELFG = 4,
	ATTR_RELBG = 8,
};

struct NFA_t                   /* Non-Deterministic Finite Automaton */
{
	vector_t    states;        /* Table of states (NFARel) */
	vector_t    classes;       /* Custom highlight classes (HilitClass) */
	vector_t    group;         /* For region token (NFAStack) */
	vector_t    stack;         /* Stack of states (NFAStack) */
	vector_t    trans;         /* All transitions of NFA (NFATrans) */
	vector_t    epsbuf;        /* For epsilon closure (NFAState) */
	Bool        noCase;        /* TRUE if letter case doesn't care */
	Bool        fullWord;      /* TRUE if implicit \< \> around reg exp */
	NFAFile     files;         /* Inclusion directive */
	NFAStackBuf stackTop;      /* Item on the top (0 otherwise) */
	NFAState    startState;    /* Where to start attaching regexp */
	NFAState    lastState;     /* Last final state after regexp parsing */
	int *       epsclosure;    /* Epsilon closure of each state */
};

struct DFAState_t
{
	ListNode  node;
	int       stateID;         /* NDFA class (index in DFA.stateidbuf.buffer) */
	int       transidx;        /* Index in transition vector (DFA.trans) */
	NFAState  trash;           /* Default fallback state number */
	uint16_t  count;           /* Nb. of transition (256 max) */
	uint8_t   isFinal;         /* True if this is an accepting state */
	uint8_t   flag;            /* C.f. FLG_* */
	NFAState  autoTrans;       /* Final state auto transitioned to this */
	NFAState  index;           /* Self index in the list */
	NFAState  offset;          /* To compress dfa */
	DFAState  next;            /* Determinization process */
};

struct DFATrans_t
{
	uint8_t  chr;
	uint8_t  count;
	NFAState dest;
};

struct DFA_t                   /* Deterministic Finite Automaton */
{
	vector_t stateidbuf;       /* Stores DFAState->stateID */
	vector_t classes;          /* HilitClass */
	vector_t trans;            /* DFATrans */
	ListHead states;           /* DFAState */
	DFAState lastFinal, last;  /* Ordering 'states' in increasing index */
	uint16_t finals;           /* Stat for CFA */
	uint16_t normals;
	uint16_t nbStates;
};

#define STATEID(dfa, nth)      ((NFAVector)(dfa->stateidbuf.buffer + nth))[0]
#define DFA_TRANS(dfa, state)  ((DFATrans)(dfa->trans.buffer + state->transidx))

struct CFA_t                   /* Compressed Finite Automaton */
{
	vector_t   classes;
	NFAVector  transitions;    /* [k] */
	NFAVector  valid;          /* [k] */
	NFAVector  trash;          /* [n] */
	uint16_t * base;           /* [n] */
	uint8_t *  hlType;         /* [n] */
	uint8_t *  hlFlag;         /* [f] */
	NFAVector  autoTrans;      /* [f] */
	STRPTR     separators;
	uint16_t   nbState;        /* => n */
	uint16_t   nbTrans;        /* => k */
	uint16_t   nbFinal;        /* => f */
};

struct LexerExtra_t            /* used by SYN_HighlightText() */
{
	vector_t   startState;     /* starting state of each line */
	int        lastLine;
	int        lastByte;
	int        highlightChr;
};

/* Public functions */
NFA    NFA_Create(void);
void   NFA_Free(NFA);
void   DFA_Free(DFA);
void   CFA_Free(CFA);
void   DFA_Init(DFA);
STRPTR NFA_ParseRegExp(NFA, STRPTR, int, int, int * error);
int    NFA_Determinize(NFA, DFA);
int    NFA_PopFile(NFA, FILE **, STRPTR * opath, int * line);
int    NFA_StackFile(NFA, FILE **, STRPTR npath, STRPTR * opath, int * line);
int    NFA_AddClass(NFA, STRPTR name, DATA8 fg, DATA8 bg, int attr);
int    NFA_CheckClass(NFA, int offset, STRPTR token);
CFA    DFA_Compress(DFA, STRPTR sep);
APTR   GetNthItem(vector v, int i);

/* Stacking method of start state (for grouping) */
#define	KEEP_START_STATE       0     /* Do nothing */
#define	PUSH_START_STATE       1
#define	POP_START_STATE        2

enum /* lower 8bits returned by SYN_Parse() */
{
	REGEXP_OK,
	REGEXP_SYNTAX_ERROR,
	REGEXP_MEMORY,
	REGEXP_INVALID_META_POS,
	REGEXP_PARENTHESIS_ERROR,
	REGEXP_BAD_INSTRUCTION,
	REGEXP_MISSING_END,
	REGEXP_BAD_UNION,
	REGEXP_DUP_CLASS,
	REGEXP_FILE_NOT_FOUND,
	REGEXP_FATAL_ERROR
};

#ifdef DEBUG_DFA
void print_ndfa_states(NFA ndfa);
void print_dfa_states(DFA dfa);
#else
#define print_ndfa_states(x)
#define print_dfa_states(x);
#endif

#endif

