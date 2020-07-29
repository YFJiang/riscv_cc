#include "ucl.h"
#include "ast.h"
#include "expr.h"
#include "gen.h"
#include "reg_riscv.h"
#include "target.h"
#include "output.h"

extern int SwitchTableNum;
enum ASMCode
{
#define TEMPLATE(code, str) code,
#include "riscvlinux.tpl"
#undef TEMPLATE
};

static const char * asmCodeName[] = {
#define TEMPLATE(code, str) #code,
#include "riscvlinux.tpl"
#undef TEMPLATE
};
const char * GetAsmCodeName(int code){
	return asmCodeName[code];
}

/**
	 TEMPLATE(X86_ORI4, 	"orl %2, %0")		X86_OR		I4
	 TEMPLATE(X86_ORU4, 	"orl %2, %0")					U4
	 TEMPLATE(X86_ORF4, 	NULL)						F4
	 TEMPLATE(X86_ORF8, 	NULL)						F8
	 
	 TEMPLATE(X86_XORI4,	"xorl %2, %0")			X86_XOR		I4
	 TEMPLATE(X86_XORU4,	"xorl %2, %0")					U4
	 TEMPLATE(X86_XORF4,	NULL)								F4
	 TEMPLATE(X86_XORF8,	NULL)								F8

 */
#define ASM_CODE(opcode, tcode) ((opcode << 2) + tcode - I4)
#define DST  inst->opds[0]
#define SRC1 inst->opds[1]
#define SRC2 inst->opds[2]
#define IsNormalRecord(rty) (rty->size != 1 && rty->size != 2 && rty->size != 4 && rty->size != 8)
/**
	 main:
		 pushl s0
 */
#define PRESERVE_REGS 0
#define SCRATCH_REGS  4
#define STACK_ALIGN_SIZE 4

static Symbol X87Top;
static int X87TCode;


/**
 * Put assembly code to move src into dst
 */
static void Move(int code, Symbol dst, Symbol src)
{
	Symbol opds[2];

	opds[0] = dst;
	opds[1] = src;
	PutASMCode(code, opds);
}


static int GetListLen(Symbol reg){
	int len = 0;
	Symbol next = reg->link;
	while(next){
		len++;
		next = next->link;
	}
	return len;
}


/**
 * Mark the variable is in register
 */
/**
	This function only add @v into @reg link-list, 
	and we are sure that @v is a SK_Temp.
	This function is different from static Symbol PutInReg(Symbol p),
		whick @p could be SK_Variable.
 */
static void AddVarToReg(Symbol reg, Symbol v)
{

	assert(v->kind == SK_Temp );
	assert(GetListLen(reg) == 0);

	v->link = reg->link;
	reg->link = v;
	v->reg = reg;
}


/**
 * When a variable is modified, if it is not in a register, do nothing;
 * otherwise, spill othere variables in this register, set the variable's
 * needWB flag.(need write back to memory)
 */
static void ModifyVar(Symbol p)
{
	Symbol reg;

	if (p->reg == NULL)
		return;

	p->needwb = 0;
	reg = p->reg;
	/**
		The following assertion seems to be right.
		In fact, the only thing we have to do here 
		is set
		p->needwb = 1; 	?
	 */
	assert(GetListLen(reg) == 1);
	assert(p == reg->link);

	SpillReg(reg);
	
	AddVarToReg(reg, p);

	p->needwb = 1;
}

/**
 * Allocate register for instruction operand. index indicates which operand
 */
static void AllocateReg(IRInst inst, int index)
{
	Symbol reg;
	Symbol p;

	p = inst->opds[index];

	// In x86, UCC only allocates register for temporary
	if (p->kind != SK_Temp)
		return;

	// if it is already in a register, mark the register as used by current uil
	if (p->reg != NULL)
	{
		UsedRegs |= 1 << p->reg->val.i[0];
		return;
	}

	/// in x86, the first source operand is also destination operand, 
	/// reuse the first source operand's register if the first source operand 
	/// will not be used after this instruction
	if (index == 0 && SRC1->ref == 1 && SRC1->reg != NULL)
	{
		reg = SRC1->reg;
		reg->link = NULL;
		AddVarToReg(reg, p);
		return;
	}

	/// get a register, if the operand is not destination operand, load the 
	/// variable into the register
	reg = GetReg();
	if (index != 0)
	{
		//Move(X86_MOVI4, reg, p);
		Symbol opds[2];

        opds[0] = reg;
		opds[1] = p;		
		PutASMCode(RISCV_MEM2REG, opds);
	
	}
	AddVarToReg(reg, p);
}

/**
 * Before executing an float instruction, UCC ensures that only TOP register of x87 stack 
 * may be used. And whenenver a basic block ends, save the x87 stack top if needed. Thus
 * we can assure that when entering and leaving each basic block, the x87 register stack
 * is in init status.
 */
static void SaveX87Top(void)
{
#if 0
	if (X87Top == NULL)
		return;

	assert(X87Top->kind == SK_Temp);
	if (X87Top->needwb && X87Top->ref > 0)
	{
		/**
			 TEMPLATE(X86_LDF4, 	"flds %0")
			 TEMPLATE(X86_LDF8, 	"fldl %0")
			 TEMPLATE(X86_STF4, 	"fstps %0")
			 TEMPLATE(X86_STF8, 	"fstpl %0")
		*/
		PutASMCode(X86_STF4 + X87TCode - F4, &X87Top);
	}
	X87Top = NULL;
#endif	
}

/**
 * Emit floating point branch instruction
 */
static void EmitX87Branch(IRInst inst, int tcode)
{
#if 0
	// see EmitBranch(). We have called ClearRegs() before jumping.
	PutASMCode(X86_LDF4 + tcode - F4, &SRC1);
	PutASMCode(ASM_CODE(inst->opcode, tcode), inst->opds);
#endif

}

/**
	@index
		0		DST
		1		SRC1
		2		SRC2
 */
static int IsLiveAfterCurEmit(IRInst inst,int index){
	int isLive = 1;
	// Try decrement ref
	if (! (inst->opcode >= JZ && inst->opcode <= IJMP) &&
		    inst->opcode != CALL){
		DST->ref--;
		if (SRC1 && SRC1->kind != SK_Function) SRC1->ref--;
		if (SRC2 && SRC2->kind != SK_Function) SRC2->ref--;
	}
	isLive = (inst->opds[index]->ref > 0);

	// roll back
	if (! (inst->opcode >= JZ && inst->opcode <= IJMP) &&
		    inst->opcode != CALL){
		DST->ref++;
		if (SRC1 && SRC1->kind != SK_Function) SRC1->ref++;
		if (SRC2 && SRC2->kind != SK_Function) SRC2->ref++;
	}	
	return isLive;
}

/**
 * Emit floating point move instruction
 */
static void EmitX87Move(IRInst inst, int tcode)
{
#if 0
	// let X87 TOP be the first source operand
	if (X87Top != SRC1)
	{
		SaveX87Top();
		// TEMPLATE(X86_LDF4,	   "flds %0")
		PutASMCode(X86_LDF4 + tcode - F4, &SRC1);
	}

	// only put temporary in register
	/**
		Because 
		TryAddValue(Type ty, int op, Symbol src1, Symbol src2) in gen.c
		knows nothing about X87 FPU,
		It seems that we have to 
	*/
	if (DST->kind != SK_Temp)
	{		
		/**
			When we get here, we are sure that
			SRC1 are on the TOP of X87 stack.
			We will do the following:
			(1) store X87 top to DST, by X86_STF4_NO_POP / X86_STF4
			(2) if SRC1 is a temporary and still alive later,
				mark SRC1 be the X87Top.
			     else
			     	mark X87Top = NULL;
		 */		
		if(SRC1->kind == SK_Temp  && IsLiveAfterCurEmit(inst,1)){
			PutASMCode(X86_STF4_NO_POP + tcode - F4, &DST);
			SRC1->needwb = 1;
			X87Top = SRC1;
			X87TCode = tcode;
		}else{
			PutASMCode(X86_STF4 + tcode - F4, &DST);
			X87Top = NULL;
		}

	}
	else
	{
		/**
			It seems that
			the  situations we Move value To a temporary variable 
			are
			(1)in function EmitDeref().
			(2)a = b > 1? c:d;
		 */		
		DST->needwb = 1;
		X87Top = DST;
		X87TCode = tcode;
	}
#endif
}

/**
 * Emit floating point assign instruction
 */
 //	1.23f + 1.1f,	1.0 * 2.0	,  -a,  see opcode.h
static void EmitX87Assign(IRInst inst, int tcode)
{
#if 0
	// SRC2 could be NULL when it is a unary operator, like '-a'
	assert(DST->kind == SK_Temp && SRC1 != NULL);
	// PRINT_CUR_IRInst(inst);
	if(SRC1 != X87Top){
		SaveX87Top();
		PutASMCode(X86_LDF4 + tcode - F4, &SRC1);
	}else{
		/**
			void GetValue(void){
				float f1 = 1.0f, f2;
				f2 = f1+1.0f;
				f2 = (f1+1.0f)+(f1+1.0f);
				printf("%f \n",f2);
			}	
			If SRC1 is also SRC2, or SRC1 is still alive after current emit,
			we copy the top to memory of SRC1, because DST will be 
			the X87 top later.
			Else,
				we can discard SRC1 later, no need to write back.
		 */		 
		if(SRC1 == SRC2 || IsLiveAfterCurEmit(inst,1)){
			PutASMCode(X86_STF4_NO_POP + tcode - F4, &SRC1);
		}		
	}
	/**
		TEMPLATE(X86_ADDF8,    "faddl %2")
		TEMPLATE(X86_SUBF8,    "fsubl %2")
	 */	
	PutASMCode(ASM_CODE(inst->opcode, tcode), inst->opds);
	// now DST be the new TOP of X87
	DST->needwb = 1;
	X87Top = DST;
	X87TCode = tcode;
#endif
}

/**
 * Emit assembly code for block move
 */
static void EmitMoveBlock(IRInst inst)
{
	if(inst->ty->size == 0){
		return;
	}
	int count = 0;
    Symbol opds[3], rg0, rg1, rg2, rg3;
	rg0 = GetReg();
	rg1 = GetReg();
	rg2 = GetReg();
	rg3 = GetReg();
	/**
		 typedef struct{
			 int data[10];
		 }Data;
		 Data a,b;
		 int main(){
			 a = b;
			 return 0;
		 }
		--------------------
		
		 leal a, %edi
		 leal b, %esi
		 movl $40, %ecx
		 rep movsb

	 */

	 opds[0] = rg0;
	 opds[1] = rg1;
	 opds[2] = inst->opds[0];	
	 PutASMCode(RISCV_LUI_ADDI_LEAL, opds);

	 opds[0] = rg3;
	 opds[1] = rg1;
	 opds[2] = inst->opds[1];
	 PutASMCode(RISCV_LUI_ADDI_LEAL, opds);

	 for(count = 0; count < inst->ty->size; count += 4)
	 {
	     //TODO: check the order
	     DST = rg2;
		 SRC1 = IntConstant(count);
		 SRC2 = rg1;
		 PutASMCode(RISCV_MEM2REG_OFFSET, inst->opds);	
		 SRC2 = SRC1;
		 SRC1 = rg2;
		 DST = rg1;
		 PutASMCode(RISCV_REG2MEM_OFFSET, inst->opds);	
	 }
}

/**
 * Emit assembly code for move
 */
static void EmitMove(IRInst inst)
{
	int tcode = TypeCode(inst->ty);
	Symbol reg, opds[2], reg0, reg1;
	//PRINT_DEBUG_INFO(("%s",GetAsmCodeName(X86_X87_POP)));
	if (tcode == F4 || tcode == F8)
	{
		EmitX87Move(inst, tcode);
		return;
	}

	if (tcode == B)
	{
		EmitMoveBlock(inst);
		return;
	}

	switch (tcode)
	{
	case I1: case U1:
		/**
			char a,b;
			int main(){
				a = b;
				a = '3';
				return 0;
			}

			----------------------------------------
			movb b, %al
			movb %al, a
			
			movb $51, a		// SK_Constant

		 */
		if (SRC1->kind == SK_Constant)
		{
			// TEMPLATE(X86_MOVI1,    "movb %1, %0")			
			reg = GetByteReg();
			SRC2 = reg;			
			PutASMCode(RISCV_IMMED2MEM, inst->opds);		
				
		}
		else
		{		    
		    Symbol opds[2];
			reg = GetByteReg();
				
			opds[0] = reg;
			opds[1] = SRC1;
			PutASMCode(RISCV_MEM2REG, opds);

			opds[0] = SRC1;
			opds[1] = reg;
			PutASMCode(RISCV_REG2MEM, opds);
		}
		break;

	case I2: case U2:
		if (SRC1->kind == SK_Constant)
		{
			reg = GetByteReg();
			SRC2 = reg;			
			PutASMCode(RISCV_IMMED2MEM, inst->opds);
		}
		else
		{
		    Symbol opds[2];
			reg = GetByteReg();
				
			opds[0] = reg;
			opds[1] = SRC1;
			PutASMCode(RISCV_MEM2REG, opds);

			opds[0] = SRC1;
			opds[1] = reg;
			PutASMCode(RISCV_REG2MEM, opds);
		}
		break;

	case I4: case U4:
		if (SRC1->kind == SK_Constant)
		{
			reg = GetByteReg();
			SRC2 = reg;			
			PutASMCode(RISCV_IMMED2MEM, inst->opds);
		}
		else
		{
			/**
				we try to reuse the temporary value in register.
			 */		
			 
			
			 if(DST->kind != SK_Register)
			 {
				 reg0 = GetReg();
				 opds[1] = DST;
				 opds[0] = reg0; 
				 PutASMCode(RISCV_MEM2REG, opds);  
			 }
			
			 if(SRC1->kind != SK_Register)
			 {
				 reg1 = GetReg();
				 opds[1] = SRC1;
				 opds[0] = reg1; 
				 PutASMCode(RISCV_MEM2REG, opds);  
			 }					 

			 
			 opds[1] = reg1;
			 opds[0] = reg0; 
			 PutASMCode(RISCV_MV_R2R, opds); 

			 if(DST->kind == SK_Variable)
			 {
			     opds[1] = reg0;
				 opds[0] = DST; 
				 PutASMCode(RISCV_REG2MEM, opds);  
			 }

			 
		}
		ModifyVar(DST);
		break;

	default:
		assert(0);
	}
}

/**
 * Put the variable in register
 */
static Symbol PutInReg(Symbol p)
{
	Symbol reg;
    Symbol opds[2];
	if (p->reg != NULL){
		assert(p->kind == SK_Temp);
		return p->reg;
	}
	reg = GetReg();
	// TEMPLATE(X86_MOVI4,    "movl %1, %0")
	//Move(X86_MOVI4, reg, p);
	opds[1] = p;
	opds[0] = reg;
	PutASMCode(RISCV_MEM2REG, opds);
	return reg;
}

/**
 * Emit assembly code for indirect move
 */
static void EmitIndirectMove(IRInst inst)
{
	Symbol reg;

	/// indirect move is the same as move, except using register indirect address
	/// mode for destination operand
	reg = PutInReg(DST);
	inst->opcode = MOV;
	DST = reg->next;
	EmitMove(inst);
}

/**
 * Emit assembly code for assign
 */
 //	a+b,		a-b,	a*b,a|b, a << 4
static void EmitAssign(IRInst inst)
{
	int code;
	int tcode= TypeCode(inst->ty);
    Symbol opds[3], reg0, reg1, reg2;

	assert(DST->kind == SK_Temp);

	if (tcode == F4 || tcode == F8)
	{
		EmitX87Assign(inst, tcode);//TODO
		return;
	}

	assert(tcode == I4 || tcode == U4);

	code = ASM_CODE(inst->opcode, tcode);

    if(SRC1->kind != SK_Register)
    {
		reg1 = GetReg();
		opds[1] = SRC1;
		opds[0] = reg1;	
		PutASMCode(RISCV_MEM2REG, opds);  
    }

	if(SRC2->kind != SK_Register)
	//if (SRC2->kind == SK_Constant)		
	{
		// Because "idivl $10" is illegal
		reg2 = GetReg();
		opds[1] = SRC2;
		opds[0] = reg2;	
		PutASMCode(RISCV_MEM2REG, opds);			
	}
	/*else
	{
		AllocateReg(inst, 2);
	}*/

	

    if(DST->kind != SK_Register)
    {
		reg0 = GetReg();
		opds[1] = DST;
		opds[0] = reg0;	
		PutASMCode(RISCV_MEM2REG, opds);  
   }
	/**
		We are sure that dst is a temporary,
		its content is in EAX or EDX
	 */ 	
	opds[0] = reg0;
	opds[1] = reg1;
	opds[2] = reg2;	
	PutASMCode(code, opds);
	/*SpillReg(FuncRegs[A0]);
	opds[1] = reg0;
	opds[0] =  FuncRegs[A0];	
	PutASMCode(RISCV_MV_R2R, opds);*/
	opds[1] = reg0;
	opds[0] = DST;	
	PutASMCode(RISCV_REG2MEM, opds);
	ModifyVar(DST);
}
/**
	 double c = 9.87;
	 double d;
	 int a = 50;char b = 'x';
	 
	 int main(){
		 a = (int)c;
		 printf("%d \n",a);
		 a = (int)(c+1.23);
		 b = (char)(c+1.23);
		 printf("%d %d .\n",a,b);
		 d = c + 1.23;
		 c = (double)((float)(c+1.23))+(c+1.23);
		 printf("%f\n",c);
		 return 0;
	 }

 */
static void EmitCast(IRInst inst)
{
	Symbol dst, reg, opds[2];
	int code;

	dst = DST;

	//  this assertion fails, because TypeCast is not treated as common subexpression in UCC.
	// assert(DST->kind == SK_Temp);		//  See TryAddValue(..)

	reg = NULL;
	code = inst->opcode + RISCV_EXTI1 - EXTI1;
	switch (code)
	{
	case RISCV_EXTI1:
	case RISCV_EXTI2:
	case RISCV_EXTU1:
	case RISCV_EXTU2:
		assert(TypeCode(inst->ty) == I4 );
		AllocateReg(inst, 0);
		if (DST->reg == NULL)
		{
			DST = GetReg();
		}		
		PutASMCode(code, inst->opds);
		//	If dst != DST, then the original destination is not temporary,
		//	we mov the value in register to the named variable.	
		if (dst != DST)
		{
			//Move(X86_MOVI4, dst, DST);			
			opds[0] = dst;
			opds[1] =  DST;	
			PutASMCode(RISCV_REG2MEM, opds);
		}		
		break;

	case RISCV_TRUI1:		// truncate I4/U4 ---------->  I1		
		assert(TypeCode(SRC1->ty) == I4 || TypeCode(SRC1->ty) == U4);

		if (SRC1->reg != NULL)
		{
			reg = GetByteReg();
		}
		if (reg == NULL)
		{
			reg = GetByteReg();
			//Move(X86_MOVI4, X86Regs[reg->val.i[0]], SRC1);
			opds[0] = reg;
			opds[1] =  SRC1;	
			PutASMCode(RISCV_MEM2REG, opds);			
		}
		//Move(X86_MOVI1, DST, reg);
		opds[0] = DST;
		opds[1] = reg;	
		PutASMCode(RISCV_REG2MEM, opds);		
		break;

	case RISCV_TRUI2:		// // truncate I4/U4 ---------->  I2

		assert(TypeCode(SRC1->ty) == I4 || TypeCode(SRC1->ty) == U4);

		if (SRC1->reg != NULL)
		{
			reg = GetReg();
		}
		if (reg == NULL)
		{
			reg = GetReg();
			//Move(X86_MOVI4, X86Regs[reg->val.i[0]], SRC1);
			opds[0] = reg;
		    opds[1] = SRC1;	
		    PutASMCode(RISCV_MEM2REG, opds);
		}
		//Move(X86_MOVI2, DST, reg);
		opds[0] = DST;
	    opds[1] = reg;	
		PutASMCode(RISCV_REG2MEM, opds);		
		break;
	/**
		Warning:
			There is not X86_CVTI1F4/X86_CVTU1F4/X86_CVTI2F4/X86_CVTU2F4
			Because the actual work is done by 2 steps:
				
			For example:
			
			char ch = 'a';
			double d;
			int main(){
				d = (double)ch;
				return 0;
			}
			-------------------------------
			function main
			t0 = (int)(char)ch;		-----------EXTI1
			d = (double)(int)t0;	-----------CVTI4F8
			return 0;
			ret
			-----------------------------------
			 movsbl ch, %eax		-----------EXTI1
			 
			 pushl %eax			-----------CVTI4F8
			 fildl (%esp)
			 fstpl d
			 
			 movl $0, %eax

	 */
	case RISCV_CVTI4F4:
	case RISCV_CVTI4F8:
	case RISCV_CVTU4F4:
	case RISCV_CVTU4F8:	
		assert(X87Top != DST);		
		/**
			 TEMPLATE(X86_CVTI4F4,	"pushl %1;fildl (%%esp);fstps %0")
			TEMPLATE(X86_CVTI4F8,  "pushl %1;fildl (%%esp);fstpl %0")
			TEMPLATE(X86_CVTU4F4,  "pushl $0;pushl %1;fildq (%%esp);fstps %0")
			TEMPLATE(X86_CVTU4F8,  "pushl $0;pushl %1;fildq (%%esp);fstpl %0")
		 */
		PutASMCode(code, inst->opds); //TODO
		break;

	default:
		/**
			Because the following PutASMCode(...) doesn't check X87 TOP,
			so we have to save it here.
		 */
		SaveX87Top();
		if(code != RISCV_CVTF4 && code != RISCV_CVTF8){
			/**
				CVTF4I4,CVTF4U4,CVTF8I4,CVTF8U4
				The assembly code in x86linux.tpl use EAX to store
				the casted result.				
			 */
			assert(code == RISCV_CVTF4I4 || code == RISCV_CVTF4U4
					|| code == RISCV_CVTF8I4 || code == RISCV_CVTF8U4 ); 
			SpillReg(FuncRegs[A0]);
			AllocateReg(inst, 0);
			PutASMCode(code, inst->opds);
			/**
				(DST->reg && DST->reg != X86Regs[EAX]) :
					DST is a temporary and allocated in another register other than EAX,
				DST->reg == NULL
					DST is global/static/local variable
			 */
			if ((DST->reg && DST->reg != FuncRegs[A0]) || DST->reg == NULL){
				//Move(X86_MOVI4, DST, X86Regs[EAX]);
				opds[0] = DST;
	    		opds[1] = FuncRegs[A0];	
				PutASMCode(RISCV_REG2MEM, opds);
			}			
		}else{
			PutASMCode(code, inst->opds); 
		}
		// we have touched the X87top in the last statement; mark it invalid explicitely.
		X87Top = NULL;
		break;
	}
	ModifyVar(dst);

}
//	a++	, float/double is also done here.	
static void EmitInc(IRInst inst)
{
	/**
		 TEMPLATE(X86_INCI1,	"incb %0")
		 TEMPLATE(X86_INCU1,	"incb %0")
		 TEMPLATE(X86_INCI2,	"incw %0")
		 TEMPLATE(X86_INCU2,	"incw %0")				 
		 TEMPLATE(X86_INCI4,	"incl %0")
		 TEMPLATE(X86_INCU4,	"incl %0")
		 TEMPLATE(X86_INCF4,	"fld1;fadds %0;fstps %0")
	 */
	PutASMCode(RISCV_INCI1 + TypeCode(inst->ty), inst->opds);
}
//	a--
static void EmitDec(IRInst inst)
{
	//PRINT_CUR_IRInst(inst);
	PutASMCode(RISCV_DECI1 + TypeCode(inst->ty), inst->opds);
}

static void EmitBranch(IRInst inst)
{
	int tcode = TypeCode(inst->ty);
	BBlock p = (BBlock)DST;
	/**
		We make the inst->opds[0] to a SK_Lable here.
	 */
	DST = p->sym;
	if (tcode == F4 || tcode == F8)
	{
		//  see examples/cfg/crossBB.c
		ClearRegs();
		EmitX87Branch(inst, tcode);//TODO
		return;
	}

	assert(tcode >= I4);
	
	if (SRC2)
	{
		if (SRC2->kind != SK_Constant)
		{
			SRC1 = PutInReg(SRC1);
		}
	}

	SRC1->ref--;
	if (SRC1->reg != NULL)
		SRC1 = SRC1->reg;
	if (SRC2)
	{
		SRC2->ref--;
		if (SRC2->reg != NULL)
			SRC2 = SRC2->reg;
	}
	ClearRegs();
	PutASMCode(ASM_CODE(inst->opcode, tcode), inst->opds);
}
/**
	(1)	the target of Jump is a BBlock, not Variable.
		So no DST->ref -- here.
	(2)  X87 top is saved in EmitBlock(),
		because Jump must be the last IL in a basic 
		block.
 */
static void EmitJump(IRInst inst)
{
	BBlock p = (BBlock)DST;

	DST = p->sym;
	assert(DST->kind == SK_Label);
	ClearRegs();
	//PutASMCode(X86_JMP, inst->opds); 
	PutASMCode(RISCV_JUMP_OFFSET, inst->opds); 
}
/**
	 switch(a){
		 case 1:
			 a = 100;
			 break;
		 case 2:
			 a = 200;
			 break;  
		 case 3:	 
			 a = 300;
			 break;
	 }
	 
	IRinst	goto (BB0,BB1,BB2,)[t0];	 ---------------  ijmp	 21 

			SRC1--------------t0
			DST	--------------[BB0,BB1,BB2,NULL]
	 ----------------------------------------------------------------
	 .data
	 swtchTable1:	 .long	 .BB0	----------	DefineAddress()
				 .long	 .BB1
				 .long	 .BB2
				 
	 .text
	 
		 jmp *swtchTable1(,%eax,4)

 */
static void EmitIndirectJump(IRInst inst)
{
	BBlock *p;
	Symbol swtch;
	int len;
	Symbol reg, reg1, reg2, opds[3];
	
	SRC1->ref--;
	p = (BBlock *)DST;
	reg = PutInReg(SRC1);

	PutString("\n");
	Segment(DATA);

	CALLOC(swtch);
	swtch->kind = SK_Variable;
	swtch->ty = T(POINTER);
	swtch->name = FormatName("swtchTable%d", SwitchTableNum++);
	swtch->sclass = TK_STATIC;
	swtch->level = 0;
	DefineGlobal(swtch);

	DST = swtch;
	len = strlen(DST->aname);
	//PRINT_DEBUG_INFO(("%s ",DST->aname));
	while (*p != NULL)
	{
		DefineAddress((*p)->sym);
		PutString("\n");
		LeftAlign(ASMFile, len);
		PutString("\t");
		p++;
	}
	PutString("\n");

	Segment(CODE);

	SRC1 = reg;
	ClearRegs();
	// TEMPLATE(X86_IJMP,     "jmp *%0(,%1,4)")
	//mul %1 * 4
	reg1 = GetReg();
	reg2 = GetReg();
	opds[0] = reg2;
	opds[1] = IntConstant(4);	
	PutASMCode(RISCV_LOADIMME2REG, opds);
    opds[0] = reg1;
    opds[1] = reg2;
    opds[2] = reg;	
	PutASMCode(RISCV_MULI4, opds);
    opds[0] = reg2;
    opds[1] = reg1;
    opds[2] = DST;	
	PutASMCode(RISCV_ADDI4, opds);
	DST = reg2;
	//PutASMCode(X86_IJMP, inst->opds);
	PutASMCode(RISCV_JUMP_OFFSET, inst->opds);
}
/**
	See TranslateReturnStatement()							
	(1) The actual return action is done by Jumping to exitBB.
	(2) EmitReturn() here is just preparing the return value.
 */
static void EmitReturn(IRInst inst)
{
	Type ty = inst->ty;

	if (IsRealType(ty))
	{		
		if (X87Top != DST)
		{			
		    //TODO:
			int tcode = TypeCode(ty);
			SaveX87Top();
			// TEMPLATE(X86_LDF4,	   "flds %0")
			// When X87Top is NULL, X87TCode could be 0.
			PutASMCode(RISCV_LDF4 + tcode - F4, inst->opds);
		}
		X87Top = NULL;
		return;
	}
	/**
		see EmitFunction() and EmitCall()
	 */
	if (IsRecordType(ty) && IsNormalRecord(ty))
	{
		inst->opcode = IMOV;
		SRC1 = DST;
		// see  EmitFunction()
		DST = FSYM->params;
		// The actual work is done in EmitMoveBlock()
		EmitIndirectMove(inst);
		return;
	}
	/**
		We don't need to  call SpillReg(...) for EAX and EDX here.
		because in UCC, register only contains value for temporaries.
		When we return from function,these temporaries is not needed
		any more.
	 */

	Symbol opds[2];
	switch (ty->size)
	{
	case 1:
		//Move(X86_MOVI1, X86ByteRegs[EAX], DST);
		opds[1] = DST;
		opds[0] = FuncRegs[A0];
		PutASMCode(RISCV_MEM2REG, opds);
		break;

	case 2:
		//Move(X86_MOVI2, X86WordRegs[EAX], DST);
		opds[1] = DST;
		opds[0] = FuncRegs[A0];
		PutASMCode(RISCV_MEM2REG, opds);		
		break;

	case 4:
		/*if (DST->reg != X86Regs[EAX])
		{
			Move(X86_MOVI4, X86Regs[EAX], DST);
		}*/
		opds[1] = DST;
		opds[0] = FuncRegs[A0];
		PutASMCode(RISCV_MEM2REG, opds);		
		break;

	case 8:
		/**
			 typedef struct{
				 int arr[2];
			 }Data;
			 ---------------------------------
			 Data GetData(void){
				Data dt;
				return dt;
			}
			-------------------------------------
			 GetData:
				........
				 subl $8, %esp
				 movl -8(%ebp), %eax
				 movl -4(%ebp), %edx

		 */
		//Move(X86_MOVI4, X86Regs[EAX], DST);
		//Move(X86_MOVI4, X86Regs[EDX], CreateOffset(T(INT), DST, 4,DST->pcoord));

		opds[1] = DST;
		opds[0] = FuncRegs[A0];
		PutASMCode(RISCV_MEM2REG, opds);

		opds[1] = CreateOffset(T(INT), DST, 4,DST->pcoord);
		opds[0] = FuncRegs[A1];
		PutASMCode(RISCV_MEM2REG, opds);
		break;

	default:
		assert(0);
	}
}

static void PushArgument(Symbol p, Type ty)
{
	int tcode = TypeCode(ty);
	/**
		We call SaveX87Top() in EmitBBlock() when it is "EmitCall()".
		In fact, when "p == X87Top",
		we can generate more efficient code here.
	 */
	if (tcode == F4)
	{
		PutASMCode(RISCV_PUSHF4, &p);//TODO
	}
	else if (tcode == F8)
	{
		PutASMCode(RISCV_PUSHF8, &p);//TODO
	}
	else if (tcode == B)
	{
		int count = 0;
		Symbol opds[3], rg0, rg1, rg2;
		rg0 = GetReg();
		rg1 = GetReg();
		rg2 = GetReg();
			
	    opds[0] = p;
		opds[1] = rg0; 	
		PutASMCode(RISCV_LUI_ADDI_LEAL, opds);
		PutASMCode(RISCV_EXPANDF, IntConstant(ALIGN(ty->size, STACK_ALIGN_SIZE)));
		

		for(count = 0; count < ty->size; count += 4)
		{
			 opds[0] = rg2;
			 opds[1] = IntConstant(count);
			 opds[2] = rg0;
			 PutASMCode(RISCV_MEM2REG_OFFSET, opds);	
			 PutASMCode(RISCV_REG2STACK, opds);	
		}
	}
	else
	{
		//PutASMCode(X86_PUSH, &p);
		Symbol opds[2];
		opds[0] = GetReg();
		opds[1] = p;
		PutASMCode(RISCV_PUSHM2S, opds);
	}
}
/**
	DST:
			return value
	SRC1:
			function name
	SRC2:
			arguments_list
*/
static void EmitCall(IRInst inst)
{
	Vector args;
	ILArg arg;
	Type rty;
	int i, stksize;

	args = (Vector)SRC2;
	stksize = 0;
	rty = inst->ty;

	/*for (i = LEN(args) - 1; i >= 0; --i)
	{
		arg = GET_ITEM(args, i);
		PushArgument(arg->sym, arg->ty);
		if (arg->sym->kind != SK_Function) arg->sym->ref--;
		stksize += ALIGN(arg->ty->size, STACK_ALIGN_SIZE);
	}*/
	 if((LEN(args) == 1) && ( ((ILArg)GET_ITEM(args, 0))->ty->size <= STACK_ALIGN_SIZE))
    {
         //put arg 0 in A0; put arg 1 in A1
        Symbol opds[2];
		SpillReg( FuncRegs[A0]);
		opds[0] = FuncRegs[A0];
		opds[1] = ((ILArg)GET_ITEM(args, 0))->sym;		
		PutASMCode(RISCV_MEM2REG, opds);
    }
    else if((LEN(args) == 2) && ( ((ILArg)GET_ITEM(args, 0))->ty->size <= STACK_ALIGN_SIZE) && (((ILArg)GET_ITEM(args, 0))->ty->size <= STACK_ALIGN_SIZE))
    {
         //put arg 0 in A0; put arg 1 in A1
        Symbol opds[2];
		SpillReg( FuncRegs[A0]);
		SpillReg( FuncRegs[A1]);
		opds[0] = FuncRegs[A0];
		opds[1] = ((ILArg)GET_ITEM(args, 0))->sym;		
		PutASMCode(RISCV_MEM2REG, opds);

		opds[0] = FuncRegs[A1];
		opds[1] = ((ILArg)GET_ITEM(args, 1))->sym;		
		PutASMCode(RISCV_MEM2REG, opds);		
    }
	else
	{
		for (i = LEN(args) - 1; i >= 0; --i)
		{
			arg = GET_ITEM(args, i);
			PushArgument(arg->sym, arg->ty);
			if (arg->sym->kind != SK_Function) arg->sym->ref--;
			stksize += ALIGN(arg->ty->size, STACK_ALIGN_SIZE);
		}	
	}
	/**
		 We don't have to call ClearRegs() in EmitCall(),
		 Because ESI/EDI/EBX are saved in EmitPrologue().
	 */
	//SpillReg(X86Regs[EAX]);
	//SpillReg(X86Regs[ECX]);
	//SpillReg(X86Regs[EDX]);
	if (IsRecordType(rty) && IsNormalRecord(rty))
	{		
		Symbol opds[2];
		
		opds[0] = GetReg();
		opds[1] = DST;
		//PutASMCode(X86_ADDR, opds);
		PutASMCode(RISCV_PUSHM2S, opds);
		//PutASMCode(X86_PUSH, opds);
		stksize += 4;
		DST = NULL;
    }

	PutASMCode(SRC1->kind == SK_Function ? RISCV_CALL : RISCV_ICALL, inst->opds);
	if(stksize != 0){
		Symbol p;
		p = IntConstant(stksize);
		PutASMCode(RISCV_REDUCEF, &p);
	}


	if (DST != NULL)
		DST->ref--;
	if (SRC1->kind != SK_Function) SRC1->ref--;

	if(DST == NULL){
		/**
			We have set X87Top to NULL in EmitReturn()
		 */
		if (IsRealType(rty)){
			PutASMCode(RISCV_X87_POP, inst->opds); //TODO
		}
		return;
	}


	if (IsRealType(rty))
	{
		// 		TEMPLATE(X86_STF4,	   "fstps %0")
		PutASMCode(RISCV_STF4 + (rty->categ != FLOAT), inst->opds); //TODO
		return;
	}


	Symbol opds[2];		
	switch (rty->size)
	{
	case 1:
		//Move(X86_MOVI1, DST, X86ByteRegs[EAX]);
		opds[0] = DST;
		opds[1] = FuncRegs[A0];
		PutASMCode(RISCV_REG2MEM, opds);
		break;

	case 2:
		//Move(X86_MOVI2, DST, X86WordRegs[EAX]);
		opds[0] = DST;
		opds[1] = FuncRegs[A0];
		PutASMCode(RISCV_REG2MEM, opds);		
		break;

	case 4:
		/*AllocateReg(inst, 0);
		if (DST->reg != X86Regs[EAX])
		{
			Move(X86_MOVI4, DST, X86Regs[EAX]);
		}
		ModifyVar(DST);*/
		opds[0] = DST;
		opds[1] = FuncRegs[A0];
		PutASMCode(RISCV_REG2MEM, opds);	
		break;

	case 8:
		//Move(X86_MOVI4, DST, X86Regs[EAX]);
		//Move(X86_MOVI4, CreateOffset(T(INT), DST, 4,DST->pcoord), X86Regs[EDX]);
		opds[0] = DST;
		opds[1] = FuncRegs[A0];
		PutASMCode(RISCV_REG2MEM, opds);

		opds[0] = CreateOffset(T(INT), DST, 4,DST->pcoord);
		opds[1] = FuncRegs[A1];
		PutASMCode(RISCV_REG2MEM, opds);				
		break;

	default:
		assert(0);
	}
}

static void EmitAddress(IRInst inst)
{
	assert(DST->kind == SK_Temp && SRC1->kind != SK_Temp);
	Symbol opds[3];	
	//AllocateReg(inst, 0);
	//PutASMCode(X86_ADDR, inst->opds);
	//PutASMCode(RISCV_REG2MEM, inst->opds);
	opds[0] = GetReg();
	opds[1] = GetReg();
	opds[2] = SRC1;
	PutASMCode(RISCV_LUI_ADDI_LEAL, opds);	
	opds[1] = opds[0];
	opds[0] = DST;
	PutASMCode(RISCV_REG2MEM, opds);
	//ModifyVar(DST); //TODO
}
/**
	 
	 int a;
	 int *ptr;
	 int main(){
		 ptr = &a;
		 a = *ptr;
		 return 0;
	 }
	 -----------------------
	function main
		t0 = &a;
		ptr = t0;
		t1 = *ptr;	-----------EmitDeref(...)
		a = t1;
		return 0;
		ret
	--------------------------

	 movl %eax, ptr
	 //	EmitDeref(t1 = *ptr;)
	 movl ptr, %ecx	---------------	PutInReg(ptr);
	 movl (%ecx), %edx
	 
	 movl %edx, a

 */
static void EmitDeref(IRInst inst)
{
	Symbol reg;

	reg = PutInReg(SRC1);
	inst->opcode = MOV;
	SRC1 = reg->next;
	assert(SRC1->kind == SK_IRegister);
	EmitMove(inst);
	ModifyVar(DST);
	return;
}
/**
	 int gArr[10] = {100};		--------  Not Need to EmitClear during runtime
	 int main(){
		 int lArr[20] = {200};	---------	EmitClear() to clear dynamic stack memory at run time
		 return 0;
	 }
 */
static void EmitClear(IRInst inst)
{
	int size = SRC1->val.i[0];
	Symbol p = IntConstant(0);
	Symbol opds[3];

	switch (size)
	{
	case 1:
		//Move(X86_MOVI1, DST, p);
		opds[2] = GetByteReg();
		opds[1] = p;
        opds[0] = DST;	
		PutASMCode(RISCV_IMMED2MEM, opds);
		break;

	case 2:
		opds[2] = GetByteReg();
		opds[1] = p;
        opds[0] = DST;	
		PutASMCode(RISCV_IMMED2MEM, opds);
		break;

	case 4:
		opds[2] = GetByteReg();
		opds[1] = p;
        opds[0] = DST;	
		PutASMCode(RISCV_IMMED2MEM, opds);
		break;

	default:
		opds[2] = GetByteReg();
		opds[1] = p;
        opds[0] = DST;	
		PutASMCode(RISCV_IMMED2MEM, opds);
		break;
	}
}

static void EmitNOP(IRInst inst)
{
	assert(0);
}
/**
	OPCODE(IJMP,    "ijmp",                 IndirectJump)
	OPCODE(INC,     "++",                   Inc)
	OPCODE(DEC,     "--",                   Dec)
	OPCODE(ADDR,    "&",                    Address)
	OPCODE(DEREF,   "*",                    Deref)
	OPCODE(EXTI1,   "(int)(char)",          Cast)
	OPCODE(EXTU1,   "(int)(unsigned char)", Cast)
 */
static void (* Emitter[])(IRInst inst) = 
{
#define OPCODE(code, name, func) Emit##func, 
#include "opcode.h"
#undef OPCODE
};

static void EmitIRInst(IRInst inst)
{
	struct irinst instc = *inst;

	(* Emitter[inst->opcode])(&instc);
	return;
}

static void EmitBBlock(BBlock bb)
{
	IRInst inst = bb->insth.next;

	while (inst != &bb->insth)
	{
		UsedRegs = 0;
		/**
			This bug is found by testing "Livermore loops".
			See	Line 1567 in cflops.c
			  for (k=1 ; k<=n ; k++) {
			    ox_1(k)= abs( x_1(k) - stat_1(7));	
			  }
			(1) see EmitX87Branch()
			(2) ClearRegs() are called before generating assembly jumping
				in EmitBranch()/EmitJump()/EmitIndirectJump()/EmitCall().			
			(3) SaveX87Top() is called here to keep it simple.
				For conditional-expr, temporaries are used across basic blocks.
				see examples/cfg/crossBB.c
		 */
		/*if( (inst->opcode >= JZ && inst->opcode <= IJMP) || (inst->opcode == CALL)){
			SaveX87Top();
		}*/ //TODO: handle float
		//  the kernel part of emit ASM from IR.
		EmitIRInst(inst);
		/**
			ref is used as a factor in spilling register 
				static int SelectSpillReg(int endr)
		*/
		if (! (inst->opcode >= JZ && inst->opcode <= IJMP) &&
		    inst->opcode != CALL)
		{
			DST->ref--;
			if (SRC1 && SRC1->kind != SK_Function) SRC1->ref--;
			if (SRC2 && SRC2->kind != SK_Function) SRC2->ref--;
		}
		inst = inst->next;
	}
	ClearRegs();	
	SaveX87Top();
}
/**
	function(parameter1, parameter2, ....)

	...............
	parameter2
	parameter1 _________	4(s0)			
	s0 ______________	0(s0)
	local variables and temporaries		-4(s0)
									....
	
 */
static int LayoutFrame(FunctionSymbol fsym, int fstParamPos)
{
	Symbol p;
	int offset;
	/**
		#include <stdio.h>
		 
		 void f(int a, int b){	-------		a, b are parameters		 	 
			 int c = a + b;		-------		c is local variables.
			 				
			 printf("%d \n",c);
		 }
		 int main(){
			 f(3,4);
			 return 0;
		 }
			movl 4(s0), %eax		--------  a is 4(s0)
			addl 8(s0), %eax		--------  b is 8(s0)
			movl %eax, -4(s0)		--------  c is -4(s0)
	 */
	offset = fstParamPos * STACK_ALIGN_SIZE;
	p = fsym->params;
	//by riscv calling convention
	while (p)
	{
		AsVar(p)->offset = offset;		
		if(p->ty->size == 0){
			//	empty struct or array of empty struct to be of 1 byte size
			offset += ALIGN(EMPTY_OBJECT_SIZE, STACK_ALIGN_SIZE);
		}else{
			offset += ALIGN(p->ty->size, STACK_ALIGN_SIZE);
		}
		p = p->next;
	}

	offset = 0;
	/**
		SK_Temp/SK_Variable are in fsym->locals.
		In fact, some SK_Temp are allocated to register,
		but UCC always keep their stack position when the 
		function is active. Of course , this cause a waste of 
		stack memory, but it made the management of temporaries
		simple.
	 */
	p = fsym->locals;
	while (p)
	{
		if (p->ref == 0)
			goto next;
		
	
		// for empty struct object or array of empty struct object
		if(p->ty->size == 0){
			offset += ALIGN(EMPTY_OBJECT_SIZE, STACK_ALIGN_SIZE);		
		}else{
			offset += ALIGN(p->ty->size, STACK_ALIGN_SIZE);	
		}
		AsVar(p)->offset = -offset;
		// PRINT_DEBUG_INFO((" offset = %d, name = %s ",AsVar(p)->offset,AsVar(p)->name));
next:
		p = p->next;
	}

	return offset;
}

static void EmitPrologue(int stksize)
{
	/**
	 * regardless of the actual register usage, always save the preserve registers.
	 * on riscv platform, they are ebp, ebx, esi and edi
	 */
	PutASMCode(RISCV_PROLOGUE, NULL);

	//del by johnson
	/*if (stksize != 0)
	{
		Symbol sym = IntConstant(stksize);
		// TEMPLATE(X86_EXPANDF,  "subl %0, %%esp")
		PutASMCode(RISCV_EXPANDF, &sym);
	}*/
}

static void EmitEpilogue(int stksize)
{
	PutASMCode(RISCV_EPILOGUE, NULL);
}

void EmitFunction(FunctionSymbol fsym)
{
	BBlock bb;
	Type rty;
	int stksize;

	FSYM = fsym;
	if (fsym->sclass != TK_STATIC)
	{
		Export((Symbol)fsym);
	}
	DefineLabel((Symbol)fsym);

	rty = fsym->ty->bty;
	/**
		 typedef struct{
			 int arr[10];	// or char arr[3];
		 }Data;
		 
		 
		 Data GetData(void){	-------------->  void GetData(Date * implicit)
			 Data dt;
			 return dt;
		 }
		 see	EmitReturn(IRInst inst)
		 Here:
		 	add a implicite T(POINTER) parameter.
	 */
	if (IsRecordType(rty) && IsNormalRecord(rty))
	{
		VariableSymbol p;
		/**
			We just add an formal parameter to act as placeholder,
			in order to let LayoutFrame allocate stack space for it.
			The actual work is done by EmitCall()
		 */
		CALLOC(p);
		p->kind = SK_Variable;
		p->name = "recvaddr";
		p->ty = T(POINTER);
		p->level = 1;
		p->sclass = TK_AUTO;
		
		p->next = fsym->params;
		fsym->params = (Symbol)p;
	}

	stksize = LayoutFrame(fsym, PRESERVE_REGS + 1);
	/**
		main:
		pushl %ebp
		pushl %ebx
		pushl %esi
		pushl %edi
		movl %esp, %ebp
		subl $32, %esp
	 */
	EmitPrologue(stksize);

	bb = fsym->entryBB;
	while (bb != NULL)
	{	
		// to show all basic blocks
		DefineLabel(bb->sym);
		EmitBBlock(bb);
		bb = bb->next;
	}
	/**
		movl %ebp, %esp
		popl %edi
		popl %esi
		popl %ebx
		popl %ebp
		ret
	 */
	EmitEpilogue(stksize);
	PutString("\n");
}
//  store register value to variable
void StoreVar(Symbol reg, Symbol v)
{
	Symbol opds[2];

	opds[0] = v;
	opds[1] = reg;
	PutASMCode(RISCV_REG2MEM, opds);
	
}

