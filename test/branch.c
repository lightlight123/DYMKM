#include <stdio.h>

// 函数定义
void direct_function() {
    printf("333This is a direct function call.\n");
}

void indirect_function() {
    printf("444This is an indirect function call.\n");
}

void conditional_jump(int value) {
    if (value > 0) {
        printf("Direct jump: value is positive.\n");
    } else {
        printf("Direct jump: value is not positive.\n");
    }
}

// 函数返回示例
int add_numbers(int a, int b) {
    return a + b; // 函数返回
}

int main() {
    char ch;
    
    // 提示用户输入
    printf("Press Enter to start the flow...\n");
    getchar();  // 等待用户输入，确保程序不会立刻退出

    // 1. 直接跳转
    int value = 1;
    if (value > 0) {
        printf("111Direct jump: value is positive.\n");
    } else {
        printf("Direct jump: value is not positive.\n");
    }

    // 2. 间接跳转
    void *jump_table[] = {&&label1, &&label2};
    void *target = jump_table[value > 0 ? 0 : 1];
    printf("Jumping to: %p\n", target);  // 打印间接跳转目标地址
    getchar();  // 等待用户输入，确保跳转发生时程序活跃
    goto *target;
    
label1:
    printf("222Indirect jump: jumped to label1.\n");
    goto end;

label2:
    printf("Indirect jump: jumped to label2.\n");
    goto end;

end:
    // 3. 直接函数调用
    getchar();  // 等待用户输入，确保程序继续
    direct_function();

    // 4. 间接函数调用
    void (*function_ptr)() = indirect_function;
    function_ptr();
    printf("Indirect function call to: %p\n", function_ptr);

    // 5. 函数返回
    int result = add_numbers(3, 5);
    printf("555The sum is: %d\n", result);

    // 等待用户按键结束程序
    printf("Press Enter to exit...\n");
    getchar();

    return 0;
}
