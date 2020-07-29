#include "ucl.h"
#include "target.h"
#include "reg_riscv.h"
#include "output.h"

static int ORG;
static int FloatNum;
/**
	TEMPLATE(X86_JMP,      "jmp %0")
	TEMPLATE(X86_IJMP,     "jmp *%0(,%1,4)")

	TEMPLATE(X86_PROLOGUE, "pushl %%ebp;pushl %%ebx;pushl %%esi;pushl %%edi;movl %%esp, %%ebp")
 */
static char *ASMTemplate[] =
{
#define TEMPLATE(code, str) str, 
#include "riscvlinux.tpl"
#undef TEMPLATE
};

/**
	see X86win32.c:
		 * When defining a symbol, it shall be in its alignment boundary. 
		 * In order to avoid too many align directives, we maintain a global
		 * address counter ORG, only when ORG is not in symbol's alignment
		 * boundary, write align directive
	char a = '1';
	double d = 1.23;
	int b = 30;
	double e = 1.23;

	.globl	a
	a:	.byte	49

	.align 8
	.globl	d

	d:	.long	2061584302
	.long	1072934420

	.globl	b
	b:	.long	30

	.align 8
	.globl	e

	e:	.long	2061584302
	.long	1072934420		 

		This function outputs ".align .." when necessary
 */

static void Align(Symbol p)
{
	int align = p->ty->align;

	if (ORG % align != 0)
	{
		Print(".align %d\n", align);
		ORG = ALIGN(ORG, align);
	}
	ORG += p->ty->size;
}
/**
	return a symbol @p 's name to be used in assembly code.
		4		---->	$4
		str0		---->	.str0
 */
static char* GetAccessName(Symbol p)
{
	if (p->aname != NULL)
		return p->aname;
	switch (p->kind)
	{
	case SK_Constant:
		// movl $4, -4(%ebp)
		p->aname = FormatName("%s", p->name);
		break;

	case SK_String:
	case SK_Label:
		// .str0:	.string	"%d \012"		
		p->aname = FormatName(".%s", p->name);
		break;

	case SK_Variable:
	case SK_Temp:		
		if (p->level == 0 || p->sclass == TK_EXTERN){
			p->aname = p->name;
		}
		else if (p->sclass == TK_STATIC)
		{
			/**
				int main(){	
					static int c = 5;
					...
				}
				c.0:	.long	5	

				Because it is illegal to declare a global var in C language as following:
					int c.0;	
				So there is no conflict to generate a name 'c.0' for static variable in 
				assembly output.
			 */
			p->aname = FormatName("%s.%d", p->name, TempNum++);
		}
		else
		{

			/**
				typedef struct{
					int arr[10];
				}Data;
				int main(){
					Data dt ;				
					dt.arr[5] = 100;		
				}
			 */
			// movl 20(%ebp), %eax			
			p->aname = FormatName("%d(s0)", AsVar(p)->offset);
		}
		break;

	case SK_Function:
		/**
			.globl	f
			f:
		**/
		p->aname = p->name;
		break;
		
	case SK_Offset:
		{
			Symbol base = p->link;
			int n = AsVar(p)->offset;
			/**
				typedef struct{
					int a;
					int b;
					int c;
					int d;
				}Data;
				Data dt2;
				int main(){
					Data dt1;
					dt1.c = 100;
					dt2.d = 200;
					return 0;
				}

				
				.lcomm	__va_arg_tmp,4
				.comm	dt2,16

				.text
				.globl	main

				main:
					pushl %ebp
					pushl %ebx
					pushl %esi
					pushl %edi
					movl %esp, %ebp
					subl $24, %esp
					leal -16(%ebp), %eax
					movl $100, -8(%ebp)	----------- -8(%ebp)
					leal dt2, %ecx
					movl $200, dt2+12		-----------dt2 + 12
					movl $0, %eax
					jmp .BB0
			*/
			if (base->level == 0 || base->sclass == TK_STATIC || base->sclass == TK_EXTERN)
			{
				p->aname = FormatName("%s%s%d", GetAccessName(base), n >= 0 ? "+" : "", n);
			}
			else
			{
				/**
					typedef struct{
						int arr[10];
					}Data;
					int main(){
						Data dt ;				----	dt is SK_Variable
						dt.arr[5] = 100;		----  dt.arr[5] is SK_Offset
					}
				 */
				n += AsVar(base)->offset;
				p->aname = FormatName("%d(s0)", n);
			}
		}
		break;

	default:
		assert(0);
	}

	return p->aname;
}


void SetupRegisters(void)
{
	TempRegs[T0] = CreateReg("t0", "(t0)", T0);
	TempRegs[T1] = CreateReg("t1", "(t1)", T1);
	TempRegs[T2] = CreateReg("t2", "(t2)", T2);
  	TempRegs[T3] = CreateReg("t3", "(t3)", T3);
	TempRegs[T4] = CreateReg("t4", "(t4)", T4);
	TempRegs[T5] = CreateReg("t5", "(t5)", T5);
	TempRegs[T6] = CreateReg("t6", "(t6)", T6);

    FuncRegs[A0] = CreateReg("a0", "(a0)", A0);
	FuncRegs[A1] = CreateReg("a1", "(a1)", A1);
	FuncRegs[A2] = CreateReg("a2", "(a2)", A2);
  	FuncRegs[A3] = CreateReg("a3", "(a3)", A3);
	FuncRegs[A4] = CreateReg("a4", "(a4)", A4);
	FuncRegs[A5] = CreateReg("a5", "(a5)", A5);
	FuncRegs[A6] = CreateReg("a6", "(a6)", A6);
	FuncRegs[A7] = CreateReg("a7", "(a7)", A7);



	SaveRegs[S1] = CreateReg("s1", "(s1)", S1);
	SaveRegs[S2] = CreateReg("s2", "(s2)", S2);
	SaveRegs[S3] = CreateReg("s3", "(s3)", S3);
  	SaveRegs[S4] = CreateReg("s4", "(s4)", S4);
	SaveRegs[S5] = CreateReg("s5", "(s5)", S5);
	SaveRegs[S6] = CreateReg("s6", "(s6)", S6);
	SaveRegs[S7] = CreateReg("s7", "(s7)", S7);
	SaveRegs[S8] = CreateReg("s8", "(s8)", S8);
	SaveRegs[S9] = CreateReg("s9", "(s9)", S9);
	SaveRegs[S10] = CreateReg("s10", "(s10)", S10);
	SaveRegs[S11] = CreateReg("s11", "(s11)", S11);
}

void PutASMCode(int code, Symbol opds[])
{
	/**
		For example:
			TEMPLATE(X86_JMP,      "jmp %0")
		If code is X86_JMP,
			fmt is "jmp %0"
	 */
	char *fmt = ASMTemplate[code];
	int i;

	PutChar('\t');
	while (*fmt)
	{
		switch (*fmt)
		{
		case ';':
			PutString("\n\t");
			break;

		case '%':	
			// 	Linux:
			// TEMPLATE(X86_MOVI4,    "movl %1, %0")
			fmt++;
			if (*fmt == '%')
			{
				PutChar('%');
			}
			else
			{
				i = *fmt - '0';
				if (opds[i]->reg != NULL)
				{
					PutString(opds[i]->reg->name);
				}
				else
				{
					PutString(GetAccessName(opds[i]));
				}
			}
			break;

		default:
			PutChar(*fmt);
			break;
		}
		fmt++;
	}
	PutChar('\n');		
}

void BeginProgram(void)
{
	int i;

	ORG = 0;
	FloatNum = TempNum = 0;
	for (i = T0; i <= T6; ++i)
	{
		// Initialize register symbols to
		// make sure that no register contains data from variables.
		if (TempRegs[i] != NULL)
		{
			TempRegs[i]->link = NULL;
		}
	}

	for (i = A0; i <= A7; ++i)
	{
		// Initialize register symbols to
		// make sure that no register contains data from variables.
		if (FuncRegs[i] != NULL)
		{
			FuncRegs[i]->link = NULL;
		}
	}

	for (i = S1; i <= S11; ++i)
	{
		// Initialize register symbols to
		// make sure that no register contains data from variables.
		if (SaveRegs[i] != NULL)
		{
			SaveRegs[i]->link = NULL;
		}
	}

	PutString("# Code auto-generated by UCC\n\n");
}

void Segment(int seg)
{
	if (seg == DATA)
	{
		PutString(".data\n\n");
	}
	else if (seg == CODE)
	{
		PutString(".text\n\n");
	}
}

void Import(Symbol p)
{
}
/**
	 .globl  f
	 
	 f:

 */
void Export(Symbol p)
{
	Print(".globl\t%s\n\n", GetAccessName(p));
}
/**
	 .str0:  .string "%d \012"
	 .str1:  .string "a + b + c + d = %d.\012"

 */
void DefineString(String str, int size)
{
	int i = 0;
	
	if(str->chs[size-1] == 0)
	{		
		PutString(".string\t\"");
		size--;
	}
	else
	{
		/**
			char str[3] = "abc";
		 */
		// PRINT_DEBUG_INFO(("%s",str->chs));
		PutString(".ascii\t\"");
	}
	while (i < size)
	{
		// if it is not printable.	ASCII value  <= 0x20 ?
		if (! isprint(str->chs[i]))
		{
			/**
				printf("\\%03o", (unsigned char)12);
					\014o
			 */
			Print("\\%03o", (unsigned char)str->chs[i]);
		}
		else
		{
			if (str->chs[i] == '"')
			{
				
				//  \"
				PutString("\\\"");
			}
			else if (str->chs[i] == '\\')
			{
				/**
						When "make test",  Ugly Bug.
						//	\\
						PutString("\\\\");
 				 */
				PutString("\\\\");
			}
			else 
			{
				PutChar(str->chs[i]);
			}
		}
		i++;
	}
	PutString("\"\n");
}

void DefineFloatConstant(Symbol p)
{
	int align = p->ty->align;
	//	.flt1		.flt2
	p->aname = FormatName(".flt%d", FloatNum++);
	
	Align(p);
	Print("%s:\t", p->aname);
	DefineValue(p->ty, p->val);
}


void DefineGlobal(Symbol p)
{
	Align(p);
	if (p->sclass != TK_STATIC)
	{
		// ".globl\t%s\n\n"
		// "PUBLIC %s\n\n"
		Export(p);
	}
	Print("%s:\t", GetAccessName(p));
}

void DefineCommData(Symbol p)
{
	GetAccessName(p);
	if (p->sclass == TK_STATIC)
	{
		/**
			#include <stdio.h>
			int a = 3;
			static int b;
		 */
		Print(".lcomm\t%s,%d\n", p->aname, p->ty->size);
	}
	else
	{
		Print(".comm\t%s,%d\n", p->aname, p->ty->size);
	}
}

void DefineAddress(Symbol p)
{
	Print(".long\t%s", GetAccessName(p));
}
/**
	 .data
	 
	 
	 
	 .lcomm  __va_arg_tmp,4
	 
	 .globl  ch 
	 ch: .byte	 97
	 
	 .align 2
	 .globl  s
	 
	 s:  .word	 100
	 
	 .globl  i 
	 i:  .long	 300
	 
	 .globl  lg 
	 lg: .long	 500
	 
	 .globl  f 
	 f:  .long	 1065353216
	 
	 .globl  d 
	 d:  .long	 0
	 .long	 1074266112

*/
void DefineValue(Type ty, union value val)
{
	int tcode = TypeCode(ty);

	switch (tcode)
	{
	case I1: case U1:
		Print(".byte\t%d\n", val.i[0] & 0xff);
		break;

	case I2: case U2:
		Print(".word\t%d\n", val.i[0] & 0xffff);
		break;

	case I4: case U4:
		// a:	.long	3
		Print(".long\t%d\n", val.i[0]);
		break;

	case F4:
		Print(".long\t%d\n", *(unsigned *)&val.f );
		break;

	case F8:
		{
			unsigned *p = (unsigned *)&val.d;
			Print(".long\t%d\n.long\t%d\n", p[0], p[1]);
			break;
		}

	default:
		assert(0);
	}
}
/**
	 typedef struct{
		 int a;
		 int b;
		 int c;
		 int d;
	 }Data;
	 Data dt2 = {100};


	 .data 
	 .lcomm  __va_arg_tmp,4
	 .globl  dt2
	 
	 dt2:	 .long	 100
		 .space  12

 */
void Space(int size)
{
	Print(".space\t%d\n", size);
}
/**
		 jmp .BB0
	 .BB0:
		 movl %ebp, %esp
		 popl %edi
 */
void DefineLabel(Symbol p)
{
	Print("%s:\n", GetAccessName(p));
}

void EndProgram(void)
{
	Flush();
}

