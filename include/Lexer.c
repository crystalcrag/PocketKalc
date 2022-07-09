/*
 * Lexer.c : on the fly lexical analyzis based on deterministic finite automaton for SIT_EDITBOX.
 *
 * written by T.Pierron, adapted from "Compiler principles" by Alfred V. Aho, Ravi Sethi, and Jeffrey D. Ullman
 */

//#define DEBUG_DFA
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <malloc.h>
#include "UtilityLibLite.h"
#include "Lexer.h"


	static TEXT SYN_Tokens[] =
		"case,separators,begin,end,match,include,name,class";
	static TEXT SYN_Classes[] =
		"text,type,identifier,keyword,comment,directive,constant,"
		"special,error,note";

	#define	TOKEN_BASE       8
	#define	MAX_TOKEN_PENS   10


static int IsSep(CFA cfa, STRPTR str, int dir)
{
	wchar_t   chr[2];
	wchar_t * p = NULL;
	STRPTR    old;

	if (cfa) p = (wchar_t *) cfa->separators;
	if (! p) p = L"!-/:-?[-]^`{-\x00BF\x00D7\x00F7";

	/* jump to next/previous character */
	for (old = str, str += dir; (*str & 0xc0) == 0x80; str += dir);

	/* get utf-16 code point */
	dir = old < str ? str - old : old - str; if (dir == 0) dir = 1;
	UTF8ToUTF16((STRPTR) chr, DIM(chr), str, dir);

	/* Check if it is a space */
	if (wcschr(L"\t\r\n \x00A0", chr[0])) return 2;

	/* Check if it is in the list of separators */
	for (; *p; p ++)
	{
		if (p[1] == '-' && p[2])
		{
			if (p[0] <= chr[0] && chr[0] <= p[2]) return 1;
			p += 2;
		}
		else if (p[0] == chr[0])
		{
			return 1;
		}
	}
	return 0;
}

/*
 * perform token recognition on specified line, using given DFA
 * returns the state reached by processing last character in the line
 */
#define	SYN_RegMatch(spec, length, hilight)   \
{	int len = length; \
	if (start < length) { \
		if (start > 0) len -= start, end -= start, start = 0; \
		if (end < len) len = end, end = 0; \
		if (spec) memset(spec, hilight, len), spec += len; \
		end -= len; \
	} \
	else start -= length, end -= length; \
}

NFAState SYN_Exec(APTR automaton, DATA8 line, int max, int maxNext, int state, DATA8 spec, int start, int end)
{
	CFA      dfa = automaton;
	DATA8    p, accept, endstr;
	ULONG    acceptLen = 0;
	Bool     acceptToken = 1;
	NFAState acceptState = 0;
	NFAState lastState = TRASH_STATE; /* last Accepting State */
	NFAState defState;
	NFAState curState = state;
	NFAState trash;
	Bool     forward = False;

	/* section not yet parsed, it will be done soon: assume start state in the meantime */
	if (curState == TRASH_STATE) curState = 0;

	dfa       = automaton;
	trash     = dfa->trash[curState];
	defState  = dfa->hlType[trash];
	if (defState > 0) defState --;

	for (p = accept = line, endstr = p + max; p < endstr; p++)
	{
		int offset = dfa->base[curState];

		if (dfa->valid[offset + *p] != curState)
		{
			/* no match: flush previously recognized token */
			if (acceptLen > 0)
			{
				if (acceptToken)
				{
					SYN_RegMatch(spec, acceptLen, acceptState);
					accept += acceptLen;
				}
				p = accept - 1;
			}
			else
			{
				if (forward) p = accept;
				SYN_RegMatch(spec, 1, defState);
			}
			acceptLen = 0;

			if (end < 0)
				return 0;

			accept = p+1;
			curState = trash;
			forward = False;
			lastState = TRASH_STATE;
		}
		else /* we can reach a new state */
		{
			NFAState hilight, match, next = dfa->transitions[offset + *p];
			if (next < dfa->nbFinal)
			{
				hilight = dfa->hlType[next];
				match   = dfa->hlFlag[next];
			}
			else hilight = match = 0;

			if (hilight == 0 ||
			    ((match & FLG_BEG_OF_WORD) && accept > line && ! IsSep(dfa, accept, -1)) ||
			    ((match & FLG_END_OF_WORD) && ! IsSep(dfa, p, 1)))
			{
				/* not not a final state */
				curState = next;
			}
			else /* final state: continue parsing in case we match a longer RegExp */
			{
				lastState   = dfa->autoTrans[next];
				trash       = dfa->trash[lastState];
				acceptState = hilight;
				acceptLen   = p - accept + 1;
				acceptToken = (match & FLG_NON_ADV_ST) == 0;
				defState    = dfa->hlType[trash];
				curState = next;
				if (acceptState > 0) acceptState --;
				if (defState > 0)    defState --;
			}
			forward = True;
		}
	}
	/* flush stack */
	if (acceptLen > 0)
	{
		SYN_RegMatch(spec, acceptLen, acceptState);
		accept += acceptLen;
		acceptLen = 0;
		curState = trash;
	}
	if (accept < p)
	{
		/* we are in an potential accepting state: continue matching (if we have room) */
		if (maxNext > 0)
		{
			/* roll the state machine for a bit longer until we reach a final state */
			DATA8 s;
			NFAState cur = curState;
			for (s = p, endstr = p + maxNext; s < endstr; s ++)
			{
				/* this is greatly simplified thankfully (only a linear scan, no backtrack) */
				int offset = dfa->base[cur];
				if (dfa->valid[offset + *s] != cur) break; /* no final state within reach: abort */
				NFAState hilight, match, next = dfa->transitions[offset + *s];
				if (next < dfa->nbFinal)
				{
					hilight = dfa->hlType[next];
					match   = dfa->hlFlag[next];
				}
				else hilight = match = 0;

				if (hilight == 0 || ((match & FLG_END_OF_WORD) && ! IsSep(dfa, s, 1)))
				{
					/* not not a final state */
					cur = next;
				}
				else /* final state: continue parsing in case we match a longer RegExp */
				{
					defState = hilight;
					if (defState > 0) defState --;
					break;
				}
			}
		}
		SYN_RegMatch(spec, p - accept, defState);
	}

	return /*forward ? trash :*/ curState;
}

static int SYN_FreeLexer(SIT_Widget w, APTR cd, APTR ud)
{
	Lexer lex = ud;
	if (lex->startState.buffer)
		free(lex->startState.buffer);
	return 1;
}

/* syntax/lexical analyzer for edit box */
int SYN_HighlightText(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnEditBox * msg = cd;

	#define STARTSTATE(lex, line)   ((NFAVector)lex->startState.buffer)[line]
	Lexer lex  = NULL;
	CFA   cfa  = ud;
	int   line = msg->line;
	DATA8 cmap = msg->cmap;

	if (cfa == NULL) return 0;
	if (cmap[9*28-1] == 0xfc)
	{
		/* hope the user did its job (a.k.a RTFM) */
		SIT_GetValues(w, SIT_UserData, &lex, NULL);
		if (lex == NULL) return 0; /* nope, it did not RTFM */

		if (cfa->classes.count > 0)
		{
			/* add custom classes at end of colormap */
			HilitClass cls;
			DATA8      last;
			uint8_t    i;
			for (last = cmap; last[8] < 0xfc; last += 9);
			for (cls = vector_first(cfa->classes), i = cfa->classes.count; i > 0; i --, cls ++)
			{
				uint8_t attr = cls->attr;
				memcpy(last,   attr & ATTR_RELFG ? cmap + cls->fg[0] * 9     : cls->fg, 4);
				memcpy(last+4, attr & ATTR_RELBG ? cmap + cls->bg[0] * 9 + 4 : cls->bg, 4);
				last[8] = attr & 3;
			}
		}
		SIT_AddCallback(w, SITE_OnFinalize, SYN_FreeLexer, lex);
		lex->startState.itemsize = SZST;
		lex->lastLine = lex->lastByte = 0;
		if (lex->startState.buffer)
			memset(lex->startState.buffer, 0xff, lex->startState.count * SZST);
		/* don't do this again */
		cmap[9*28-1] = 0xf8;
		/* will prevent from reading that property again */
		memcpy(cmap + 9 * 27, &lex, sizeof lex);
	}
	else memcpy(&lex, cmap + 9 * 27, sizeof lex);

	/* need to keep line starting state into a cache */
	if (line+1 >= lex->startState.max)
	{
		int old = lex->startState.max;
		vector_nth(&lex->startState, line+1);
		memset(&STARTSTATE(lex, old), 0xff, (lex->startState.max - old) * SZST);
		if (old == 0) STARTSTATE(lex, 0) = 0;
	}

	if (lex->lastLine < line)
	{
		/* skipped some lines: parse them now */
		DATA8 text;
		int   length, i, byte;
		SIT_GetValues(w, SIT_Title, &text, NULL);
		for (i = lex->lastLine, byte = lex->lastByte; i < line; i ++, byte += length)
		{
			length = SIT_TextEditLineLength(w, i, NULL, False);
			STARTSTATE(lex, i+1) = SYN_Exec(cfa, text + byte, length, 0, STARTSTATE(lex, i), NULL, 0, length);
		}
	}
	/* this is where the magic happens */
	int last = SYN_Exec(cfa, msg->textBuffer, msg->length, SIT_TextEditLineLength(w, line+1, NULL, False), STARTSTATE(lex, line), msg->lexerCMap, 0, msg->length);
	int prev = STARTSTATE(lex, line+1);
	STARTSTATE(lex, line+1) = last;

	if (prev != last || line > lex->lastLine)
	{
		lex->lastLine = line + 1;
		lex->lastByte = msg->byte + msg->length;
	}

	/* quick hack for bracket matching */
	last = lex->highlightChr - 1;
	if (last >= 0 && msg->byte <= last && last < msg->byte + msg->length)
		msg->lexerCMap[last - msg->byte] = 9;
	return 1;
}

void SYN_MatchBracket(SIT_Widget w, int pos)
{
	Lexer lex;

	SIT_GetValues(w, SIT_UserData, &lex, NULL);

	if (lex) lex->highlightChr = pos;
}


/*
 * Syntax File parsing
 */

/* skip token and following white spaces [^\s]*\s* */
static STRPTR SYN_SkipToken(STRPTR tok)
{
	STRPTR p;

	for (p = tok; *p && !isspace(*p); p++); if (*p == 0) return p; *p++ = 0;
	while (*p && isspace(*p)) p++;

	return p;
}

/* Parse a custom class for highlighting tokens */
static Bool SYN_ParseClass(NFA nfa, STRPTR format)
{
	/* format is "name color_fg color_bg style" */
	STRPTR  name = format;
	STRPTR  clsname = name;
	uint8_t fg[4];
	uint8_t bg[4];
	uint8_t attr;
	int     type;

	format = SYN_SkipToken(format);
	if (FindInList(SYN_Classes, name, 0) >= 0)
		return REGEXP_BAD_INSTRUCTION;

	/* parse foreground color */
	format = SYN_SkipToken(name = format);
	type = FindInList(SYN_Classes, name, 0);
	attr = 0;
	if (type >= 0) memset(fg, 0, 4), fg[0] = type, attr |= ATTR_RELFG; else
	if (! SIT_ParseCSSColor(format, fg)) return False;

	/* background color */
	format = SYN_SkipToken(name = format);
	type = FindInList(SYN_Classes, name, 0);
	if (type >= 0) memset(bg, 0, 4), bg[0] = type, attr |= ATTR_RELBG; else
	if (! SIT_ParseCSSColor(format, fg)) return False;

	/* attributes */
	while (*format)
	{
		format = SYN_SkipToken(name = format);
		switch (FindInList("bold,underline", name, 0)) {
		case 0: attr |= ATTR_BOLD; break;
		case 1: attr |= ATTR_LINE;
		}
	}

	return NFA_AddClass(nfa, clsname, fg, bg, attr);
}

/* Convert utf8 sequence to unicode code point */
STRPTR SYN_ParseSep(STRPTR sep)
{
	int nb = UTF8ToUTF16(NULL, 0, sep, -1);
	STRPTR dest = malloc(nb * sizeof (wchar_t));

	UTF8ToUTF16(dest, nb, sep, -1);

	return dest;
}

#define SZ_LINE     256

/* Transform a syntax file into a deterministic finite automaton */
int SYN_Parse(STRPTR path, STRPTR buffer, CFA * ret)
{
	STRPTR line, p, n, sep;
	DFABuf dfa;
	NFA    nfa;
	int    error = 0;
	int    linenum = 1;
	FILE * io = NULL;

	*ret = NULL;
	if (path)
	{
		io = fopen(path, "rb");
		if (io == NULL)
			return REGEXP_FILE_NOT_FOUND;
	}

	DFA_Init(&dfa);
	nfa  = NFA_Create();
	sep  = NULL;
	*ret = NULL;
	line = io ? alloca(SZ_LINE) : NULL;

	while (! error)
	{
		if (io == NULL)
		{
			if (buffer == NULL) break;
			line = buffer;
			buffer = NextLine(buffer, True);
		}
		else if (fgets(line, SZ_LINE, io) == NULL)
		{
			if (! NFA_PopFile(nfa, &io, &path, &linenum)) break;
			if (! fgets(line, SZ_LINE, io)) break;
		}
		int type;
		linenum ++;

		StripCRLF(line);

		/* ignore comment */
		if (line[0] == '#') continue;

		/* skip starting spaces */
		for (p = line; *p && isspace(*p); p++);

		/* discard empty lines */
		if (*p == 0) continue;

		n = SYN_SkipToken(p);
		type = FindInList(SYN_Tokens, p, 0);
		if (type < 0)
		{
			type = FindInList(SYN_Classes, p, 0);
			/* if regular tokens don't match, look for custom class */
			if (type < 0)
				type = NFA_CheckClass(nfa, MAX_TOKEN_PENS + TOKEN_BASE + 1, p);
			else
				type += TOKEN_BASE;
		}

		switch (type) {
		case -1:
			error = REGEXP_BAD_INSTRUCTION;
			break;
		case 0: /* case sensitiveness */
			switch (FindInList("sensitive,insensitive", n, 0)) {
			case 0: nfa->noCase = False; break;
			case 1: nfa->noCase = True; break;
			default:
				error = REGEXP_BAD_INSTRUCTION;
			}
			break;
		case 1: /* separators */
			if (nfa->files == NULL) /* only parse directive from current file, not those included */
				sep = SYN_ParseSep(n);
			break;
		case 2: /* start of a region */
			p    = SYN_SkipToken(n);
			type = FindInList(SYN_Classes, n, 0);
			if (type < 0)
				type = NFA_CheckClass(nfa, MAX_TOKEN_PENS + TOKEN_BASE + 1, n);
			else
				type += TOKEN_BASE;

			if (type >= TOKEN_BASE && *p)
				NFA_ParseRegExp(nfa, p, type-TOKEN_BASE+1, PUSH_START_STATE, &error);
			else
				error = REGEXP_BAD_INSTRUCTION;
			break;
		case 3: /* end of region */
			if (*n == 0) n = "\\n";
			NFA_ParseRegExp(nfa, n, 0, POP_START_STATE, &error);
			break;
		case 4: /* match */
			switch (FindInList("noseparator,wordonly", n, 0)) {
			case 0:  nfa->fullWord = False; break;
			case 1:  nfa->fullWord = True; break;
			default: error = REGEXP_BAD_INSTRUCTION;
			}
			break;
		case 5: /* include */
			error = NFA_StackFile(nfa, &io, n, &path, &linenum);
			break;
		case 6: /* syntax's name -- processed elsewhere */
			break;
		case 7: /* custom highlight class */
			error = SYN_ParseClass(nfa, n);
			break;
		default: /* simple token */
			/* extract tokens and build state machine */
			for (p = n; p && *p; p = n)
			{
				n = NFA_ParseRegExp(nfa, p, type - TOKEN_BASE + 1, KEEP_START_STATE, &error);

				if (n)
					while (*n && isspace(*n)) n ++;
			}
		}
	}

	if (io) fclose(io);

	/* if we exited from parsing abruptly, some files might be stacked */
	while (NFA_PopFile(nfa, &io, &path, &linenum)); /* will close io */

	if (! error)
	{
		if (NFA_Determinize(nfa, &dfa))
		{
			*ret = DFA_Compress(&dfa, sep);
		}
	}
	else error |= (linenum-1) << 8;

	if (sep) free(sep);
	DFA_Free(&dfa);
	NFA_Free(nfa);

	return error;
}

/*
 * DFA procssing: convert a NFA (non-deterministic finite automaton) into a DFA, and finally CFA
 * (compressed DFA).
 */
static Bool NFA_PushState(NFA nfa, NFAState state)
{
	NFAStack top = vector_nth(&nfa->stack, nfa->stack.count);

	if (top)
	{
		top->state1 = state;
		top->state2 = TRASH_STATE;
		nfa->stackTop = *top;
		return True;
	}
	return False;
}

/* remove last state */
static NFAState NFA_PopState(NFA nfa)
{
	static NFAStackBuf dummyStack = {0, TRASH_STATE};

	if (nfa->stack.count == 0)
	{
		//error = REGEXP_PARENTHESIS_ERROR;
		return TRASH_STATE;
	}
	else
	{
		/* get the actual top, and pops it */
		NFAStack pop = vector_nth(&nfa->stack, nfa->stack.count - 1);
		NFAStack top;
		nfa->stack.count --;

		/* put new top into `na_StackTop' */
		if (nfa->stack.count > 0)
			top = vector_nth(&nfa->stack, nfa->stack.count - 1);
		else
			top = &dummyStack;

		nfa->stackTop = *top;

		return pop->state1;
	}
}

/* add a new state in the automaton */
static Bool NFA_AddRel(NFA nfa, NFAState src, int chrTrans, NFAState dest)
{
	NFARel state = vector_nth(&nfa->states, src);

	if (state)
	{
		int      pos   = nfa->trans.count;
		NFATrans trans = vector_nth(&nfa->trans, pos);

		if (trans)
		{
			trans->chrtrans = chrTrans;
			trans->state    = dest;
			trans->next     = 0;
			state->trash    = nfa->startState;
			if (state->last >= 0)
			{
				trans = vector_nth(&nfa->trans, state->last);
				trans->next = pos;
			}
			if (state->first < 0) state->first = pos;
			state->last = pos;
			return True;
		}
	}
	return False;
}

/*
 * the following functions enable to simplify indirectly DFA, by marking some state as being the default
 * fallback when no transition are defined for a given state (useful for tokens that start with a certain
 * regexp and ends with another one).
 */
static Bool NFA_PushFinalState(NFA nfa, int prev, int hlType)
{
	NFAStack top = vector_nth(&nfa->group, nfa->group.count);

	/* add a transition to break the epsilon-closure mecanism */
	if (top && NFA_AddRel(nfa, prev, SUBDFA_TRANSITION, nfa->lastState+1))
	{
		nfa->lastState ++;
		top->state1     = nfa->startState;
		top->state2     = hlType;
		nfa->startState = nfa->lastState;
		return True;
	}
	return False;
}

static int NFA_PopFinalState(NFA nfa)
{
	if (nfa->group.count > 0)
	{
		NFAStack top = vector_nth(&nfa->group, nfa->group.count-1);
		nfa->group.count --;

		nfa->startState = top->state1;

		return top->state2;
	}
	else /* an "end" directive is placed before a "begin" one */
	{
		return -1;
	}
}

/*
 * those functions are used to handle 'include' directive
 */
int NFA_StackFile(NFA nfa, FILE ** io, STRPTR npath, STRPTR * opath, int * line)
{
	NFAFile file = calloc(sizeof *file + strlen(npath) + strlen(*opath) + 2, 1);
	int err;

	if (file)
	{
		FILE * newio;
		file->nf_Fh   = *io;
		file->nf_Line = *line;
		file->nf_Next = nfa->files;
		file->nf_Name = (STRPTR) (file + 1);
		file->nf_Old  = *opath;

		/* path of included file, will be relative to file where directive is */
		strcpy(file->nf_Name, *opath);
		ParentDir(file->nf_Name);
		AddPart(file->nf_Name, npath, 0xffff);

		newio = fopen(file->nf_Name, "rb");

		if (newio)
		{
			nfa->files = file;
			*line = 1;
			*io = newio;
			return REGEXP_OK;
		}
		else err = REGEXP_FILE_NOT_FOUND;
		free(file);
	}
	else err = REGEXP_MEMORY;
	return err;
}

int NFA_PopFile(NFA nfa, FILE ** io, STRPTR * opath, int * line)
{
	NFAFile file = nfa->files;

	if (*io) fclose(*io);
	if (file)
	{
		*io    = file->nf_Fh;
		*line  = file->nf_Line;
		*opath = file->nf_Old;
		nfa->files = file->nf_Next;
		free(file);
	}
	else *io = NULL;
	return REGEXP_OK;
}

/*
 * the following functions transform regular expressions into Non-deterministic Finite Automaton (NFA),
 * using a simple constructive algorithm
 */
NFA NFA_Create(void)
{
	NFA nfa = calloc(sizeof *nfa, 1);

	if (nfa)
	{
		static struct NFARel_t defstate = {.first = -1, .last = -1};
		nfa->states.clearwith = &defstate;
		nfa->states.itemsize = sizeof (struct NFARel_t);
		nfa->stackTop.state2 = TRASH_STATE;
		nfa->classes.itemsize = sizeof (struct HilitClass_t);
		nfa->stack.itemsize =
		nfa->group.itemsize = sizeof (struct NFAStack_t);
		nfa->trans.itemsize = sizeof (struct NFATrans_t);
		nfa->epsbuf.itemsize = SZST;
	}
	return nfa;
}

/* free all memory associated to a NDFA */
void NFA_Free(NFA nfa)
{
	if (nfa)
	{
		vector_free(nfa->stack);
		vector_free(nfa->group);
		vector_free(nfa->states);
		vector_free(nfa->classes);
		vector_free(nfa->trans);
		vector_free(nfa->epsbuf);
		if (nfa->epsclosure) free(nfa->epsclosure);
		free(nfa);
	}
}

/* add a new custom highlight class */
int NFA_AddClass(NFA nfa, STRPTR name, DATA8 fg, DATA8 bg, int attr)
{
	HilitClass cls = vector_nth(&nfa->classes, nfa->classes.count);

	if (cls)
	{
		CopyString(cls->name, name, sizeof cls->name);
		memcpy(cls->fg, fg, 4);
		memcpy(cls->bg, bg, 4);
		cls->attr = attr;
		return REGEXP_OK;
	}
	return REGEXP_MEMORY;
}

/* check if a class exists and returns its index */
int NFA_CheckClass(NFA nfa, int offset, STRPTR token)
{
	HilitClass cls;
	STRPTR     p;
	int        i;

	for (p = token; *p && !isspace(*p); p++);

	for (cls = (APTR) nfa->classes.buffer, i = nfa->classes.count; i > 0; cls ++, offset ++, i --)
		if (strncmp(token, cls->name, p-token) == 0) return offset;

	return -1;
}

static int HexToBin(TEXT c)
{
	if ('0' <= c && c <= '9') return c - '0';
	if ('a' <= c && c <= 'f') return c - 'a' + 10;
	return c - 'A' + 10;
}

/* parse special character */
static STRPTR NFA_ParseEscape(NFA nfa, STRPTR p, NFAState oldState)
{
	STRPTR set = "0123456789abcdefABCDEF\t\n\r ";
	int    len = 1;
	TEXT   chr = p[0];

	switch (chr) {
	case 'o': len = 8;  break;
	case 'd': len = 10; break;
	case 'h': len = 16; break;
	case 'H': len = 22; break;
	case 's': len = 4;  set += 22; break;
	case 'x': chr = (HexToBin(p[1]) << 4) | HexToBin(p[2]); p += 3; // no break;
	default : set = &chr;
	}

	/* set multiple relations at once */
	for (; len > 0; len --)
	{
		if (! NFA_AddRel(nfa, oldState, *set++, oldState+1))
			return NULL;
	}

	return p;
}

enum /* special characters */
{
	MATCH_BEG_OF_WORD = 256,
	MATCH_END_OF_WORD = 257,
	MATCH_BEG_OF_LINE = 258
};

static int ReadChar(STRPTR fmt, int * n)
{
	*n = 1;
	if (*fmt == '\\')
	{
		*n = 2;
		switch (fmt[1]) {
		case  0:  *n = 1; return '\\';
		case '$': /* no break; */
		case 'n': return '\n';
		case 'r': return '\r';
		case 't': return '\t';
		case 'e': return 0x1b;
		case '<': return MATCH_BEG_OF_WORD;
		case '>': return MATCH_END_OF_WORD;
		case '^': return MATCH_BEG_OF_LINE;
		default : *n = 1;
		}
	}
	return *fmt;
}

/* parse character set (only suited for 8bit chars) */
static int NFA_ParseSet(NFA nfa, STRPTR set, NFAState old, NFAState new)
{
	TEXT chars[32];
	Bool inv;
	int  chr, len, n, end;

	if (*set == '^') inv = True, set ++, len = 1;
	else len = 0, inv = False;

	memset(chars, 0, sizeof chars);

	while ((chr = ReadChar(set, &n)) != ']')
	{
		if (chr == 0) return REGEXP_PARENTHESIS_ERROR;
		len += n;
		set += n;
		if (chr == '\\') chr = *set ++, len ++;
		/* range of char specifier? */
		if (set[0] == '-' && (end = ReadChar(set+1, &n)) != ']')
		{
			len += n+1;
			set += n+1;
		}
		else end = chr;
		while (chr <= end)
			chars[chr>>3] |= 1 << (chr&7), chr ++;
	}
	if (inv)
		for (set = chars; set < chars+32; *set++ ^= 0xff);

	for (n = 0; n < 256; n ++)
		if ((chars[n>>3] & (1<<(n&7))) && ! NFA_AddRel(nfa, old, n, new))
			return REGEXP_MEMORY;

	return len;
}


/* alloc nfa state according to regular expression */
STRPTR NFA_ParseRegExp(NFA nfa, STRPTR regexp, int hlType, int stackMethod, int * error)
{
	NFAState last, prev, star = TRASH_STATE;
	int      chr, flag, len, i;
	STRPTR   p;

	/* add a starting epsilon transition for separating each regexp */
	if (! NFA_AddRel(nfa, nfa->startState, EPSILON_TRANSITION, prev = nfa->lastState+1))
	{
		*error = REGEXP_MEMORY;
		return NULL;
	}

	last  = prev + 1;
	flag = 0;

	/* this is the core of token recognition */
	for (p = regexp; (chr = ReadChar(p, &len)); )
	{
		if (*p == '\n' || *p == ' ') break; p += len;

		if (chr == 'r' && strncmp(p, "eusetoken:", 10) == 0) {
			/* keep the matched token available for next DFA states */
			p += 10; flag |= FLG_NON_ADV_ST; continue;
		}

		switch (chr) {
		case '.': /* anything except newline */
			for (i = 0; i < 256; i ++)
			{
				if (i != '\n' && ! NFA_AddRel(nfa, prev, i, last))
				{
					*error = REGEXP_MEMORY;
					return NULL;
				}
			}

			star = prev;
			prev = last++;
			break;
		case '\\':
			p = NFA_ParseEscape(nfa, p, prev);
			if (p == NULL)
			{
				*error = REGEXP_MEMORY;
				return NULL;
			}
			star = prev;
			prev = last++;  p++;
			break;

		/* meta-characters */
		case '[':
			if ((len = NFA_ParseSet(nfa, p, prev, last)) == 0)
				return NULL;
			star = prev;
			prev = last++;  p += len+1;
			break;
		case '?':
		case '+':
		case '*':
			/* a star should follow a non-meta character */
			if (star == TRASH_STATE)
			{
				*error = REGEXP_INVALID_META_POS;
				return NULL;
			}
			if ((chr != '?' && ! NFA_AddRel(nfa, prev, EPSILON_TRANSITION, star)) ||
			    (chr != '+' && ! NFA_AddRel(nfa, star, EPSILON_TRANSITION, prev)))
				return NULL;
			star = TRASH_STATE;
			/* required if there is a ?, * or + following this one */
			if(! NFA_AddRel(nfa, prev, EPSILON_TRANSITION, last))
				return NULL;
			prev = last++;
			break;
		case '|':
			if (nfa->stackTop.state2 == TRASH_STATE)
			{
				/* no union registered yet */
				if (nfa->stack.count > 0)
				{
					NFAStack top = vector_nth(&nfa->stack, nfa->stack.count-1);
					top->state2 = nfa->stackTop.state2 = prev;
				}
				else /* no parenthesis before */
				{
					*error = REGEXP_BAD_UNION;
					return NULL;
				}
			}
			else /* already registered, add epsilon transition to starting state */
			{
				if (! NFA_AddRel(nfa, prev, EPSILON_TRANSITION, nfa->stackTop.state2))
					return NULL;
			}
			star = TRASH_STATE;
			prev = nfa->stackTop.state1;
			break;
		case '(':
			/* the epsilon transition is required for star */
			if (! NFA_AddRel(nfa, prev, EPSILON_TRANSITION, last))
				return NULL;
			prev = last++;
			if (! NFA_PushState(nfa, prev))
				return NULL;
			star = TRASH_STATE;
			break;
		case ')':
			/* add an espilon-transition if there was union regexp */
			if (nfa->stackTop.state2 != TRASH_STATE)
			{
				NFAState state = nfa->stackTop.state2;
				if (! NFA_AddRel(nfa, prev, EPSILON_TRANSITION, state))
					return NULL;

				prev = state;
			}
			star = NFA_PopState(nfa);
			if (star == TRASH_STATE) return NULL;
			break;
		case MATCH_BEG_OF_WORD: flag |= FLG_BEG_OF_WORD; break;
		case MATCH_END_OF_WORD: flag |= FLG_END_OF_WORD; break;
		case MATCH_BEG_OF_LINE: flag |= FLG_BEG_OF_LINE; break;
		/* standard characters */
		default :
			if(! NFA_AddRel(nfa, prev, chr, last))
				return NULL;

			if (nfa->noCase)
			{
				/* case insensitive pattern (only us-ascii) */
				if ('a' <= chr && chr <= 'z')
					NFA_AddRel(nfa, prev, chr + ('A'-'a'), last);
				if ('A' <= chr && chr <= 'Z')
					NFA_AddRel(nfa, prev, chr + ('a'-'A'), last);
			}
			star = prev;
			prev = last++;
		}
		if (nfa->lastState < prev)
			nfa->lastState = prev;
	}

	if (nfa->stack.count > 0) {
		*error = REGEXP_PARENTHESIS_ERROR;
		return NULL;
	}

	/* stacking method */
	star = nfa->group.count;
	switch (stackMethod) {
	case PUSH_START_STATE:
		if (! NFA_PushFinalState(nfa, prev, hlType))
		{
			*error = REGEXP_MEMORY;
			return NULL;
		}

		/* prev = nfa->startState; */
		last = prev;
		break;
	case POP_START_STATE:
		if ((hlType = NFA_PopFinalState(nfa)) == -1)
		{
			*error = REGEXP_BAD_INSTRUCTION;
			return NULL;
		}

		/* no break; */
	default:
		last = prev;
		/* need to break epsilon closure */
		if (NFA_AddRel(nfa, prev, SUBDFA_TRANSITION, nfa->lastState+1))
			nfa->lastState ++;

		NFA_AddRel(nfa, nfa->lastState, EPSILON_TRANSITION, nfa->startState);
	}

	/* keep track of last state reached */
	NFARel state = vector_nth(&nfa->states, last);

	if (nfa->fullWord && ! (flag & FLG_BEG_OF_LINE))
		flag |= FLG_BEG_OF_WORD | FLG_END_OF_WORD;

	state->isFinal = hlType;
	state->flag    = flag;
	state->trash   = nfa->startState;

	return p;
}

/*
 * This part deals with NFA transformation into DFA. Deterministic automatons can be processed in linear
 * time, while non-deterministics need a polynomial time. Sadly, making an automaton deterministic is
 * an exponential process, and can result in a big amount of state (although most of the time, it will
 * be of polynomial complexity).
 */

/* need to be sorted so that they can be compared */
static int sortstate(const void * item1, const void * item2)
{
	return * (NFAVector) item1 - * (NFAVector) item2;
}

#define RELOCATE(dst, src, old)    (APTR) ((DATA8) src + ((DATA8) dst - (DATA8)old))

/* compute list of joinable states with epsilon-transition */
static int NFA_EpsilonClosure(NFA nfa, NFAState stindex)
{
	NFAVector ret;
	NFATrans  trans;
	NFARel    state;
	int       nb, i, mark;

	ret = vector_nth(&nfa->epsbuf, nfa->epsbuf.count + 1);
	ret --;
	ret[0] = 1;
	ret[1] = stindex;

	for (nb = i = 1, mark = stindex + 1; i <= nb; i++)
	{
		state = vector_nth(&nfa->states, ret[i]);

		if (state->visit == mark || state->first < 0) continue;
		state->visit = mark;

		for (trans = vector_nth(&nfa->trans, state->first); ;
		     trans = vector_nth(&nfa->trans, trans->next))
		{
			if (trans->chrtrans == EPSILON_TRANSITION)
			{
				STRPTR old = nfa->epsbuf.buffer;
				NFAVector st = vector_nth(&nfa->epsbuf, nfa->epsbuf.count);
				*st = trans->state;
				ret = RELOCATE(ret, nfa->epsbuf.buffer, old);
				ret[0] ++; nb ++;
			}
			if (trans->next == 0) break;
		}
	}
	switch (ret[0]) {
	case 1:  break;
	case 2:  if (ret[1] > ret[2]) swap(ret[1], ret[2]); break;
	default: qsort(ret + 1, ret[0], sizeof *ret, sortstate);
	}
	/* cannot use direct pointer because of realloc() */
	return (STRPTR) ret - nfa->epsbuf.buffer;
}

static void NFA_MergeStateId(int * offset, vector buf, NFAVector merge)
{
	int       pos = *offset, length;
	NFAVector state, vec;

	if (pos)
	{
		NFAVector src, dst;
		int i, j;

		state  = (NFAVector) (buf->buffer + pos - 1);
		length = merge[0] + state[0] + 1;
		vec    = alloca(sizeof *vec * length);

		memcpy(vec, merge, (merge[0]+1) * sizeof *vec);

		/* merge table 'state' with 'vec' */
		for (src = state, i = src[0], src ++; i > 0; i --, src ++)
		{
			/* sort insert and remove duplicates */
			for (j = vec[0], dst = vec + 1; j > 0 && *dst < *src; dst ++, j --);
			if (j == 0)
				*dst = *src; /* add at end */
			else if (*dst > *src)
				memmove(dst+1, dst, j * sizeof *dst), *dst = *src;
			else continue;
			vec[0] ++;
		}
		if (state[0] < vec[0])
		{
			NFAVector end = state + state[0] + 1;
			STRPTR    old = buf->buffer;
			if (buf->count > (end - (NFAVector) old))
				goto relocate_vec;
			vector_nth(buf, buf->count + vec[0] - state[0]);
			state = RELOCATE(state, buf->buffer, old);
		}
		else return; /* same vector (cannot be smaller) */
	}
	else /* copy 'merge' at end of 'buf' */
	{
		vec = merge;
		relocate_vec:
		state = vector_nth(buf, buf->count + vec[0] + 1);
		state -= vec[0] + 1;
		*offset = (STRPTR) state - buf->buffer + 1;
	}
	memcpy(state, vec, (vec[0]+1) * sizeof *vec);
}

#ifdef DEBUG_DFA
static void NFA_PrintState(NFAState * id, int nb)
{
	printf("State %d = ", nb);
	if (id != NULL)
	{
		for (nb = id[0]; nb; nb--, id++)
			printf("%d ", id[1]);
	}
	else printf("TRASH");
	printf("\n");
}
#else
#define	NFA_PrintState(x,y)
#endif

/*
 * when 2 regions start with same prefix, NFA trash state ID won't exist anymore.
 * Actually other NFA state ids will be added to the trash state ID, therefore, we
 * just have to check in which state the desired 'id' is fully included. XXX not clear.
 */
static int NFA_FuzzyMatch(DFA dfa, NFAVector id)
{
	DFAState s;
	for (s = HEAD(dfa->states); s; NEXT(s))
	{
		int i, j;
		NFAVector k = &STATEID(dfa, s->stateID);

		if (s == NULL || k[0] < id[0]) continue;
		for (i = id[0], j = k[0]; i > 0 && j > 0; j --)
			 if (id[i] == k[j]) i --;

		if (i == 0) return s->index;
	}
	return TRASH_STATE;
}

/* check if a state already exists by returning its index */
static int NFA_StateExists(DFA dfa, NFAState * id)
{
	DFAState s;
	int      len = (id[0] + 1) * sizeof *id;
	STRPTR   states = dfa->stateidbuf.buffer;
	for (s = HEAD(dfa->states); s; NEXT(s))
	{
		if (memcmp(id, states + s->stateID, len) == 0)
			return s->index;
	}
	return TRASH_STATE;
}

/* add a new state to the automaton */
static int DFA_AllocState(DFA dfa, NFA nfa, NFAVector id)
{
	DFAState ds = calloc(sizeof *ds, 1);

	if (ds)
	{
		int i, trash = 0;

		/* keep a linked list of the order states have been created */
		if (dfa->last) dfa->last->next = ds;
		dfa->last = ds;
		dfa->nbStates ++;

		/* copy content of id[] into stateidbuf vector */
		i = dfa->stateidbuf.count;
		if (i > 0) i --;
		if (vector_nth(&dfa->stateidbuf, i + id[0] + 1))
		{
			NFAVector dest = vector_nth(&dfa->stateidbuf, i);
			memcpy(dest, id, (id[0] + 1) * SZST);
			id = dest;
		}
		else return -1;

		/* cannot use pointer, because vector can be reallocated */
		ds->stateID = (STRPTR) id - dfa->stateidbuf.buffer;

		/*
		 * Check if the deterministic state is final (accepting state).
		 * This is the case, if there is one non-deterministic final state in the id of DFA state.
		 */
		for (i = id[0]; i > 0; i--)
		{
			NFARel state = vector_nth(&nfa->states, id[i]);

			ds->isFinal = state->isFinal;
			ds->flag    = state->flag;
			trash       = state->trash;
			if (state->isFinal) break;
		}

		/* separate: initial state, final states and normal states */
		ds->index = dfa->states.lh_Head == NULL ? 0 : (ds->isFinal ? ++ dfa->finals : -- dfa->normals);
		NFA_PrintState(id, ds->index);

		/* sort them in increasing ds->index with O(1) complexity */
		if (dfa->lastFinal)
			ListInsert(&dfa->states, &ds->node, &dfa->lastFinal->node);
		else
			ListAddTail(&dfa->states, &ds->node);
		if (ds->isFinal || ds->index == 0) dfa->lastFinal = ds;

		/*
		 * This is the default state to jump to if no transition is defined
		 * for a character. We need to convert NDFA state ID into DFA state
		 * ID, since NDFA ID are meaningless in the DFA set.
		 */
		if (trash > 0)
		{
			id = EPSILON_CLOSURE(nfa, trash);
			int ret = NFA_StateExists(dfa, id);
			if (ret == TRASH_STATE) ret = NFA_FuzzyMatch(dfa, id);
			if (ret != TRASH_STATE) ds->trash = ret;
		}
		return ds->index;
	}
	//*error = REGEXP_FATAL_ERROR;
	return -1;
}

void DFA_Init(DFA dfa)
{
	memset(dfa, 0, sizeof *dfa);
	dfa->normals = TRASH_STATE;
	dfa->stateidbuf.itemsize = SZST;
	dfa->trans.itemsize = sizeof (struct DFATrans_t);
}

/* determinize a non-deterministic automaton */
int NFA_Determinize(NFA nfa, DFA dfa)
{
	DFAState state;
	vector_t sigmavec = {.itemsize = SZST};
	int      i, j;

	if (nfa->group.count > 0)
		return REGEXP_MISSING_END;

	print_ndfa_states(nfa);

	#ifdef DEBUG_DFA
	printf("NFA epsilon closure:\n");
	#endif

	/* compute epsilon closure for each state of NDFA */
	nfa->epsclosure = malloc(sizeof *nfa->epsclosure * nfa->states.count);
	if (nfa->epsclosure)
	{
		for (i = 0, j = nfa->states.count; j > 0; j --, i ++)
		{
			nfa->epsclosure[i] = NFA_EpsilonClosure(nfa, i);
			NFA_PrintState(EPSILON_CLOSURE(nfa, i), i);
		}
	}
	else return False;

	#ifdef DEBUG_DFA
	printf("DFA epsilon closure:\n");
	#endif

	/* initial state of DFA is the epsilon closure of NDFA's init state */
	DFA_AllocState(dfa, nfa, EPSILON_CLOSURE(nfa, 0));

	/* process next state */
	for (state = HEAD(dfa->states); state; state = state->next)
	{
		int Sigma[256], min, max;

		memset(Sigma, 0, sizeof Sigma);
		sigmavec.count = 0;
		min = 256;
		max = 0;

		/*
		 * This is the core of the determinization process: for each state of
		 * the DFA, computes which states can be reach by epsilon closure
		 * in the *NDFA*. Then create states that don't exist yet, or link
		 * to existing states. Process until no more states are added.
		 * Sadly, this is an exponential algorithm.
		 */
		for (i = state->stateID, j = STATEID(dfa, i), i += SZST; j > 0; j --, i += SZST)
		{
			NFARel   s = vector_nth(&nfa->states, STATEID(dfa, i));
			NFATrans t;

			if (s->first < 0) continue;
			for (t = vector_nth(&nfa->trans, s->first); ;
			     t = vector_nth(&nfa->trans, t->next))
			{
				int chr = t->chrtrans;
				if (chr == SUBDFA_TRANSITION)
				{
					NFAVector vec = EPSILON_CLOSURE(nfa, t->state);
					state->flag |= FLG_SUBDFA;
					NFAState idx = NFA_StateExists(dfa, vec);
					if (idx == TRASH_STATE)
						idx = DFA_AllocState(dfa, nfa, vec);
					state->autoTrans = idx;
				}
				else if (chr < EPSILON_TRANSITION)
				{
					NFA_MergeStateId(Sigma + chr, &sigmavec, EPSILON_CLOSURE(nfa, t->state));
					/* limit range of characters to scan */
					if (min > chr) min = chr;
					if (max < chr) max = chr;
				}
				if (t->next == 0) break;
			}
		}

		/*
		 * Now we have the states reached by each character for the current
		 * state. Add missing states, or link to the existing one otherwise.
		 */
		state->transidx = dfa->trans.count * sizeof (struct DFATrans_t);
		DFATrans tr = DFA_TRANS(dfa, state);
		for (i = min; i <= max; i++)
		{
			if (Sigma[i])
			{
				NFAVector id = (APTR) (sigmavec.buffer + Sigma[i] - 1);
				j = NFA_StateExists(dfa, id);
				if (j == TRASH_STATE) j = DFA_AllocState(dfa, nfa, id);
				if (state->count == 0 || tr->chr + tr->count != i || tr->dest != j)
				{
					tr = vector_nth(&dfa->trans, dfa->trans.count);
					tr->count = 1;
					tr->chr = i;
					tr->dest = j;
					state->count ++;
				}
				else tr->count ++;
			}
		}
	}

	/* transfert custom classes from NFA to DFA */
	dfa->classes = nfa->classes;
	memset(&nfa->classes, 0, sizeof nfa->classes);
	vector_free(sigmavec);

	print_dfa_states(dfa);

	return True;
}

/* free ressources allocated for a DFA */
void DFA_Free(DFA dfa)
{
	DFAState state, next;
	vector_free(dfa->classes);
	vector_free(dfa->stateidbuf);
	vector_free(dfa->trans);
	for (state = next = HEAD(dfa->states); state; NEXT(next), free(state), state = next);
}

#define	SIZE_N        (sizeof(NFAState) + 3)
#define	SIZE_K        (sizeof(NFAState) * 2)
#define SIZE_F        (sizeof(NFAState) + 1)

static CFA CFA_Alloc(int states, int transitions, int finals, STRPTR sep)
{
	int len = sep ? (wcslen((wchar_t *)sep)+1) * sizeof (wchar_t) : 0;
	CFA cfa = calloc(sizeof *cfa + states * SIZE_N + transitions * SIZE_K + len + finals * SIZE_F, 1);

	#ifdef DEBUG_SIT
	fprintf(stderr, "CFA size = %d\n", sizeof *cfa + states * SIZE_N + transitions * SIZE_K + len + finals * SIZE_F);
	#endif

	if (cfa)
	{
		cfa->transitions = (APTR) (cfa + 1);
		cfa->valid       = cfa->transitions + transitions;
		cfa->trash       = cfa->valid       + transitions;
		cfa->autoTrans   = cfa->trash       + states;
		cfa->base        = cfa->autoTrans   + finals;
		cfa->hlType      = (DATA8) (cfa->base + states);
		cfa->hlFlag      = cfa->hlType      + states;
		cfa->separators  = cfa->hlFlag      + finals;
		cfa->nbTrans     = transitions;
		cfa->nbState     = states;
		cfa->nbFinal     = finals;

		if (len == 0) cfa->separators = NULL;
		else wcscpy((wchar_t *)cfa->separators, (wchar_t *)sep);
	}
	return cfa;
}

/*
 * DFA tables contains big holes, so we can heavily reduce its size
 * Compression rate is usually near 80/90%, therefore it is useless to apply
 * a normalization algorithm on the DFA.
 */
CFA DFA_Compress(DFA dfa, STRPTR sep)
{
	static uint8_t mask[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
	DFAState state;
	uint8_t  window[256/8];
	CFA      cfa;
	int      n, k, base, diff;

	memset(window, 0, sizeof window);

	/* compute base offset to compress tables */
	for (base = n = 0, state = HEAD(dfa->states); state; NEXT(state), n++)
	{
		DFATrans tr;
		uint8_t  curset[256/8];
		int      offset, i;

		/* convert transition table into array bitflag */
		if (state->count == 0) continue;
		memset(curset, 0, sizeof curset);
		for (tr = DFA_TRANS(dfa, state), diff = state->count; diff; ) {
			for (k = tr->count, i = tr->chr; k > 0; curset[i>>3] |= mask[i&7], k --, i ++);
			tr ++; diff --;
		}

		if (n > 0)
		{
			for (offset = 0; offset < 256; offset ++)
			{
				for (i = 0; i < sizeof window && (curset[i] & window[i]) == 0; i ++);
				if (i < sizeof window)
				{
					/* shift one bit to the left */
					DATA8 p;
					for (p = window; p < EOT(window)-1; p[0] <<= 1, p[0] |= p[1]>>7, p++);
					p[0] <<= 1;
				}
				else break;
			}

			/* potentially serious limitation: number of states is limited to 65535 */
			base += offset;
			if (base > 65535)
			{
				fprintf(stderr, "exceeding 65535 states: ignoring remaining\n");
				return False;
			}
			/* merge <curset> with window */
			for (i = 0; i < sizeof window; window[i] |= curset[i], i ++);
		}
		else memcpy(window, curset, sizeof window);

		state->offset = base;
	}

	base += 256;
	diff = dfa->normals - (dfa->finals + 1);
	for (n = 0, state = HEAD(dfa->states); state; NEXT(state), n++);

	cfa = CFA_Alloc(n, base, dfa->finals+1, sep);
	if (cfa == NULL) return NULL;

	memset(cfa->valid, 0xff, sizeof(NFAState) * base);

	/* fill the transition tables */
	for (n = 0, state = HEAD(dfa->states); state; NEXT(state), n++)
	{
		static struct DFATrans_t dummy = {0};
		DFATrans  tr, prev = &dummy;
		NFAVector trans = cfa->transitions + state->offset,
		          valid = cfa->valid       + state->offset;

		cfa->trash[n]  = state->trash > dfa->finals ? state->trash - diff : state->trash;
		cfa->base[n]   = state->offset;
		cfa->hlType[n] = state->isFinal;
		if (n < cfa->nbFinal)
		{
			cfa->hlFlag[n] = state->flag;
			cfa->autoTrans[n] = state->flag & FLG_SUBDFA ? state->autoTrans - diff : n;
		}

		for (k = state->count, tr = DFA_TRANS(dfa, state); k > 0; k --, prev = tr, tr ++)
		{
			int nextState = tr->dest, step;
			if (nextState > dfa->finals) nextState -= diff;
			for (base = tr->count, step = tr->chr - prev->chr - prev->count, trans += step, valid += step;
			     base > 0; *trans++ = nextState, *valid++ = n, base --);
		}
	}

	for (n = 1, k = dfa->finals; n <= k; n ++)
		cfa->hlType[cfa->autoTrans[n]] = cfa->hlType[n];

	/* transfer custom classes */
	cfa->classes = dfa->classes;
	memset(&dfa->classes, 0, sizeof dfa->classes);

	return cfa;
}

void SYN_Free(CFA cfa)
{
	if (cfa) {
		vector_free(cfa->classes);
		free(cfa);
	}
}

#ifdef DEBUG_DFA
void print_trans(int start, int trans, int tochr, int end, int is_final)
{
	static uint8_t chrCodes[] = "0001020304050607BS09LFVTFFCR0E0F101112131415161718191A1B1C1D1E1F";
	printf("%c %5d ", is_final ? '*' : ' ', start);

	if (trans == EPSILON_TRANSITION)
		printf("EPS ");
	else if (trans == SUBDFA_TRANSITION)
		printf("SUB ");
	else if (tochr != trans)
		printf("%c~%c ", trans, tochr);
	else if (trans < 32)
		printf("%.2s  ", chrCodes + trans * 2);
	else
		printf("%c   ", trans);

	if (end == TRASH_STATE)
		printf("     0");
	else
		printf(" %5d", end);
}

void print_ndfa_states(NFA ndfa)
{
	NFARel   state;
	NFATrans trans;
	NFAState nb, j;

	printf("NDFA: %d states ==============\n", ndfa->states.count);

	for (nb = ndfa->states.count, j = 0; nb; nb--, j++)
	{
		state = vector_nth(&ndfa->states, j);

		if (state->first >= 0)
		{
			for (trans = vector_nth(&ndfa->trans, state->first); ;
				 trans = vector_nth(&ndfa->trans, trans->next))
			{
				print_trans(j, trans->chrtrans, trans->chrtrans, trans->state, state->isFinal);

				printf("   %d\n", state->trash);
				if (trans->next == 0) break;
			}
		}
		else
		{
			print_trans(j, '~', '~', state->trash, state->isFinal);
			printf("\n");
		}
	}
}

void print_dfa_states(DFA dfa)
{
	DFAState state;
	int      i, j, nb;

	for (state = HEAD(dfa->states), i = 0; state; NEXT(state), i++);

	printf("DFA: %d states -------------\n", i);

	for (state = HEAD(dfa->states), i = 0; state; NEXT(state), i++)
	{
		DFATrans tr = DFA_TRANS(dfa, state);

		for (j = state->count, nb = 0; j > 0; j --, tr ++)
		{
			print_trans(state->index, tr->chr, tr->chr + tr->count - 1, tr->dest, state->isFinal);
			printf("-> %d\n", state->trash); nb ++;
		}
		if (nb == 0)
		{
			print_trans(state->index, '~', '~', state->trash, state->isFinal);
			printf("=> %d  0x%02x\n", state->trash, state->flag);
		}
		if (state->flag & FLG_SUBDFA) printf("auto switch to %d\n", state->autoTrans);
	}
}
#endif
