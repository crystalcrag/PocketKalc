/*
 * calc.c : contain the callback implementation of ParseExpression.
 *
 * Written by T.Pierron, may 2022.
 */

#define __MSVCRT_VERSION__     0x0601    // _time64()
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include "UtilityLibLite.h"
#include "parse.h"
#include "symtable.h"
#include "script.h"
#include "config.h"


SymTable_t symbols;
int        tempVarCount;
int        tagFrame;

void freeAllVars(void)
{
	symTableFree(&symbols);
	tempVarCount = 0;
}

/* pad digits by octet with '_' separator */
static int printbin(DATA8 dest, int max, uint64_t nb)
{
	TEXT   buffer[70];
	STRPTR p = buffer + sizeof buffer - 1;
	int    digit = 0;

	if (max == 0) return 0;

	if (appcfg.use64b == 0)
		nb &= 0xffffffff;

	*p-- = 0;
	while (nb != 0)
	{
		/* separator every 8 digits */
		if (digit == 8)
		{
			*p-- = '_';
			digit = 0;
		}
		digit ++;
		*p-- = (nb & 1) + '0';
		nb >>= 1;
	}
	/* make sure number of digits is multiple of 8 */
	if (digit > 0)
		while (digit != 8) *p-- = '0', digit ++;
	dest[0] = 0;
	return StrCat(dest, max, 0, p[1] == 0 ? "0" : p + 1);
}

/* escape some characters before displaying it to the user: you should be able to copy this string as-is into a C program */
static void formatString(STRPTR dest, STRPTR src, int max)
{
	DATA8 s = src;
	int   esc = 0;
	if (max > 0)
		*dest ++ = '\"', max --;
	while (*s)
	{
		TEXT chr[8]; int sz;
		if (*s == '\a') chr[0] = '\\', chr[1] = 'a', sz = 2; else
		if (*s == '\b') chr[0] = '\\', chr[1] = 'b', sz = 2; else
		if (*s == '\f') chr[0] = '\\', chr[1] = 'f', sz = 2; else
		if (*s == '\r') chr[0] = '\\', chr[1] = 'r', sz = 2; else
		if (*s == '\n') chr[0] = '\\', chr[1] = 'n', sz = 2; else
		if (*s == '\"') chr[0] = '\\', chr[1] = '\"', sz = 2; else
		if (*s == '\t') chr[0] = '\\', chr[1] = 't', sz = 2; else
		if (*s == '\v') chr[0] = '\\', chr[1] = 'v', sz = 2; else
		if (*s < 32)
		{
			sz = sprintf(chr, "\\x%02x", *s);
			esc = 1;
		}
		else
		{
			if (esc && isxdigit(*s))
				sz = sprintf(chr, "\\x%02x", *s);
			else
				chr[0] = *s, sz = 1, esc = 0;
		}
		if (sz > max) sz = max;
		memcpy(dest, chr, sz);
		max -= sz;
		dest += sz;
		s ++;
	}
	if (max > 0)
		*dest ++ = '\"', max --;

	if (max > 0) *dest = 0;
	else dest[-1] = 0;
}

/*
 * format a variant to be displayed in the main user interface.
 * if varName is NULL, result will be formatted such as it can be parsed back by ParseExpression()
 * (ie: no localization and using FORMAT_DEFAULT).
 */
void formatResult(Variant v, STRPTR varName, STRPTR out, int max)
{
	DATA8 suffix = NULL;
	int   mode   = appcfg.format;

	out[0] = 0;
	if (v->type == TYPE_ERR)
	{
		if (varName)
		{
			mode = StrCat(out, max, 0, "   ");
			mode = StrCat(out, max, mode, varName);
			if (v->int32 > 31)
			{
				TEXT buffer[24];
				sprintf(buffer, " on line %d", v->int32 >> 13);
				StrCat(out, max, mode, buffer);
			}
		}
		return;
	}

	if (varName == (STRPTR) -1)
	{
		/* keep format mode */
		;
	}
	else if (varName)
	{
		int pos = StrCat(out, max, 0, "   ");
		pos = StrCat(out, max, 0, varName);
		pos = StrCat(out, max, pos, IsDef(varName) ? " = " : "= ");
		max -= pos;
		out += pos;
	}
	else mode = FORMAT_DEFAULT;

	/* check if the number has a unit: compute suffix if yes (will be added after) */
	if (v->type < TYPE_STR && v->unit)
	{
		int cat = (v->unit >> 4) - 1;
		int id  = v->unit & 15;

		Unit unit = units + (firstUnits[cat] + id);
		suffix = unit->suffix;
		switch (unit->conv) {
		case CONV_SISUFFIX:
			/* metric units */
			if (v->real64 < 0.001)
			{
				/* use micron */
				v->real64 *= 1e6;
				id = 'u';
			}
			else if (v->real64 < 0.01)
			{
				/* use milli */
				v->real64 *= 1000;
				id = 'm';
			}
			else if (v->real64 < 1)
			{
				/* use centi */
				v->real64 *= 100;
				id = 'c';
			}
			else if (v->real64 > 1000)
			{
				/* use kilo */
				v->real64 *= 0.001;
				id = 'K';
			}
			else id = 0;
			if (id > 0)
				sprintf(suffix = alloca(16), "%c%s", id, unit->suffix);
			/* else use suffix as is */
			break;
		case CONV_NEXTUNIT:
			/* imperial unints */
			while (unit[1].conv == CONV_NEXTUNIT)
			{
				double val = v->real64 * unit->toMetricA / unit[1].toMetricA;
				if (val < 1) break;
				unit ++;
			}
			suffix = unit->suffix;
		}
	}

	/* format the number itself */
	switch (v->type) {
	default:
		strcpy(out, "#NaN");
		break;
	case TYPE_INT:
		switch (mode) {
		case FORMAT_HEX: snprintf(out, max, "0x%I64x", v->int64); break;
		case FORMAT_OCT: snprintf(out, max, "0%I64o",  v->int64); break;
		case FORMAT_BIN: printbin(out, max, v->int64); break;
		default:
			if (varName)
				/* will take care of localization */
				FormatNumber(out, max, "%d", v->int64);
			else
				snprintf(out, max, "%I64d", v->int64);
			break;
		}
		break;
	case TYPE_INT32:
		switch (mode) {
		case FORMAT_HEX: snprintf(out, max, "0x%x", v->int32); break;
		case FORMAT_OCT: snprintf(out, max, "0%o",  v->int32); break;
		case FORMAT_BIN: printbin(out, max, v->int32); break;
		default:
			if (varName)
				/* will take care of localization */
				FormatNumber(out, max, "%d", v->int32);
			else
				snprintf(out, max, "%d", v->int32);
			break;
		}
		break;
		break;
	case TYPE_DBL:
		/* appcfg.format useless here */
		if (suffix)
			/* no need to have 20 digits for unit numbers */
			snprintf(out, max, "%g", v->real64);
		else
			snprintf(out, max, "%.20g", v->real64);
		/* XXX not localized because the mix of , and . in US-en is kind of confusing */
		break;
	case TYPE_FLOAT:
		if (suffix)
			snprintf(out, max, "%g", v->real32);
		else
			/* will be promoted to double, but (lack of) precision should be kept */
			snprintf(out, max, "%.10g", v->real32);
		break;
	case TYPE_STR:
		if (mode > FORMAT_DEFAULT)
		{
			DATA8 p;
			int i;
			/* format individual characters as number */
			if (max > 0)
				*out++ = '[', max --;
			for (i = 0, p = v->string; p[i]; i ++)
			{
				if (i > 0 && max > 0)
				{
					*out++ = ',';
					max --;
				}
				int n;
				switch (mode) {
				case FORMAT_HEX: n = snprintf(out, max, "0x%x", p[i]); break;
				case FORMAT_OCT: n = snprintf(out, max, "0%o",  p[i]); break;
				case FORMAT_DEC: n = snprintf(out, max, "%d",   p[i]); break;
				default:         n = printbin(out, max, p[i]); break;
				}
				max -= n;
				out += n;
			}
			if (max > 0)
				*out++ = ']';
			if (max > 0) out[0] = 0;
			else out[-1] = 0;
		}
		/* format as a C-string */
		else formatString(out, v->string, max);
		break;

	case TYPE_ARRAY:
		{
			int i, items;
			if (max > 0)
				*out ++ = '[', max --;
			for (i = 0, items = VAR_LENGTH(v) - 1; i <= items; i ++)
			{
				formatResult(v->array + i, (STRPTR) -1, out, max);
				while (*out) out ++, max --;
				if (i == items || max == 0) break;
				switch (max) {
				default: out[0] = ','; out[1] = ' '; out += 2; max -= 2; break;
				case 1:  out[0] = ','; max --; out ++; break;
				case 0:  break;
				}
			}
			if (max > 0)
				*out ++ = ']', max --;
			if (max > 0) out[0] = 0;
			else out[-1] = 0;
		}
	}
	if (suffix)
		StrCat(out, max, 0, suffix);
}

/* try to convert arg to double */
static double GetArg64(Variant v, int idx, int count)
{
	if (idx > count) return 0; v += idx;
	switch (v->type) {
	case TYPE_FLOAT: return v->real32;
	case TYPE_DBL:   return v->real64;
	case TYPE_INT:   return v->int64;
	case TYPE_INT32: return v->int32;
	case TYPE_STR:   return strtod(v->string, NULL);
	default:         return 0;
	}
}

static float GetArg32(Variant v, int idx, int count)
{
	if (idx > count) return 0; v += idx;
	switch (v->type) {
	case TYPE_FLOAT: return v->real32;
	case TYPE_DBL:   return v->real64;
	case TYPE_INT:   return v->int64;
	case TYPE_INT32: return v->int32;
	case TYPE_STR:   return strtof(v->string, NULL);
	default:         return 0;
	}
}

Bool IsNull(Variant arg);

/* callback from ParseExpression */
void parseExpr(STRPTR name, Variant v, int store, APTR data)
{
	ParseExprData expr = data;
	if (store < 0) /* function call */
	{
		store = -store-1;
		if (scriptExecute(name, store, v))
		{
			/* a program with that name exists */
			return;
		}
		int func = FindInList("sin,cos,tan,asin,acos,atan,pow,exp,log,sqrt,floor,ceil,round", name, 0);
		if (appcfg.use64b)
		{
			double arg = GetArg64(v, 0, store);
			v->type = TYPE_DBL;
			switch (func) {
			case  0: v->real64 = sin(arg); break;
			case  1: v->real64 = cos(arg); break;
			case  2: v->real64 = tan(arg); break;
			case  3: v->real64 = acos(arg); break;
			case  4: v->real64 = asin(arg); break;
			case  5: v->real64 = atan(arg); break;
			case  6: v->real64 = pow(arg, GetArg64(v, 1, store)); break;
			case  7: v->real64 = exp(arg); break;
			case  8: v->real64 = log(arg); break;
			case  9: v->real64 = sqrt(arg); break;
			case 10: v->real64 = floor(arg); break;
			case 11: v->real64 = ceil(arg); break;
			case 12: v->real64 = round(arg); break;
			default: v->int32 = PERR_UnknownFunction; v->type = TYPE_ERR;
			}
		}
		else /* use 32bit math functions instead */
		{
			float arg = GetArg32(v, 0, store);
			/* this is mostly to check the limit of 32bit floating point precision */
			v->type = TYPE_FLOAT;
			switch (func) {
			case  0: v->real32 = sinf(arg); break;
			case  1: v->real32 = cosf(arg); break;
			case  2: v->real32 = tanf(arg); break;
			case  3: v->real32 = acosf(arg); break;
			case  4: v->real32 = asinf(arg); break;
			case  5: v->real32 = atanf(arg); break;
			case  6: v->real32 = powf(arg, GetArg32(v, 1, store)); break;
			case  7: v->real32 = expf(arg); break;
			case  8: v->real32 = logf(arg); break;
			case  9: v->real32 = sqrtf(arg); break;
			case 10: v->real32 = floorf(arg); break;
			case 11: v->real32 = ceilf(arg); break;
			case 12: v->real32 = roundf(arg); break;
			default: v->int32 = PERR_UnknownFunction; v->type = TYPE_ERR;
			}
		}
	}
	else if (name == NULL)
	{
		/* "print" result */
		if (expr->cb == NULL)
		{
			/* graph mode: just store result */
			expr->res = *v;
			return;
		}

		if (v->type == TYPE_VOID)
			return;

		/* print result, check if already stored */
		Result var = expr->assignTo ? symTableFindByName(&symbols, expr->assignTo) : symTableFindByValue(&symbols, v);

		if (var)
		{
			/* already stored */
			symTableAssign(var, v);
			if (var->frame == tagFrame)
				/* already printed */
				return;
		}
		else if (expr->assignTo)
		{
			STRPTR varname = expr->assignTo;
			if (varname[0] == '$')
			{
				store = atoi(varname + 1);
				if (store > tempVarCount)
					tempVarCount = store;
			}
			var = symTableAdd(&symbols, expr->assignTo, v);
		}
		else if (! IsNull(v))
		{
			/* variable does not exists and has a somewhat meaningful value: stores it */
			TEXT varname[16];
			sprintf(varname, "$%d", ++ tempVarCount);
			var = symTableAdd(&symbols, varname, v);
			*v = var->bin;
		}
		/* else result == 0, no need to store it in a variable */

		if (var)
		{
			/* this will mark the variable as already printed: don't do it twice */
			var->frame = tagFrame;
			expr->cb(&var->bin, var->name);
		}
		else expr->cb(v, "");
	}
	else /* get variable content */
	{
		Result var;
		int constant = FindInList("pi,e,ln2,time,now", name, 0);

		if (constant < 0)
		{
			if (data == NULL)
			{
				/* trying to fold constant expression: this is a user-defined var name: not constant */
				v->type = TYPE_ERR;
				return;
			}
			if (expr->cb == NULL)
			{
				/* graph mode: assign all var to this value */
				*v = expr->res;
				return;
			}
			var = symTableFindByName(&symbols, name);
			if (store == 0)
			{
				/* non-existant variable == integer 0 */
				if (var)
				{
					memcpy(v, &var->bin, sizeof *v);
					if (v->type == TYPE_ARRAY || v->type == TYPE_STR)
						v->lengthFree &= 0x0fffffff;
				}
				else memset(v, 0, sizeof *v);
			}
			else
			{
				if (var == NULL)
					var = symTableAdd(&symbols, name, v);
				else
					symTableAssign(var, v);
				var->frame = tagFrame;
				expr->cb(v, var->name);
			}
		}
		else if (appcfg.use64b)
		{
			switch (constant) {
			case 0: v->real64 = M_PI;  v->type = TYPE_DBL; break;
			case 1: v->real64 = M_E;   v->type = TYPE_DBL; break;
			case 2: v->real64 = M_LN2; v->type = TYPE_DBL; break;
			case 3: /* time, now */
			/* time64 is a Microsoft msvcrt function :-/ */
			case 4: v->int64 = _time64(0); v->type = TYPE_INT; break;
			}
			v->unit = 0;
		}
		else /* 32bit constants */
		{
			switch (constant) {
			case 0: v->real32 = M_PI;  v->type = TYPE_FLOAT; break;
			case 1: v->real32 = M_E;   v->type = TYPE_FLOAT; break;
			case 2: v->real32 = M_LN2; v->type = TYPE_FLOAT; break;
			case 3: /* time, now */
			case 4: v->int32  = time(0); v->type = TYPE_INT32; break;
			}
			v->unit = 0;
		}
	}
}

/* ParseExpression() front end */
int evalExpr(STRPTR expr, ParseExprData data)
{
	/* used to check if a variable has already been "printed" */
	tagFrame ++;

	int error = ParseExpression(expr, parseExpr, data);
	if (error == 0)
		return 1;

	data->res.type = TYPE_ERR;
	if (data->cb)
	{
		data->res.int32 = error;
		data->cb(&data->res, errorMessages[error & 31]);
	}
	return 0;
}
