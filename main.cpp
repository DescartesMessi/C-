#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <chrono>
#include "001.SPSCRingBuffer.h"
#include "001test.h"

int main() {
    std::cout << "--- Starting SPSC_RingBuffer Validations ---\n";
    test_basic_logic();
    test_concurrency();
    test_benchmark();
    std::cout << "--- All Validations Completed ---\n";
    return 0;
}