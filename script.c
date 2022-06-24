/*
 * script.c : quick and dirty scripting language to extend calculator capabilities,
 *            also handle user interface of "PROG" tab.
 *
 * written by T.Pierron, june 2022
 */


#include <stdio.h>
#include "SIT.h"
#include "config.h"
#include "script.h"
#include "Lexer.c"
#include "extra.h"

struct
{
	SIT_Widget  progList, statPos;
	SIT_Widget  progEdit, statSize;
	SIT_Widget  editName;
	ConfigChunk curEdit;
	ListHead    programs;
	Bool        curProgChanged;
	int         cancelEdit;

}	script;

/* colormap used by lexer for script editor */
static uint8_t colorMap[] = {
	10,
	0x00, 0x00, 0x00, 0x00,      0x00, 0x00, 0x00, 0x00,    0x00,    /* normal text */
	0x14, 0x6a, 0x31, 0xff,      0x00, 0x00, 0x00, 0x00,    0x01,    /* type */
	0x30, 0x60, 0x30, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* identifier */
	0x6d, 0x1a, 0x1a, 0xff,      0x00, 0x00, 0x00, 0x00,    0x01,    /* keywords */
	0x22, 0x22, 0xaa, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* comments */
	0x00, 0x69, 0xac, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* directive */
	0xc1, 0x6a, 0x53, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* constants */
	0xee, 0x00, 0x67, 0xff,      0x00, 0x00, 0x00, 0x00,    0x00,    /* special */
	0xff, 0xff, 0xff, 0xff,      0xff, 0x33, 0x33, 0xff,    0x00,    /* errors */
	0x00, 0x00, 0x00, 0xff,      0xff, 0xea, 0x4d, 0xff,    0x00,    /* notes */
};

static void scriptSaveChanges(ConfigChunk chunk)
{
	STRPTR text;
	int    length;
	SIT_GetValues(script.progEdit, SIT_Title, &text, NULL);
	length = strlen(text) + 1;
	memcpy(configAddChunk(chunk->name, length), text, length);
}

/* list selection changed: show program content */
static int scriptSelectProgram(SIT_Widget w, APTR cd, APTR ud)
{
	ConfigChunk chunk = cd;
	ConfigChunk old   = script.curEdit;
	int index;

	SIT_GetValues(script.progList, SIT_SelectedIndex, &index, NULL);

	if (old != chunk)
	{
		if (old && (script.curProgChanged || old->changed))
			scriptSaveChanges(old);

		script.curEdit = chunk;
		script.curProgChanged = 0;
		SIT_SetValues(script.progEdit, SIT_Title, chunk ? chunk->content : (DATA8) "", SIT_ReadOnly, cd == NULL, NULL);
	}

	return 1;
}

/* SITE_OnChange callback on progedit: show stat of file edited */
static int scriptEditStat(SIT_Widget w, APTR cd, APTR ud)
{
	DATA8 text;
	TEXT  buffer[32];
	int * stat = cd;

	SIT_GetValues(w, SIT_Title, &text, NULL);

	if (stat[7] > 0)
	{
		/* highlight bracket */
		static char brackets[] = "([{)]}";
		DATA8  scan = text + stat[6];
		STRPTR sep  = scan[0] ? strchr(brackets, scan[0]) : NULL;
		int    pos  = -1;

		if (sep == NULL && stat[6] > 0)
			/* check one character before */
			sep = strchr(brackets, scan[-1]), scan --;

		if (sep)
		{
			/* find matching bracket */
			DATA8 eof;
			char  chr1 = *sep, chr2;
			int   depth = 0;
			if (sep < brackets + 3)
			{
				for (chr2 = sep[3], eof = text + stat[7]; scan < eof; scan ++)
				{
					if (scan[0] == chr1) depth ++; else
					if (scan[0] == chr2)
					{
						depth --;
						if (depth <= 0) { pos = scan - text; break; }
					}
				}
			}
			else
			{
				for (chr2 = sep[-3], eof = text; scan >= eof; scan --)
				{
					if (scan[0] == chr1) depth ++; else
					if (scan[0] == chr2)
					{
						depth --;
						if (depth <= 0) { pos = scan - text; break; }
					}
				}
			}
		}
		SYN_MatchBracket(w, pos+1);
	}
	else SYN_MatchBracket(w, 0);

	sprintf(buffer, "L:%d C:%d", stat[1]+1, stat[0]);
	SIT_SetValues(script.statPos,  SIT_Title, buffer, NULL);

	if (script.curEdit->changed)
		stat[8] = 1;

	sprintf(buffer, "%dL %dB%c", stat[5], stat[7], stat[8] ? '*' : ' ');
	SIT_SetValues(script.statSize, SIT_Title, buffer, NULL);
	script.curProgChanged = stat[8];

	return 1;
}

/* lost focus on temp edit box */
static int scriptFinishEdit(SIT_Widget w, APTR cd, APTR ud)
{
	if (! script.cancelEdit)
	{
		ConfigChunk chunk;
		STRPTR      name;
		int         index;
		SIT_GetValues(w, SIT_Title, &name, NULL);
		SIT_GetValues(script.progList, SIT_SelectedIndex, &index, NULL);
		SIT_GetValues(script.progList, SIT_RowTag(index), &chunk, NULL);

		CopyString(chunk->name + 1, name, sizeof chunk->name - 1);
		chunk->changed = 1;
		SIT_ListSetCell(script.progList, index, 0, DontChangePtr, DontChange, chunk->name+1);
		script.cancelEdit = 1;
	}
	if (script.editName)
	{
		script.editName = NULL;
		SIT_RemoveWidget(w);
	}
	return 1;
}

static int scriptAcceptEdit(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnKey * msg = cd;
	if (msg->keycode == SITK_Return)
	{
		script.cancelEdit = 0;
		scriptFinishEdit(w, NULL, ud);
		return 1;
	}
	else if (msg->keycode == SITK_Escape)
	{
		/* removing editbox widget will cause an OnBlur event */
		script.cancelEdit = 1;
		script.editName = NULL;
		SIT_RemoveWidget(w);
		return 1;
	}
	return 0;
}

/* double-click on program name in list: edit its name */
static int scriptRename(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget parent;
	float      rect[4];
	int        item;
	SIT_GetValues(w, SIT_SelectedIndex, &item, NULL);

	parent = NULL;
	if (SIT_ListGetItemOver(w, rect, SIT_ListItem(item,0), &parent) >= 0)
	{
		ConfigChunk chunk;
		SIT_GetValues(w, SIT_RowTag(item), &chunk, NULL);
		script.cancelEdit = 0;
		w = script.editName = SIT_CreateWidget("editname#edit", SIT_EDITBOX, parent,
			/* cannot edit chunk->name directly: we want this to be cancellable */
			SIT_Title,      chunk->name + 1,
			SIT_EditLength, 14,
			SIT_X,          (int) rect[0],
			SIT_Y,          (int) rect[1],
			SIT_Width,      (int) (rect[2] - rect[0] - 4),
			SIT_Height,     (int) (rect[3] - rect[1] - 4),
			NULL
		);
		SIT_SetFocus(w);
		SIT_AddCallback(w, SITE_OnBlur,   scriptFinishEdit, NULL);
		SIT_AddCallback(w, SITE_OnRawKey, scriptAcceptEdit, NULL);
	}
	return 1;
}

/* hit enter of lost focus */
Bool scriptCancelRename(void)
{
	if (script.editName)
	{
		script.cancelEdit = 1;
		scriptFinishEdit(script.editName, NULL, NULL);
		return True;
	}
	return False;
}

/* use escape key */
void scriptCommitChanges(void)
{
	if (script.curEdit && (script.curProgChanged || script.curEdit->changed))
		scriptSaveChanges(script.curEdit);
}

/* click on "yes" button */
static int scriptConfirmDel(SIT_Widget w, APTR cd, APTR ud)
{
	ConfigChunk chunk;
	int row = (int) ud, count;
	SIT_GetValues(script.progList, SIT_ItemCount, &count, SIT_RowTag(row), &chunk, NULL);
	configDelChunk(chunk->name);
	SIT_ListDeleteRow(script.progList, row);
	if (row == count - 1) row --;
	if (row >= 0) SIT_SetValues(script.progList, SIT_SelectedIndex, row, NULL);
	SIT_CloseDialog(w);
	return 1;
}

/* SITE_OnActivate on delete button */
static int scriptDel(SIT_Widget w, APTR cd, APTR ud)
{
	int index;
	SIT_GetValues(script.progList, SIT_SelectedIndex, &index, NULL);
	if (index >= 0)
	{
		ConfigChunk chunk;
		SIT_GetValues(script.progList, SIT_RowTag(index), &chunk, NULL);
		if (chunk->content[0] == 0)
		{
			/* empty program, delete without asking */
			scriptConfirmDel(NULL, NULL, (APTR) index);
		}
		else /* ask before */
		{
			extern SIT_Accel defAccels;

			SIT_Widget diag = SIT_CreateWidget("helpdlg.bg", SIT_DIALOG, w,
				SIT_DialogStyles, SITV_Plain | SITV_Transcient | SITV_Modal,
				SIT_AccelTable,   defAccels,
				NULL
			);

			SIT_CreateWidgets(diag,
				"<label name=info.hdr title=", "Are you sure you want to delete that program?", ">"
				"<button name=no title=No right=FORM top=WIDGET,info,0.5em buttonType=", SITV_CancelButton, ">"
				"<button name=ok.danger title=Yes right=WIDGET,no,0.5em top=OPPOSITE,no buttonType=", SITV_DefaultButton, ">"
			);

			SIT_AddCallback(SIT_GetById(diag, "ok"), SITE_OnActivate, scriptConfirmDel, (APTR) index);

			SIT_ManageWidget(diag);
		}
	}
	return 1;
}

/* SITE_OnActivate on add button */
static int scriptAdd(SIT_Widget w, APTR cd, APTR ud)
{
	TEXT name[16];
	int  id = 0, count, i;

	SIT_GetValues(script.progList, SIT_ItemCount, &count, NULL);

	/* create a generic name */
	next: for (;;)
	{
		sprintf(name, "$PROG%d", id); id ++;
		for (i = 0; i < count; i ++)
		{
			if (strcasecmp(SIT_ListGetCellText(script.progList, 0, i), name+1) == 0)
				goto next;
		}
		break;
	}

	configAddChunk(name, 1);
	ConfigChunk chunk = TAIL(config->chunks);

	id = SIT_ListInsertItem(script.progList, -1, chunk, name + 1);
	SIT_SetValues(script.progList, SIT_SelectedIndex, id, NULL);

	return 1;
}

/* user clicked on "PROG" tab */
void scriptShow(SIT_Widget app)
{
	if (! script.progList)
	{
		/* init on the fly: no need to alloc anything if user never comes here */
		script.progList = SIT_GetById(app, "proglist");
		script.progEdit = SIT_GetById(app, "progedit");
		script.statPos  = SIT_GetById(app, "posval");
		script.statSize = SIT_GetById(app, "sizeval");

		SIT_AddCallback(script.progList, SITE_OnChange, scriptSelectProgram, NULL);
		SIT_AddCallback(script.progList, SITE_OnActivate, scriptRename, NULL);
		SIT_AddCallback(script.progEdit, SITE_OnChange, scriptEditStat, NULL);
		SIT_AddCallback(SIT_GetById(app, "addprog"), SITE_OnActivate, scriptAdd, NULL);
		SIT_AddCallback(SIT_GetById(app, "delprog"), SITE_OnActivate, scriptDel, NULL);

		/* activate lexer on multi-line editor: a bit overkill for something so simple, but hey, it does the job */
		CFA lexer = NULL;
		SYN_Parse("resources/script.syntax", NULL, &lexer);
		SIT_SetValues(script.progEdit,
			SIT_Lexer,     SYN_HighlightText,
			SIT_LexerData, lexer,
			SIT_ColorMap,  colorMap,
			NULL
		);

		/* fill list of programs from config */
		ConfigChunk chunk;
		int count;
		for (chunk = HEAD(config->chunks), count = 0; chunk; NEXT(chunk))
		{
			if (chunk->name[0] == '$')
			{
				count ++;
				SIT_ListInsertItem(script.progList, -1, chunk, chunk->name + 1);
			}
		}
		if (count == 0)
		{
			/* no program defined yet: create one on the fly */
			configAddChunk("$PROG0", 128);
			chunk = TAIL(config->chunks);

			strcpy(chunk->content, "# CLICK \"HELP\" FOR SYNTAX\n\nPRINT \"Hello, world !\"\n");

			SIT_ListInsertItem(script.progList, -1, chunk, chunk->name + 1);
		}
		SIT_SetValues(script.progList, SIT_SelectedIndex, 0, NULL);
	}
	SIT_SetFocus(script.progEdit);
}

/*
 * bytecode generation and execution below
 */

typedef enum
{
	STOKEN_SPACES,
	STOKEN_IF,
	STOKEN_THEN,
	STOKEN_ELSE,
	STOKEN_END,
	STOKEN_ELSEIF,
	STOKEN_WHILE,
	STOKEN_DO,
	STOKEN_BREAK,
	STOKEN_CONTINUE,
	STOKEN_GOTO,
	STOKEN_RETURN,
	STOKEN_EXIT,
	STOKEN_PRINT,
	STOKEN_REDIM,
	STOKEN_PUSH,
	STOKEN_POP,
	STOKEN_SHIFT,
	STOKEN_UNSHIFT,

	STOKEN_EXPR,
	STOKEN_IMMEXPR,
	STOKEN_ANY,
	STOKEN_LABEL,
	STOKEN_VAR,
}	STOKEN;

static uint8_t tokenSize[] = {
	[STOKEN_IF]     = 3, [STOKEN_ELSEIF]   = 3, [STOKEN_WHILE] = 3,
	[STOKEN_BREAK]  = 3, [STOKEN_CONTINUE] = 3, [STOKEN_GOTO]  = 3,
	[STOKEN_RETURN] = 1, [STOKEN_PRINT]    = 1, [STOKEN_POP]   = 1,
	[STOKEN_REDIM]  = 1, [STOKEN_UNSHIFT]  = 1, [STOKEN_SHIFT] = 1,
	[STOKEN_PUSH]   = 1, [STOKEN_ELSE]     = 3, [STOKEN_EXIT]  = 1
};

enum /* grammar action */
{
	NOTHING = 0,
	PUSH    = 1,    /* push a new state on the stack */
	POP     = 2,    /* pop last state, restore parse index */
	ACCEPT  = 4,    /* fallback state is next state, don't push a new state though */

	SETDEF  = 253,  /* set default state to next matching grammar */
	ERROR   = 254,  /* stop parsing and return "syntax error" */
	RESTART = 255,  /* restart state from beginning of current stack */
};

/*
 * This is the definition of the language grammar: it is a simple finite-state stack-automaton.
 * It is composed of quadruplet of: token, action if match, jump to state if match, jump to state if fail
 */
static uint8_t scriptGrammar[] = {
	/* if then elseif else end */
	STOKEN_IF,PUSH,1,14,    STOKEN_IMMEXPR,0,1,ERROR,   STOKEN_THEN,SETDEF,1,ERROR,

		STOKEN_ANY,RESTART,0,1,

		STOKEN_ELSEIF,0,1,6,   STOKEN_IMMEXPR,0,1,ERROR,   STOKEN_THEN,ACCEPT,1,ERROR,

			STOKEN_ANY,RESTART,0,1,
			STOKEN_ELSEIF,0,255-3,1,
			STOKEN_ELSE,ACCEPT,2,ERROR,

		STOKEN_ELSE,ACCEPT,1,3,

			STOKEN_ANY,RESTART,0,1,
			STOKEN_END,POP,0,ERROR,

		STOKEN_END,POP,0,ERROR,

	/* while do end */
	STOKEN_WHILE,PUSH,1,5,   STOKEN_IMMEXPR,0,1,ERROR,   STOKEN_DO,SETDEF,1,ERROR,

		STOKEN_ANY,0,0,1,
		STOKEN_END,POP,0,ERROR,


	/* built-in instructions */
	STOKEN_CONTINUE,RESTART,0,1,
	STOKEN_BREAK,RESTART,0,1,
	STOKEN_GOTO,RESTART,0,1,
	STOKEN_EXIT,RESTART,0,1,

	STOKEN_PRINT,0,1,2,   STOKEN_IMMEXPR,RESTART,0,ERROR,
	STOKEN_RETURN,0,1,2,  STOKEN_IMMEXPR,RESTART,0,ERROR,
	STOKEN_POP,0,1,2,     STOKEN_VAR,RESTART,0,ERROR,
	STOKEN_SHIFT,0,1,2,   STOKEN_VAR,RESTART,0,ERROR,
	STOKEN_PUSH,0,1,3,    STOKEN_VAR,0,1,ERROR,    STOKEN_IMMEXPR,RESTART,0,ERROR,
	STOKEN_REDIM,0,1,3,   STOKEN_VAR,0,1,ERROR,    STOKEN_IMMEXPR,RESTART,0,ERROR,
	STOKEN_UNSHIFT,0,1,3, STOKEN_VAR,0,1,ERROR,    STOKEN_IMMEXPR,RESTART,0,ERROR,

	/* or simply some expressions to evaluate */
	STOKEN_EXPR,RESTART,0,ERROR
};


int IsKeyword(DATA8 * start)
{
	/* not the most elegant solution, but a good compromise between speed and amount of code */
	#define MATCH(str, token)    \
	if (strncasecmp(mem+1, str, sizeof (str)-1) == 0 && (isspace(mem[sizeof str]) || mem[sizeof str] == 0)) \
		{ *start = mem + sizeof (str); return token; }

	DATA8 mem = *start;
	switch (toupper(mem[0])) {
	case 'B': MATCH("REAK",     STOKEN_BREAK); break;
	case 'C': MATCH("CONTINUE", STOKEN_CONTINUE); break;
	case 'D': MATCH("O",        STOKEN_DO); break;
	case 'E': MATCH("LSEIF",    STOKEN_ELSEIF);
	          MATCH("LSE",      STOKEN_ELSE);
	          MATCH("XIT",      STOKEN_EXIT);
	          MATCH("ND",       STOKEN_END); break;
	case 'G': MATCH("OTO",      STOKEN_GOTO); break;
	case 'I': MATCH("F",        STOKEN_IF); break;
	case 'P': MATCH("RINT",     STOKEN_PRINT);
	          MATCH("OP",       STOKEN_POP);
	          MATCH("USH",      STOKEN_PUSH); break;
	case 'R': MATCH("ETURN",    STOKEN_RETURN);
	          MATCH("EDIM",     STOKEN_REDIM); break;
	case 'S': MATCH("HIFT",     STOKEN_SHIFT); break;
	case 'T': MATCH("HEN",      STOKEN_THEN); break;
	case 'U': MATCH("NSHIFT",   STOKEN_UNSHIFT); break;
	case 'W': MATCH("HILE",     STOKEN_WHILE);
	}
	return 0;
}

/* goto implementation */
static ProgLabel scriptGetLabel(ProgByteCode prog, DATA8 name, int len, Bool create)
{
	ProgLabel label;
	/* check if label already exists */
	for (label = HEAD(prog->labels); label; NEXT(label))
		if (strncasecmp(label->name, name, len) == 0 && label->name[len] == 0) break;

	/* if creating a label, there must be only one with the same name */
	if (label && create)
		return NULL;

	if (label == NULL)
	{
		label = malloc(sizeof *label + len);
		CopyString(label->name, name, len + 1);
		label->jumpTo = -1; /* not known yet */
		label->writeTo = 0xffff;
		ListAddTail(&prog->labels, &label->node);
	}

	if (create)
	{
		while (label->writeTo < 0xffff)
		{
			/* now that jump location is known, write address */
			DATA8 mem = prog->bc.code + label->writeTo;
			label->writeTo = (mem[1] << 8) | mem[2];
			mem[1] = prog->bc.size >> 8;
			mem[2] = prog->bc.size & 255;
		}
		label->jumpTo = prog->bc.size;
	}
	else
	{
		/* write address dest if known */
		uint16_t address = label->jumpTo < 0 ? label->writeTo : label->jumpTo;
		DATA8 mem = prog->bc.code + prog->bc.size - 3;
		mem[1] = address >> 8;
		mem[2] = address & 0xff;
		if (label->jumpTo < 0)
		{
			/* need to overwrite it later */
			label->writeTo = prog->bc.size - 3;
		}
	}

	return label;
}

/* get the innermost loop */
static ProgState scriptGetWhile(ProgByteCode prog, ListHead * states)
{
	ProgState state;

	for (state = TAIL(*states); state && (state->jumpIfFalse < 0 || prog->bc.code[state->jumpIfFalse] != STOKEN_WHILE);
		PREV(state));

	return state;
}

/* lexical analyzer for the language */
static int scriptFindToken(ProgByteCode prog, DATA8 * start, int lineEnd)
{
	DATA8 mem;
	int token = IsKeyword(start);

	if (token > 0) return token;

	/* check for goto label */
	for (mem = *start; isalpha(*mem); mem ++);
	if (mem > *start && *mem == ':')
	{
		/* goto target label */
		ProgLabel label = scriptGetLabel(prog, *start, mem - *start, True);

		if (label == NULL)
			return -PERR_DuplicateLabel;

		*start = mem + 1;

		return STOKEN_SPACES;
	}

	/* IF <COND> THEN <INST>\nA++ => A++ must be preceded with an END */
	if (lineEnd > 0 && lineEnd != prog->line)
		return STOKEN_END;

	/* can only be an expression at this point or an error */
	mem = ByteCodeAdd(&prog->bc, 1);
	mem[0] = STOKEN_EXPR;
	int error = ParseExpression(*start, ByteCodeGenExpr, &prog->bc);

	if (error == 0)
	{
		/* end of expr marker */
		mem = ByteCodeAdd(&prog->bc, 1);
		mem[0] = 255;
		*start = prog->bc.exp;
		return STOKEN_EXPR;
	}

	return -error;
}

/* main function to convert string into bytecode */
static void scriptToByteCode(ProgByteCode prog, DATA8 source)
{
	ProgState state;
	ListHead  states;
	DATA8     mem, prev;

	state = alloca(sizeof *state);
	memset(state, 0, sizeof *state);
	state->jumpIfFalse = -2;
	ListNew(&states);
	ListAddTail(&states, &state->node);

	prog->line = 1;
	for (mem = prev = source; mem[0]; )
	{
		STOKEN token;
		DATA8  inst, grammar;
		int    progCounter;

		if (mem[0] == ';')
			/* instruction separator */
			mem ++;

		while (prev < mem)
		{
			/* keep track of which line we are (for export reporting) */
			if (prev[0] == '\n') prog->line ++;
			prev ++;
		}
		while (isspace(mem[0]))
		{
			if (*mem == '\n') prog->line ++;
			mem ++;
			prev = mem;
		}

		if (mem[0] == '#')
		{
			/* comment: ignore up to end of line */
			for (mem ++; *mem != '\n'; mem ++)
				if (*mem == 0) return;
			mem ++;
			continue;
		}

		token = scriptFindToken(prog, &mem, state->pendingEnd ? state->line : 0);

		if (token == STOKEN_EXPR)
		{
			DATA8 p;
			/* check if it is an IMMEXPR instead: expression must be on same line than a previous token */
			for (p = prev - 1; p >= source && *p != '\n' && isspace(*p); p --);
			if (p >= source && *p != '\n')
				/* this will spare you from writing "END" token */
				token = STOKEN_IMMEXPR;
		}
		if (token == STOKEN_SPACES)
			continue;

		if (token < 0)
		{
			prog->errCode = - token;
			return;
		}

		/* now we can roll our state machine for grammar analysis */
		grammar = scriptGrammar + state->grammar;
		/* <grammar> points to quadruplet of: token, action if match, jump to state if match, jump to state if fail */
		while (grammar[0] != token)
		{
			if (grammar[0] == STOKEN_ANY)
			{
				/* check for all "top level" tokens */
				DATA8 any;
				for (any = scriptGrammar; any[0] != token; any += any[3] << 2)
				{
					if (any[0] == STOKEN_EXPR && token == STOKEN_IMMEXPR)
					{
						/* almost the same thing */
						token = STOKEN_EXPR;
						break;
					}
					if (any[3] == ERROR)
						goto not_found;
				}
				grammar = any;
				break;
			}
			not_found:
			if (grammar[0] == STOKEN_EXPR && token == STOKEN_IMMEXPR)
			{
				/* almost the same thing */
				token = STOKEN_EXPR;
				break;
			}
			if (grammar[3] == ERROR)
			{
				prog->errCode = PERR_SyntaxError;
				return;
			}
			grammar += grammar[3] << 2;
		}
		if ((state->jumpIfFalse >= 0 || state->jumpAtEnd > 0) && prog->line == state->line && grammar[3] != ERROR)
		{
			/* IF <COND> THEN <EXPR>: add an END if an error is generated */
			state->pendingEnd = 1;
		}

		if (state->pendingEnd && grammar[1] == PUSH)
		{
			/* another edge case for automatic END insertion */
			token = STOKEN_END;
			mem = prev;
			grammar = scriptGrammar + (13 * 4);
		}

		progCounter = prog->bc.size;

		/* alloc byte for instruction */
		if (token < DIM(tokenSize) && tokenSize[token] > 0)
		{
			inst = ByteCodeAdd(&prog->bc, tokenSize[token]);
			inst[0] = token;
			if (tokenSize[token] > 1)
			{
				/* address to jump to, if condition fails, but we don't know it yet */
				inst[1] = inst[2] = 0;
			}
		}

		/* this part is the semantic analyzer */
		switch (token) {
		case STOKEN_ELSE:
		case STOKEN_ELSEIF:
			/* need another jump to skip the following block */
			inst[0] = STOKEN_GOTO;
			if (state->jumpAtEnd > 0)
			{
				/* previous jump: use a linked list */
				inst[1] = state->jumpAtEnd >> 8;
				inst[2] = state->jumpAtEnd & 255;
			}
			else inst[1] = inst[2] = 0;
			state->jumpAtEnd = progCounter;
			state->line = prog->line;
			progCounter += 3;
			if (state->jumpIfFalse >= 0)
			{
				inst = prog->bc.code + state->jumpIfFalse;
				inst[1] = prog->bc.size >> 8;
				inst[2] = prog->bc.size & 0xff;
			}
			state->jumpIfFalse = (token == STOKEN_ELSEIF ? progCounter : -1);
			state->pendingEnd = 0;

			/* this is the proper elseif */
			if (token == STOKEN_ELSEIF)
			{
				inst = ByteCodeAdd(&prog->bc, 3);
				inst[0] = STOKEN_IF;
				inst[1] = inst[2] = 0;
			}
			break;

		case STOKEN_BREAK:
		case STOKEN_CONTINUE:
			/* can be several states below the one we are in */
			state = scriptGetWhile(prog, &states);
			if (state == NULL)
			{
				prog->errCode = PERR_NotInsideLoop;
				return;
			}

			/* jump to end of loop, but address in not known yet (for "break") */
			inst = ByteCodeAdd(&prog->bc, 3);
			inst[0] = STOKEN_GOTO;
			if (token == STOKEN_BREAK)
			{
				if (state->jumpAtEnd > 0)
				{
					/* previous jump: use a linked list */
					inst[1] = state->jumpAtEnd >> 8;
					inst[2] = state->jumpAtEnd & 255;
				}
				else inst[1] = inst[2] = 0;
				state->jumpAtEnd = progCounter;
			}
			else /* "continue": address is known */
			{
				inst[1] = state->jumpIfFalse >> 8;
				inst[2] = state->jumpIfFalse & 255;
			}
			state = TAIL(states);
			break;

		case STOKEN_GOTO:
			/* manually parse label XXX could be better */
			while (isspace(*mem)) mem ++; inst = mem;
			while (isalpha(*mem)) mem ++;
			if (! scriptGetLabel(prog, inst, mem - inst, False))
			{
				prog->errCode = PERR_MissingLabel;
				return;
			}

		default: break;
		}

		state->grammar = grammar - scriptGrammar;
		switch (grammar[1]) {
		case PUSH:
			if (! state->node.ln_Next)
			{
				state = alloca(sizeof *state);
				memset(state, 0, sizeof *state);
				ListAddTail(&states, &state->node);
			}
			else
			{
				NEXT(state);
				state->pendingEnd = 0;
				state->defState = 0;
			}
			/* we don't know the address yet, need to keep a linked list of these */
			state->jumpIfFalse = progCounter;
			state->jumpAtEnd = 0;
			state->grammar = state->defState = grammar - scriptGrammar;
			state->line = prog->line;
			break;
		case RESTART:
			state->grammar = state->defState;
			break;
		case POP:
			while (state->jumpAtEnd > 0)
			{
				/* now we can overwrite all the address with the real value */
				inst = prog->bc.code + state->jumpAtEnd;
				state->jumpAtEnd = (inst[1] << 8) | inst[2];
				inst[1] = progCounter >> 8;
				inst[2] = progCounter & 0xff;
			}
			if (state->jumpIfFalse < -1)
			{
				/* "END" token without IF or WHILE */
				prog->errCode = PERR_InvalidOperation;
				return;
			}
			else if (state->jumpIfFalse >= 0)
			{
				inst = prog->bc.code + state->jumpIfFalse;
				if (inst[0] == STOKEN_WHILE)
				{
					/* loop back to start of loop */
					inst[0] = STOKEN_IF;
					inst = ByteCodeAdd(&prog->bc, 3);
					inst[0] = STOKEN_GOTO;
					inst[1] = state->jumpIfFalse >> 8;
					inst[2] = state->jumpIfFalse & 0xff;
					inst = prog->bc.code + state->jumpIfFalse;
				}
				/* skip whole branch if condition is false */
				inst[1] = prog->bc.size >> 8;
				inst[2] = prog->bc.size & 0xff;
			}
			if (state->node.ln_Prev)
				PREV(state);
			else
				state->jumpIfFalse = -2;
			state->grammar = state->defState;
			continue;

		case ACCEPT:
			state->pendingEnd = 0;
			if (state->jumpIfFalse >= 0)
			{
				inst = prog->bc.code + state->jumpIfFalse;
				inst[1] = prog->bc.size >> 8;
				inst[2] = prog->bc.size & 0xff;
			}
			// no break;
		case SETDEF:
			state->defState = state->grammar + (grammar[2] << 2);
		}
		if (grammar[2] > 240)
			state->grammar -= (255 - grammar[2]) << 2;
		else
			state->grammar += grammar[2] << 2;
	}

	/* free stuff that are not needed anymore */
	while (prog->labels.lh_Head)
	{
		ProgLabel label = (ProgLabel) ListRemHead(&prog->labels);

		if (label->jumpTo < 0)
			prog->errCode = PERR_MissingLabel;

		free(label);
	}

	if (prog->errCode == 0 && (state->node.ln_Prev || state->jumpIfFalse >= 0))
		prog->errCode = PERR_MissingEnd;
}

/* high-level function to transform program string into bytecode */
ProgByteCode scriptGenByteCode(STRPTR prog)
{
	ConfigChunk chunk;
	ProgByteCode list;
	for (list = HEAD(script.programs); list; NEXT(list))
		if (strcasecmp(list->name, prog) == 0) return list;

	/* not yet compiled: do it now */
	for (chunk = HEAD(config->chunks); chunk; NEXT(chunk))
	{
		if (chunk->name[0] == '$' && strcasecmp(chunk->name + 1, prog) == 0)
		{
			list = calloc(sizeof *list, 1);
			ListAddHead(&script.programs, &list->node);
			CopyString(list->name, prog, sizeof list->name);
			scriptToByteCode(list, chunk->content);
			return list;
		}
	}
	return NULL;
}

/* no need to bloat this file */
#ifdef KALC_DEBUG
#include "scripttest.h"
#endif
