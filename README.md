This is a little riscv 32bit platform C compiler, it takes preprocessing C files as input,
and output riscv assembly files. It inherits from ucc. The author of ucc is Wenjun Wang.

# Preparation
## 1. build riscv 32bit tool-chain
//donwload riscv-gnu-toolchain at https://github.com/riscv/riscv-gnu-toolchain, then start building#donwload riscv-gnu-toolchain at https://github.com/riscv/riscv-gnu-toolchain, then start building

$./configure --prefix=riscv32  --with-arch=rv32g --with-abi=ilp32

//build and install tool-chain

$sudo make linux

## 2.build qemu emulator in user mode
//download qmeu at https://www.qemu.org/, then start building

$./configure --prefix=/opt/qemu_user32 --target-list=riscv32-linux-user --enable-debug

//build and install qemu

$make

$sudo make install

## 3. add riscv-gnu-toolchain and qemu to your PATH
$ vim ~/.bashrc
//add the following lines at the bottom, then save and quit
> export PATH=$PATH:/opt/qemu_user32/bin:/opt/riscv32/bin

$ source ~/.bashrc

# Build riscv_cc

//goto riscv_cc folder and start building 

$cd riscv_cc

$make

# Testing
## 1.compile a c file using riscv_cc

$cd riscv_cc/demo

$touch hello.c

$vim hello.c

//add the following lines to it, then save and quit

> include <stdio.h>
void main()

{

   int a = 3, b = 3;

   a = a + b;

   printf("%d\r\n", a);

}

## 2. Build hello.c using riscv_cc

//preprocessing C file using gcc

$riscv32-unknown-linux-gnu-gcc -U\_\_GNUC\_\_  -D_UCC -I../ucl/linux/include -std=c89 -E hello.c -o hello.i

//compile it to assembly file using riscv_cc(ucl)

$../ucl/ucl -o hello.s hello.i

//compile assembly file to executable 

$riscv32-unknown-linux-gnu-gcc -o hello hello.s

//run the executable using qemu emulator

$qemu-riscv32 -L /opt/riscv32/sysroot/ ./hello
