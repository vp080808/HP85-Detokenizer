#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "hp85opcodes.h"

//#define STACK_TRACE
//#define DUMP_TOKENS

/*
 * HP-85 BASIC Detokenizer - converts binary BASIC program to ASCII
 *
 *	Based on code written by Leif Jon Harcke (http://web.archive.org/web/20130320012825/http://rocknroll.stanford.edu/~lharcke/)
 *
 *	The original code listed the tokens, but made no effort to combine them into a BASIC program.
 *
 *	I have added the neccessary code to provide such a listing, but with some provisos, some major, others nits
 *	The most important perhaps is that adding new keywords is a pain. This needs to be fixed in order to be able
 *	to add the option ROMs to the basic (no pun intended) set.
 *
 *	Another visible problem is that there are tons of extra brackets. E.g. D$[J,J+1] is printed as D$[J,(J+2)]
 *	annoying, but the HP-85 will trim the extra brackets, so the program is still loadable by an HP-85.
 *
 *	This is work in progress, as I am still trying to detokenize the BASIC programs from the Standard PAC.
 *
 *	Version 1.0 (7.9.19): can list all programs in the HP-85 standard pac
 *
 *
 *	Fixed:
 *			ON S GOTO 1,2,3,4 does not work
 *			ON S GOSUB 1,2,3,4 does not work
 *			DIM does not work
 *			INPUT has a problem with the assingment token
 *			IMAGE statement is not printing properly (extra quotes are included).
 *			Re-done formatting from scratch to simplify code
 *			Formatting is a mess, we need a variable with flags, to select spaces, commas, whatever.
 * 			Need to fix double $$ as in 
 *				O=((POSM$$[(((F1-1)*10)+1)]("*")+((F1-1)*10))-1)
 * 				O=M$$[(((F1-1)*10)+1),O]
 *			commands with optional arguments (e.g. GCLEAR [v], BIORYTHMS line 2080)
 *			no comma befre last arg
 *				635 IF P9 THEN PRINT USING "K,4D"; "# COMPOUNDING PERIODS/YR."N1 
 *			missing $ 
 *				60 M[91,120]="OCTOBER***NOVEMBER**DECEMBER**"
 *				1660 O=((POS(M$[(((F1-1)*10)+1)],"*")+((F1-1)*10))-1)
 *			out of order BEEP args
 *			out of order IF THEN
 *				1230 GOSUB 5000  @ DISP  IF ((N*J)=0) THEN "INSUFFICIENT DATA", @ 50150BEEP @ GOTO 180 
 *			comma before @
 *				370 PRINT "FOR VALUE, PRESS SLV/AD(K7),THEN", @ PRINT "UNKNOWN (K1-K4 & K8)." 
 *			fixed comma in 70 ON KEY# 1,"ENTER" GOSUB 270
 *			fixed DEF FN with 1 and no arguments
 *			missing ELSE
 *				COMPZR: 3360 IF ((C=-1) OR ((C=1) AND NOT FNP9)) THEN RETURNCOPY @ RETURN
 *			FNB and FNB(X) need to work, FNB in COMPZR and FNL(X) in CALEND
 *			COMPZR: 560 DISP ("? "&R$[C,C]);" AT ";C	(extra brackets around srt cat operation (&))
 *				removed bracket option from & command.
 *	Problems:
 *			strings: Series 80 considers NULL as a printable char, so Series 80 strings may contain a NULL.
 *				meaning that C strings cannot be used for Series 80 strings. We need to use a normal
 *				buffer with the length.
 *				E.g.   struct string5 {
 *						int str_len;
 *						unsigned char *str_sp;
 *					}
 *			AMORT: 1340 PRINT TAB(10)"","AMORTIZATION"	(no , after TAB() )
 *			various errors here and there, output needs proofreading.
 *			need to change command definition so as to make it easy to add new commands (e.g. ROMs)
 *
 *	Vassilis Prevelakis, AEGIS IT RESEARCH UG
 */
#define HDRLEN 512

int bcd2int(char *bcd);
int bcd3int(char *bcd);
char* getstring(FILE *fp);
int getlineno(FILE *fp);
char* getvarname(FILE *fp);
char* getvarstr(FILE *fp);	// internal to getvarname

#define TK_ERROR	-1

#define TRUE ((1==1))
#define FALSE ((1!=1))

struct hp85_token *next_token(FILE *fp);
void update_token(struct hp85_token *tk);

int linect;	// line count

/* print action description */
const char *get_action(int action)
{
	switch(action) {
	case OP_UNKNOWN:
		return("OP_UNKNOWN");
	case OP_EOL:
		return("OP_EOL");
	case OP_IGNORE:
		return("OP_IGNORE");
	case OP_ASGN_MULT:
		return("OP_ASGN_MULT");
	case OP_ND02:
		return("OP_ND02");
	case OP_ND1:
		return("OP_ND1");
	case OP_ND2:
		return("OP_ND2");
	case OP_ND3:
		return("OP_ND3");
	case OP_NDN:
		return("OP_NDN");
	case OP_IFGOTO:
		return("OP_IFGOTO");
	case OP_IFTHEN:
		return("OP_IFTHEN");
	case OP_IFELSE:
		return("OP_IFELSE");
	case OP_TERM:
		return("OP_TERM");
	default:
		return("BUMMER");
	}
}

struct hp85_token *pop_cmd();
struct hp85_token *process_token(struct hp85_token *cp);

#define OUTBUFLEN	2048
static char outbuf[OUTBUFLEN];
static char tmpbuf[OUTBUFLEN]; // doesn't need to be that big
static char bufi = 0;

#define NO_TRIM	0
#define TRIM	1

/*
 * buf_printf -- print everything backwards
 */
void buf_replace_last(char oc, char nc)
{
	
	if (outbuf[bufi-1] == oc)
		outbuf[bufi-1] = nc;
}

void buf_printf(int trim, const char *fmt, ...) {
	int l;
	va_list ap;
	va_start(ap, fmt);

	vsnprintf(tmpbuf, OUTBUFLEN, fmt, ap);
	va_end(ap);

	char *vp = tmpbuf;
	l = strlen(vp);
	if (trim != 0) {
		while (*vp == ' ')		// trim leading blanks
			vp++;
		// trim trailing blanks
		for (l = strlen(vp); vp[l-1] == ' '; l--)
			;
	}
		
	// int l = strlen(vp);
	if ((bufi + l) > OUTBUFLEN) {
		fprintf(stderr, "buf_printf: buffer overflow (%s, %d+%d)\n", outbuf, bufi, l);
		exit(1);
	}

	for (int i = l; i > 0; )
		outbuf[bufi++] = vp[--i];
};

void wrap_print(FILE *fd, int lineno)
{
	int i;
	int st = 0;

	outbuf[bufi] = '\0';

	// time to delete all these $@#@ ',' that end up at the
	// end of lists!!!!!

	// staep 1: skip spaces from beginning of array
	for (i=0; i < bufi; i++) {
		if (outbuf[i] != ' ')
			break;
		else
			st++;
	}
	// step 2 if a , follows the spaces, replace it with a ' '
	if (outbuf[st] == ',')
		st++;

	// if lineno < 0 (-ve), then we are in debug
	// mode and we do not print the line number
	// and we leave the bufi pointer intact.
	if (lineno > 0)
		fprintf(fd, "%d ", lineno);
	for (i = bufi; i > st; ) {
		char c = outbuf[--i];
		if (isprint(c))
			fputc(c, fd);
		else
			fprintf(fd, "<0x%02x>", c);
	}
	if (lineno > 0)
		bufi = 0;
}


/*
 *	some commands need their arguments to be processed in a special 
 *	manner BEFORE they are encountered by the main loop (process_token).
 *	One major example is the ON S GOTO 1,2,3,4,5,...
 *	where the tokens are stored as:
 *		GOTO 5
 *		GOTO 4
 *		GOTO 3
 *		GOTO 2
 *		GOTO 1
 *		ON
 *		S
 *	So we need to prevent the program from printing the extra GOTOs
 *	and place comas to separate the addresses.
 *
 *	For this reason the state variables tell our program how to deal with
 *	such lists without over complicating the program structure.
 *
 *	So for certain commands we set cur_cmd with the number of the command
 *	and when we reach the end of line or see the command separator (@),
 *	we resert the cur_cmd to CMD_NONE.
 *
 */

static struct hp85_token *sp_tk;
static int  cur_cmd = CMD_NONE;
static int  last_cmd = CMD_NONE;	// last command executed
static char cur_arg[NS_STR_LEN];

/* stack related definitions */
#define STACK_SIZE	200
struct hp85_token *cmd_stack[STACK_SIZE];	// plenty of space
int tos = 0;	// top os stack (tos)

void fix_stack(int cmd, int act)
{
	struct hp85_token *tkp = cmd_stack[tos-1];

	if (tkp->tk_code == cmd)
		tkp->tk_action = act;
#ifdef STACK_TRACE
	printf("FIX [%d] <0x%02x> %s, tos=%d, %s\n", tos, tkp->tk_code, tkp->tk_name, tos, get_action(tkp->tk_action));
#endif
}

void push_cmd(struct hp85_token *ntk)
{
	last_cmd = ntk->tk_code;

	if (tos == STACK_SIZE) {
		// abort
		fprintf(stderr, "stack overflow (%d)\n", tos);
		exit(1);
	}

	if (ntk->tk_action == OP_UNKNOWN) {
		fprintf(stderr, "push_cmd: token 0x%x has unkown action code\n", ntk->tk_code);
		update_token(ntk);
	}
	switch (ntk->tk_code) {
	case CMD_DATA:
	case CMD_ON:
	case CMD_DIM:
		cur_cmd = ntk->tk_code;
		break;
	case CMD_AT:
		cur_cmd = CMD_NONE;
	}

	cmd_stack[tos++] = ntk;
#ifdef STACK_TRACE
	printf("PUSH [%d] <0x%02x> %s, tos=%d, %s\n", tos, ntk->tk_code, ntk->tk_name, tos, get_action(ntk->tk_action));
#endif
}


struct hp85_token *pop_cmd()
{
	struct hp85_token *res;

	if (tos == 0) // empty stack
		return(NULL);
	res = cmd_stack[--tos];
	cmd_stack[tos] = NULL;
#ifdef STACK_TRACE
	printf("POP  [%d] <0x%02x> %s, %s, %s\n", tos+1, res->tk_code, res->tk_name, res->tk_arg, get_action(res->tk_action));
#endif
	return(res);
}

/*
 * process_token -- read token and decide whether 
 *	a) its a terminal
 *	   in which case you just process it, 
 *	b) its a node with one terminal, 
 *	   so you process the terminal and then
 *	   process the node, or
 *	c) its a node with two terminals
 *	   where depending on the node
 *	   (see format) you process the terminals
 *	   and the node itself (the order depends)
 *	   on the format
 */


struct hp85_token *process_token(struct hp85_token *cp)
{
	struct hp85_token *arg1;
	struct hp85_token *arg2;
	struct hp85_token *arg3;
	int fmt;


	// if (cp->tk_code == CMD_DATA) {
	//	printf("\n***<0x%02x> nm:'%s' arg:'%s' act:%s, (nargs = %d, fmt = 0x%x)\n", cp->tk_code, cp->tk_name, cp->tk_arg, get_action(cp->tk_action), cp->tk_nargs, cp->tk_fmt);
	//	wrap_print(stdout, -1); printf("\n");
	// }

	fmt = cp->tk_fmt;

	/* ------ OP_IGNORE -------- */
	if (cp->tk_action == OP_IGNORE)
		return(cp);

	/* ------ OP_TERM -------- */
	if (cp->tk_action == OP_TERM) {
		// special processing for ON command
		// see NDN secion below for the explanation
		if (cur_cmd == CMD_ON) {
			if (cur_arg[0] == '\0')
				strncpy(cur_arg, cp->tk_name, NS_STR_LEN);
			buf_printf(NO_TRIM, "%s, ", cp->tk_arg);
			return(cp);
		}
		if (fmt & MODE_SPACE_TR) 
			buf_printf(NO_TRIM, " ");
		if (fmt & MODE_BRACKETS )
			buf_printf(NO_TRIM, "()");
		if (cp->tk_code == CMD_2DARRAY)
			buf_printf((fmt & MODE_TRIM_STR), "(,)");
		if (fmt & MODE_DSP_ARG) 
			buf_printf((fmt & MODE_TRIM_ARG), "%s", cp->tk_arg);
		if (fmt & MODE_SPACE_ARG) 
			buf_printf(NO_TRIM, " ");
		if (fmt & MODE_STR)
			buf_printf(NO_TRIM, "$");
		buf_printf((fmt & MODE_TRIM_STR), "%s", cp->tk_name);
		return(cp);
	}

	/* ------ Assign Multiple -------- */
	/*
	 *	Multiple assignment (e.g. A,B=3 which is equivalent to A=3 @ B=3)
	 *	simply needs special processing the 1st time the token is seen,
	 *	so we have two states for this command cur_cmd = CMD_MULASG the
	 *	first time and cur_cmd = (CMD_MULASG + 0x100) the 2nd.
	 */
	if (cp->tk_action == OP_ASGN_MULT) {
		int level = 0;

		// count lultiple assignment commands
		while (( arg1 = pop_cmd()) != NULL) {
			if (arg1->tk_action != OP_ASGN_MULT)
				break;
			level++;
		}
		// print the value to be assigned
		arg1 = process_token(arg1);
		free(arg1);

		if (fmt & MODE_STR)
			buf_printf(NO_TRIM, "$");
		buf_printf((fmt & MODE_TRIM_STR), "%s", cp->tk_name);

		// print the variable names separated by commas.
		for (;level > 0; level--) {
			arg1 = pop_cmd();
			arg1 = process_token(arg1);
			buf_printf(NO_TRIM, ",");
			free(arg1);
		}
		return(cp);
	}

	/* ------ IF .. THEN -------- */
	if (cp->tk_action == OP_IFTHEN) {
		buf_printf(NO_TRIM, " THEN ");
		arg1 = pop_cmd();
		arg1 = process_token(arg1);
		free(arg1);

		buf_printf(NO_TRIM, "IF ");
		return(cp);
	}

	/* ------ IF .. GOTO -------- */
	if (cp->tk_action == OP_IFGOTO) {
		if (fmt & MODE_SPACE_TR) 
			buf_printf(NO_TRIM, " ");
		if (fmt & MODE_DSP_ARG) 
			buf_printf((fmt & MODE_TRIM_ARG), "%s", cp->tk_arg);
		buf_printf(NO_TRIM, " THEN ");
		arg1 = pop_cmd();
		arg1 = process_token(arg1);
		free(arg1);

		buf_printf(NO_TRIM, "IF ");
		return(cp);
	}

	/* ------ OP_ND1 -------- */
	if (cp->tk_action == OP_ND1) {
		if (fmt & MODE_BRACKETS )
			buf_printf(NO_TRIM, "%c", ((fmt & MODE_SQ_BRACK) ? ']' : ')'));

		arg1 = pop_cmd();
		// this is a kludge related to the inout and output of arrays, e.g. A() or A(,)
		// the commands involved are READ, READ#, and PRINT#
		if ((cp->tk_code == CMD_1DARRAY_I) || (cp->tk_code == CMD_1DARRAY_O))
			if (arg1->tk_code == CMD_ARRAY_VAR)
				buf_printf(MODE_TRIM_STR, "()");
		arg1 = process_token(arg1);
		free(arg1);

		if (fmt & MODE_BRACKETS)
			buf_printf(NO_TRIM, "%c", ((fmt & MODE_SQ_BRACK) ? '[' : '('));
		if (fmt & MODE_SPACE_TR)
			buf_printf(NO_TRIM, " ");
		if (fmt & MODE_DSP_ARG)
			buf_printf((fmt & MODE_TRIM_ARG), "%s", cp->tk_arg);
		if (fmt & MODE_SPACE_ARG) 
			buf_printf(NO_TRIM, " ");
		if (fmt & MODE_STR)
			buf_printf(NO_TRIM, "$");
		buf_printf((fmt & MODE_TRIM_STR), "%s", cp->tk_name);
		return(cp);
	}

	/* ------ OP_ND2 -------- */
	if (cp->tk_action == OP_ND2) {
		if (fmt & MODE_INFIX) {
			// infix layout
			if (fmt & MODE_BRACKETS)
				buf_printf(NO_TRIM, ")");	// force brackets
			arg2 = pop_cmd();
			arg2 = process_token(arg2);
			free(arg2);

			
			if (fmt & MODE_SPACE_ARG)
				buf_printf(NO_TRIM, " ");
			if (fmt & MODE_STR)
				buf_printf(NO_TRIM, "$");
			buf_printf((fmt & MODE_TRIM_STR), "%s", cp->tk_name);
			if (fmt & MODE_SPACE_ARG)
				buf_printf(NO_TRIM, " ");

			arg1 = pop_cmd();
/*
			if (fmt & MODE_STR)
				arg1->tk_fmt |= MODE_STR;	// add $ to name
*/
			arg1 = process_token(arg1);
			free(arg1);

			if (fmt & MODE_BRACKETS)
				buf_printf(NO_TRIM, "(");	// force brackets
		} else {
			if (fmt & MODE_SPACE_TR)
				buf_printf(NO_TRIM, " ");
			if (fmt & MODE_BRACKETS )
				buf_printf(NO_TRIM, "%c", ((fmt & MODE_SQ_BRACK) ? ']' : ')'));
			arg2 = pop_cmd();
			arg2 = process_token(arg2);
			free(arg2);

			if ((fmt & MODE_BRACKETS ) && ((fmt & MODE_FUNCTION) == 0))
				buf_printf(NO_TRIM, "%c", ((fmt & MODE_SQ_BRACK) ? '[' : '('));
			else if (fmt & MODE_COMMA)
				buf_printf(NO_TRIM, ",");
			if (fmt & MODE_SPACE_ARG)
				buf_printf(NO_TRIM, " ");

			arg1 = pop_cmd();
			arg1 = process_token(arg1);
			free(arg1);

			if (fmt & MODE_FUNCTION)
				buf_printf(NO_TRIM, "(");
			if (fmt & MODE_SPACE_TR)
				buf_printf(NO_TRIM, " ");
			if (fmt & MODE_STR)
				buf_printf(NO_TRIM, "$");
			buf_printf((fmt & MODE_TRIM_STR), "%s", cp->tk_name);
		}
		return(cp);
	}
	/* ------ OP_ND3 -------- */
	if (cp->tk_action == OP_ND3) {
		if (fmt & MODE_BRACKETS )
			buf_printf(NO_TRIM, "%c", ((fmt & MODE_SQ_BRACK) ? ']' : ')'));

		arg2 = pop_cmd();
		arg2 = process_token(arg2);
		free(arg2);

		if (fmt & MODE_COMMA )
			buf_printf(NO_TRIM, ",");

		arg1 = pop_cmd();
		arg1 = process_token(arg1);
		free(arg1);

		if (fmt & MODE_BRACKETS )
			buf_printf(NO_TRIM, "%c", ((fmt & MODE_SQ_BRACK) ? '[' : '('));

		arg3 = pop_cmd();
		arg3 = process_token(arg3);
		free(arg3);

		if (fmt & MODE_STR)
			buf_printf(NO_TRIM, "$");
		buf_printf((fmt & MODE_TRIM_STR), "%s", cp->tk_name);
		return(cp);
	}
	/* ------ OP_NDN -------- */
	if (cp->tk_action == OP_NDN) {
	// if last command is @ or EOL, then there are no arguments to get
	// this applies for commands with variable number of arguments that
	// includes none (e.g. BEEP, which can show up as BEEP and BEEP x,y)
		if (cur_cmd == CMD_ON) {
			// ON command processing
			// we deal with the following format:
			// 	ON expression {GOTO,GOSUB) lnum1, ...
			// So we save the GOTO/GOSUB in last arg (in OP_TERM above)
			// and here we print it followed by the expression then we
			// enter the NDN look to process the sequence of line numbers

			buf_printf(NO_TRIM, " %s ", cur_arg);	// print ON command name
			cur_arg[0] = '\0';
			cur_cmd = CMD_NONE;

			// now print the ON condition
			arg1 = pop_cmd();
			if ((arg1->tk_code == CMD_AT) || (arg1->tk_code == CMD_IFTHEN))
				push_cmd(arg1);
			else {
				arg1 = process_token(arg1);
				free(arg1);
			}
		}


		// process arguments till we see an '@' or the buffer empties
		// we need to be careful here in the sense that here we go BACK
		// till we reach the PREVIOUS command (or the beginning of the
		// current line)
		int print_commas = TRUE;

		if (fmt & MODE_SPACE_ARG)
			buf_printf(NO_TRIM, " ");
		while(TRUE) {
			arg1 = pop_cmd();
			if (arg1 == NULL)
				break;
			if ((arg1->tk_code == CMD_AT) || (arg1->tk_code == CMD_IFTHEN)) {
				push_cmd(arg1);
				break;
			}
			arg1 = process_token(arg1);
			free(arg1);

			if (print_commas)
				buf_printf(NO_TRIM, ",");
		}

		buf_replace_last(',', ' ');	// remove any leftover ','

		if (fmt & MODE_SPACE_TR)
			buf_printf(NO_TRIM, " ");
		buf_printf((fmt & MODE_TRIM_STR), "%s", cp->tk_name);
		return(cp);
	}
	
	// actually this should not be reached 
	fprintf(stderr, "process_token: don't know how to process command action %s.\n", get_action(cp->tk_action));
	return(NULL);
}
		
void print_stack(int lineno)
{
	struct hp85_token *cp;

	while ((cp = pop_cmd()) != NULL) {
		cp = process_token(cp);	// process block
		free(cp);		// free data of processed block
	}
	wrap_print(stdout, lineno);
}

char *get_tkn_name(struct hp85_token *tkp) {

	switch(tkp->tk_code) {
        case 0x05:	return("string5");
        case 0x06:	return("string6");
        case 0x09:	return("1d integer array subscript");
        case 0x0A:	return("2d integer array subscript");
        case 0x0B:	return("1d real array subscript");
        case 0x0C:	return("2d real array subscript");
        case 0x1D:	return("1d string array subscript");
        case 0x1E:	return("2d string array subscript");
        case 0xE8:	return("input numeric from keyboard");
        case 0xE9:	return("input string from keyboard");
        case 0xEA:	return("single line function assignment");
        case 0xEC:	return("line terminator(<CR>LF>)");
        case 0xED:	return("(;) format and output numeric no CRLF");
        case 0xEE:	return("(,) format and output numeric with CRLF");
        case 0xEF:	return("(;) format and output string no CRLF");
        case 0xF0:	return("(,) format and output string with CRLF");
        case 0xF3:	return("push array variable()");
        case 0xF8:	return("UNKNOWN 0xF8 + 2 bytes");
        case 0xF9:	return("UNKNOWN 0xF9 + 2 bytes");
	default:
		return(tkp->tk_name);
	}
}

int main (int argc, char* argv[])
{
	FILE *fp;
	unsigned short int lineno,sitmp;
	unsigned char linelen;
	int linestart,lineend;
	int headerlen = HDRLEN;
	char stmp[NS_STR_LEN];	/* temporary string */
	unsigned char ctmp; 		/* temporary value */
	int itmp;
	float ftmp;
	unsigned char slen;		/* string length */
	int i;
	int myline = 0;		// start line

	if (argc > 3) {
		fprintf(stderr, "Usage parse_hp85 [line number] file\n");
		exit(1);
	}
	if (argc == 3) {
		myline = atoi(argv[1]);
		fprintf(stderr, "myline = %d\n", myline);
		argv++; argc--;
	}

	if ((fp = fopen(argv[1],"r")) == NULL) {
		fprintf(stderr,"error opening file\n");
		exit(1);
	}

	fseek(fp,512+24,SEEK_CUR);	/* skip LIF headers + file header */
	linect = 0;

	while (fread(&lineno,2,1,fp)) {
		lineno = bcd2int((char *) &lineno);
		fread(&linelen,1,1,fp);

		if (myline > lineno) {
			fseek(fp, linelen, SEEK_CUR); // move to next line
			continue;
		}

#ifdef DUMP_TOKENS
		printf("\t\t\t*** line: %d length: %03d filepos: 0x%lx\n",
		// lineno, linelen, ftell(fp)-3-headerlen);
		lineno, linelen, ftell(fp));
#endif /*DUMP_TOKENS*/
		lineend = ftell(fp) + linelen;
		/*
		 * A major exception and royal pain is the DATA and DIM statements
		 * which have a series of arguments that may be simple
		 * numbers, but may be expressions as well.
		 * The problem with the DIM and DATA (and perhaos others) is that unlike 
		 * "normal" commands where the command FOLLOWS the arguments, in this
		 * special case the command comes first. So when we are popping tokens 
		 * from the stack when we see the command its too late, as the arguments
		 * have already been processed.
		 * we address this by detecting the start of a DATA, or DIM sequence and
		 * addng a fake token at the end of the sequence, so that it acts as
		 * a fake NDN (node with multiple branches).
		 * A similar trick is also used by the input command where we have to remove
		 * spurious assignment tokens
		 */

		sp_tk = NULL;
		cur_cmd = CMD_NONE;
		cur_arg[0] = '\0';;

		while (ftell(fp) < lineend) {
			struct hp85_token *ntk;

			ntk = next_token(fp);

			if (ntk->tk_code == TK_ERROR) {
				fprintf(stderr, "Error token found\n");
				// abort
				exit(1);
			}

#ifdef DUMP_TOKENS
			/*
			 * debug mode -- just print info on each token
			 * 	there is no processing or resequencing
			 */
			if (ntk->tk_code != 4)
				printf("<0x%02x> %s %s (action = %s)\n", ntk->tk_code, get_tkn_name(ntk),
				ntk->tk_arg, get_action(ntk->tk_action));
			else { // real number
				float fn;

				fn = strtof(ntk->tk_arg, NULL);
				printf("<0x%02x> %s %s (%12.12g)\n", ntk->tk_code, ntk->tk_name, ntk->tk_arg, fn);
			}
#else /*DUMP_TOKENS*/
			/*
			 * here we push stuff into the stack
			 * and when we see an EOL, we pop and
			 * print (ha!)
			 */

			if (ntk->tk_action == OP_EOL) {
				if (sp_tk != NULL) {
					push_cmd(sp_tk);
					sp_tk = NULL;
				}
				last_cmd = CMD_EOL;
#ifdef STACK_TRACE
				printf("-------------------------\n");
#endif /* STACK_TRACE */
				print_stack(lineno);
			} else  {
				switch(ntk->tk_code) {
				case CMD_NEWLINE:
					// if we have a NEWLINE token at the end of a print list
					// we remove it as it will result in a comma being printed
					// at the end of the print list
					// e.g. PRINT I, @ GOTO 2540
					if ((last_cmd == CMD_COMMA_EE) || (last_cmd == CMD_COMMA_F0)) {
						struct hp85_token *arg1 = pop_cmd();
						free(arg1);
					}
					break;
				case CMD_FN_DEREF:
					// this is speculation, needs testing
					// when we have a function with no args
					// we only get the CMD_FN
					// if we have one arg we get one CMD_FN_DEREF
					// after the CMD_FN, so we go to the stack
					// and change the action code of the CMD_FN
					// from OP_TERM to OP_ND1, which allows us
					// to read in the function argument from 
					// the stack and print it correctly using the
					// machinery for built-in functions, e.g. VAL$(X)
					fix_stack(CMD_FN, OP_ND1);
					break;
				// case CMD_READ_SEP:
					break;
				case CMD_INPUT:
					cur_cmd = CMD_INPUT;
					push_cmd(ntk);
					break;
				case CMD_AT:
					if (sp_tk != NULL) {
						push_cmd(sp_tk);
						sp_tk = NULL;
					}
					push_cmd(ntk);
					break;

				/*
				 * if we see a DATA or DIM token, we create a special
				 * marker token that will be pushed on the stack
				 * AFTER the data.  Later when we see an EOL or an
				 * AT (@) we push the marker token in the stack.
				*/
				case CMD_DATA:
				case CMD_DIM:
				case CMD_OPTBASE:
				case CMD_INTEGER:
					sp_tk = malloc(sizeof(struct hp85_token));
					sp_tk->tk_code = ntk->tk_code;
					strncpy(sp_tk->tk_name, ntk->tk_name, NS_STR_LEN);
					sp_tk->tk_arg[0] = '\0';
					sp_tk->tk_nargs = 0;
					sp_tk->tk_action = OP_NDN;
					sp_tk->tk_fmt = ntk->tk_fmt;
					sp_tk->tk_fmt = MODE_TRIM_STR;
					break;
				case CMD_ASSIGN_NUM:
				case CMD_ASSIGN_STR:
					if (cur_cmd == CMD_INPUT) {
						/* fix block from ASSIGN to , */
						ntk->tk_code = CMD_COMMA_EE;
						strncpy(ntk->tk_name, ",", NS_STR_LEN);
						ntk->tk_arg[0] = '\0';
						ntk->tk_nargs = 0;
						ntk->tk_action = OP_TERM;
						ntk->tk_fmt = 0;
					}
					push_cmd(ntk);
					break;
				default:
					push_cmd(ntk);
				}
			}
#endif /*DUMP_TOKENS*/
		}
	
		fseek(fp,lineend,SEEK_SET);	/* recover from parse error */
		printf("\n");
		if (lineno == 10999) {
			break;
		}
	}
	printf("linect: %d\n",linect);
}

int bcd1int(unsigned char *bcd) {
	return (((bcd[0] & 0xf0) >> 4) * 10 +
		(bcd[0] & 0x0f));
}	

int bcd2int(char *bcd) {
	return (((bcd[0] & 0xf0) >> 4) * 10 +
		(bcd[0] & 0x0f)  +
		((bcd[1] & 0xf0) >> 4) * 1000 +
		(bcd[1] & 0x0f) * 100);
}	

int bcd3int(char *bcd) {
	return (((bcd[0] & 0xf0) >> 4) * 10 +
		(bcd[0] & 0x0f)  +
		((bcd[1] & 0xf0) >> 4) * 1000 +
		(bcd[1] & 0x0f) * 100 +
		//((bcd[2] & 0xf0) >> 4) * 100000 +
		(bcd[2] & 0x0f) * 10000);
}	

int getlineno(FILE *fp) {
	unsigned short ustmp;
	long cpos;

	fread(&ustmp,2,1,fp);
	cpos = ftell(fp);		/* current position */
	fseek(fp,ustmp+HDRLEN,SEEK_SET); /* seek to memory location */
	fread(&ustmp,2,1,fp);
	fseek(fp,cpos,SEEK_SET);	/* back to original spot */
	return(bcd2int((char *) &ustmp));
}

#define VARLEN 10

char* getvarname(FILE *fp) {
	unsigned short ustmp;
	long cpos;
	char *vp;

	fread(&ustmp,2,1,fp);		/* get pointer into memory */
	cpos = ftell(fp);		/* store current position */
	fseek(fp,ustmp+HDRLEN,SEEK_SET); /* seek to memory location */
	vp = getvarstr(fp);
	fseek(fp,cpos,SEEK_SET);	/* back to original spot */
	return(vp);
}

char* getvarstr(FILE *fp) {
	unsigned char name[2];
	unsigned int subidx[2];
	static char vname[VARLEN];
	char *vp;

	fread(name,2,1,fp);
	vp = vname;
	*vp++ = (name[1] & 0x1f)+64; /* convert to ASCII */
	subidx[1] = name[0] & 0x0f;   /* get BCD subscript */
	if (subidx[1] < 10) {
		*vp++ = subidx[1] + '0';
	}
	if (subidx[1] > 10) {
		fprintf(stderr, "error: unknown BCD subscript on variable: %02d\n",subidx[1]);
		exit(1);
	}
	*vp = '\0';
	return(vname);
}

/* FIX to deal with non-printable characters -- may be a flag (literal print or use <HH> format)
   need to check is HP-85 considers 0 as a character
 */
char* getstring(FILE *fp) {
	int slen;
	static char stmp[256];

	slen = fgetc(fp);
	fread(stmp,slen,1,fp);
	stmp[slen] = '\0';	// fat chance as the HP-85 used NULL as a printable char !!
	return(stmp);
}

void update_token(struct hp85_token *tk)
{
        int i;

        for (i=0; bas_tbl[i].bct_opcode != CMD_NONE; i++)
                if (bas_tbl[i].bct_opcode == tk->tk_code) {
			snprintf(tk->tk_name, NS_STR_LEN, "%s ", bas_tbl[i].bct_name);
			tk->tk_nargs = bas_tbl[i].bct_nargs;
			tk->tk_action = bas_tbl[i].bct_action;
			tk->tk_fmt = bas_tbl[i].bct_fmt;
			return;
		} 
	fprintf(stderr, "*** ERROR: opcode tk_code 0x%x not found\n", tk->tk_code);
}


struct hp85_token *next_token(FILE *fp)
{
      struct hp85_token *rval;
      char *vp;
      char *arg;
      char tstr[NS_STR_LEN];
      int  linenum;
      unsigned char ctmp;	/* temporary value */
      int itmp;
      int r_exp;			/* exponent for reals */
      char s_exp[10];			/* exponent for reals */

      rval = malloc(sizeof(struct hp85_token));
      rval->tk_code = fgetc(fp);
      rval->tk_name[0] = '\0';
      rval->tk_arg[0] = '\0';
      rval->tk_nargs = -1;

      switch (rval->tk_code) {
      case 0x07:	/* "store string variable" */
      case 0x08:	/* "store numeric variable" */
      case 0x09:	/* "1d integer array subscript" */
      case 0x0A:	/* "2d integer array subscript" */
      case 0x0B:	/* "1d real array subscript" */
      case 0x0C:	/* "2d real array subscript" */
      case 0x14:	/* "store numeric, multiple assignment" */
      case 0x19:	/* "END" */
      case 0x1D:	/* "1d string array subscript" */
      case 0x1E:	/* "2d string array subscript" */
      case 0x26:	/* "& " */
      case 0x27:	/* "; " */
      case 0x2A:	/* "* " */
      case 0x2B:	/* "+ " */
      case 0x2D:	/* "- " */
      case 0x2F:	/* "/ " */
      case 0x30:	/* "^ " */
      case 0x31:	/* "# (string)" */
      case 0x35:	/* "= (string)" */
      case 0x38:	/* "change sign" */
      case 0x39:	/* "# " */
      case 0x3A:	/* "<= " */
      case 0x3B:	/* ">= " */
      case 0x3C:	/* "< " */
      case 0x3D:	/* "= (numeric)" */
      case 0x3E:	/* "> " */
      case 0x3F:	/* "< " */
      case 0x40:	/* "@ (statement separator)" */
      case 0x41:	/* ON ERROR */
      case 0x42:	/* OFF ERROR */
      case 0x43:	/* "ON KEY#" */
      case 0x46:	/* "CLEAR" */
      case 0x47:	/* "CLEAR" */
      case 0x50:	/* "READ#" */
      case 0x52:	/* "ALPHA" */
      case 0x55:	/* "DEG" */
      case 0x56:	/* "DISP" */
      case 0x57:	/* "GCLEAR" */
      case 0x5C:	/* "PRINT#" */
      case 0x5E:	/* "GRAPH" */
      case 0x5F:	/* "INPUT" */
      case 0x62:	/* "LET" */
      case 0x65:	/* "DRAW" */
      case 0x66:	/* "ON" */
      case 0x67:	/* "LABEL" */
      case 0x69:	/* "PLOT" */
      case 0x6A:	/* "PRINTER IS" */
      case 0x6B:	/* "PRINT" */
      case 0x6C:	/* "RAD" */
      case 0x6D:	/* "RANDOMIZE" */
      case 0x71:	/* "RETURN" */
      case 0x73:	/* "MOVE" */
      case 0x77:	/* "PENUP" */
      case 0x7A:	/* "XAXIS" */
      case 0x7B:	/* "YAXIS" */
      case 0x7F:	/* "INTEGER" */
      case 0x80:	/* FN dereference */
      case 0x82:	/* "SCALE" */
      case 0x84:	/* "OPTION BASE" */
      case 0x88:	/* "DIM" */
      case 0x89:	/* "KEY LABEL" */
      case 0x8C:	/* "FOR" */
      case 0x8E:	/* "PRINT USING" */
      case 0x8F:	/* "NEXT" */
      case 0x92:	/* "ASSIGN#" */
      case 0x93:	/* "CREATE" */
      case 0x97:	/* "PAUSE" */
      case 0x9D:	/* "PEN" */
      case 0x9F:	/* "LDIR" */
      case 0xA1:	/* "FN RETVAL" */
      case 0xA4:	/* "TO" */
      case 0xA5:	/* "OR" */
      case 0xA6:	/* "MAX" */
      case 0xA7:	/* "TIME" */
      case 0xAE:	/* "ATN2" */
      case 0xB0:	/* "SQR" */
      case 0xB1:	/* "MIN" */
      case 0xB3:	/* "ABS" */
      case 0xB7:	/* "SGN" */
      case 0xBC:	/* "EXP" */
      case 0xBD:	/* "INT" */
      case 0xBE:	/* "LGT" */
      case 0xBF:	/* "LOG" */
      case 0xC3:	/* "VAL$" */
      case 0xC4:	/* "LEN" */
      case 0xC6:	/* "VAL" */
      case 0xC7:	/* "INF" */
      case 0xC8:	/* "RND" */
      case 0xC9:	/* "PI" */
      case 0xCA:	/* "UPC$" */
      case 0xCB:	/* "USING" */
      case 0xCD:	/* TAB */
      case 0xCE:	/* "STEP" */
      case 0xD0:	/* "NOT" */
      case 0xD2:	/* "ERRN" */
      case 0xD5:	/* "AND" */
      case 0xD8:	/* "SIN" */
      case 0xD9:	/* "COS" */
      case 0xDB:	/* "TO" */
      case 0xE1:	/* "POS" */
      case 0xE8:	/* "input numeric from keyboard" */
      case 0xE9:	/* "input string from keyboard" */
      case 0xEC:	/* "terminate output" */
      case 0xED:	/* "format and output numeric w/o CRLF" */
      case 0xEE:	/* "format and output numeric w/ CRLF" */
      case 0xEF:	/* "format and output string w/o CRLF" */
      case 0xF0:	/* "format and output string w/ CRLF" */
	update_token(rval);
	break;


      /* Special Cases */
	// operations with variables
      case 0x01:	/* "push scalar variable" */
      case 0x02:	/* "push array variable" */
      case 0x03:	/* "push string variable" */
      case 0x11:	/* "push scalar variable address" */
      case 0x12:	/* "push array variable address" */
      case 0x13:	/* "push string variable address>" */
      case 0xEA:	/* "single line function assignment" */
	update_token(rval);
	vp = getvarname(fp);
	if (rval->tk_code != 0xEA) // VP
		strncpy(rval->tk_name, vp, NS_STR_LEN);
	break;

      case 0xF3:
      case 0xF4:
	update_token(rval);	/* array variable name without subscript */
	vp = getvarname(fp);
	strncat(rval->tk_arg, vp, NS_STR_LEN - strlen(rval->tk_name));
	break;

      case 0x87:	/* "DEF FN" */
	update_token(rval);
	vp = getvarname(fp);
	snprintf(rval->tk_name, NS_STR_LEN, "DEF FN%s", vp);
	// strncat(rval->tk_name, vp, NS_STR_LEN - strlen(rval->tk_name));
	linenum = getlineno(fp); 	// prob not a line number
	ctmp = fgetc(fp);
	if (ctmp != 0) {
		vp = getvarstr(fp);
		snprintf(tstr, NS_STR_LEN, "(%s) = ", vp);
		// I wonder what is encoded here (perhaps stuff for string functions?)
		fseek(fp,8,SEEK_CUR);		// skip 8 bytes
	} else {
		snprintf(tstr, NS_STR_LEN, " = ");
	}
	strncat(rval->tk_name, tstr, NS_STR_LEN-strlen(rval->tk_name));
	break;

      case 0x04:	/* "real" */		/* 8 byte real number */
	update_token(rval);
	fseek(fp,7,SEEK_CUR);	/* jump to last byte */
	ctmp = fgetc(fp);
	vp = rval->tk_arg;
	*vp++ = ((ctmp >> 4) & 0x0f) + '0'; /* first sig digit */
	*vp++ = '.';
	*vp++ = (ctmp & 0x0f) + '0';
	for (itmp = 1; itmp <= 5; itmp++) {
	  fseek(fp,-2,SEEK_CUR);
	  ctmp = fgetc(fp);
	  *vp++ = ((ctmp >> 4) & 0x0f) + '0';
	  *vp++ = (ctmp & 0x0f) + '0';
	}
	*vp++ = 'E';
	*vp = '\0';
	fseek(fp,-2,SEEK_CUR);
	ctmp = fgetc(fp);
	itmp = (ctmp >> 4) & 0x0f;
	r_exp = (ctmp & 0x0f) * 100;
	fseek(fp,-2,SEEK_CUR);
	ctmp = fgetc(fp);
	r_exp += ((ctmp >> 4) & 0x0f) * 10;
	r_exp += ctmp & 0x0f;
	if (itmp == 9) // negative number
		snprintf(s_exp, 10, "-%03d", 100 - r_exp);
	else
		snprintf(s_exp, 10, "%03d", r_exp);
	strncat(rval->tk_arg, s_exp, NS_STR_LEN-strlen(vp));

	float fn = strtof(rval->tk_arg, NULL);
	// snprintf(rval->tk_name, NS_STR_LEN, "%12.12g", fn);
	snprintf(rval->tk_name, NS_STR_LEN, "%12.6g", fn);
	// strncpy(rval->tk_name, rval->tk_arg, NS_STR_LEN); 
	fseek(fp,7,SEEK_CUR);	/* skip to end of number */
	break;

      case 0x05:	/* "string5" */
	update_token(rval);
	snprintf(tstr, NS_STR_LEN, "\"%s\"", getstring(fp));
	strncat(rval->tk_arg, tstr, NS_STR_LEN - strlen(rval->tk_name));
	strncpy(rval->tk_name, rval->tk_arg, NS_STR_LEN); 
	break;

      case 0x06:	/* "string6" */
	update_token(rval);
	snprintf(tstr, NS_STR_LEN, "%s", getstring(fp));
	strncat(rval->tk_arg, tstr, NS_STR_LEN - strlen(rval->tk_name));
	strncpy(rval->tk_name, rval->tk_arg, NS_STR_LEN); 
	break;

      case 0x0E:	/* "end line" */
	update_token(rval);
	linect++;
	break;

      case 0x16:	/* "FN call" */
	update_token(rval);
	vp = getvarname(fp);
	strncat(rval->tk_arg, vp, NS_STR_LEN);
	// printf("(number of arguments: %03d), name %s.\n",fgetc(fp), vp);
	rval->tk_nargs = (fgetc(fp) + 1) * -1;
/*
	if ((ctmp = fgetc(fp)) != 0x80) {
		fprintf(stderr, "FN call: No terminating 0x80 char, got 0x%x\n", ctmp);
		ungetc(ctmp, fp);
	}
*/
	break;

      case 0x18:	/* "IF .. GOTO (arithmetic?)" */
	update_token(rval);
	snprintf(tstr, NS_STR_LEN, "%d", getlineno(fp));
	strncat(rval->tk_arg, tstr, NS_STR_LEN);
	break;

      case 0x1A:	/* "integer" */
	update_token(rval);
	fread(&itmp,3,1,fp);
	itmp = bcd3int((char *) &itmp);
	snprintf(tstr, NS_STR_LEN, "%d", itmp);
	strncpy(rval->tk_name, tstr, NS_STR_LEN);
	break;

      case 0x1B:	/* "THEN" */
      case 0x1C:	/* "ELSE" */
	update_token(rval);
	fread(&itmp,2,1,fp);		// I have no idea what this number
	itmp = bcd2int((char *) &itmp); // is supposed to be used for
	// printf("%d\n",itmp);		// so I read it and discard it
	break;

      case 0x1F:	/* "silent GOTO" */
      case 0x5A:	/* "GOTO" */
      case 0x5B:	/* "GOSUB" */
      case 0xE7:	/* "USING" */
	update_token(rval);
	snprintf(tstr, NS_STR_LEN, "%d", getlineno(fp));
	strncat(rval->tk_arg, tstr, NS_STR_LEN - strlen(rval->tk_name));
	break;

      case 0x8B:	/* "FN END" */
	update_token(rval);
	fread(&itmp,2,1,fp);		// consume 2 bytes
	break;

	// unknown opcodes with 2 arguments
      case 0xF8:
      case 0xF9:
	update_token(rval);
	snprintf(tstr, NS_STR_LEN, "0x%02x 0x%02x", fgetc(fp), fgetc(fp));
	strncat(rval->tk_arg, tstr, NS_STR_LEN - strlen(rval->tk_name));
	break;

     default:
	update_token(rval); // unknown
        break;
      }
      return(rval);
}
