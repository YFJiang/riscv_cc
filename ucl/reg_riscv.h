#ifndef __REG_RISCV_H_
#define __REG_RISCV_H_

enum {T0, T1, T2, T3, T4, T5, T6};
enum {A0, A1, A2, A3, A4, A5, A6, A7};
enum {S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11};
enum {S0, RA, SP, GP, TP};
//  indirect addressing   register,  [eax] or (%eax)
#define SK_IRegister (SK_Register + 1)
//  no register is satisfied
#define NO_REG -1

void StoreVar(Symbol reg, Symbol v);
void SpillReg(Symbol reg);
void ClearRegs(void);
Symbol CreateReg(char *name, char *iname, int no);
Symbol GetByteReg(void);
Symbol GetWordReg(void);
Symbol GetReg(void);

extern Symbol TempRegs[];
extern Symbol FuncRegs[];
extern Symbol SaveRegs[];
// bit mask for register use
extern int UsedRegs;

#endif
