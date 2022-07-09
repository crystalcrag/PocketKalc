/*
 * ui.c : user-interface creation/management for PocketKalc using SITGL/SDL1.
 *
 * written by T.Pierron, may 2022.
 */

#include <stdio.h>
#include <malloc.h>
#include <SDL/SDL.h>
#include <GL/GL.h>
#include "nanovg.h"
#include <math.h>
#include "SIT.h"
#include "parse.h"
#include "graph.h"
#include "config.h"
#include "script.h"
#include "extra.h"
#include "ui.h"

static int SDLKtoSIT[] = {
	SDLK_HOME,      SITK_Home,
	SDLK_END,       SITK_End,
	SDLK_PAGEUP,    SITK_PrevPage,
	SDLK_PAGEDOWN,  SITK_NextPage,
	SDLK_UP,        SITK_Up,
	SDLK_DOWN,      SITK_Down,
	SDLK_LEFT,      SITK_Left,
	SDLK_RIGHT,     SITK_Right,
	SDLK_LSHIFT,    SITK_LShift,
	SDLK_RSHIFT,    SITK_RShift,
	SDLK_LAST,      SITK_LAlt,
	SDLK_RALT,      SITK_RAlt,
	SDLK_LCTRL,     SITK_LCtrl,
	SDLK_RCTRL,     SITK_RCtrl,
	SDLK_LSUPER,    SITK_LCommand,
	SDLK_RSUPER,    SITK_RCommand,
	SDLK_MENU,      SITK_AppCommand,
	SDLK_RETURN,    SITK_Return,
	SDLK_CAPSLOCK,  SITK_Caps,
	SDLK_INSERT,    SITK_Insert,
	SDLK_DELETE,    SITK_Delete,
	SDLK_NUMLOCK,   SITK_NumLock,
	SDLK_PRINT,     SITK_Impr,
	SDLK_F1,        SITK_F1,
	SDLK_F2,        SITK_F2,
	SDLK_F3,        SITK_F3,
	SDLK_F4,        SITK_F4,
	SDLK_F5,        SITK_F5,
	SDLK_F6,        SITK_F6,
	SDLK_F7,        SITK_F7,
	SDLK_F8,        SITK_F8,
	SDLK_F9,        SITK_F9,
	SDLK_F10,       SITK_F10,
	SDLK_F11,       SITK_F11,
	SDLK_F12,       SITK_F12,
	SDLK_F13,       SITK_F13,
	SDLK_F14,       SITK_F14,
	SDLK_F15,       SITK_F15,
	SDLK_BACKSPACE, SITK_BackSpace,
	SDLK_ESCAPE,    SITK_Escape,
	SDLK_SPACE,     SITK_Space,
	SDLK_HELP,      SITK_Help,
};

static int SDLMtoSIT(int mod)
{
	int ret = 0;
	if (mod & KMOD_CTRL)  ret |= SITK_FlagCtrl;
	if (mod & KMOD_SHIFT) ret |= SITK_FlagShift;
	if (mod & KMOD_ALT)   ret |= SITK_FlagAlt;
	return ret;
}

static struct
{
	SIT_Widget app, units, formats[4];
	SIT_Widget list, edit, calc, draw;
	SIT_Widget expr, graph, prog, light;
	ListHead   rowTags;
	int        insertAt;
}	ctrls;

struct SIT_Accel_t defAccels[] = {
	{SITK_FlagCapture + SITK_Escape, SITE_OnClose},
	{0}
};

/* batch allocator for items of same size */
static int firstFree(uint32_t * usage, int count)
{
	static uint8_t multiplyDeBruijnBitPosition[] = {
		0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
		31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
	};
	int base, i;
	for (i = count, base = 0; i > 0; i --, usage ++, base += 32)
	{
		uint32_t bits = *usage ^ 0xffffffff;
		if (bits == 0) continue;
		/* count leading 0 */
		bits = multiplyDeBruijnBitPosition[((uint32_t)((bits & -(signed)bits) * 0x077CB531U)) >> 27];
		*usage |= 1 << bits;
		return base + bits;
	}
	return -1;
}

static RowTag allocRowTag(void)
{
	BatchResult batch;

	for (batch = HEAD(ctrls.rowTags); batch && batch->alloc == -1; NEXT(batch));

	if (batch == NULL)
	{
		batch = malloc(sizeof *batch);
		batch->alloc = 0;
		ListAddHead(&ctrls.rowTags, &batch->node);
	}

	return batch->results + firstFree(&batch->alloc, 1);
}

static void freeRowTag(RowTag tag)
{
	BatchResult batch;
	for (batch = HEAD(ctrls.rowTags); batch && batch->alloc != -1; NEXT(batch))
	{
		if (batch->results <= tag && tag < batch->results + 32)
		{
			int slot = tag - batch->results;
			batch->alloc ^= 1 << slot;
			break;
		}
	}
}

/* select a line in listbox: copy text to edit box */
static int copyLine(SIT_Widget w, APTR cd, APTR ud)
{
	STRPTR text = NULL;
	int    row = -1;
	SIT_GetValues(w, SIT_SelectedIndex, &row, NULL);
	if (row >= 0)
	{
		RowTag tag;
		SIT_GetValues(w, SIT_RowTag(row), &tag, NULL);
		if (tag == TAG_STDOUT)
		{
			text = SIT_ListGetCellText(ctrls.list, 0, row);
			while (isspace(*text)) text ++;
		}
		else if (tag)
		{
			/* result line */
			text = alloca(128);
			formatResult(&tag->res, NULL, text, 128);
		}
		else /* expression line */
		{
			text = SIT_ListGetCellText(ctrls.list, 0, row);
		}

		SIT_SetFocus(ctrls.edit);
		SIT_SetValues(ctrls.edit, SIT_Title, text, SIT_StartSel, strlen(text), NULL);
		return 1;
	}
	return 0;
}

/* show errors within list in different color (SIT_CellPaint callback) */
static int highlightError(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnCellPaint * paint = cd;
	RowTag tag;
	SIT_GetValues(w, SIT_RowTag(paint->rowColumn>>8), &tag, NULL);
	if (tag && tag != TAG_STDOUT && tag->res.type == TYPE_ERR)
		memcpy(paint->fgColor, "\xff\x00\x00\xff", 4);

	return 0;
}

/* move cursor of listbox from SIT_Accel */
static int selectLine(int keycode)
{
	int oldLine, line;
	SIT_OnKey key = {.keycode = keycode};

	if (SIT_GetFocus() != ctrls.edit) return 0;

	SIT_GetValues(ctrls.list, SIT_SelectedIndex, &oldLine, NULL);
	SIT_ApplyCallback(ctrls.list, &key, SITE_OnRawKey);
	SIT_GetValues(ctrls.list, SIT_SelectedIndex, &line, NULL);
	return 1;
}

/* format <v> to be displayed in list box */
void formatExprToList(Variant v, STRPTR varName)
{
	RowTag tag = allocRowTag();
	TEXT   buffer[128];

	tag->res = *v;
	tag->var = varName;
	formatResult(&tag->res, varName, buffer, sizeof buffer);

	int item = SIT_ListInsertItem(ctrls.list, ctrls.insertAt, tag, buffer);
	SIT_SetValues(ctrls.list, SIT_MakeVisible, item, NULL);
}

void addOutputToList(STRPTR line)
{
	if (*line == '\t')
	{
		/* convert to spaces to line up with results */
		STRPTR dup = alloca(strlen(line) + 4);
		strcpy(dup, "   ");
		strcat(dup, line + 1);
		line = dup;
	}
	/* ctrl character: convert them to spaces */
	STRPTR cleanup;
	for (cleanup = line; *cleanup; cleanup ++)
		if (*cleanup < 32) *cleanup = 32;

	int item = SIT_ListInsertItem(ctrls.list, ctrls.insertAt, TAG_STDOUT, line);
	SIT_SetValues(ctrls.list, SIT_MakeVisible, item, NULL);
	if (ctrls.insertAt >= 0) ctrls.insertAt ++;
}

/* expression on an existing line has changed: redo operation and update results */
static void redoOperation(STRPTR expr, int startRow)
{
	struct ParseExprData_t data = {.cb = formatExprToList};
	/* delete following result rows */
	ctrls.insertAt = ++ startRow;
	for (;;)
	{
		RowTag tag;
		SIT_GetValues(ctrls.list, SIT_RowTag(startRow), &tag, NULL);
		if (tag)
		{
			if (tag->var && tag->var[0] == '$')
			{
				/* temp var name: keep it assigned to same name */
				data.assignTo = tag->var;
			}
			SIT_ListDeleteRow(ctrls.list, startRow);
		}
		else break;
	}

	/* add new restuls just after */
	evalExpr(expr, &data);
}

/* doesn't have to be precise, worst case scenario is to recompute an expression which will give the same result */
static Bool hasVar(STRPTR expr, STRPTR var)
{
	int len = strlen(var);
	while (expr)
	{
		expr = strstr(expr, var);

		if (expr)
		{
			expr += len;
			if (var[0] == '$')
			{
				/* $%d == temp varname */
				if (! isdigit(expr[0])) return True;
			}
			/* [:alpha:][:alnum]* == regular variable name */
			else if (! isalnum(expr[0]))
			{
				return True;
			}
		}
	}
	return False;
}

/* modify subsequent row that might depend on previous results */
static void propagateResult(RowTag tag, int startRow)
{
	STRPTR check[16];
	int    max, count, i;

	/* you can see this as a very rudimentary spreadsheet */
	SIT_GetValues(ctrls.list, SIT_ItemCount, &count, NULL);
	check[0] = tag->var;
	max = 1;

	next: while (startRow < count)
	{
		RowTag rowtag;
		SIT_GetValues(ctrls.list, SIT_RowTag(startRow), &rowtag, NULL);
		if (rowtag == NULL)
		{
			STRPTR expr = SIT_ListGetCellText(ctrls.list, 0, startRow);
			for (i = 0; i < max; i ++)
			{
				if (! hasVar(expr, check[i])) continue;

				redoOperation(expr, startRow);
				SIT_GetValues(ctrls.list, SIT_ItemCount, &count, NULL);
				for (startRow ++; startRow < count; startRow ++)
				{
					/* add the list of variables modified by this <expr> to the list we need to check for subsequent expression */
					SIT_GetValues(ctrls.list, SIT_RowTag(startRow), &rowtag, NULL);
					/* rowtag == NULL => next expression */
					if (rowtag == NULL) break;
					if (rowtag->var && max < DIM(check))
						check[max++] = rowtag->var;
				}
				goto next;
			}
			/* this line does not depend on previous lines: skip everything */
			for (startRow ++; startRow < count; startRow ++)
			{
				SIT_GetValues(ctrls.list, SIT_RowTag(startRow), &rowtag, NULL);
				if (rowtag == NULL) break;
			}
		}
		else startRow ++;
	}
}

/* evaluate what's in the edit box and add result to the list */
static Bool addExprToList(STRPTR expr)
{
	struct ParseExprData_t data = {.cb = formatExprToList};
	int index;
	SIT_GetValues(ctrls.list, SIT_SelectedIndex, &index, NULL);
	if (index >= 0)
	{
		RowTag tag;
		SIT_GetValues(ctrls.list, SIT_RowTag(index), &tag, NULL);
		if (tag == NULL)
		{
			/* expression selected: modify this line */
			STRPTR old = SIT_ListGetCellText(ctrls.list, 0, index);
			if (strcasecmp(old, expr))
			{
				SIT_ListSetCell(ctrls.list, index, 0, DontChangePtr, DontChange, expr);
				redoOperation(expr, index);

				/* check if there was a meaningful result */
				SIT_GetValues(ctrls.list, SIT_RowTag(index+1), &tag, NULL);
				if (tag && tag != TAG_STDOUT && tag->res.type != TYPE_ERR && tag->var)
				{
					/* we might have to propagate the result to the following rows */
					propagateResult(tag, index + 1);
				}

			}
			/* keep content of edit field */
			return False;
		}
	}
	ctrls.insertAt = -1;
	SIT_SetValues(ctrls.list, SIT_SelectedIndex, -1, NULL);
	SIT_ListInsertItem(ctrls.list, -1, NULL, expr);
	scriptResetStdout();
	evalExpr(expr, &data);
	return True;
}

/* SITK_Delete shortcut */
static void deleteExpr(int index)
{
	RowTag rowTag;
	/* get expression line (rowTag == NULL) */
	while (index > 0)
	{
		SIT_GetValues(ctrls.list, SIT_RowTag(index), &rowTag, NULL);
		if (rowTag == NULL) break;
		index --;
	}

	/* expression row: delete it */
	SIT_ListDeleteRow(ctrls.list, index);

	/* delete result row(s) */
	for (;;)
	{
		SIT_GetValues(ctrls.list, SIT_RowTag(index), &rowTag, NULL);
		if (rowTag)
		{
			freeRowTag(rowTag);
			SIT_ListDeleteRow(ctrls.list, index);
		}
		else break;
	}
}

/* handler for globals shortcuts (SIT_Accel) */
static int redirectKeys(SIT_Widget w, APTR cd, APTR ud)
{
	STRPTR expr;
	int    index;
	switch ((int)ud) {
	case ACTION_BROWSE_PREV:  return selectLine(SITK_Up);
	case ACTION_BROWSE_NEXT:  return selectLine(SITK_Down);
	case ACTION_BROWSE_PPAGE: return selectLine(SITK_PrevPage);
	case ACTION_BROWSE_NPAGE: return selectLine(SITK_NextPage);
	case ACTION_CLEAROREXIT:
		SIT_GetValues(ctrls.edit, SIT_Title, &expr, NULL);
		switch (appcfg.mode) {
		case MODE_EXPR:
			if (IsDef(expr))
			{
				SIT_SetValues(ctrls.list, SIT_SelectedIndex, -1, NULL);
				SIT_SetValues(ctrls.edit, SIT_Title, "", NULL);
			}
			else SIT_Exit(1);
			break;
		case MODE_GRAPH:
			if (IsDef(expr))
				SIT_SetValues(ctrls.edit, SIT_Title, "", NULL);
			else
				SIT_Exit(1);
			break;
		case MODE_PROG:
			if (! scriptCancelRename())
				SIT_Exit(1);
		}
		break;
	case ACTION_DELETE:
		if (appcfg.mode != MODE_EXPR) return 0;
		SIT_GetValues(ctrls.list, SIT_SelectedIndex, &index, NULL);
		if (index >= 0)
		{
			deleteExpr(index);
		}
		break;
	case ACTION_DELALL:
		switch (appcfg.mode) {
		case MODE_EXPR:
			SIT_ListDeleteRow(ctrls.list, DeleteAllRows);
			freeAllVars();
			break;
		case MODE_GRAPH:
			graphReset();
			SIT_ForceRefresh();
			break;
		case MODE_PROG:
			return 0;
		}
		break;
	case ACTION_ACCEPT:
		if (SIT_GetFocus() != ctrls.edit) return 0;
		SIT_GetValues(ctrls.edit, SIT_Title, &expr, NULL);
		if (IsDef(expr))
		{
			switch (appcfg.mode) {
			case MODE_EXPR:
				if (addExprToList(expr))
					SIT_SetValues(ctrls.edit, SIT_Title, "", NULL);
				break;
			case MODE_GRAPH:
				graphSetFunc(expr);
				break;
			case MODE_PROG:
				return 0;
			}
			SIT_ForceRefresh();
		}
	}
	return 1;
}

/* auto/bin/hex/dec format toggle buttons */
static int setFormat(SIT_Widget w, APTR cd, APTR ud)
{
	int count, i;
	SIT_GetValues(ctrls.list, SIT_ItemCount, &count, NULL);

	for (i = 0; i < count; i ++)
	{
		RowTag tag;
		SIT_GetValues(ctrls.list, SIT_RowTag(i), &tag, NULL);
		if (tag)
		{
			TEXT format[128];
			formatResult(&tag->res, tag->var, format, sizeof format);
			SIT_ListSetCell(ctrls.list, i, 0, DontChangePtr, DontChange, format);
		}
	}
	return 1;
}

static int redirect(SIT_Widget w, APTR cd, APTR ud)
{
	redirectKeys(w, NULL, ud);
	return 1;
}

/* HELP link in the bottom right corner */
static int showHelp(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_Widget diag = SIT_CreateWidget("helpdlg", SIT_DIALOG, w,
		SIT_DialogStyles, SITV_Plain | SITV_Transcient | SITV_Modal,
		SIT_AccelTable,   defAccels,
		NULL
	);

	if (appcfg.mode != MODE_PROG)
	{
		/* EXPR and GRAPH quick help */
		static TEXT helpMsg[] =
			"<hdr>Build-in functions:</hdr><br>"
			"- sin(x), cos(x), tan(x): x is in radians.<br>"
			"- asin(x), acos(x), atan(x): reciprocal.<br>"
			"- floor(x), ceil(x), round(x): rounding functions.<br>"
			"- pow(x, y): x to the power of y.<br>"
			"- exp(x), log(x): natural exponential/logarithm.<br>"
			"- sqrt(x): square root.<br>"
			"<br>"
			"<hdr>Build-in constants:</hdr><br>"
			"- pi: %.20g<br>"
			"- e: %.20g<br>"
			"- ln2: %.20g<br>"
			"- time, now: unix epoch of current time.<br>"
			"<br>"
			"<hdr>Shortcuts:</hdr><br>"
			"- F1, F2, F3: select calc/graph/prog.<br>"
			"- F4: check program syntax.<br>"
			"- ALT+1,2,3,4: select display mode.<br>"
			"- Shift+DEL: delete selected line.<br>"
			"- Ctrl+Shift+BS: delete all."
		;

		TEXT formatted[768];
		sprintf(formatted, helpMsg, M_PI, M_E, M_LN2);
		SIT_CreateWidgets(diag,
			"<label name=info title=", formatted, ">"
			"<button name=ok title=Close right=FORM bottom=FORM buttonType=", SITV_CancelButton, ">"
		);
	}
	else /* program quick help */
	{
		static TEXT progHelp1[] =
			"<sec>LOOP:</sec>\n"
			"<hdr>WHILE</hdr> COND <hdr>DO</hdr>\n"
			"  # CODE\n"
			"  <hdr>IF</hdr> COND1 <hdr>THEN continue</hdr>\n"
			"  <hdr>IF</hdr> COND2 <hdr>THEN break</hdr>\n"
			"<hdr>END</hdr>\n"
			"\n"
			"<sec>ARRAYS:</sec>\n"
			"MyArray = <hdr>array</hdr>(len)\n"
			"MyArray<hdr>[0]</hdr> <sec># first item</sec>\n"
			"<hdr>LENGTH</hdr>(MyArray) == len\n"
			"<hdr>REDIM</hdr>(MyArray, len)\n"
			"MyArray = [0,11,123,\"ABC\"]\n"
			"<hdr>PUSH</hdr> MyArray expr\n"
			"<hdr>POP</hdr> MyArray\n"
			"<hdr>SHIFT</hdr> MyArray expr\n"
			"<hdr>UNSHIFT</hdr> MyArray\n"
		;

		static TEXT progHelp2[] =
			"<sec>CONDITIONNAL:</sec>\n"
			"<hdr>IF</hdr> COND1 <hdr>THEN</hdr>\n"
			"  # CODE\n"
			"<hdr>ElseIf</hdr> COND2 <hdr>THEN</hdr>\n"
			"  # CODE\n"
			"<hdr>End</hdr>\n"
			"\n"
			"<sec>JUMP:</sec>\n"
			"LABEL:\n"
			"  # CODE\n"
			"<hdr>GOTO</hdr> LABEL\n"
			"\n"
			"<sec>COMMANDS:</sec>\n"
			"<hdr>PRINT</hdr> expr\n"
			"<hdr>RETURN</hdr> expr\n"
			"<hdr>ARGV</hdr> <sec>(array)</sec>\n"
		;

		SIT_CreateWidgets(diag,
			"<label name=info1 title=", progHelp1, "style='white-space: pre'>"
			"<label name=info2 title=", progHelp2, "style='white-space: pre' left=WIDGET,info1,1em>"
			"<button name=ok title=Close top=WIDGET,info1,0.5em buttonType=", SITV_CancelButton, ">"
		);
	}

	SIT_ManageWidget(diag);

	return 1;
}

/* ctrls.modes toggle buttons */
static int setTab(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_SetValues(SIT_GetById(w, "/tabs"), SIT_TabActive, appcfg.mode, NULL);
	switch (appcfg.mode) {
	case MODE_GRAPH:
		SIT_SetValues(ctrls.edit, SIT_Title, graphGetFunc(), NULL);
		goto case_common;
	case MODE_EXPR:
		if (! copyLine(ctrls.list, NULL, NULL))
			SIT_SetValues(ctrls.edit, SIT_Title, "", NULL);
	case_common:
		SIT_SetFocus(ctrls.edit);
		/* clear selection set by SIT_SetFocus() */
		SIT_SetValues(ctrls.edit, SIT_StartSel, 1000000, NULL);
		break;
	case MODE_PROG:
		scriptShow(ctrls.app);
	}
	return 1;
}

/* SITE_OnActivate on a listbox entry */
static int gotoErrorLine(SIT_Widget w, APTR cd, APTR ud)
{
	RowTag tag;
	int    row;
	SIT_GetValues(w, SIT_SelectedIndex, &row, NULL);
	SIT_GetValues(w, SIT_RowTag(row), &tag, NULL);

	if (tag && tag != TAG_STDOUT && tag->res.type == TYPE_ERR && tag->res.int32 > 31)
	{
		appcfg.mode = MODE_PROG;
		scriptShowProgram(ctrls.app, tag->res.int32 >> 13, (tag->res.int32 >> 5) & 255);
		setTab(w, NULL, NULL);
	}
	return 1;
}


/* if checkbox is checked, "disabled" nearby label */
static int disaLabel(SIT_Widget w, APTR cd, APTR ud)
{
	int checked;
	SIT_GetValues(w, SIT_CheckState, &checked, NULL);
	SIT_SetValues(ud, SIT_Classes, checked ? "dis" : "", NULL);
	if (w == ctrls.light)
	{
		/* replace the entire stylesheet */
		SIT_SetValues(ctrls.app, SIT_StyleSheet, checked ? "resources/light.css" : "resources/dark.css", NULL);
	}
	return 1;
}

static int selectUnit(SIT_Widget w, APTR cd, APTR ud)
{
	Unit unit = ud;
	int  cur  = appcfg.defUnits[unit->cat];

	if (cur != unit->id)
	{
		SIT_SetValues(units[firstUnits[unit->cat] + cur].widget, SIT_Classes, "dis", NULL);
		SIT_SetValues(w, SIT_Classes, "", NULL);
		appcfg.defUnits[unit->cat] = unit->id;
	}
	return 1;
}


/* save selection and show it on the main interface */
static int setDefUnit(SIT_Widget w, APTR cd, APTR ud)
{
	TEXT buffer[32];

	int i, pos;
	for (i = pos = 0, buffer[0] = 0; i < DIM(appcfg.defUnits); i ++)
	{
		if (pos > 0) StrCat(buffer, sizeof buffer, pos, "/");
		pos = StrCat(buffer, sizeof buffer, pos, units[firstUnits[i] + appcfg.defUnits[i]].suffix);
	}

	if (strcmp(appcfg.defUnitNames, buffer))
	{
		strcpy(appcfg.defUnitNames, buffer);
		SIT_SetValues(ctrls.units, SIT_Title, buffer, NULL);
	}

	return 1;
}

/* show dialog to edit default units XXX not super discoverable :-/ */
static int editUnits(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	if (msg->state == SITOM_ButtonPressed && msg->button == SITOM_ButtonLeft)
	{
		SIT_Widget diag = SIT_CreateWidget("defunits", SIT_DIALOG, w,
			SIT_DialogStyles, SITV_Plain | SITV_Transcient | SITV_Modal,
			SIT_AccelTable,   defAccels,
			NULL
		);

		static TEXT SIprefix[] =
			" - u: micro (10e-6)<br>"
			" - m: milli (0.001)<br>"
			" - c: centi (0.01)<br>"
			" - K: kilo  (1,000)<br>";

		SIT_CreateWidgets(diag, "<label name=note#hdr title='Select default units for conversion:'>");

		SIT_Widget top  = SIT_GetById(diag, "note");
		SIT_Widget hdr  = NULL;
		SIT_Widget left = NULL;
		int        last = UNIT_EOF;
		Unit       unit;

		for (unit = units; unit->cat != UNIT_EOF; unit ++)
		{
			static STRPTR category[] = {
				"Dist", "Temp", "Mass", "Angle"
			};
			TEXT name[16];
			TEXT title[16];
			if (unit->cat != last)
			{
				last = unit->cat;
				sprintf(title, "  * %s:", category[unit->cat]);
				sprintf(name, "%s.hdr", category[unit->cat]);
				hdr = left = SIT_CreateWidget(name, SIT_LABEL, diag,
					SIT_Title, title,
					SIT_Top,   SITV_AttachWidget, top, SITV_Em(0.7),
					NULL
				);
			}

			unit->widget = left = SIT_CreateWidget(unit->suffix, SIT_LABEL, diag,
				SIT_Title,   unit->name,
				SIT_Top,     SITV_AttachOpposite, hdr, 0,
				SIT_Left,    SITV_AttachWidget, left, SITV_Em(0.5),
				SIT_Classes, unit->id == appcfg.defUnits[unit->cat] ? NULL : "dis",
				NULL
			);
			SIT_AddCallback(left, SITE_OnClick, selectUnit, unit);

			sprintf(name, "unit%s.dis", unit->suffix);
			sprintf(title, "(%s)", unit->suffix);
			top = SIT_CreateWidget(name, SIT_LABEL, diag,
				SIT_Title, title,
				SIT_Top,   SITV_AttachWidget, left, SITV_Em(0.2),
				SIT_Left,  SITV_AttachMiddle, left, 0,
				NULL
			);
		}

		SIT_CreateWidgets(diag,
			"<label name=note2#hdr title='You can add these SI prefix to units:' top=", SITV_AttachWidget, top, SITV_Em(0.5), ">"
			"<label name=sipref.hdr title=", SIprefix, "top=WIDGET,note2,0.5em>"
			"<button name=ok title=Close right=FORM top=WIDGET,sipref,0.5em buttonType=", SITV_CancelButton, ">"
		);

		SIT_AddCallback(diag, SITE_OnFinalize, setDefUnit, NULL);

		SIT_ManageWidget(diag);
	}
	return 1;
}

/* OnClick on title */
static int about(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnMouse * msg = cd;
	if (msg->state == SITOM_ButtonPressed && msg->button == SITOM_ButtonLeft)
	{
		SIT_Widget diag = SIT_CreateWidget("about", SIT_DIALOG, w,
			SIT_DialogStyles, SITV_Plain | SITV_Transcient | SITV_Modal,
			SIT_AccelTable,   defAccels,
			NULL
		);

		SIT_CreateWidgets(diag,
			"<label name=appname#hdr title='" APPNAME " v" VERSION "' left=", SITV_AttachCenter, ">"
			"<label name=author.hdr title='Written by T.Pierron' top=WIDGET,appname,0.5em left=", SITV_AttachCenter, ">"
			"<label name=tools.hdr title='Build on " PLATFORM " using " COMPILER "<br>" __DATE__ "' top=WIDGET,author,0.5em left=FORM right=FORM>"
			"<label name=license title='Free software under terms of 2-clause BSD<br>No warranty, use at your own risk' top=WIDGET,tools,0.5em left=FORM right=FORM>"
			"<label name=font.hdr title=", "Font: <a href='https://www.dafont.com/led-calculator.font'>LED Calculator</a> by Colonel Sanders", "top=WIDGET,license,0.5em>"
			"<button name=ok title=Ok right=FORM top=WIDGET,font,0.5em buttonType=", SITV_CancelButton, ">"
		);

		SIT_ManageWidget(diag);
	}
	return 1;
}

/*
 * creation of the calculator main user interface
 */
static void createUI(SIT_Widget app)
{
	static struct SIT_Accel_t accels[] = {
		{SITK_FlagCapture + SITK_Up,       -1, ACTION_BROWSE_PREV,  NULL, redirectKeys},
		{SITK_FlagCapture + SITK_Down,     -1, ACTION_BROWSE_NEXT,  NULL, redirectKeys},
		{SITK_FlagCapture + SITK_PrevPage, -1, ACTION_BROWSE_PPAGE, NULL, redirectKeys},
		{SITK_FlagCapture + SITK_NextPage, -1, ACTION_BROWSE_NPAGE, NULL, redirectKeys},
		{SITK_FlagCapture + SITK_Return,   -1, ACTION_ACCEPT,       NULL, redirectKeys},
		{SITK_FlagCapture + SITK_Escape,   -1, ACTION_CLEAROREXIT,  NULL, redirectKeys},
		{SITK_FlagCapture + SITK_FlagShift + SITK_Delete, -1, ACTION_DELETE, NULL, redirectKeys},
		{SITK_FlagCapture + SITK_FlagCtrl  + SITK_FlagShift + SITK_BackSpace, -1, ACTION_DELALL, NULL, redirectKeys},
		{SITK_FlagCapture + SITK_FlagAlt + SITK_F4, SITE_OnClose},

		{SITK_FlagCapture + SITK_F1, SITE_OnActivate, 0, "expr"},
		{SITK_FlagCapture + SITK_F2, SITE_OnActivate, 0, "graph"},
		{SITK_FlagCapture + SITK_F3, SITE_OnActivate, 0, "prog"},
		{SITK_FlagCapture + SITK_F4, SITE_OnActivate, 0, "check"},

		{SITK_FlagCapture + SITK_FlagAlt + '1', SITE_OnActivate, 0, "auto"},
		{SITK_FlagCapture + SITK_FlagAlt + '2', SITE_OnActivate, 0, "bin"},
		{SITK_FlagCapture + SITK_FlagAlt + '3', SITE_OnActivate, 0, "dec"},
		{SITK_FlagCapture + SITK_FlagAlt + '4', SITE_OnActivate, 0, "hex"},
		{0}
	};

	SIT_CreateWidgets(app,
		/* default unit for conversion */
		"<label name=title right=FORM title='" APPNAME " v" VERSION "'>"
		"<label name=units.danger title=Units:>"
		"<label name=unit title=", appcfg.defUnitNames, "left=WIDGET,units,0.5em>"
		"<canvas name=div2#div left=FORM,,NOPAD right=FORM,,NOPAD top=WIDGET,title,0.5em/>"

		/* theme/precision/help "toolbar" */
		"<label name=theme.danger title=THEME: bottom=FORM>"
		"<label name=", appcfg.lightMode == 1 ? "dark.dis" : "dark", "title=DARK left=WIDGET,theme,0.5em bottom=FORM>"
		"<button name=light title=LIGHT left=WIDGET,dark,0.5em curValue=", &appcfg.lightMode, "buttonType=", SITV_CheckBox, "bottom=FORM>"

		/* precision switch */
		"<label name=prec.danger title=Prec: left=WIDGET,light,1.5em bottom=FORM>"
		"<label name=", appcfg.use64b ? "f32.dis" : "f32", " title=32 left=WIDGET,prec,0.5em bottom=FORM>"
		"<button name=f64 title=64 left=WIDGET,f32,0.5em curValue=", &appcfg.use64b, "buttonType=", SITV_CheckBox, " bottom=FORM>"
		"<label name=bits.danger title=bits left=WIDGET,f64,0.5em bottom=FORM>"

		/* help link */
		"<label name=help title='<a href=#>HELP</a>' right=FORM bottom=FORM>"

		"<canvas name=div#div left=FORM,,NOPAD right=FORM,,NOPAD bottom=WIDGET,theme,0.5em/>"

		/* operating mode toggle buttons */
		"<label name=mode.danger title='MODE:'>"
		"<canvas name=modes.group left=WIDGET,mode,0.5em top=WIDGET,div2,0.5em>"
			"<button name='expr#toggle.first' curValue=", &appcfg.mode, "radioGroup=1"
			" title=EXPR nextCtrl=NONE buttonType=", SITV_ToggleButton, ">"
			"<button name='graph#toggle' curValue=", &appcfg.mode, "radioGroup=1 title=GRAPH "
			" left=WIDGET,expr,0.2em nextCtrl=NONE buttonType=", SITV_ToggleButton, "top=OPPOSITE,expr>"
			"<button name='prog#toggle.last' curValue=", &appcfg.mode, "radioGroup=1 title=PROG "
			" left=WIDGET,graph,0.2em nextCtrl=NONE buttonType=", SITV_ToggleButton, "top=OPPOSITE,expr>"
		"</canvas>"

		/* formatting mode */
		"<canvas name=display.group right=FORM top=WIDGET,div2,0.5em>"
			"<button name='auto#toggle.first' radioGroup=2 title=AUTO radioID=", FORMAT_DEFAULT, "nextCtrl=NONE"
			" curValue=", &appcfg.format, "buttonType=", SITV_ToggleButton, ">"
			"<button name='bin#toggle' radioGroup=2 title=BIN nextCtrl=NONE radioID=", FORMAT_BIN, "left=WIDGET,auto,0.2em"
			" curValue=", &appcfg.format, "buttonType=", SITV_ToggleButton, ">"
			"<button name='dec#toggle' radioGroup=2 title=DEC nextCtrl=NONE radioID=", FORMAT_DEC, "left=WIDGET,bin,0.2em"
			" curValue=", &appcfg.format, "buttonType=", SITV_ToggleButton, ">"
			"<button name='hex#toggle.last' radioGroup=2 title=HEX nextCtrl=NONE radioID=", FORMAT_HEX, "left=WIDGET,dec,0.2em"
			" curValue=", &appcfg.format, "buttonType=", SITV_ToggleButton, ">"
		"</canvas>"
		"<label name=disp.danger title='DISP:' right=WIDGET,display,0.5em top=MIDDLE,display>"

		/* "panels" of the user-interface */
		"<tab name=tabs left=FORM right=FORM top=WIDGET,modes,0.5em tabActive=", appcfg.mode, "bottom=WIDGET,div,0.5em"
		" tabStyle=", SITV_TabInvisible | SITV_TabVisiblityBitField, "tabStr='\t\t'>"

			/* prog tab */
			"<button name=addprog tabNum=4 title=Add left=FORM bottom=FORM nextCtrl=NONE>"
			"<button name=delprog.danger tabNum=4 title=Del left=WIDGET,addprog,0.3em top=OPPOSITE,addprog nextCtrl=NONE>"
			"<label name=editpos.danger tabNum=4 title=Pos: left=WIDGET,delprog,0.3em top=MIDDLE,addprog>"
			"<label name=posval tabNum=4 width=7em left=WIDGET,editpos,0.3em top=MIDDLE,addprog>"
			"<label name=size.danger tabNum=4 title=SIZE: left=WIDGET,posval,0.3em top=MIDDLE,addprog>"
			"<label name=sizeval tabNum=4 left=WIDGET,size,0.3em top=MIDDLE,addprog>"
			"<button name=check tabNum=4 title=Check right=FORM top=OPPOSITE,addprog>"
			"<label name=error tabNum=4 overflow=", SITV_Hidden, "left=WIDGET,delprog,0.3em right=WIDGET,check,0.3em"
			" top=MIDDLE,check visible=0 style='white-space: pre'>"

			"<listbox name=proglist nextCtrl=NONE tabNum=4 left=FORM top=FORM bottom=WIDGET,addprog,0.3em right=OPPOSITE,delprog"
			" listBoxFlags=", SITV_SelectAlways, ">"
			"<editbox name=progedit extra=", LEXER_EXTRA, "tabNum=4 right=FORM left=WIDGET,proglist,0.3em top=OPPOSITE,proglist"
			" bottom=OPPOSITE,proglist editType=", SITV_Multiline, "caretStyle=", SITV_CaretBlock | SITV_CaretNotify, ">"

			/* expr/graph tab */
			"<editbox name=repl editLength=256 maxUndo=2048 tabNum=3 left=FORM caretStyle=", SITV_CaretBlock, ">"
			"<button name=cls.danger tabNum=3 title=CLEAR right=FORM nextCtrl=NONE bottom=FORM>"
			"<button name=calc tabNum=3 title=Calc right=WIDGET,cls,0.3em nextCtrl=NONE bottom=FORM>"

			"<listbox name=results tabNum=1 bottom=WIDGET,calc,0.3em right=FORM left=FORM top=FORM listBoxFlags=", SITV_NoHeaders, ">"
			"<canvas name=draw tabNum=2 bottom=WIDGET,calc,0.3em left=FORM right=FORM top=FORM/>"

		"</tab>"
	);
	SIT_SetAttributes(app, "<repl right=WIDGET,calc,0.3em top=OPPOSITE,calc bottom=OPPOSITE,calc><mode top=MIDDLE,modes>");

	int i;
	for (i = 0; i < 4; i ++)
	{
		static STRPTR formats[] = {"hex", "dec", "bin", "auto"};
		SIT_AddCallback(ctrls.formats[i] = SIT_GetById(app, formats[i]),
			SITE_OnActivate, setFormat, NULL);
	}

	ctrls.app   = app;
	ctrls.calc  = SIT_GetById(app, "calc");
	ctrls.edit  = SIT_GetById(app, "repl");
	ctrls.list  = SIT_GetById(app, "results");
	ctrls.expr  = SIT_GetById(app, "expr");
	ctrls.graph = SIT_GetById(app, "graph");
	ctrls.prog  = SIT_GetById(app, "prog");
	ctrls.light = SIT_GetById(app, "light");
	ctrls.units = SIT_GetById(app, "unit");

	graphInit(ctrls.draw = SIT_GetById(app, "draw"));
	graphSetFunc(configGetChunk("_GRAPH", NULL));
	if (appcfg.mode == MODE_GRAPH)
		SIT_SetValues(ctrls.edit, SIT_Title, graphGetFunc(), NULL);

	SIT_AddCallback(ctrls.expr,  SITE_OnActivate, setTab, NULL);
	SIT_AddCallback(ctrls.graph, SITE_OnActivate, setTab, NULL);
	SIT_AddCallback(ctrls.prog,  SITE_OnActivate, setTab, NULL);
	SIT_AddCallback(ctrls.light, SITE_OnActivate, disaLabel, SIT_GetById(app, "dark"));

	SIT_AddCallback(SIT_GetById(app, "units"), SITE_OnClick,    editUnits, NULL);
	SIT_AddCallback(SIT_GetById(app, "title"), SITE_OnClick,    about, NULL);
	SIT_AddCallback(SIT_GetById(app, "f64"),   SITE_OnActivate, disaLabel, SIT_GetById(app, "f32"));
	SIT_AddCallback(SIT_GetById(app, "help"),  SITE_OnActivate, showHelp, NULL);
	SIT_AddCallback(SIT_GetById(app, "cls"),   SITE_OnActivate, redirect, (APTR) ACTION_DELALL);
	SIT_AddCallback(SIT_GetById(app, "check"), SITE_OnActivate, scriptCheck, NULL);
	SIT_AddCallback(ctrls.list,  SITE_OnChange,   copyLine, NULL);
	SIT_AddCallback(ctrls.list,  SITE_OnActivate, gotoErrorLine, NULL);
	SIT_AddCallback(ctrls.calc,  SITE_OnActivate, redirect, (APTR) ACTION_ACCEPT);
	SIT_SetValues(ctrls.list, SIT_CellPaint, highlightError, NULL);


	/* restore expressions from previous session */
	DATA8 exprList = configGetChunk("_EXPR", NULL);
	if (exprList)
	{
		struct ParseExprData_t data = {.cb = formatExprToList};
		TEXT tempVar[10];
		int  nb;
		for (nb = (exprList[0] << 8) | exprList[1], exprList += 2; nb > 0; nb --)
		{
			if (exprList[0] > 0)
			{
				data.assignTo = tempVar;
				sprintf(tempVar, "$%d", exprList[0]);
			}
			else data.assignTo = NULL;
			ctrls.insertAt = -1;
			SIT_ListInsertItem(ctrls.list, -1, NULL, exprList + 1);
			scriptResetStdout();
			evalExpr(exprList + 1, &data);
			exprList = strchr(exprList, 0) + 1;
		}
	}

	/* set keyboard focus on correct input depending on operating mode */
	switch (appcfg.mode) {
	case MODE_GRAPH:
	case MODE_EXPR:
		SIT_SetFocus(ctrls.edit);
		/* clear selection set by SIT_SetFocus() */
		SIT_SetValues(ctrls.edit, SIT_StartSel, 1000000, NULL);
		break;
	case MODE_PROG:
		scriptShow(ctrls.app);
	}
	SIT_SetValues(app, SIT_AccelTable, accels, NULL);
}

static void readPrefs(void)
{
	configRead("calc.prefs");

	Unit unit;
	int  last;
	for (unit = units, last = UNIT_EOF; unit->cat != UNIT_EOF; unit ++)
	{
		int cat = FindInList(appcfg.defUnitNames, unit->suffix, FIL_CHRLEN('/', 0));
		if (cat >= 0 && cat < UNIT_EOF)
			appcfg.defUnits[cat] = unit->id;
		if (unit->cat != last)
			firstUnits[unit->cat] = unit - units, last = unit->cat;
	}

	setDefUnit(NULL, NULL, NULL);
}

/* save all expression in ctrls.list */
static void saveExpr(void)
{
	RowTag tag;
	DATA8  mem, prev;
	int    size, count, i, nb;

	SIT_GetValues(ctrls.list, SIT_ItemCount, &count, NULL);

	for (i = 0, size = nb = 0; i < count; i ++)
	{
		SIT_GetValues(ctrls.list, SIT_RowTag(i), &tag, NULL);
		if (tag == NULL)
			size += strlen(SIT_ListGetCellText(ctrls.list, 0, i)) + 2, nb ++;
	}

	if (size > 0)
	{
		DATA8 start = mem = alloca(size + 2);
		/* 2 bytes for nb of expr */
		mem[0] = nb >> 8;
		mem[1] = nb;

		/* expression right after, separated by 0 */
		for (i = 0, mem += 2, prev = mem; i < count; i ++)
		{
			SIT_GetValues(ctrls.list, SIT_RowTag(i), &tag, NULL);
			if (tag == NULL)
			{
				prev = mem;
				prev[0] = 0;
				mem += sprintf(mem + 1, "%s", SIT_ListGetCellText(ctrls.list, 0, i)) + 2;
			}
			else if (tag != TAG_STDOUT && tag->var && tag->var[0] == '$')
			{
				/* keep this expression assigned to the same temp var */
				prev[0] = atoi(tag->var+1);
			}
		}

		DATA8 old = configGetChunk("_EXPR", &nb);

		/* do not overwrite stuff, if we don't have to */
		if (nb < size + 2 || memcmp(start, old, size + 2))
		{
			old = configAddChunk("_EXPR", size + 2);
			memcpy(old, start, size + 2);
		}
	}
	else configDelChunk("_EXPR");
}

/*
 * SDL specific code below
 */
int main(int nb, char * argv[])
{
	SDL_Surface * screen;
	SDL_Event     event;
	SIT_Widget    app;
	int           exitProg;

	readPrefs();
//	scriptTest();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
		return 1;

	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);

    screen = SDL_SetVideoMode(appcfg.width, appcfg.height, 32, SDL_HWSURFACE | SDL_GL_DOUBLEBUFFER | SDL_OPENGL | SDL_RESIZABLE);
    if (screen == NULL)
    {
		fprintf(stderr, "failed to set video mode, aborting.\n");
		return 1;
	}
	SDL_WM_SetCaption(APPNAME, APPNAME);

	app = SIT_Init(SIT_NVG_FLAGS, appcfg.width, appcfg.height, appcfg.lightMode == 1 ? "resources/light.css" : "resources/dark.css", 1);

	if (app == NULL)
	{
		SIT_Log(SIT_ERROR, "could not init SITGL: %s.\n", SIT_GetError());
		return 1;
	}

	exitProg = 0;
	SIT_SetValues(app,
		SIT_DefSBSize,   SITV_Em(0.9),
		SIT_DefSBArrows, SITV_NoArrows,
		SIT_RefreshMode, SITV_RefreshAsNeeded,
		SIT_AddFont,     "sans-serif", "resources/LEDCalculator.TTF",
		SIT_ExitCode,    &exitProg,
		NULL
	);

	createUI(app);

	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
	SDL_EnableUNICODE(1);

	glViewport(0, 0, appcfg.width, appcfg.height);

	FrameSetFPS(50);
	while (! exitProg)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if (appcfg.mode == MODE_GRAPH)
				{
					/* shift key: peek Y value, alt: peek X values */
					if (event.key.keysym.sym == SDLK_LSHIFT || event.key.keysym.sym == SDLK_RSHIFT)
						graphSetPeek(event.type == SDL_KEYDOWN, False);
					else if (event.key.keysym.sym == SDLK_LALT || event.key.keysym.sym == SDLK_RALT)
						graphSetPeek(event.type == SDL_KEYDOWN, True);
				}

				{
					int * sdlk;
					for (sdlk = SDLKtoSIT; sdlk < EOT(SDLKtoSIT); sdlk += 2)
					{
						if (sdlk[0] == event.key.keysym.sym) {
							SIT_ProcessKey(sdlk[1], SDLMtoSIT(event.key.keysym.mod), event.type == SDL_KEYDOWN);
							goto break_loop;
						}
					}
				}
				if (event.key.keysym.unicode > 0)
					SIT_ProcessChar(event.key.keysym.unicode, SDLMtoSIT(event.key.keysym.mod));
				else if (event.key.keysym.sym < 128 && event.type == SDL_KEYDOWN)
					SIT_ProcessChar(event.key.keysym.sym, SDLMtoSIT(event.key.keysym.mod));
			break_loop:
				break;
			case SDL_MOUSEBUTTONDOWN:
				SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 1);
				break;
			case SDL_MOUSEBUTTONUP:
				SIT_ProcessClick(event.button.x, event.button.y, event.button.button-1, 0);
				break;
			case SDL_MOUSEMOTION:
				SIT_ProcessMouseMove(event.motion.x, event.motion.y);
				break;
			case SDL_VIDEOEXPOSE:
				SIT_ForceRefresh();
				break;
			case SDL_VIDEORESIZE:
				appcfg.width  = event.resize.w;
				appcfg.height = event.resize.h;
				SIT_ProcessResize(appcfg.width, appcfg.height);
				glViewport(0, 0, appcfg.width, appcfg.height);
				break;
			case SDL_QUIT:
				goto exit;
			default:
				continue;
			}
		}

		/* update and render */
		if (SIT_RenderNodes(FrameGetTime()))
			SDL_GL_SwapBuffers();
		FrameWaitNext();
	}

	exit:

	scriptCommitChanges();
	saveExpr();
	configSave();

	SDL_FreeSurface(screen);
	SDL_Quit();

	return 0;
}

#ifdef	WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	return main(0, NULL);
}
#endif
