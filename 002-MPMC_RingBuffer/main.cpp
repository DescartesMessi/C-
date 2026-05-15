
#include "002test.h" // 仅引入声明，不关心具体实现
#include "002MPMCRingBuffer.h"
#include <iostream>
#include <cstdlib>

int main() {
    // 禁用 C/C++ IO 同步，提升高并发下的日志打印性能
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::cout << "--- Starting MPMC RingBuffer Validations ---\n";

    // 调用分离在另一个 .cpp 文件中的测试函数
    if (!run_mpmc_concurrency_test()) {
        std::cerr << "--- Validations FAILED ---\n";
        return EXIT_FAILURE; 
    }

    std::cout << "--- All Validations Completed Successfully ---\n";
    return EXIT_SUCCESS;
}