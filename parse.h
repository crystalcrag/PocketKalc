

#ifndef EXPR_PARSE_H
#define EXPR_PARSE_H

enum /* Return codes of ParseExpression */
{
	PERR_SyntaxError = 1,
	PERR_DivisionByZero,
	PERR_LValueNotModifiable,
	PERR_TooManyClosingParens,
	PERR_MissingOperand,
	PERR_InvalidOperation,
	PERR_NoMem
};

typedef struct Variant_t *       Variant;
typedef struct Variant_t         VariantBuf;
typedef struct Result_t *        Result;
typedef struct Unit_t *          Unit;

typedef enum /* possible values for 'Variant_t.type' field */
{
	TYPE_INT,
	TYPE_INT32,
	TYPE_DBL,
	TYPE_FLOAT,
	TYPE_IDF,
	TYPE_STR,
	TYPE_ARRAY,
	TYPE_OPE,
	TYPE_FUN,
	TYPE_ERR
}	TYPE;


struct Variant_t
{
	TYPE type;                   /* TYPE_* */
	int  tag;                    /* padding bytes */
	union {
		int64_t   int64;
		int       int32;
		double    real64;
		float     real32;
		STRPTR    string;
		Variant * array;
		APTR      ope;
	};
};

#define MAX_VAR_NAME             32

struct Result_t
{
	VariantBuf bin;              /* out */
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

int  ParseExpression(DATA8 exp, ParseExpCb cb, APTR data);
int  evalExpr(STRPTR expr, ParseExprData data);
void formatResult(Variant v, STRPTR varName, STRPTR out, int max);
void freeAllVars(void);
void parseExpr(STRPTR name, Variant v, int store, APTR data);
void ToString(Variant, DATA8 out, int max);

extern struct Unit_t units[];
extern int firstUnits[];

#endif
