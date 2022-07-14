

#ifndef KALC_PARSE_H
#define KALC_PARSE_H

enum /* Return codes of ParseExpression */
{
	PERR_SyntaxError = 1,
	PERR_DivisionByZero,
	PERR_LValueNotModifiable,
	PERR_TooManyClosingParens,
	PERR_MissingOperand,
	PERR_InvalidOperation,
	PERR_IndexOutOfRange,
	PERR_NoMem,
	PERR_UnknownFunction,
	PERR_LastError
};

/* to get a string from PERR_* enum */
extern STRPTR errorMessages[];

typedef struct Variant_t *       Variant;
typedef struct Variant_t         VariantBuf;
typedef struct Result_t *        Result;
typedef struct ByteCode_t *      ByteCode;
typedef struct Unit_t *          Unit;

typedef enum /* possible values for 'Variant_t.type' field */
{
	TYPE_INT,
	TYPE_INT32,
	TYPE_DBL,
	TYPE_FLOAT,
	TYPE_STR,
	TYPE_ARRAY,
	TYPE_IDF,
	TYPE_OPE,
	TYPE_FUN,
	TYPE_ERR,
	TYPE_VOID
}	TYPE;

#define TYPE_SCALAR              TYPE_STR

struct Variant_t
{
	TYPE type;                   /* TYPE_* */
	union {
		int eval;                /* TYPE_OPE */
		int unit;                /* TYPE_INT - TYPE_FLOAT */
		int lengthFree;          /* TYPE_STRING, TYPE_ARRAY */
	};
	union {
		int64_t int64;
		int     int32;
		double  real64;
		float   real32;
		STRPTR  string;
		Variant array;
		APTR    ope;
	};
};

#define MAX_VAR_NAME             32
#define VAR_LENGTH(variant)      ((variant)->lengthFree & 0x0fffffff)
#define VAR_TOFREE(variant)      ((variant)->lengthFree & 0x10000000)
#define VAR_SETFREE(variant)     ((variant)->lengthFree |= 0x10000000)


struct Result_t
{
	VariantBuf bin;              /* out */
	int frame;                   /* prevent var from being displayed twice */
	TEXT name[MAX_VAR_NAME];     /* as stored in symbol table */
};

struct Unit_t
{
	uint8_t cat;
	uint8_t id;
	uint8_t conv;
	STRPTR  name;
	STRPTR  suffix;
	double  toMetricA;
	double  toMetricB;
	APTR    widget;
};

struct ByteCode_t
{
	DATA8   code, exp;
	int     max, size;
};

enum /* possible values for Unit_t.cat */
{
	UNIT_DIST,
	UNIT_TEMP,
	UNIT_MASS,
	UNIT_ANGLE,
	UNIT_EOF
};

enum /* possible values for Unit_t.conv */
{
	CONV_SISUFFIX = 1,
	CONV_NEXTUNIT = 2
};

enum
{
	FORMAT_DEFAULT,
	FORMAT_DEC,
	FORMAT_HEX,
	FORMAT_BIN,
	FORMAT_OCT,
};


typedef void (*ParseExpCb)(STRPTR, Variant, int store, APTR data);
typedef void (*FormatResult)(Variant, STRPTR varName);

typedef struct ParseExprData_t *       ParseExprData;
struct ParseExprData_t
{
	FormatResult cb;
	VariantBuf   res;
	STRPTR       assignTo;
};

int   ParseExpression(DATA8 exp, ParseExpCb cb, APTR data);
int   evalExpr(STRPTR expr, ParseExprData data);
void  formatResult(Variant v, STRPTR varName, STRPTR out, int max);
void  freeAllVars(void);
void  parseExpr(STRPTR name, Variant v, int store, APTR data);
void  ToString(Variant, DATA8 out, int max);
void  ByteCodeGenExpr(STRPTR unused, Variant v, int arity, APTR data);
DATA8 ByteCodeAdd(ByteCode bc, int size);
Bool  ByteCodeExe(DATA8 start, DATA8 * end, Bool isTrue, ParseExpCb cb, APTR data);

extern struct Unit_t units[];
extern int firstUnits[];

#endif
