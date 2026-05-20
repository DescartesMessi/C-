#ifndef BPLUS_TREE_H_
#define BPLUS_TREE_H_

#include <vector>        // 显式包含：提供连续物理内存页中的键值对与指针存储
#include <string>        // 显式包含：用于格式化树形控制台可视化打印
#include <iostream>      // 显式包含：提供基础流控制
#include <algorithm>     // 显式包含：引入 std::lower_bound 二分查找提速寻道
#include <stdexcept>     // 显式包含：用于处理 M 阶范围越界等核心异常
#include <queue>         // 显式包含：提供广度优先搜索（BFS）层级打印容器

// 定义高亲和性的底层物理数据类型
using KeyType = int;        // 数据库索引主键类型
using DataAddr = long long; // 模拟磁盘文件系统的绝对字节偏移量 (Byte Offset)

// 显式节点标签：取代运行时虚函数表指针（vptr），为单页高密度存储节约 8 字节空间
enum class NodeType {
    NON_LEAF,               // 非叶子路由节点（仅包含索引 Key 与子树指针）
    LEAF                    // 叶子数据节点（包含真实主键 Key 与数据磁盘地址）
};

// 统一的节点基类，承载多态底座与树形结构的自底向上级联指针
struct BPlusNode {
    NodeType type;          // 标识节点当前物理属性的显式标签
    BPlusNode* parent;      // 指向父节点的指针，用于分裂与合并时的自底向上级联更新

    // 强迫构造函数显式传递节点类型，防止隐式转型错误
    explicit BPlusNode(NodeType t) : type(t), parent(nullptr) {}
    // 虚析构函数：确保多态场景下派生类内存空间被完整释放
    virtual ~BPlusNode() = default;
};

// 非叶子路由节点：纯粹的物理索引项，完全不保存任何真实业务记录的偏移量
struct NonLeafNode : public BPlusNode {
    std::vector<KeyType> keys;        // 有序索引关键字数组，数量上限为 M-1
    std::vector<BPlusNode*> children; // 子树分支指针数组，数量上限为 M

    // 默认构造函数：向基类灌入 NON_LEAF 显式标签
    NonLeafNode() : BPlusNode(NodeType::NON_LEAF) {}
    // 重写析构函数
    ~NonLeafNode() override = default;
};

// 叶子数据节点：真正保存主键与磁盘物理文件偏移量映射关系的存储页面
struct LeafNode : public BPlusNode {
    std::vector<KeyType> keys;        // 有序主键 key 数组，数量上限为 M-1
    std::vector<DataAddr> data_addrs; // 磁盘数据物理地址数组，与 keys 数组形成绝对的一一对应
    LeafNode* prev;                   // 水平双向链表前驱指针，指向左侧相邻的叶子页
    LeafNode* next;                   // 水平双向链表后继指针，指向右侧相邻的叶子页

    // 默认构造函数：向基类灌入 LEAF 显式标签，并初始化双向纽带
    LeafNode() : BPlusNode(NodeType::LEAF), prev(nullptr), next(nullptr) {}
    // 重写析构函数
    ~LeafNode() override = default;
};

// B+ 树索引结构管理类：封装全套自平衡物理算法，对外提供极简原子接口
class BPlusTree {
public:
    // 构造函数：初始化全空索引树状态
    BPlusTree() : root_(nullptr), head_(nullptr), order_(4) {}
    // 析构函数：触发整树资源的级联安全物理销毁
    ~BPlusTree() { destroy(); }

    // 严禁拷贝：锁死指针对象，防止多线程或浅拷贝引起的指针悬挂与内存多重释放
    BPlusTree(const BPlusTree&) = delete;
    // 严禁赋值：同上
    BPlusTree& operator=(const BPlusTree&) = delete;

    // 工业级核心标准原子接口
    void init(int order);
    bool insert(KeyType key, DataAddr addr);
    bool remove(KeyType key);
    DataAddr search(KeyType key);
    std::vector<DataAddr> rangeSearch(KeyType l, KeyType r);
    void traverse();
    void destroy();

    // 树形完备性自检与可视化调试接口
    int getHeight() const;
    void printTree() const;
    bool validate() const;

private:
    // 解耦设计的内部工具私有函数（单效函数，严禁杂糅）
    LeafNode* findLeaf(KeyType key);
    void insertIntoLeaf(LeafNode* leaf, KeyType key, DataAddr addr);
    void splitLeaf(LeafNode* leaf);
    void insertIntoParent(BPlusNode* old_node, KeyType new_key, BPlusNode* new_node);
    void splitNonLeaf(NonLeafNode* node);

    void removeFromLeaf(LeafNode* leaf, KeyType key);
    void handleUnderflow(BPlusNode* node);
    bool borrowFromSibling(BPlusNode* node, NonLeafNode* parent, int idx);
    void mergeSiblings(BPlusNode* node, NonLeafNode* parent, int idx);

    void recursiveDestroy(BPlusNode* node);
    int calculateHeight(BPlusNode* node) const;
    bool validateNode(BPlusNode* node, KeyType& min_key, KeyType& max_key) const;

    BPlusNode* root_;   // 树形结构的根节点句柄（可随分裂与合并动态变更属性）
    LeafNode* head_;    // 水平双向链表的绝对头指针（永远锚定在最左侧的底层叶子页上）
    int order_;         // B+ 树的阶数 M（控制整棵树分支数量的拓扑核心）
};

#endif // BPLUS_TREE_H_