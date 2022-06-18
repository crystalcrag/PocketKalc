/*
 * script.c : quick and dirty scripting language to extend calculator capabilities.
 *            also handle user interface of "PROG" tab.
 *
 * written by T.Pierron, june 2022
 */


#include <stdio.h>
#include <SDL/SDL.h>
#include <GL/GL.h>
#include "nanovg.h"
#include <math.h>
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

/* double-click on program name in list */
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
			/* cannot edit wp->name directly: we want this to be cancellable */
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

void scriptCommitChanges(void)
{
	if (script.curEdit && (script.curProgChanged || script.curEdit->changed))
		scriptSaveChanges(script.curEdit);
}

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
		script.progList = SIT_GetById(app, "proglist");
		script.progEdit = SIT_GetById(app, "progedit");
		script.statPos  = SIT_GetById(app, "posval");
		script.statSize = SIT_GetById(app, "sizeval");

		SIT_AddCallback(script.progList, SITE_OnChange, scriptSelectProgram, NULL);
		SIT_AddCallback(script.progList, SITE_OnActivate, scriptRename, NULL);
		SIT_AddCallback(script.progEdit, SITE_OnChange, scriptEditStat, NULL);
		SIT_AddCallback(SIT_GetById(app, "addprog"), SITE_OnActivate, scriptAdd, NULL);
		SIT_AddCallback(SIT_GetById(app, "delprog"), SITE_OnActivate, scriptDel, NULL);

		/* activate lexer on multi-line editor: a bit overkill for something so simple */
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
