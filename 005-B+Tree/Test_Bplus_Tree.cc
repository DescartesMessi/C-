#include "Bplus_Tree.h" // 引入重构后的自包含头文件
#include <iostream>     // 强加标准输入输出流包含，确保终端输出通路畅通
#include <vector>       // 强加显式标准动态数组容器声明，彻底解决编译期 vector 缺失漏报
#include <cassert>      // 断言机制，用于严苛拦截错误逻辑状态

int main() {
    // 彻底切断标准 C 与 C++ 流缓冲区同步，最大化压榨控制台 IO 打印效率
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::cout << "======================================================================" <<std::endl;
    std::cout << ">>> [STAGE 1] INITIALIZING M-ORDER B+ TREE STORAGE ENGINE INSTANCE <<<"<<std::endl;
    std::cout << "======================================================================"<<std::endl;
    BPlusTree tree;
    // 初始化 4 阶 B+ 树，这意味着每个索引页面的记录项容量范围被严密锁死在 [2, 4]
    tree.init(4);
    std::cout << "[SUCCESS] 4-Order B+ Tree Index Cluster Engine initialized smoothly."<<std::endl;

    std::cout << "\n----------------------------------------------------------------------"<<std::endl;
    std::cout << "SCENARIO 1: MASS DISORDERED DATA BATCHING AND AUTO-SPLITTING SYSTEM "<<std::endl;
    std::cout << "----------------------------------------------------------------------"<<std::endl;
    std::cout << "[ACTION] Injecting chaotic distinct keys to simulate random transactional flow..."<<std::endl;
    // 灌入大量无序测试数据，强迫底层引擎在 4 阶约束下发生多路分裂与树形长高
    assert(tree.insert(10, 10001));
    assert(tree.insert(20, 20002));
    assert(tree.insert(5, 5005));
    assert(tree.insert(15, 15015));
    assert(tree.insert(30, 30030));
    assert(tree.insert(25, 25025));
    assert(tree.insert(17, 17017));
    std::cout << "[SUCCESS] 7 unique transactional data rows committed to storage."<<std::endl;

    std::cout << "[ACTION] Attempting to insert duplicate record (Key 15) to test unique constraint..."<<std::endl;
    // 验证引擎的主键去重过滤能力
    bool duplicate_allowed = tree.insert(15, 99999);
    std::cout << "         Filter Feedback: " << (duplicate_allowed ? "CRITICAL FAILURE (Duplicate Allowed)" : "SUCCESS (Duplicate Blocked)") <<std::endl;
    assert(!duplicate_allowed); // 必须被严密拒绝

    // 图形化呈现当前全分裂树的完整物理拓扑
    tree.printTree();
    tree.traverse();

    // 进行全树静态自检验
    bool structure_legal = tree.validate();
    std::cout << "[VERIFY] Mathematical Balancing Metric Verification: " << (structure_legal ? "STRUCTURALLY SOUND" : "CORRUPTED") <<std::endl;
    assert(structure_legal);
    std::cout << "         Current Equilibrium Absolute Height: " << tree.getHeight() << ""<<std::endl;
    assert(tree.getHeight() == 3); // 4阶插入7个元素在平分公式下必然产生 3 层高形态

    std::cout << "\n----------------------------------------------------------------------"<<std::endl;
    std::cout << "SCENARIO 2: POINT-TO-POINT ISOLATED DATA RETRIEVAL (EQUIVALENT SEARCH)"<<std::endl;
    std::cout << "----------------------------------------------------------------------"<<std::endl;
    std::cout << "[ACTION] Testing point queries for existing Key 17 and non-existing Key 99..."<<std::endl;
    
    // 精准修复：显式使用 DataAddr 强绑定声明，彻底摧毁 addr_99 未定义报错隐患
    DataAddr addr_17 = tree.search(17);
    std::cout << "         Point-Query Result (Key 17): File Byte Offset -> " << addr_17 << " (Expected: 17017)"<<std::endl;
    assert(addr_17 == 17017);

    DataAddr addr_99 = tree.search(99);
    std::cout << "         Point-Query Result (Key 99): File Byte Offset -> " << addr_99 << " (Expected: -1 -> Vacant)"<<std::endl;
    assert(addr_99 == -1); // 未命中必须返回统一的非法偏移地址 -1

    std::cout << "\n----------------------------------------------------------------------";
    std::cout << "SCENARIO 3: HIGH-SPEED Bounded Range SCANNING OPERATIONS (RANGE SEARCH)";
    std::cout << "----------------------------------------------------------------------";
    std::cout << "[ACTION] Querying massive contiguous block from Key 10 to Key 26...";
    
    // 精准修复：通过上方强加的 <vector> 包含，确保该高密度扫描行在 g++ 联编下顺利编译通过
    std::vector<DataAddr> range_res = tree.rangeSearch(10, 26);
    std::cout << "         Collected Physical Disk File Offsets in Range [10, 26]: " <<std::endl;
    for (auto file_offset : range_res) {
        std::cout << "0x" << std::hex << file_offset << std::dec << "  " <<std::endl;
    }
    std::cout << "\n         Total entries harvested: " << range_res.size() << " (Expected: 4 entries -> Keys: 10, 15, 17, 20)"<<std::endl;
    assert(range_res.size() == 4);

    std::cout << "\n----------------------------------------------------------------------" <<std::endl;
    std::cout << "SCENARIO 4: ADAPTIVE SHIFTING DELETION & COMPLEX CASCADE CASCADE FLOW " <<std::endl;
    std::cout << "----------------------------------------------------------------------" <<std::endl;
    // 逆向自平衡压力破坏性压测
    std::cout << "[ACTION] 4.1 Evacuating Key 30 (Expected to trigger page borrowing from left sibling)" <<std::endl;
    assert(tree.remove(30));
    tree.printTree();
    assert(tree.validate());

    std::cout << "[ACTION] 4.2 Evacuating Key 25 & Key 17 (Expected to trigger recursive non-leaf page merges)" <<std::endl;
    assert(tree.remove(25));
    assert(tree.remove(17));
    tree.printTree(); // 树高应当发生逆向跌落，重新缩减回平衡状态
    assert(tree.validate());
    std::cout << "         Current Equilibrium Absolute Height after Shrinking: " << tree.getHeight() <<std::endl;

    std::cout << "[ACTION] 4.3 Total evacuation down to zero tree status...\n";
    assert(tree.remove(5));
    assert(tree.remove(10));
    assert(tree.remove(15));
    tree.printTree();
    assert(tree.validate());

    std::cout << "\n======================================================================\n";
    std::cout << ">>> [TEST SUMMARY] ALL COMBINED RE-COMPILATION VALIDATIONS PASSED! <<<\n";
    std::cout << "======================================================================\n";
    return 0;
}