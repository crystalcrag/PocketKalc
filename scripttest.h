/*
 * scripttest.h: unit tests for scripting language.
 *
 * written by T.Pierron, june 2022
 */


DATA8 ByteCodeDebug(DATA8 start, DATA8 end);

static void scriptDebug(ByteCode bc)
{
	DATA8 inst, eof;
	int arg;

	/* scriptToByteCode() will turn a structured program into a linear flow of instructions */
	for (inst = bc->code, eof = inst + bc->size, arg = 0; inst < eof; )
	{
		if (arg == 0)
			fprintf(stderr, "%3d: ", inst - bc->code);
		/* first byte: opcode */
		switch (inst[0]) {
		case STOKEN_IF:
			fprintf(stderr, "if (jump if fail: %d) ", (inst[1] << 8) | inst[2]);
			arg = 1;
			break;
		case STOKEN_EXPR:
			inst = ByteCodeDebug(inst+1, eof);
			if (arg >  0) arg --;
			if (arg == 0) fputc('\n', stderr);
			continue;
		case STOKEN_GOTO:
			fprintf(stderr, "goto %d\n", (inst[1] << 8) | inst[2]);
			arg = 0;
			break;
		case STOKEN_EXIT:
			fprintf(stderr, "exit\n");
			arg = 0;
			break;
		case STOKEN_RETURN:
			fprintf(stderr, "return ");
			arg = 1;
			break;
		case STOKEN_PRINT:
			fprintf(stderr, "print ");
			arg = 1;
		}
		if (tokenSize[inst[0]] == 0)
		{
			fprintf(stderr, "incorrect token %d: aborting\n", inst[0]);
			break;
		}
		inst += tokenSize[inst[0]];
	}
	fprintf(stderr, "%3d:\n", bc->size);
}

/* simple unit tests */
void scriptTest(void)
{
	static STRPTR prog[] = {
		/* PROG0 - syntax check */
		"IF A != 0 THEN\n"
		"	WHILE A > 0 DO\n"
		"		PRINT A\n"
		"		A --\n"
		"	END\n"
		"ELSEIF B != 0 THEN\n"
		"	PRINT B\n"
		"ELSE\n"
		"	PRINT C\n"
		"END",

		/* PROG1 - syntax check */
		"A = 0\n"
		"LABEL:\n"
		"	PRINT A\n"
		"	A ++\n"
		"	IF A == 10 THEN EXIT END\n"
		"GOTO LABEL",

		/* PROG2 - fizz buzz */
		"# FIZZ-BUZZ\n"
		"A = 1\n"
		"WHILE A < 100 DO\n"
		"	IF A % 5 THEN PRINT \"BUZZ\"\n"
		"   ELSEIF A % 3 THEN PRINT \"FIZZ\"\n"
		"   ELSEIF A % 15 THEN PRINT \"FIZZ \"; PRINT \"BUZZ\"\n"
		"   ELSE PRINT A\n"
		"	A ++\n"
		"END",

		/* PROG3 - fibonacci number */
		"J = 1; K = 1\n"
		"IF N == 0 THEN RETURN 0\n"
		"WHILE K < N DO\n"
		"	T = I + J\n"
		"	I = J\n"
		"	J = T\n"
		"   K ++\n"
		"END\n"
		"RETURN J"

	};
	/* sample programs converted to byte code (note: suppose little endian and use64b enabled) */
	static uint8_t byteCode[] = {
		111,
		/* PROG0 */
		STOKEN_IF, 0, 69, STOKEN_EXPR, TYPE_OPE, 0x11, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 0,0,0,0,0,0,0,0, 0xff,
		STOKEN_IF, 0, 66, STOKEN_EXPR, TYPE_OPE, 0x0d, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 0,0,0,0,0,0,0,0, 0xff,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_IDF, 0, 5, 'A', 0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 0x04, TYPE_IDF, 0, 5, 'A', 0, 0xff,
		STOKEN_GOTO, 0x00, 0x17,
		STOKEN_GOTO, 0x00, 0x6f,
		STOKEN_IF,   0x00, 0x67, STOKEN_EXPR, TYPE_OPE, 0x11, TYPE_IDF, 0, 5, 'B', 0, TYPE_INT, 0, 11, 0,0,0,0,0,0,0,0, 0xff,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_IDF, 0, 5, 'B', 0, 0xff,
		STOKEN_GOTO, 0x00, 0x6f,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_IDF, 0, 5, 'C', 0, 0xff,

		64,
		/* PROG1 */
		STOKEN_EXPR, TYPE_OPE, 0x19, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 0,0,0,0,0,0,0,0, 0xff,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_IDF, 0, 5, 'A', 0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 0x03, TYPE_IDF, 0, 5, 'A', 0, 0xff,
		STOKEN_IF, 0, 0x3d, STOKEN_EXPR, TYPE_OPE, 16, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 10,0,0,0,0,0,0,0, 0xff,
		STOKEN_EXIT,
		STOKEN_GOTO, 0, 0x14,

		186,
		/* PROG2 */
		STOKEN_EXPR, TYPE_OPE, 0x19, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 1,0,0,0,0,0,0,0, 0xff,
		STOKEN_IF, 0, 186, STOKEN_EXPR, TYPE_OPE, 12, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 100,0,0,0,0,0,0,0, 0xff,
		STOKEN_IF, 0, 80, STOKEN_EXPR, TYPE_OPE, 7, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 5,0,0,0,0,0,0,0, 0xff,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_STR, 0, 8, 'B', 'U', 'Z', 'Z', 0, 0xff,
		STOKEN_GOTO, 0, 174,
		STOKEN_IF, 0, 117, STOKEN_EXPR, TYPE_OPE, 7, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 3,0,0,0,0,0,0,0, 0xff,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_STR, 0, 8, 'F', 'I', 'Z', 'Z', 0, 0xff,
		STOKEN_GOTO, 0, 174,
		STOKEN_IF, 0, 166, STOKEN_EXPR, TYPE_OPE, 7, TYPE_IDF, 0, 5, 'A', 0, TYPE_INT, 0, 11, 15,0,0,0,0,0,0,0, 0xff,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_STR, 0, 9, 'F', 'I', 'Z', 'Z', ' ', 0, 0xff,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_STR, 0, 8, 'B', 'U', 'Z', 'Z', 0, 0xff,
		STOKEN_GOTO, 0, 174,
		STOKEN_PRINT, STOKEN_EXPR, TYPE_IDF, 0, 5, 'A', 0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 3, TYPE_IDF, 0, 5, 'A', 0, 0xff,
		STOKEN_GOTO, 0, 20,

		163,
		/* PROG3 */
		STOKEN_EXPR, TYPE_OPE, 0x19, TYPE_IDF, 0, 5, 'J', 0, TYPE_INT, 0, 11, 1,0,0,0,0,0,0,0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 0x19, TYPE_IDF, 0, 5, 'K', 0, TYPE_INT, 0, 11, 1,0,0,0,0,0,0,0, 0xff,
		STOKEN_IF, 0, 77, STOKEN_EXPR, TYPE_OPE, 16, TYPE_IDF, 0, 5, 'N', 0, TYPE_INT, 0, 11, 0,0,0,0,0,0,0,0, 0xff,
		STOKEN_RETURN, STOKEN_EXPR, TYPE_INT, 0, 11, 0,0,0,0,0,0,0,0, 0xff,
		STOKEN_IF, 0, 155, STOKEN_EXPR, TYPE_OPE, 12, TYPE_IDF, 0, 5, 'K', 0, TYPE_IDF, 0, 5, 'N', 0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 8, TYPE_IDF, 0, 5, 'I', 0, TYPE_IDF, 0, 5, 'J', 0, TYPE_OPE, 25, TYPE_IDF, 0, 5, 'T', 0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 25, TYPE_IDF, 0, 5, 'I', 0, TYPE_IDF, 0, 5, 'J', 0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 25, TYPE_IDF, 0, 5, 'J', 0, TYPE_IDF, 0, 5, 'T', 0, 0xff,
		STOKEN_EXPR, TYPE_OPE, 3, TYPE_IDF, 0, 5, 'K', 0, 0xff,
		STOKEN_GOTO, 0, 77,
		STOKEN_RETURN, STOKEN_EXPR, TYPE_IDF, 0, 5, 'J', 0, 0xff,
	};

	struct ProgByteCode_t program;
	DATA8 code;
	int i;
	for (i = 0, code = byteCode; i < DIM(prog); i ++, code += code[0] + 1)
	{
		memset(&program, 0, sizeof program);
		scriptToByteCode(&program, prog[i]);

		/* check if it matches what we expected */
		if (program.errCode > 0)
		{
			fprintf(stderr, "PROG%d: error %d on line %d\n", i, program.errCode, program.line);
		}
		else if (code[0] != program.bc.size)
		{
			scriptDebug(&program.bc);
			fprintf(stderr, "PROG%d: byte code size differs: expected: %d, got: %d\n", i, code[0], program.bc.size);
		}
		else
		{
			DATA8 s, d;
			int   n;
			for (s = code + 1, n = code[0], d = program.bc.code; n > 0 && *s == *d; d ++, s ++, n --);
			if (n > 0)
			{
				scriptDebug(&program.bc);
				fprintf(stderr, "PROG%d: byte code differs at offset %d: %02x != %02x\n", i, d - program.bc.code, *s, *d);
				break;
			}
			else fprintf(stderr, "PROG%d test passed\n", i);
		}

		free(program.bc.code);
	}
}
