#include <stdio.h>

/*
 * HP-85 BASIC Detokenizer - converts binary BASIC program to ASCII
 *
 *      Based on code written by Leif Jon Harcke (http://web.archive.org/web/20130320012825/http://rocknroll.stanford.edu/~lharcke/)
 *
 *      Version 1.0 (7.9.19)
 *
 *      Vassilis Prevelakis, AEGIS IT RESEARCH UG
 */

/*
 * common definitions
 */
#define NS_STR_LEN 256

/*
 * output format for one or two argument functions
 *	When we have to print the following:
 *	term1 term2 op
 *	we need to know whether we should print
 *	MOVE X, Y
 *	X + Y
 *	VAL$(X)   (ok this has only one terminal but you get the idea)
 *
 *	So we need to specify the format appropriate for the "op"
 */


/*
 * nargs = represents two things
 *	arguments after the opcode (e.g. INTERGER 7)
 *	and an operation involving arguments that
 *	have been pushed in the stack (e.g. +)
 *
 *	- so if nargs > 0, then nargs is the number of
 *	  arguments FOLLOWING the command (i.e. in the
 *	  tk_args buffer of the token descriptor struct.
 *	- if nargs = 0, then the command has no
 *	  arguments (e.g. END)
 *	- if nargs < 0 then its is the number of
 *	  arguments that need to be poped from the
 *	  stack in order to perform the operation
 *	  described by the current token.
 */

#define OP_UNKNOWN		0
#define OP_IGNORE		1
#define OP_EOL			2
#define OP_ND02			3
#define OP_ND1			4
#define OP_ND2			5
#define OP_NDN			6
#define OP_ND3			7
#define OP_IFGOTO		8
#define OP_IFTHEN		9
#define OP_IFELSE		10
#define OP_ASGN_MULT		12
#define OP_TERM			13	// terminal symbol

/*
 * formatting commands
 */
#define MODE_NULL	0x000000
#define MODE_COMMA	0x000001
#define MODE_INFIX	0x000020
#define MODE_SPACE_TR	0x000040
#define MODE_SPACE_ARG	0x000080
#define MODE_BRACKETS	0x000100
#define MODE_FUNCTION	0x000200
#define MODE_DSP_ARG	0x000400
#define MODE_SQ_BRACK	0x000800
#define MODE_TRIM_STR	0x001000
#define MODE_TRIM_ARG	0x002000
#define MODE_STR	0x004000
#define MODE_TAB_COMMA	0x008000  /* this does not work!!! */

/*
 * some pre-packaged defs - you can use these, or create your own.
 */
#define FMT_DFLT        /* MOVE X, Y */		(MODE_COMMA | MODE_TRIM_STR)
#define FMT_NOCOMMA     /* ASSIGN# 1 TO A$ */	(MODE_SPACE_TR | MODE_TRIM_STR | MODE_SPACE_ARG)
#define FMT_INFIX       /* (X+Y) */		(MODE_INFIX | MODE_BRACKETS | MODE_TRIM_STR)
#define FMT_INFIXSP     /* (X AND Y) */		(MODE_INFIX | MODE_SPACE_ARG | MODE_BRACKETS | MODE_TRIM_STR | MODE_TRIM_ARG)
#define FMT_INFNB       /* X=Y */		(MODE_INFIX | MODE_TRIM_STR | MODE_TRIM_ARG)
#define FMT_FUNC        /* VAL$(X) */		(MODE_FUNCTION | MODE_BRACKETS | MODE_COMMA | MODE_TRIM_STR | MODE_TRIM_ARG)
#define FMT_ARG         /* GOTO 2345 */		(MODE_DSP_ARG | MODE_SPACE_TR | MODE_TRIM_STR | MODE_SPACE_ARG | MODE_TRIM_ARG)
#define FMT_NOTRIM      /* leav b4re/aftr sp */	(MODE_NULL)
#define FMT_SPACE       /* 1 trailing space */	(MODE_SPACE_TR | MODE_TRIM_STR)
#define FMT_NUM         /* numeric array */	(MODE_BRACKETS | MODE_TRIM_STR )
#define FMT_STR         /* string array */	(MODE_BRACKETS | MODE_TRIM_STR | MODE_SQ_BRACK )


struct bas_cmd_tbl {
	int		bct_opcode;
	int		bct_action;	// what action to take
	int		bct_nargs;
	int		bct_fmt;	// output format
	const char	*bct_name;
};


/*
 * hp-85 mainframe commands
 */

#define CMD_NONE        0x100
#define CMD_ASSIGN_STR  0x07
#define CMD_ASSIGN_NUM	0x08
#define CMD_REAL        0x04
#define CMD_ON          0x66
#define CMD_DIM         0x88
#define CMD_DATA        0x86
#define CMD_MULASG      0x14
#define CMD_AT          0x40
#define CMD_EOL		0x0E
#define CMD_ONKEY	0x43
#define CMD_INPUT	0x5F
#define CMD_IFTHEN	0x1B
#define CMD_OPTBASE	0x84
#define CMD_INTEGER	0x7F
#define CMD_FN_DEREF	0x80
#define CMD_FN		0x16
#define CMD_1DARRAY_O	0x22
#define CMD_1DARRAY_I	0x24
#define CMD_2DARRAY	0xF4
#define CMD_SEMICOL_ED	0xED
#define CMD_COMMA_EE	0xEE
#define CMD_SEMICOL_EF	0xEF
#define CMD_SEMICOL_27	0x27
#define CMD_COMMA_F0	0xF0
#define CMD_READ	0x6E
#define CMD_READ_SEP	0xE6
#define CMD_NEWLINE	0xEC
#define CMD_READ_CH	0x50
#define CMD_ARRAY_VAR	0xF3

#define CMD_MULASG2     (CMD_MULASG | 0x100)

struct bas_cmd_tbl bas_tbl[] = {
	0x00,	OP_ND2,		1,	FMT_NUM | MODE_COMMA,		"POS",
	0x01,	OP_TERM,	1,	MODE_TRIM_STR,			"scalar variable",
	0x02,	OP_TERM,	1,	MODE_TRIM_STR,			"array variable",
	0x03,	OP_TERM,	1,	MODE_TRIM_STR | MODE_STR,	"string variable",
	0x04,	OP_TERM,	1,	MODE_TRIM_STR,			"real",		/* 8 byte real number */
	0x05,	OP_TERM,	1,	MODE_TRIM_STR,			"string5",
	0x06,	OP_TERM,	1,	MODE_TRIM_STR,			"string6",
	0x07,	OP_ND2,		-2,	FMT_INFNB,			":=",		// string assignment
	0x08,	OP_ND2,		-2,	FMT_INFNB,			":=",		// numeric assignment
	0x09,	OP_ND2,		-2,	FMT_NUM,			"",		// 1d integer? array subscript
	0x0A,	OP_ND3,		-3,	FMT_NUM | MODE_COMMA,		"",		// 2d integer? array subscript
	0x0B,	OP_ND2,		-2,	FMT_NUM,			"",		// 1d real? array subscript
	0x0C,	OP_ND3,		-3,	FMT_NUM | MODE_COMMA,		"",		// 2d real? array subscript
	0x0E,	OP_EOL,		0,	FMT_DFLT,			"end line",
	0x11,	OP_TERM,	1,	FMT_DFLT,			"scalar variable address",
	0x12,	OP_TERM,	1,	FMT_DFLT,			"array variable address",
	0x13,	OP_TERM,	1,	FMT_DFLT | MODE_DSP_ARG | MODE_STR,	"string variable address>",
	0x14,	OP_ASGN_MULT,	-2,	FMT_DFLT,			":=",		// store numeric, multiple assignment?
	0x16,	OP_TERM,	1,	MODE_BRACKETS | MODE_DSP_ARG | MODE_TRIM_ARG | MODE_TRIM_STR,	"FN",	// FN call
	0x18,	OP_IFGOTO,	-1,	FMT_ARG,			"IF .. GOTO (arithmetic?)",
	0x19,	OP_TERM,	0,	FMT_DFLT,			"END",
	0x1A,	OP_TERM,	1,	FMT_DFLT,			"integer",
	0x1B,	OP_IFTHEN,	0,	FMT_DFLT,			"IF .. THEN",
	0x1C,	OP_TERM,	0,	MODE_NULL,			" ELSE",
	0x1D,	OP_ND2,		-2,	FMT_STR,			"", 		// 1d string? array subscript
	0x1E,	OP_ND3,		-3,	FMT_STR | MODE_COMMA,		"",		// 2d string? array subscript
	0x1F,	OP_TERM,	1,	FMT_DFLT,			"silent GOTO",
	0x22,	OP_ND1,		0,	FMT_DFLT,			"", 		// () 1 out, e.g. A(), used in PRINT# 1; A()
	0x24,	OP_ND1,		0,	FMT_DFLT,			"",		// () 1 in, e.g. A(), used in READ# 1; A()
	0x26,	OP_ND2,		-2,	MODE_INFIX | MODE_TRIM_STR,	"&",
	0x27,	OP_TERM,	0,	FMT_SPACE,			";",
	0x2A,	OP_ND2,		-2,	FMT_INFIX,			"*",
	0x2B,	OP_ND2,		-2,	FMT_INFIX,			"+",
	0x2D,	OP_ND2,		-2,	FMT_INFIX,			"-",
	0x2F,	OP_ND2,		-2,	FMT_INFIX,			"/",
	0x30,	OP_ND2,		-2,	FMT_INFIX,			"^",		// exponent
	0x31,	OP_ND2,		-2,	FMT_INFIX,			"#",		// (string?) not equal? (like <>)
	0x35,	OP_ND2,		-2,	FMT_INFIX,			"=",		// (string?) equal?
	0x38,	OP_ND1,		-1,	MODE_TRIM_STR,			"-",		// "change sign",
	0x39,	OP_ND2,		-2,	FMT_INFIX,			"#",
	0x3A,	OP_ND2,		-2,	FMT_INFIX,			"<=",
	0x3B,	OP_ND2,		-2,	FMT_INFIX,			">=",
	0x3C,	OP_ND2,		-2,	FMT_INFIX,			"<>",
	0x3D,	OP_ND2,		-2,	FMT_INFIX,			"=",		// (numeric)",
	0x3E,	OP_ND2,		-2,	FMT_INFIX,			">",
	0x3F,	OP_ND2,		-2,	FMT_INFIX,			"<",
	0x40,	OP_TERM,	0,	FMT_NOTRIM,			" @",		// (statement separator)",
	0x41,	OP_TERM,	0,	(FMT_DFLT | MODE_SPACE_TR),	"ON ERROR",
	0x42,	OP_TERM,	0,	FMT_DFLT,			"OFF ERROR",
	0x43,	OP_NDN,		-2,	(MODE_TRIM_STR | MODE_SPACE_TR |MODE_SPACE_ARG | MODE_COMMA),"ON KEY#",
	0x44,	OP_ND1,		-2,	(MODE_TRIM_STR | MODE_SPACE_TR),"OFF KEY#",
	0x46,	OP_NDN,		-2,	FMT_DFLT | MODE_COMMA,		"BEEP",		// may have no args or 2 args !!
	0x47,	OP_TERM,	0,	FMT_DFLT,			"CLEAR",
	0x49,	OP_ND2,		0,	FMT_DFLT | MODE_SPACE_TR,	"ON TIMER#",
	0x4C,	OP_ND2,		0,	FMT_DFLT | MODE_SPACE_TR,	"BPLOT",
	0x4D,	OP_ND2,		0,	FMT_DFLT | MODE_SPACE_TR,	"SETTIME",
	0x50,	OP_ND1,		1,	FMT_DFLT | MODE_SPACE_TR,	"READ#",
	0x52,	OP_TERM,	0,	FMT_DFLT,			"ALPHA",
	0x55,	OP_TERM,	0,	FMT_DFLT,			"DEG",
	0x56,	OP_NDN,		1,	FMT_SPACE,			"DISP",
	0x57,	OP_NDN,		0,	FMT_DFLT,			"GCLEAR",
	0x5A,	OP_TERM,	1,	FMT_ARG,			"GOTO",
	0x5B,	OP_TERM,	1,	FMT_ARG,			"GOSUB",
	0x5C,	OP_ND1,		1,	FMT_DFLT | MODE_SPACE_TR,	"PRINT#",
	0x5E,	OP_TERM,	0,	FMT_DFLT,			"GRAPH",
	0x5F,	OP_NDN,		0,	FMT_SPACE,			"INPUT",
	0x60,	OP_ND2,		0,	FMT_DFLT | MODE_SPACE_TR,	"IDRAW",
	0x62,	OP_TERM,	0,	FMT_DFLT | MODE_SPACE_TR,	"LET",
	0x65,	OP_ND2,		1,	FMT_DFLT | MODE_SPACE_TR,	"DRAW",
	0x66,	OP_NDN,		1,	FMT_SPACE,			"ON",
	0x67,	OP_ND1,		1,	FMT_SPACE,			"LABEL",
	0x68,	OP_ND1,		1,	FMT_SPACE,			"WAIT",
	0x69,	OP_ND2,		1,	FMT_DFLT,			"PLOT",
	0x6A,	OP_ND1,		1,	FMT_DFLT | MODE_SPACE_TR,	"PRINTER IS",
	0x6B,	OP_NDN,		1,	FMT_DFLT | MODE_SPACE_TR,	"PRINT",
	0x6C,	OP_TERM,	1,	FMT_DFLT,			"RAD",
	0x6D,	OP_ND1,		1,	FMT_DFLT,			"RANDOMIZE",
	0x6E,	OP_NDN,		1,	FMT_DFLT | MODE_SPACE_TR,	"READ",
	0x70,	OP_TERM,	1,	FMT_DFLT,			"RESTORE",
	0x71,	OP_TERM,	1,	FMT_DFLT,			"RETURN",
	0x72,	OP_ND1,		1,	FMT_DFLT | MODE_SPACE_TR,	"OFF TIMER#",
	0x73,	OP_ND2,		1,	FMT_DFLT | MODE_SPACE_TR,	"MOVE",
	0x75,	OP_TERM,	1,	FMT_DFLT,			"STOP",
	0x77,	OP_TERM,	1,	FMT_DFLT,			"PENUP",
	0x7A,	OP_NDN,		1,	FMT_DFLT,			"XAXIS",
	0x7B,	OP_NDN,		1,	FMT_DFLT,			"YAXIS",
	0x7C,	OP_TERM,	0,	FMT_DFLT,			"COPY",
	0x7F,	OP_ND1,		1,	FMT_DFLT,			"INTEGER",
	0x80,	OP_ND2,		1,	FMT_INFIX,			" ",		// FN dereference
	0x82,	OP_NDN,		1,	FMT_DFLT,			"SCALE",
	0x84,	OP_ND1,		1,	FMT_DFLT,			"OPTION BASE",
	0x86,	OP_TERM,	1,	FMT_DFLT,	"DATA",
	0x87,	OP_TERM,	1,	FMT_SPACE,			"DEF FN",
	0x88,	OP_NDN,		1,	FMT_DFLT,			"DIM",
	0x89,	OP_TERM,	1,	FMT_DFLT,			"KEY LABEL",
	0x8A,	OP_TERM,	1,	FMT_DFLT,			"END",
	0x8B,	OP_TERM,	1,	FMT_DFLT,			"FN END",
	0x8C,	OP_TERM,	1,	FMT_NOTRIM,			"FOR",
	0x8E,	OP_TERM,	1,	FMT_SPACE,			"IMAGE",
	0x8F,	OP_ND1,		1,	FMT_SPACE,			"NEXT",
	0x92,	OP_ND2,		1,	FMT_NOCOMMA,			"ASSIGN#",
	0x93,	OP_ND2,		1,	FMT_DFLT | MODE_SPACE_TR,	"CREATE",
	0x97,	OP_TERM,	1,	FMT_DFLT,			"PAUSE",
	0x9D,	OP_ND1,		1,	FMT_DFLT | MODE_SPACE_TR,	"PEN",
	0x9F,	OP_ND1,		1,	FMT_DFLT | MODE_SPACE_TR,	"LDIR",
	0xA0,	OP_ND2,		0,	FMT_DFLT | MODE_SPACE_TR,	"IMOVE",
	0xA1,	OP_TERM,	1,	MODE_TRIM_STR,			"FN",		// return value of FN
	0xA4,	OP_ND1,		1,	FMT_NOTRIM,			" TO",
	0xA5,	OP_ND2,		1,	FMT_INFIXSP,			"OR",
	0xA6,	OP_ND2,		1,	FMT_FUNC,			"MAX",
	0xA7,	OP_TERM,	1,	MODE_TRIM_STR,			"TIME",
	0xA8,	OP_TERM,	1,	FMT_SPACE,			"DATE",
	0xA9,	OP_ND1,		1,	FMT_FUNC,			"FP",
	0xAA,	OP_ND1,		1,	FMT_FUNC,			"IP",
	0xAE,	OP_ND1,		1,	FMT_FUNC,			"ATN2",
	0xB0,	OP_ND1,		1,	FMT_FUNC,			"SQR",
	0xB1,	OP_ND2,		1,	FMT_FUNC,			"MIN",
	0xB3,	OP_ND1,		1,	FMT_FUNC,			"ABS",
	0xB7,	OP_ND1,		1,	FMT_FUNC,			"SGN",
	0xBC,	OP_ND1,		1,	FMT_FUNC,			"EXP",
	0xBD,	OP_ND1,		1,	FMT_FUNC,			"INT",
	0xBE,	OP_ND1,		1,	FMT_FUNC,			"LGT",
	0xBF,	OP_ND1,		1,	FMT_FUNC,			"LOG",
	0xC3,	OP_ND1,		1,	FMT_FUNC,			"VAL$",
	0xC4,	OP_ND1,		1,	FMT_FUNC,			"LEN",
	0xC5,	OP_ND1,		1,	FMT_FUNC,			"NUM",
	0xC6,	OP_ND1,		1,	FMT_FUNC,			"VAL",
	0xC7,	OP_TERM,	1,	FMT_DFLT,			"INF",
	0xC8,	OP_TERM,	1,	FMT_DFLT,			"RND",
	0xC9,	OP_TERM,	1,	FMT_DFLT,			"PI",
	0xCA,	OP_ND1,		1,	FMT_FUNC,			"UPC$",
	0xCB,	OP_ND1,		1,	FMT_NOTRIM,			"USING",
	0xCD,	OP_ND1,		1,	FMT_FUNC,			"TAB",
	0xCE,	OP_ND1,		1,	FMT_NOTRIM,			" STEP",
	0xD0,	OP_ND1,		1,	FMT_DFLT | MODE_SPACE_TR,	"NOT",
	0xD2,	OP_TERM,	1,	FMT_DFLT,			"ERRN",
	0xD5,	OP_ND2,		1,	FMT_INFIXSP,			"AND",
	0xD6,	OP_ND2,		1,	FMT_INFIXSP,			"MOD",
	0xD8,	OP_ND1,		1,	FMT_FUNC,			"SIN",
	0xD9,	OP_ND1,		1,	FMT_FUNC,			"COS",
	0xDB,	OP_ND1,		1,	FMT_SPACE,			"TO",
	0xE0,	OP_ND2,		1,	FMT_INFIXSP,			"\\",		//  (DIV) Integer division
	0xE1,	OP_ND2,		1,	FMT_FUNC,			"POS",
	0xE6,	OP_TERM,	0,	MODE_NULL,			",",		// used in READ and READ# statements
	0xE7,	OP_TERM,	1,	FMT_ARG,			"USING",
	0xE8,	OP_IGNORE,	1,	FMT_DFLT,			"",		// input numeric from keyboard",
	0xE9,	OP_IGNORE,	1,	FMT_DFLT,			"",		// input string from keyboard",
	0xEA,	OP_IGNORE,	1,	FMT_DFLT,			"", 		// single line function assignment?
	0xEC,	OP_ND1,		1,	FMT_DFLT,			"",		// line terminator(<CR>LF>)
	0xED,	OP_TERM,	1,	FMT_DFLT,			";",		// (;) format and output numeric w/o CRLF?
	0xEE,	OP_TERM,	1,	FMT_DFLT,			",",		// (,) format and output numeric w/ CRLF?
	0xEF,	OP_TERM,	1,	FMT_DFLT,			";",		// (;) format and output string w/o CRLF?
	0xF0,	OP_TERM,	1,	FMT_DFLT,			",",		// (,) format and output string w/ CRLF?
	0xF3,	OP_TERM,	1,	FMT_DFLT | MODE_DSP_ARG,	"",		// array variable()
	0xF4,	OP_TERM,	1,	FMT_DFLT | MODE_DSP_ARG,	"",		// null array 2d X(,) ??
	0xF8,	OP_ND1,		1,	FMT_DFLT,			"UNKNOWN 0xF8 + 2 bytes",
	0xF9,	OP_ND1,		1,	FMT_DFLT,			"UNKNOWN 0xF9 + 2 bytes",
	CMD_NONE,	0,		0,	0,			"end of table"
};


struct hp85_token {
	int  tk_code;
	char tk_name[NS_STR_LEN];
	char tk_arg[NS_STR_LEN];
	int  tk_nargs;
	int  tk_action;		/* what action to take	*/
	int  tk_fmt;		/* output format (e.g. MOVE X, Y @ VAL$(X) @ X + Y */
};

/*
int main() {
	int i;

	for (i=0; bas_tbl[i].bct_opcode != 0; i++)
		printf(0,	"0x%02X, \"%s\",\n", bas_tbl[i].bct_opcode, bas_tbl[i].bct_name);
}
*/
