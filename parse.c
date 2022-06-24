/*
 * parse.c: expression evaluator to parse C-like expressions using Shunting-Yard algorithm
 *
 * written by T.Pierron, jan 2008.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#include <math.h>
#include "UtilityLibLite.h"
#include "config.h"
#include "parse.h"

#define RIGHT               1
#define LEFT                2
// #define	DEBUG_STACK

typedef struct Operator_t *     Operator;
typedef struct Stack_t *        Stack;

int firstUnits[3]; /* index in units[] where category starts */

struct Operator_t
{
	DATA8 token;
	int   arity;
	int   associativity;
	int   priority;
};

/*
 * all the operators supported by this module: precedence will be same as C, which means expression
 * like "a & b == 0x55" will be evaluated as "a & (b == 0x55)", sadly.
 */
static struct Operator_t OperatorList[] =
{
	{ "-",   1, RIGHT, 14 }, { "~",   1, RIGHT, 14 }, //  0
	{ "!",   1, RIGHT, 14 }, { "++",  1, LEFT,  15 }, //  2
	{ "--",  1, LEFT,  15 }, { "*",   2, LEFT,  13 }, //  4
	{ "/",   2, LEFT,  13 }, { "%",   2, LEFT,  13 }, //  6
	{ "+",   2, LEFT,  12 }, { "-",   2, LEFT,  12 }, //  8
	{ "<<",  2, LEFT,  11 }, { ">>",  2, LEFT,  11 }, // 10
	{ "<",   2, LEFT,  10 }, { ">",   2, LEFT,  10 }, // 12
	{ "<=",  2, LEFT,  10 }, { ">=",  2, LEFT,  10 }, // 14
	{ "==",  2, LEFT,   9 }, { "!=",  2, LEFT,   9 }, // 16
	{ "&",   2, LEFT,   8 }, { "^",   2, LEFT,   7 }, // 18
	{ "|",   2, LEFT,   6 }, { "&&",  2, LEFT,   5 }, // 20
	{ "||",  2, LEFT,   4 }, { "?",   3, RIGHT,  3 }, // 22
	{ ":",   0, RIGHT,  3 }, { "=",   2, RIGHT,  2 }, // 24
	{ "*=",  2, RIGHT,  2 }, { "/=",  2, RIGHT,  2 }, // 26
	{ "%=",  2, RIGHT,  2 }, { "+=",  2, RIGHT,  2 }, // 28
	{ "-=",  2, RIGHT,  2 }, { "<<=", 2, RIGHT,  2 }, // 30
	{ ">>=", 2, RIGHT,  2 }, { "&=",  2, RIGHT,  2 }, // 32
	{ "^=",  2, RIGHT,  2 }, { "|=",  2, RIGHT,  2 }, // 34
	{ ",",   2, RIGHT,  1 },                          // 36
};

/* special operators */
static struct Operator_t functionCall = { "(",   0, LEFT,  17 };
//static struct Operator_t arrayStart   = { "[",   0, LEFT,  18 };
//static struct Operator_t arrayEnd     = { "]",   1, LEFT,  18 };

/* special operators */
#define binaryMinus     (OperatorList+9)
#define commaSeparator  (OperatorList+36)
#define logicalAnd      (OperatorList+21)
#define logicalOr       (OperatorList+22)
#define ternaryLeft     (OperatorList+23)
#define ternaryRight    (OperatorList+24)

struct Unit_t units[] =
{
	{UNIT_DIST, 0, CONV_SISUFFIX, "meters", "m",  1},
	{UNIT_DIST, 1, CONV_NEXTUNIT, "inch",   "in", 0.0254},
	{UNIT_DIST, 2, CONV_NEXTUNIT, "feet",   "ft", 0.3048},
	{UNIT_DIST, 3, CONV_NEXTUNIT, "miles",  "mi", 1609.344},
	{UNIT_DIST, 4, 0,             "points", "pt", 0.0254/72},

	{UNIT_TEMP, 0, 0, "Celcius",    "degC", 1},
	{UNIT_TEMP, 1, 0, "Fahrenheit", "degF", 5/9., -5*32/9.},
	{UNIT_TEMP, 2, 0, "Kelvin",     "degK", 1, -273.15},

	{UNIT_MASS, 0, CONV_SISUFFIX, "Gram",  "g",  1},
	{UNIT_MASS, 1, 0,             "Pound", "Lb", 453.59237},
	{UNIT_MASS, 2, 0,             "Ounce", "oz", 28.349523125},

	{UNIT_ANGLE, 0, 0, "Radians", "rad", 1},
	{UNIT_ANGLE, 1, 0, "Degrees", "deg", M_PI/180},

	{UNIT_EOF}
};


struct Stack_t
{
	Stack      next;
	VariantBuf value;
};

enum
{
	TOKEN_UNKNOWN,
	TOKEN_SCALAR,
	TOKEN_INCPRI,
	TOKEN_DECPRI,
	TOKEN_ARRAYSTART,
	TOKEN_ARRAYEND,
	TOKEN_OPERATOR,
	TOKEN_IDENT,             /* internal: converted to TOKEN_SCALAR */
	TOKEN_STRING,            /* internal: converted to TOKEN_SCALAR */
	TOKEN_END
};

#ifdef	CRASH_TEST
#define	SZ_POOL      300
#else
#define	SZ_POOL      1024
#endif

void ByteCodeGenExpr(STRPTR unused, Variant argv, int arity, APTR data);
void ByteCodeAddVariant(ByteCode, Variant);

/*
 * we are using our own crummy allocator to use mem from stack first, then mem from heap if the former
 * is full (99.9% of expressions will only need stack mem).
 */
static void * MyCalloc(DATA8 buffer, int length)
{
	DATA8 p = buffer;

	length = (length + 7) & ~7; /* align to 8bytes boundary */

	while (1)
	{
		int len = (p[0] << 8) | p[1];

		switch (p[2]) {
		case 2: /* block freed */
			if (length <= len)
			{
				p[2] ^= 3;
				p += 4;
				memset(p, 0, length);
				return p;
			}
			// no break;
		case 1: /* block not free */
			p += len + 4;
			break;
		case 0: /* block free up to the end */
			if (length <= len - 8)
			{
				p[0] = length >> 8;
				p[1] = length & 0xff;
				p[2] = 1;
				p[3] = 0;
				memset(buffer = p + 4, 0, length);
				p   += length + 4;
				len -= length + 4;
				p[0] = length >> 8;
				p[1] = length & 0xff;
				p[2] = p[3] = 0;
				return buffer;
			}
			/* no need to allocate multi-megabyte strings, more often than not, it is an error */
			return length < 1024 ? calloc(length, 1) : NULL;
		}
	}
}

static void MyFree(DATA8 buffer, Stack stack)
{
	DATA8 mem = (DATA8) stack;
	if (buffer <= mem && mem < buffer + SZ_POOL)
	{
		DATA8 ptr  = mem - 4;
		int   len  = (ptr[0] << 8) | ptr[1];
		DATA8 next = mem + len;
		if (ptr[2] == 0)
			/* next block is free up to the end: merge with this one */
			len += 4 + (next[0] << 8) + next[1];
		else
			ptr[2] = 2;
		ptr[0] = len >> 8;
		ptr[1] = len;
		ptr[3] = 0;
	}
	else free(mem);
}

static Stack NewOperator(DATA8 buffer, Operator ope)
{
	Stack oper = MyCalloc(buffer, sizeof *oper);

	oper->value.ope  = ope;
	oper->value.type = TYPE_OPE;
	oper->value.tag  = True;

	return oper;
}

static void UnescapeAntiSlash(DATA8 src)
{
	DATA8 token;

	for (token = src; *token; token ++)
	{
		if (*token != '\\') continue;
		DATA8 overwrite = token + 2;
		switch (token[1]) {
		case 'a':  *token = '\a'; break;
		case 'b':  *token = '\b'; break;
		case 't':  *token = '\t'; break;
		case 'n':  *token = '\n'; break;
		case 'v':  *token = '\v'; break;
		case 'f':  *token = '\f'; break;
		case 'r':  *token = '\r'; break;
		case 'x':  *token = strtoul(token + 2, (char **) &overwrite, 16); break;
		default:   *token = token[1];
		}
		strcpy(token + 1, overwrite);
	}
}

static Stack NewNumber(DATA8 buffer, int type, ...)
{
	va_list args;
	Stack   num;
	DATA8   str = NULL;
	int     len = 0;
	int     sz  = sizeof *num;

	va_start(args, type);
	if (type == TYPE_STR || type == TYPE_IDF)
	{
		str = va_arg(args, DATA8);
		len = va_arg(args, int) + 1;
		sz += len;
	}

	num = MyCalloc(buffer, sz);
	num->value.type = type;

	switch (type) {
	case TYPE_INT32: num->value.int32  = va_arg(args, int); break;
	case TYPE_INT:   num->value.int64  = va_arg(args, int64_t); break;
	case TYPE_DBL:   num->value.real64 = va_arg(args, double); break;
	/* float will be promoted to double in a vararg section, they need to be transmitted as pointer to prevent this :-/ */
	case TYPE_FLOAT: num->value.real32 = * va_arg(args, float *); break;
	case TYPE_STR:
	case TYPE_IDF:
		CopyString(num->value.string = (DATA8) (num + 1), str, len);
		if (type == TYPE_STR) UnescapeAntiSlash(num->value.string);
	}
	va_end(args);
	return num;
}

static void ConvertToDefUnit(Stack number, Unit unit, double mult)
{
	Unit def = units + (appcfg.defUnits[unit->cat] + firstUnits[unit->cat]);
	double val;

	switch (number->value.type) {
	case TYPE_INT:   val = number->value.int64 * mult; break;
	case TYPE_INT32: val = number->value.int32 * mult; break;
	case TYPE_DBL:   val = number->value.real64 * mult; break;
	case TYPE_FLOAT: val = number->value.real32 * mult; break;
	default: return;
	}

	if (def != unit)
	{
		/* convert to metric, based on a linear equation: y = ax + b */
		val = val * unit->toMetricA + unit->toMetricB;

		/* convert to desired unit */
		number->value.real64 = (val - def->toMetricB) / def->toMetricA;
	}
	else number->value.real64 = val;
	number->value.type = TYPE_DBL;
	number->value.tag = (def->id | ((def->cat + 1) << 4)) << 4;
}


static DATA8 ParseUnit(DATA8 start, Stack object)
{
	DATA8 end;
	Unit  unit;
	/* must be right after number, without spaces */
	for (end = start + 1; isalpha(*end); end ++);
	for (unit = units; unit->cat != UNIT_EOF; unit ++)
	{
		int len = strlen(unit->suffix);
		if (strncasecmp(end - len, unit->suffix, len) == 0)
		{
			/* check if there are a SI suffix */
			double mult = 1;
			if (len + 1 == end - start)
			{
				switch (FindInList("U,M,C,K", end-len-1, 1)) {
				case 0: mult = 1e-6; break;
				case 1: mult = 1e-3; break;
				case 2: mult = 1e-2; break;
				case 3: mult = 1000; break;
				default: continue; /* unknown SI suffix */
				}
			}
			else if (len != end - start)
			{
				/* too many characters between number and unit */
				continue;
			}
			/* convert to default unit */
			start = end;
			ConvertToDefUnit(object, unit, mult);
			break;
		}
	}
	return start;
}

/* try to parse a number (using 64bit precision) */
static int GetNumber64(DATA8 buffer, Stack * object, DATA8 * exp, Bool neg)
{
	STRPTR  cur = *exp;
	STRPTR  str = cur;
	int64_t nbi;
	double  nbf;

	/* signed number are interpreted as a unsigned preceeded by an unary - */
	if (neg && (neg = *cur == '-')) cur ++;
	if (! isdigit(*cur) && *cur != '.') return 0;

	/* first try if we can parse an integer (octal, dec or hexa, like in C) */
	nbi = strtoull(cur, &str, 0);

	if (str == cur || (*str && strchr("eE.", *str)))
	{
		/* try a double */
		nbf = strtod(cur, &str);
		if (str == cur)
			return 0;

		*object = NewNumber(buffer, TYPE_DBL, neg ? -nbf : nbf);
	}
	else *object = NewNumber(buffer, TYPE_INT, neg ? -nbi : nbi);

	/* check if there is an unit suffix */
	if (isalpha(*str))
		str = ParseUnit(str, *object);

	*exp = str;
	return 1;
}

/* try to parse a number (using 32bit precision) */
static int GetNumber32(DATA8 buffer, Stack * object, DATA8 * exp, Bool neg)
{
	STRPTR cur = *exp;
	STRPTR str = cur;
	int    nbi;
	float  nbf;

	/* identical parsing method than GetNumber64() */
	if (neg && (neg = *cur == '-')) cur ++;
	if (! isdigit(*cur) && *cur != '.') return 0;

	/*
	 * we are using unsigned integer conversion, doesn't matter if it overflows
	 * that's the point of this function: see what happens if it overlows.
	 */
	nbi = strtoul(cur, &str, 0);

	if (str == cur || (*str && strchr("eE.", *str)))
	{
		/* try a double */
		nbf = strtof(cur, &str);
		if (str == cur)
			return 0;
		if (neg)
			nbf = -nbf;

		/* use a pointer to prevent promotion to double */
		*object = NewNumber(buffer, TYPE_FLOAT, &nbf);
	}
	else *object = NewNumber(buffer, TYPE_INT32, neg ? -nbi : nbi);

	/* check if there is an unit suffix */
	if (isalpha(*str))
		str = ParseUnit(str, *object);

	*exp = str;
	return 1;
}

/* will point to GetNumber64 or GetNumber32 depending on <use64b> */
static int (*GetNumber)(DATA8 buffer, Stack * object, DATA8 * exp, Bool neg);

/* our main lexical analyser, this should've been the lex part, if we ever used it */
static int GetToken(DATA8 buffer, Stack * object, DATA8 * exp)
{
	static uint8_t chrClass[128];

	DATA8 str;
	int   type;

	if (chrClass[0] == 0)
	{
		/* init on the fly */
		int i;
		for (i = 0; i < DIM(OperatorList); i ++)
			chrClass[OperatorList[i].token[0]] = TOKEN_OPERATOR;

		memset(chrClass + 'a', TOKEN_IDENT, 'z' - 'a' + 1);
		memset(chrClass + 'A', TOKEN_IDENT, 'Z' - 'A' + 1);
		memset(chrClass + '0', TOKEN_SCALAR, '9' - '0' + 1);
		chrClass['_']  = TOKEN_IDENT;
		chrClass['$']  = TOKEN_IDENT;
		chrClass['(']  = TOKEN_INCPRI;
		chrClass[')']  = TOKEN_DECPRI;
		chrClass['[']  = TOKEN_ARRAYSTART;
		chrClass[']']  = TOKEN_ARRAYEND;
		chrClass['-']  = TOKEN_SCALAR;
		chrClass['\''] = TOKEN_STRING;
		chrClass['\"'] = TOKEN_STRING;
		chrClass[0]    = TOKEN_END;
	}

	/* skip starting space */
	for (str = *exp; isspace(*str); str ++);

	type = chrClass[str[0]];

	switch (type) {
	case TOKEN_STRING:
		{
			uint8_t start = *str;

			for (*exp = ++ str; *str && *str != start; str ++)
				if (*str == '\\' && str[1]) str ++;

			*object = NewNumber(buffer, TYPE_STR, *exp, (int) (str - *exp));
			type    = TOKEN_SCALAR;

			if (*str) str ++;
		}
		break;
	case TOKEN_IDENT:
		for (*exp = str ++; *str == '_' || isalnum(*str); str ++);

		*object = NewNumber(buffer, TYPE_IDF, *exp, (int) (str - *exp));
		type    = TOKEN_SCALAR;
		break;
	case TOKEN_SCALAR:
		if (GetNumber(buffer, object, &str, False))
			break;
		// else no break;
	case TOKEN_OPERATOR:
		{
			Operator best, cur;

			/* use same rule as C : longest match is winner */
			for (best = NULL, cur = OperatorList; cur < OperatorList + DIM(OperatorList); cur ++)
			{
				int length = strlen(cur->token);

				if (strncmp(str, cur->token, length) == 0 && (best == NULL || strlen(best->token) < length))
					best = cur;
			}
			if (best)
			{
				str += strlen(best->token);
				*object = NewOperator(buffer, best);
				type = TOKEN_OPERATOR;
			}
			else type = TOKEN_UNKNOWN;
		}
		break;

	default:
		if (*str) str ++;
	}

	*exp = str;

	return type;
}

static void PushStack(Stack * top, Stack object)
{
	object->next = *top;
	(*top) = object;
}

static Stack PopStack(Stack * top)
{
	Stack ret = *top;

	if (ret) *top = ret->next;

	return ret;
}

/* transform an ident into a TYPE_INT, TYPE_DBL or TYPE_STR */
static void AffectArg(Stack arg, ParseExpCb cb, APTR data)
{
	if (arg->value.type == TYPE_IDF)
	{
		cb(arg->value.string, &arg->value, 0, data);
		if (arg->value.type == TYPE_STR && arg->value.string == NULL)
			arg->value.string = "";
	}
}

Bool IsNull(Variant arg)
{
	switch (arg->type) {
	case TYPE_INT32: return arg->int32  == 0;
	case TYPE_INT:   return arg->int64  == 0;
	case TYPE_DBL:   return arg->real64 == 0;
	case TYPE_FLOAT: return arg->real32 == 0.0f;
	case TYPE_STR:   return arg->string[0] == 0;
	default:         return False;
	}
}

/* make the type of arg1 and arg2 the same, based on "widest" type */
static void Promote(Stack arg1, Stack arg2)
{
	static uint8_t sizes[] = {8, 4, 9, 5, 0, 0, 0, 0};

	if (arg1->value.type >= TYPE_IDF || arg2->value.type >= TYPE_IDF)
		/* only works with numbers */
		return;

	if (sizes[arg1->value.type] > sizes[arg2->value.type])
	{
		/* convert arg2 number to arg1 type */
		;
	}
	else if (sizes[arg2->value.type] > sizes[arg1->value.type])
	{
		/* convert arg1 number to arg2 type */
		Stack tmp;
		tmp = arg1; arg1 = arg2; arg2 = tmp;
	}
	else return;

	/* convert arg2 into arg1 type */
	switch (arg1->value.type) {
	default: return;
	case TYPE_INT:
		switch (arg2->value.type) {
		case TYPE_INT32: arg2->value.int64 = arg2->value.int32; break;
		case TYPE_FLOAT: arg2->value.int64 = arg2->value.real32; break;
		default: return;
		}
		break;
	case TYPE_FLOAT:
		/* can only be int32 at this point */
		arg2->value.real32 = arg2->value.int32;
		break;
	case TYPE_DBL:
		switch (arg2->value.type) {
		case TYPE_INT:   arg2->value.real64 = arg2->value.int64; break;
		case TYPE_INT32: arg2->value.real64 = arg2->value.int32; break;
		case TYPE_FLOAT: arg2->value.real64 = arg2->value.real32; break;
		default: return;
		}
	}
	arg2->value.type = arg1->value.type;
}

void ToString(Variant arg, DATA8 out, int max)
{
	switch (arg->type) {
	case TYPE_INT32: snprintf(out, max, "%d",    arg->int32); break;
	case TYPE_INT:   snprintf(out, max, "%I64d", arg->int64); break;
	case TYPE_DBL:   snprintf(out, max, "%.20g", arg->real64); break;
	/* will be promoted to double, but lack of precision will be preserved */
	case TYPE_FLOAT: snprintf(out, max, "%.10g", arg->real32); break;
	default:         out[0] = 0; break;
	}
}

/* perform a lexicographic compare whatever type of arguments are (at least one is string) */
int CompareString(Stack arg1, Stack arg2)
{
	TEXT number[32];
	Bool invert = False;
	if (arg2->value.type != TYPE_STR)
	{
		Stack arg3 = arg1; arg1 = arg2; arg2 = arg3;
		invert = True;
	}
	ToString(&arg1->value, number, sizeof number);
	return invert ? strcmp(arg2->value.string, number) :
	                strcmp(number, arg2->value.string);
}


#ifdef	DEBUG_STACK
void DebugStacks(Stack values, Stack oper)
{
	Stack s;

	printf("Values: ");
	for (s = values; s; s = s->next) {
		switch (s->type) {
		case TYPE_INT: printf("%ld ", s->value.integer); break;
		case TYPE_DBL: printf("%g ", s->value.real); break;
		case TYPE_STR:
		case TYPE_IDF: printf("%s ", s->value.string);
		}
	}

	printf("\nOper:   ");
	for (s = oper; s; s = s->next)
		printf("%s:%d ", s->value.ope->token, s->type);
	printf("\n================================================\n");
}
#endif

static void MakeCall(DATA8 buffer, Stack * values, ParseExpCb cb, APTR data, Bool eval)
{
	Stack val;
	int   i, nb;
	/* count number of arguments */
	for (nb = 0, val = *values; val && val->value.type != TYPE_FUN; val = val->next, nb ++);

	if (val)
	{
		Variant list = alloca(MAX(nb, 1) * sizeof *val);
		for (val = *values, i = nb-1; val && val->value.type != TYPE_FUN; val = val->next, i --)
		{
			list[i] = val->value;
			if (val->value.type == TYPE_IDF)
				cb(val->value.string, list + i, 0, data);
		}
		if (eval)
		{
			/* evaluate function */
			cb(val->value.string, list, -nb-1, data);
			val->value = list[0];
			if (list->type == TYPE_STR && list->string == NULL)
				val->value.string = "";
		}
	}
	while (*values != val)
		MyFree(buffer, PopStack(values));
}

/*
 * This is the function that takes operand and perform operation according to top most operator
 * This is the syntax analyser, usually produced by tools like yacc
 */
static int MakeOp(DATA8 buffer, Stack * values, Stack * oper, ParseExpCb cb, APTR data)
{
	Stack    arg1, arg2, arg3;
	Operator ope = (*oper)->value.ope;
	Bool     eval;
	int      nb, error = 0;

	#define	THROW(err)	{ error = err; goto error_case; }

	#ifdef	DEBUG_STACK
	DebugStacks(*values, *oper);
	#endif

	/* do we need to evalutate something ? */
	for (arg1 = (*oper)->next, eval = True; eval && arg1; eval = arg1->value.tag & 1, arg1 = arg1->next);

	arg1 = arg2 = arg3 = NULL;
	switch (ope->arity) {
	case 3: arg3 = PopStack(values); if (arg3 == NULL) THROW(PERR_MissingOperand);
	case 2: arg2 = PopStack(values); if (arg2 == NULL) THROW(PERR_MissingOperand);
	case 1: arg1 = PopStack(values); if (arg1 == NULL) THROW(PERR_MissingOperand);
	}
	MyFree(buffer, PopStack(oper));

	/* script parsing */
	if (cb == ByteCodeGenExpr)
	{
		if ((arg1 && arg1->value.type > TYPE_SCALAR) ||
		    (arg2 && arg2->value.type > TYPE_SCALAR) ||
		    (arg3 && arg3->value.type > TYPE_SCALAR) || ope == &functionCall)
		{
			/* cannot be evaluated at "compile" time: add into the byte code */
			VariantBuf argv[4];
			argv[0].type = TYPE_OPE;
			argv[0].ope  = ope;
			if (arg1) argv[1] = arg1->value;
			if (arg2) argv[2] = arg2->value;
			if (arg3) argv[3] = arg3->value;
			cb(NULL, argv, ope->arity, data);
			arg1->value.type = TYPE_OPE;
			PushStack(values, arg1);
			arg1 = NULL;
			THROW(0);
		}
		/* fold constant expressions */
		eval = True;
	}

	if (ope == &functionCall)
	{
		/* arity is set to 0 for this */
		MakeCall(buffer, values, cb, data, eval);
		return 0;
	}

	if (! eval) /* short circuit */
	{
		PushStack(values, arg1); /* push a dummy value */
		arg1 = NULL;
		THROW(0);
	}
	nb = ope - OperatorList;

	if (5 <= nb && nb <= 22)
	{
		/* invariant for a few operators: first convert ident into scalar */
		AffectArg(arg1, cb, data);
		AffectArg(arg2, cb, data);

		/* if one of the arg is a string and the other a number, check if the string can be converted to number */
		if (arg1->value.type != TYPE_STR || arg2->value.type != TYPE_STR)
		{
			DATA8 p;
			p = arg1->value.string;
			if (arg1->value.type == TYPE_STR && GetNumber(buffer, &arg3, &p, True) && *p == 0)
				memcpy(arg1, arg3, sizeof *arg1);

			p = arg2->value.string;
			if (arg2->value.type == TYPE_STR && GetNumber(buffer, &arg3, &p, True) && *p == 0)
				memcpy(arg2, arg3, sizeof *arg2);

			if (arg3) MyFree(buffer, arg3);
		}
		/* if type of arg1 and arg2 are not the same, promote them to "widest" type  */
		Promote(arg1, arg2);
	}

	switch (nb) {
	case 0: /* unary - */
		AffectArg(arg1, cb, data);
		switch (arg1->value.type) {
		case TYPE_INT32: arg1->value.int32  = - arg1->value.int32; break;
		case TYPE_INT:   arg1->value.int64  = - arg1->value.int64; break;
		case TYPE_DBL:   arg1->value.real64 = - arg1->value.real64; break;
		case TYPE_FLOAT: arg1->value.real32 = - arg1->value.real32; break;
		default:         THROW(PERR_InvalidOperation);
		}
		PushStack(values, arg1);
		break;

	case 1: /* unary ~ */
		AffectArg(arg1, cb, data);
		switch (arg1->value.type) {
		case TYPE_INT32: arg1->value.int32 = ~ arg1->value.int32; break;
		case TYPE_INT:   arg1->value.int64 = ~ arg1->value.int64; break;
		case TYPE_DBL:   arg1->value.int64 = ~ (int64_t) arg1->value.real64; arg1->value.type = TYPE_INT; break;
		case TYPE_FLOAT: arg1->value.int32 = ~ (int)     arg1->value.real32; arg1->value.type = TYPE_INT32; break;
		default:         THROW(PERR_InvalidOperation);
		}
		PushStack(values, arg1);
		break;

	case 2: /* unary ! */
		AffectArg(arg1, cb, data);
		switch (arg1->value.type) {
		case TYPE_INT32: arg1->value.int32 = ! arg1->value.int32; break;
		case TYPE_INT:   arg1->value.int64 = ! arg1->value.int64; break;
		case TYPE_DBL:   arg1->value.int64 = arg1->value.real64 != 0; arg1->value.type = TYPE_INT; break;
		case TYPE_FLOAT: arg1->value.int32 = arg1->value.real32 != 0; arg1->value.type = TYPE_INT32; break;
		case TYPE_STR:   arg1->value.int64 = ! arg1->value.string[0]; arg1->value.type = TYPE_INT; break;
		default:         THROW(PERR_InvalidOperation);
		}
		PushStack(values, arg1);
		break;

#define	MAKE_OP(operator) \
		if (arg1->value.type == TYPE_STR || arg2->value.type == TYPE_STR) THROW(PERR_InvalidOperation); \
		switch (arg1->value.type) { \
		case TYPE_INT32: arg1->value.int32  operator arg2->value.int32; break; \
		case TYPE_INT:   arg1->value.int64  operator arg2->value.int64; break; \
		case TYPE_DBL:   arg1->value.real64 operator arg2->value.real64; break; \
		case TYPE_FLOAT: arg1->value.real32 operator arg2->value.real32; break; \
		default:         THROW(PERR_InvalidOperation); \
		} \
		if (arg1->value.tag == 0) \
			arg1->value.tag = arg2->value.tag; \
		MyFree(buffer, arg2); \
		PushStack(values, arg1)

	case 5: /* binary * */
		if ((arg1->value.type == TYPE_STR) ^ (arg2->value.type == TYPE_STR))
		{
			/* multiply a string by a number : repeat string */
			DATA8 mem;
			int   mult, len;

			if (arg2->value.type != TYPE_STR)
				arg3 = arg1, arg1 = arg2, arg2 = arg3;

			switch (arg1->value.type) {
			case TYPE_INT32: mult = arg1->value.int32; break;
			case TYPE_INT:   mult = arg1->value.int64; break;
			case TYPE_DBL:   mult = arg1->value.real64; break;
			case TYPE_FLOAT: mult = arg1->value.real32; break;
			default:         mult = 1e8;
			}
			/* prevent from allocating megabyte long string */
			if (mult < 0 || mult > 1000)
				THROW(PERR_InvalidOperation);

			len = strlen(arg2->value.string);
			arg3 = MyCalloc(buffer, sizeof *arg3 + len * mult + 1);
			if (arg3 == NULL) THROW(PERR_NoMem);

			arg3->value.type = TYPE_STR;
			for (arg3->value.string = mem = (DATA8) (arg3 + 1); mult > 0; mult --, mem += len)
				strcpy(mem, arg2->value.string);
			MyFree(buffer, arg1);
			MyFree(buffer, arg2);
			PushStack(values, arg3);
			break;
		}
		MAKE_OP(*=);
		break;
	case 6: /* division / */
		if (IsNull(&arg2->value) && arg2->value.type <= TYPE_INT32)
			/* divide by 0 using integers will cause a CPU exception, but will work on float/double */
			THROW(PERR_DivisionByZero);
		MAKE_OP(/=);
		break;
	case 7: /* modulus % */
		if (IsNull(&arg2->value)) THROW(PERR_DivisionByZero);
		if (arg1->value.type == TYPE_STR || arg2->value.type == TYPE_STR) THROW(PERR_InvalidOperation);
		/* floating point needs to use a function, instead of an operator */
		switch (arg1->value.type) {
		case TYPE_INT32: arg1->value.int32 %= arg2->value.int32; break;
		case TYPE_INT:   arg1->value.int64 %= arg2->value.int64; break;
		case TYPE_DBL:   arg1->value.real64 = fmod(arg1->value.real64, arg2->value.real64); break;
		case TYPE_FLOAT: arg1->value.real32 = fmodf(arg1->value.real32, arg2->value.real32); break;
		default:         THROW(PERR_InvalidOperation);
		}
		if (arg1->value.tag == 0)
			arg1->value.tag = arg2->value.tag;
		MyFree(buffer, arg2);
		PushStack(values, arg1);
		break;
	case 8: /* addition + */
		if (arg1->value.type == TYPE_STR || arg2->value.type == TYPE_STR)
		{
			/* string concatenation instead */
			TEXT number[32];
			Bool invert = False;

			if (arg2->value.type != TYPE_STR)
				arg3 = arg1, arg1 = arg2, arg2 = arg3, invert = True;
			if (arg1->value.type != TYPE_STR)
				ToString(&arg1->value, arg1->value.string = number, sizeof number);

			arg3 = MyCalloc(buffer, sizeof *arg3 + strlen(arg1->value.string) + strlen(arg2->value.string) + 1);
			if (arg3 == NULL) THROW(PERR_NoMem);
			arg3->value.type = TYPE_STR;
			arg3->value.string = (DATA8) (arg3 + 1);
			if (invert) sprintf(arg3->value.string, "%s%s", arg2->value.string, arg1->value.string);
			else        sprintf(arg3->value.string, "%s%s", arg1->value.string, arg2->value.string);
			MyFree(buffer, arg1);
			MyFree(buffer, arg2);
			PushStack(values, arg3);
		}
		else /* normal addition */
		{
			MAKE_OP(+=);
		}
		break;
	case 9: /* subtraction - */
		MAKE_OP(-=);
		break;

	/*
	 * bit shifting operators: need to convert floating points to integer
	 */
#undef	MAKE_OP

/* arg2 cannot be more than 64, storing it into an int will be enough */
#define	MAKE_OP(operator) \
		switch (arg2->value.type) { \
		case TYPE_INT32: nb = arg2->value.int32;break; \
		case TYPE_INT:   nb = arg2->value.int64; break; \
		case TYPE_DBL:   nb = arg2->value.real64; break; \
		case TYPE_FLOAT: nb = arg2->value.real32; break; \
		default:         THROW(PERR_InvalidOperation); \
		} \
\
		switch (arg1->value.type) { \
		case TYPE_INT32: arg1->value.int32  = arg1->value.int32 operator nb; break; \
		case TYPE_INT:   arg1->value.int64  = arg1->value.int64 operator nb; break; \
		case TYPE_DBL:   arg1->value.real64 = (int64_t) arg1->value.real64 operator nb; arg1->value.type = TYPE_INT; break; \
		case TYPE_FLOAT: arg1->value.real32 = (int)     arg1->value.real32 operator nb; arg1->value.type = TYPE_INT32; break; \
		default:         THROW(PERR_InvalidOperation); \
		} \
		if (arg1->value.tag == 0) \
			arg1->value.tag = arg2->value.tag; \
		MyFree(buffer, arg2); \
		PushStack(values, arg1)

	case 10: /* left bit shifting */
		MAKE_OP(<<);
		break;
	case 11: /* right bit shifting */
		MAKE_OP(>>);
		break;
	case 18: /* binary and */
		MAKE_OP(&);
		break;
	case 19: /* binary xor */
		MAKE_OP(^);
		break;
	case 20: /* binary or */
		MAKE_OP(|);
		break;

	/*
	 * comparison operators
	 */
#undef	MAKE_OP
#define	MAKE_OP(operator) \
		Promote(arg1, arg2); \
		switch (arg1->value.type) { \
		case TYPE_INT32: arg1->value.int32 = arg1->value.int32  operator arg2->value.int32; break; \
		case TYPE_INT:   arg1->value.int64 = arg1->value.int64  operator arg2->value.int64; break; \
		case TYPE_DBL:   arg1->value.int64 = arg1->value.real64 operator arg2->value.real64; arg1->value.type = TYPE_INT; break; \
		case TYPE_FLOAT: arg1->value.int32 = arg1->value.real32 operator arg2->value.real32; arg1->value.type = TYPE_INT32; break; \
		case TYPE_STR:   arg1->value.int64 = CompareString(arg1, arg2) operator 0;           arg1->value.type = TYPE_INT; break; \
		default:         THROW(PERR_InvalidOperation); \
		} \
		arg1->value.tag = 0; \
		MyFree(buffer, arg2); \
		PushStack(values, arg1)

	case 12:
		MAKE_OP(<);
		break;
	case 13:
		MAKE_OP(>);
		break;
	case 14:
		MAKE_OP(<=);
		break;
	case 15:
		MAKE_OP(>=);
		break;
	case 16:
		MAKE_OP(==);
		break;
	case 17:
		MAKE_OP(!=);
		break;

#undef MAKE_OP

	case 21: nb = ! IsNull(&arg1->value) && ! IsNull(&arg2->value); goto case_XY; /* &&: logical and */
	case 22: nb = ! IsNull(&arg1->value) || ! IsNull(&arg2->value); /* ||: logical or */
	case_XY:
		if (appcfg.use64b)
		{
			arg1->value.type  = TYPE_INT;
			arg1->value.int64 = nb;
		}
		else
		{
			arg1->value.type  = TYPE_INT32;
			arg1->value.int32 = nb;
		}
		PushStack(values, arg1);
		MyFree(buffer, arg2);
		break;
	case 23: /* ternary ? : */
		if (IsNull(&arg1->value))
			PushStack(values, arg3), MyFree(buffer, arg2);
		else
			PushStack(values, arg2), MyFree(buffer, arg3);
		MyFree(buffer, arg1);
		break;

	case 25: /* assignment = */
		if (arg1->value.type != TYPE_IDF) THROW(PERR_LValueNotModifiable);
		AffectArg(arg2, cb, data);
		cb(arg1->value.string, &arg2->value, 1, data);
		// no break;
	case 36: /* separator , */
		MyFree(buffer, arg1);
		PushStack(values, arg2);
		break;
	case 3: /* increment ++ */
	case 4: /* decrement -- */
		arg2 = NewNumber(buffer, TYPE_INT, 1UL);
		// no break;
	case 26: case 27: case 28: case 29: case 30: // *=, /=, %=, +=, -=
	case 31: case 32: case 33: case 34: case 35: // <<=, >>=, &=, ^=, |=
		nb = ope - OperatorList;
		/* handle this by splitting assignment and operation */
		if (arg1->value.type != TYPE_IDF) THROW(PERR_LValueNotModifiable);
		arg3 = MyCalloc(buffer, sizeof *arg3);
		memcpy(arg3, arg1, sizeof *arg3);
		PushStack(values, arg1);
		PushStack(values, arg3);
		PushStack(values, arg2);
		/* first push is assignment, second push is operator */
		PushStack(oper, NewOperator(buffer, OperatorList + 25));
		PushStack(oper, NewOperator(buffer, OperatorList + nb - (nb < 24 ? -5 : nb < 31 ? 21 : 15)));
		MakeOp(buffer, values, oper, cb, data);
		error = MakeOp(buffer, values, oper, cb, data);
	}
	return error;

	error_case:
	if (arg1) MyFree(buffer, arg1);
	if (arg2) MyFree(buffer, arg2);
	if (arg3) MyFree(buffer, arg3);
	return error;
}

#if 0
/* arrays behave differently from scalar */
static int MakeOpArray(DATA8 buffer, Stack * values, Stack * oper, ParseExpCb cb, APTR data)
{
	Stack ope = *oper;

	if (ope->value.ope == &arrayStart)
	{
		/* constructor */
		int   count = ope->value.tag >> 4;
		Stack array = MyCalloc(buffer, sizeof *array);
		Stack value;

		/* memory for array must be malloced: it can be resized */
		array->value.array = malloc(sizeof (Variant) * count);
		array->value.type = TYPE_ARRAY;
		array->value.tag = ope->value.tag; /* item count actually */

		for (count --; *values && count >= 0; count --)
		{
			value = PopStack(values);
			array->value.array[count] = &value->value;
		}

		if (count < 0)
		{
			PushStack(values, array);
			return 0;
		}
		/* shouldn't happen: the code is buggy if it gets here */
		return PERR_MissingOperand;
	}
	else if (ope->value.ope == &arrayEnd) /* dereference */
	{
		*oper = ope->next;
		MyFree(buffer, ope);
		ope = *oper;
		Stack value = PopStack(values);

		if (value == NULL)
			/* expression was something like "array[]": you need a number in those bracket */
			return PERR_MissingOperand;

		/* convert index to integer */
		int index;

		if (value->value.type == TYPE_IDF)
			AffectArg(value, cb, data);

		switch (value->value.type) {
		case TYPE_INT:   index = value->value.int64; break;
		case TYPE_INT32: index = value->value.int32; break;
		case TYPE_DBL:   index = value->value.real64; break;
		case TYPE_FLOAT: index = value->value.real32; break;
		default:         index = -1; /* number and nothing else */
		}
		MyFree(buffer, value);
		value = PopStack(values);
		Bool ext = False;
		if (value->value.type == TYPE_IDF)
			AffectArg(value, cb, data), ext = True;

		if (value->value.type == TYPE_ARRAY)
		{
			int count = value->value.tag >> 4;
			/* bounds checking, unlike C */
			if (0 <= index && index < count)
			{
				Variant item = value->value.array[index];
				if (ext)
				{
					/* need to duplicate object */
					value = MyCalloc(buffer, sizeof *value);
					value->value = *item;
					PushStack(values, value);
				}
				else
				{
					value->value.array[index] = NULL;
					MyFree(buffer, value);
					PushStack(values, (Stack) ((DATA8) item - offsetp(Stack, value)));
				}
				return 0;
			}
		}
	}
	return PERR_InvalidOperation;
}
#endif

int IsKeyword(DATA8 * start); /*  from script.c */

int ParseExpression(DATA8 exp, ParseExpCb cb, APTR data)
{
	uint8_t  buffer[SZ_POOL];
	Operator ope;
	DATA8    next;
	int      curpri, pri, error, tok;
	Stack    values, oper, object;

	buffer[0] = (SZ_POOL-4) >> 8;
	buffer[1] = (SZ_POOL-4) & 0xff;
	buffer[2] = 0;
	buffer[3] = 0;

	GetNumber = appcfg.use64b ? GetNumber64 : GetNumber32;

	for (curpri = error = tok = 0, values = oper = NULL, next = exp; error == 0 && *exp && *exp != ';'; exp = next)
	{
		switch (GetToken(buffer, &object, &next)) {
		case TOKEN_SCALAR: /* number => stack it */
			if (tok == TOKEN_SCALAR) THROW(PERR_SyntaxError);
			if (object->value.type == TYPE_IDF && cb == ByteCodeGenExpr)
			{
				DATA8 kwd = object->value.string;
				if (IsKeyword(&kwd) > 0)
					goto error_case;
			}
			tok = TOKEN_SCALAR;
			PushStack(&values, object);
			break;
		case TOKEN_DECPRI:
			if (tok == TOKEN_OPERATOR) THROW(PERR_SyntaxError);
			curpri -= 30;
			if (curpri < 0) THROW(PERR_TooManyClosingParens);
			break;
		#if 0
		case TOKEN_ARRAYSTART:
			if (values && (values->value.type == TYPE_IDF || values->value.type == TYPE_ARRAY))
				/* dereference */
				ope = &arrayEnd;
			else
				/* create array */
				ope = &arrayStart;
			goto case_OPE;
		case TOKEN_ARRAYEND:
			/* build an array or dereference */
			MakeOpArray(buffer, &oper, &values, cb, data);
			break;
		#endif
		case TOKEN_INCPRI:
			if (tok == TOKEN_SCALAR)
			{
				/* if last token was an ident, and here found a '(' == function call */
				if (values->value.type == TYPE_IDF)
				{
					object = NewOperator(buffer, &functionCall);
					values->value.type = TYPE_FUN; /* function instead */
					// no break
				}
				else THROW(PERR_SyntaxError);
			}
			else { curpri += 30; break; }
		case TOKEN_OPERATOR:
			ope = object->value.ope;
			pri = curpri + ope->priority - (ope->associativity == LEFT);

			/* check if it is a binary '-' instead */
			if (ope == OperatorList && tok == TOKEN_SCALAR)
				ope = object->value.ope = binaryMinus, pri = 11 + curpri;

			/*
			 * evaluation of expression happens here: keep operator stack in increasing priority.
			 * this is the core of the shunting-yard algorithm.
			 */
			while (error == 0 && oper && pri < oper->value.type)
				error = MakeOp(buffer, &values, &oper, cb, data);

			if (error) { MyFree(buffer, object); break; }

			tok = TOKEN_OPERATOR;
			object->value.type = curpri + ope->priority;

			if (ope == ternaryRight) /* <c> clause of a?b:c */
			{
				if (oper == NULL || oper->value.ope != ternaryLeft) /* misplaced : */
					error = PERR_SyntaxError;
				else
					oper->value.tag = ! oper->value.tag;
				MyFree(buffer, object);
				break;
			}
			else if (ope == ternaryLeft || ope == logicalAnd) /* <b> clause of a?b:c or <b> clause of a&&b */
			{
				if (values->value.type == TYPE_IDF) AffectArg(values, cb, data);
				object->value.tag = ! IsNull(&values->value);
			}
			else if (ope == logicalOr) /* <b> clause of a||b */
			{
				if (values->value.type == TYPE_IDF) AffectArg(values, cb, data);
				object->value.tag = IsNull(&values->value);
			}
			if (ope == &functionCall) curpri += 30;
			if (ope == commaSeparator)
			{
				#if 0
				if (oper->value.ope == &arrayStart)
					/* building an array: keep track of number of items */
					oper->value.tag += 1<<4;
				#endif

				/* ',' - never stacked */
				MyFree(buffer, object);
			}
			else PushStack(&oper, object);
		case TOKEN_END:
			break;
		default:
			error = PERR_SyntaxError;
		}
	}
	error_case:
	if (cb == ByteCodeGenExpr)
	{
		/* error recovery similar to javascript */
		if (error == PERR_SyntaxError)
			error = 0;
		/* this is the last character we manage to parse */
		((ByteCode) data)->exp = exp;
	}

	while (error == 0 && oper)
		error = MakeOp(buffer, &values, &oper, cb, data);

	if (cb == ByteCodeGenExpr)
	{
		if (values)
			ByteCodeAddVariant(data, &values->value);
	}
	else if (error == 0 && values)
	{
		/* final result */
		VariantBuf v;
		if (values->value.type == TYPE_IDF)
			AffectArg(values, cb, data);
		v = values->value;
		cb(NULL, &v, 0, data);
	}
	while (oper)   MyFree(buffer, PopStack(&oper));
	while (values) MyFree(buffer, PopStack(&values));
	return error;
}

#define ROUNDTO    512

DATA8 ByteCodeAdd(ByteCode bc, int size)
{
	DATA8 mem;
	if (bc->size + size > bc->max)
	{
		int max = (bc->size + size + ROUNDTO - 1) & ~(ROUNDTO-1);
		mem = realloc(bc->code, max);
		if (mem == NULL) return NULL;
		bc->code = mem;
		bc->max  = max;
	}
	mem = bc->code + bc->size;
	bc->size += size;
	return mem;
}

void ByteCodeAddVariant(ByteCode bc, Variant v)
{
	APTR arg;
	int  size;
	switch (v->type) {
	case TYPE_INT:    arg = &v->int64;  size = 8; break;
	case TYPE_INT32:  arg = &v->int32;  size = 4; break;
	case TYPE_DBL:    arg = &v->real64; size = 8; break;
	case TYPE_FLOAT:  arg = &v->real32; size = 4; break;
	case TYPE_STR:
	case TYPE_IDF:    arg = v->string;  size = strlen(v->string)+1; break;
	case TYPE_FUN:    arg = NULL; size = 0; break; // TODO
	default:          return;
	}
	size += 3;
	DATA8 mem = ByteCodeAdd(bc, size);
	mem[0] = v->type;
	mem[1] = size >> 8;
	mem[2] = size & 0xff;
	memcpy(mem+3, arg, size-3);
}

/* generate byte code from expression */
void ByteCodeGenExpr(STRPTR exp, Variant argv, int arity, APTR data)
{
	int i = (Operator) argv->ope - OperatorList;
	/* first: add operator */
	DATA8 mem = ByteCodeAdd(data, 2);
	mem[0] = TYPE_OPE;
	mem[1] = i;
	for (i = 1; i <= arity; i ++)
		ByteCodeAddVariant(data, argv + i);
}

#ifdef KALC_DEBUG
DATA8 ByteCodeDebug(DATA8 start, DATA8 end)
{
	while (start < end)
	{
		VariantBuf buf;
		Operator ope;
		/* first byte: Variant type (TYPE_*) */
		switch (start[0]) {
		case 255: return start + 1;
		case TYPE_OPE:
			ope = OperatorList + start[1];
			fprintf(stderr, "%s ", ope->token);
			start += 2;
			continue;
		case TYPE_INT:
			memcpy(&buf.int64, start + 3, 8);
			fprintf(stderr, "%I64d ", buf.int64);
			break;
		case TYPE_INT32:
			memcpy(&buf.int32, start + 3, 4);
			fprintf(stderr, "%d ", buf.int32);
			break;
		case TYPE_DBL:
			memcpy(&buf.real64, start + 3, 8);
			fprintf(stderr, "%gd ", buf.real64);
			break;
		case TYPE_FLOAT:
			memcpy(&buf.real32, start + 3, 4);
			fprintf(stderr, "%g ", buf.real32);
			break;
		case TYPE_STR:
			fprintf(stderr, "\"%s\" ", start + 3);
			break;
		case TYPE_IDF:
			fprintf(stderr, "%s ", start + 3);
		}
		start += (start[1] << 8) | start[2];
	}
	return start;
}
#endif
