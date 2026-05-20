#include "Bplus_Tree.h"

// 初始化 B+ 树的拓扑阶数约束
void BPlusTree::init(int order) {
    // 核心约束：M 阶树的阶数至少为 3，否则无法在平衡公式下引发合法的分裂、合并与借调
    if (order < 3) {
        throw std::invalid_argument("B+ Tree order must be greater than or equal to 3.");
    }
    order_ = order;   // 设置当前的阶数 M
    root_ = nullptr;  // 显式重置根指针
    head_ = nullptr;  // 显式重置双向链表头
}

// 物理回收销毁整棵树
void BPlusTree::destroy() {
    if (root_) {
        recursiveDestroy(root_); // 触发深度优先的递归回收
        root_ = nullptr;         // 清空树根
    }
    head_ = nullptr;             // 清空水平头
}

// 深度优先递归析构
void BPlusTree::recursiveDestroy(BPlusNode* node) {
    if (node->type == NodeType::NON_LEAF) {
        // 如果检测到是非叶子节点，强制向下转型并遍历销毁所有的子树分支
        auto* non_leaf = static_cast<NonLeafNode*>(node);
        for (BPlusNode* child : non_leaf->children) {
            recursiveDestroy(child); // 递归下沉
        }
    }
    delete node; // 物理归还内存空间
}

// 磁盘寻道路由：依据目标 key 自上而下快速查找其物理归属的底层叶子节点
LeafNode* BPlusTree::findLeaf(KeyType key) {
    if (!root_) return nullptr; // 空树直接截断
    BPlusNode* curr = root_;
    // 模拟轻量级非叶子页面文件的寻道流程，纯粹在内存/缓存空间中完成路由
    while (curr->type == NodeType::NON_LEAF) {
        auto* non_leaf = static_cast<NonLeafNode*>(curr);
        // 使用 std::upper_bound 在有序键数组中通过二分查找准确定位首个大于 key 的边界位置
        auto it = std::upper_bound(non_leaf->keys.begin(), non_leaf->keys.end(), key);
        // 计算目标子树指针在物理 children 数组中的精确偏移下标
        int idx = std::distance(non_leaf->keys.begin(), it);
        curr = non_leaf->children[idx]; // 切换指针，深入至下一层级
    }
    return static_cast<LeafNode*>(curr); // 触达叶子数据页，强转输出
}

// 等值精准点查询接口
DataAddr BPlusTree::search(KeyType key) {
    LeafNode* leaf = findLeaf(key); // 首先定位到数据块应当归属的唯一叶子页
    if (!leaf) return -1;           // 树结构为空，直接判定未命中

    // 在叶子节点内部执行高速二分查找，搜寻是否存在完全等值的主键
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        // 键值完全匹配成功，计算其在物理 vector 中的绝对偏移下标
        int idx = std::distance(leaf->keys.begin(), it);
        return leaf->data_addrs[idx]; // 返回模拟的物理磁盘偏移量
    }
    return -1; // 索引未命中，安全返回 -1 地址
}

// 区间范围大扫描接口（完美复现 InnoDB 物理页间与页内连续 I/O 预读）
std::vector<DataAddr> BPlusTree::rangeSearch(KeyType l, KeyType r) {
    std::vector<DataAddr> results; // 结果存储集
    if (l > r) return results;     // 防御性编程：非法范围直接拒绝

    LeafNode* curr = findLeaf(l);  // 第一步：通过 O(log N) 磁盘路由精准定位区间左边界
    if (!curr) return results;

    bool keep_scanning = true;     // 水平跨页扫描控制开关
    while (curr && keep_scanning) {
        // 顺序扫描当前物理数据页（叶子节点）内的所有有序记录
        for (size_t i = 0; i < curr->keys.size(); ++i) {
            if (curr->keys[i] >= l && curr->keys[i] <= r) {
                results.push_back(curr->data_addrs[i]); // 搜集符合闭区间范围的数据物理地址
            }
            if (curr->keys[i] > r) {
                keep_scanning = false; // 一旦发现键值已溢出区间右边界，立刻完全阻断扫描
                break;
            }
        }
        curr = curr->next; // 核心亮点：直接沿着物理页的横向静态 next 指针跳转，免去树的回溯代价
    }
    return results;
}

// 插入主键记录接口
bool BPlusTree::insert(KeyType key, DataAddr addr) {
    if (!root_) {
        // 边界情况：处理空树首次添加记录的状态，直接初始化根叶子
        auto* new_root = new LeafNode();
        new_root->keys.push_back(key);
        new_root->data_addrs.push_back(addr);
        root_ = new_root;
        head_ = new_root; // 水平双向链表的头指针同步锚定
        return true;
    }

    LeafNode* leaf = findLeaf(key); // 二分寻道目标叶子
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        return false; // 严格去重规则：主键唯一性约束，重复记录直接拒绝插入并返回失败
    }

    insertIntoLeaf(leaf, key, addr); // 安全执行页内有序插入
    
    // 自平衡边界检查：判断叶子内数据条目数是否突破了阶数 M 的极限上限
    if (static_cast<int>(leaf->keys.size()) >= order_) {
        splitLeaf(leaf); // 满状态引发物理叶子页的分裂重构
    }
    return true;
}

// 页内有序插入辅助实现
void BPlusTree::insertIntoLeaf(LeafNode* leaf, KeyType key, DataAddr addr) {
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    int idx = std::distance(leaf->keys.begin(), it);
    leaf->keys.insert(it, key); // 有序嵌入键
    leaf->data_addrs.insert(leaf->data_addrs.begin() + idx, addr); // 顺次嵌入磁盘数据物理偏移
}

// 叶子节点满引发的物理物理分裂处理
void BPlusTree::splitLeaf(LeafNode* leaf) {
    auto* new_leaf = new LeafNode(); // 创建分裂后的全新右侧数据页
    int split_idx = order_ / 2;     // 选取核心黄金分割位置进行平分

    // 采用assign移动分配，将原叶子节点右半部分的数据平滑割让给新产生的右侧叶子节点
    new_leaf->keys.assign(leaf->keys.begin() + split_idx, leaf->keys.end());
    new_leaf->data_addrs.assign(leaf->data_addrs.begin() + split_idx, leaf->data_addrs.end());

    // 物理擦除原叶子节点内部已经割让给右邻居的重叠数据项
    leaf->keys.erase(leaf->keys.begin() + split_idx, leaf->keys.end());
    leaf->data_addrs.erase(leaf->data_addrs.begin() + split_idx, leaf->data_addrs.end());

    // 精密织造底层双向跨物理页水平链表的前后驱指针，维持高并发快速大范围扫描的通路
    new_leaf->next = leaf->next;
    if (leaf->next) leaf->next->prev = new_leaf;
    leaf->next = new_leaf;
    new_leaf->prev = leaf;

    // 冗余规则：提取右侧新叶子的最小键，作为分界键向上提级插入到父索引节点中
    KeyType split_key = new_leaf->keys.front();
    insertIntoParent(leaf, split_key, new_leaf);
}

// 递归自底向上级联调整父索引节点
void BPlusTree::insertIntoParent(BPlusNode* old_node, KeyType new_key, BPlusNode* new_node) {
    if (old_node == root_) {
        // 如果触发分裂的节点原本就是树的根节点，说明整棵索引树层高需要自增 1 
        auto* new_root = new NonLeafNode(); // 创建全新的非叶子路由根节点
        new_root->keys.push_back(new_key); // 填入引发分裂上提的分界键
        new_root->children.push_back(old_node); // 左手分支指向旧根
        new_root->children.push_back(new_node); // 右手分支指向全新分裂节点
        old_node->parent = new_root; // 重塑亲缘绑定
        new_node->parent = new_root; // 同上
        root_ = new_root; // 将树的全局根句柄重置移交至新根
        return;
    }

    auto* parent = static_cast<NonLeafNode*>(old_node->parent);
    auto it = std::lower_bound(parent->keys.begin(), parent->keys.end(), new_key);
    int idx = std::distance(parent->keys.begin(), it);

    // 将上提的分界键和对应的右手新分支顺次插入到父索引节点的有序 vector 中
    parent->keys.insert(it, new_key);
    parent->children.insert(parent->children.begin() + idx + 1, new_node);
    new_node->parent = parent; // 建立亲缘绑定

    // 强硬判定非叶子路由节点的关键字数量是否违反了上限（键数必须小于等于 M-1）
    if (static_cast<int>(parent->keys.size()) >= order_) {
        splitNonLeaf(parent); // 非叶子节点继续向上递归分裂
    }
}

// 非叶子路由页面的独立分裂函数
void BPlusTree::splitNonLeaf(NonLeafNode* node) {
    auto* new_non_leaf = new NonLeafNode(); // 生成全新的右侧非叶子节点
    int split_idx = node->keys.size() / 2;   // 取物理中心点

    // 核心差异：提取出来的中间分界键将直接被“抽离上提”到更高层，在当前层不留任何备份
    KeyType push_up_key = node->keys[split_idx];

    // 将右半部分多余的路由键与子树指针平移分配给新生的右侧索引页
    new_non_leaf->keys.assign(node->keys.begin() + split_idx + 1, node->keys.end());
    new_non_leaf->children.assign(node->children.begin() + split_idx + 1, node->children.end());

    // 重新修正被切分移交的所有子树节点的 parent 指针归属，使其完全对齐至新生成的右侧索引页
    for (BPlusNode* child : new_non_leaf->children) {
        child->parent = new_non_leaf;
    }

    // 从原非叶子节点中擦除已被提走和迁走的全部多余路由元素
    node->keys.erase(node->keys.begin() + split_idx, node->keys.end());
    node->children.erase(node->children.begin() + split_idx + 1, node->children.end());

    // 将抽离上提的分界键及新索引页指针继续向更高层级递归递归提交
    insertIntoParent(node, push_up_key, new_non_leaf);
}

// 主键删除接口
bool BPlusTree::remove(KeyType key) {
    LeafNode* leaf = findLeaf(key); // 一路向下一探到底定位到所在的叶子页
    if (!leaf) return false;

    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end() || *it != key) {
        return false; // 主键不在此索引树中，宣告删除失败
    }

    removeFromLeaf(leaf, key); // 启动底层的元素擦除与平衡回归流
    return true;
}

// 有序剔除叶子内部的物理记录并评估自平衡
void BPlusTree::removeFromLeaf(LeafNode* leaf, KeyType key) {
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    int idx = std::distance(leaf->keys.begin(), it);
    leaf->keys.erase(it); // 擦除主键键项
    leaf->data_addrs.erase(leaf->data_addrs.begin() + idx); // 擦除对应的真实物理磁盘地址映射

    if (leaf == root_) {
        // 边界：若当前树高度只有 1 层（即根叶子处于同一节点）
        if (leaf->keys.empty()) {
            delete root_;
            root_ = nullptr;
            head_ = nullptr; // 索引树彻底退化回全空状态
        }
        return;
    }

    // 核心下界约束检测：判定当前数据页持有的记录数是否已经低于临界安全值 ceil(M / 2)
    int min_size = (order_ + 1) / 2;
    if (static_cast<int>(leaf->keys.size()) < min_size) {
        handleUnderflow(leaf); // 触发下溢异常修复流（向邻居借调或物理大合并）
    }
}

// 级联自底向上修复节点下溢（Underflow）异常
void BPlusTree::handleUnderflow(BPlusNode* node) {
    if (node == root_) {
        // 已经回溯触达整棵树的最高端根节点
        if (node->type == NodeType::NON_LEAF) {
            auto* non_leaf = static_cast<NonLeafNode*>(node);
            if (non_leaf->keys.empty()) {
                // 如果非叶子路由根节点的所有键都被下层借调清空，则将其仅存的独生子直接扶正为全局新根，树高缩减 1
                auto root = non_leaf->children.front();
                root_->parent = nullptr; // 切断旧根羁绊
                delete non_leaf;         // 回收旧路由根页
            }
        }
        return;
    }

    auto* parent = static_cast<NonLeafNode*>(node->parent);
    int idx = 0;
    // 精确锁定当前下溢节点在父节点持有的 children 数组中的哪一个下标位置
    while (idx << static_cast<int>(parent->keys.size()) && parent->children[idx] != node) {
        idx++;
    }

    // 自平衡高级策略 1：首先尝试向亲左邻居或亲右邻居节点高吞吐借调富余元素
    if (borrowFromSibling(node, parent, idx)) return;

    // 自平衡高级策略 2：如果左右邻居均紧巴巴处于饱合下限，则强行执行物理合并
    mergeSiblings(node, parent, idx);

    // 递归检验：非叶子父节点在剔除和缩减路由项后，需要重新评估其自身的键数下限是否合法
    int min_keys = (parent == root_) ? 1 : (order_ / 2);
    int current_keys = static_cast<int>(parent->keys.size());

    if (current_keys < min_keys) {
        handleUnderflow(parent); // 父节点陷入下溢，继续向上递归回溯平衡
    }
}

// 借调邻居节点的详细算法落地方案
bool BPlusTree::borrowFromSibling(BPlusNode* node, NonLeafNode* parent, int idx) {
    int min_size = (node->type == NodeType::LEAF) ? ((order_ + 1) / 2) : (order_ / 2);

    // 策略 A：向亲左邻居借调
    if (idx > 0) {
        BPlusNode* left_sib = parent->children[idx - 1];
        int left_size = (left_sib->type == NodeType::LEAF) ? 
            static_cast<int>(static_cast<LeafNode*>(left_sib)->keys.size()) : 
            static_cast<int>(static_cast<NonLeafNode*>(left_sib)->keys.size());

        if (left_size > min_size) { // 左兄弟有富余
            if (node->type == NodeType::LEAF) {
                auto* curr = static_cast<LeafNode*>(node);
                auto* sib = static_cast<LeafNode*>(left_sib);
                // 把左邻居最大尾项完美移殖安插到当前节点的最前端首位
                curr->keys.insert(curr->keys.begin(), sib->keys.back());
                curr->data_addrs.insert(curr->data_addrs.begin(), sib->data_addrs.back());
                sib->keys.pop_back();
                sib->data_addrs.pop_back();
                parent->keys[idx - 1] = curr->keys.front(); // 严格修正父路由节点中对应的冗余界限键
            } else {
                auto* curr = static_cast<NonLeafNode*>(node);
                auto* sib = static_cast<NonLeafNode*>(left_sib);
                // 非叶子路由借调：必须让父非叶子节点的对应路由键下沉实施“旋转”借调
                curr->keys.insert(curr->keys.begin(), parent->keys[idx - 1]);
                curr->children.insert(curr->children.begin(), sib->children.back());
                curr->children.front()->parent = curr; // 更正子树家谱父指针
                parent->keys[idx - 1] = sib->keys.back(); // 父节点项同步顶替
                sib->keys.pop_back();
                sib->children.pop_back();
            }
            return true; // 成功借调，直接截断自平衡流
        }
    }

    // 策略 B：向亲右邻居借调
    if (idx < static_cast<int>(parent->children.size()) - 1) {
        BPlusNode* right_sib = parent->children[idx + 1];
        int right_size = (right_sib->type == NodeType::LEAF) ? 
            static_cast<int>(static_cast<LeafNode*>(right_sib)->keys.size()) : 
            static_cast<int>(static_cast<NonLeafNode*>(right_sib)->keys.size());

        if (right_size > min_size) { // 右兄弟有富余
            if (node->type == NodeType::LEAF) {
                auto* curr = static_cast<LeafNode*>(node);
                auto* sib = static_cast<LeafNode*>(right_sib);
                // 把右邻居的最小首项追加到当前数据页的最尾端位置
                curr->keys.push_back(sib->keys.front());
                curr->data_addrs.push_back(sib->data_addrs.front());
                sib->keys.erase(sib->keys.begin());
                sib->data_addrs.erase(sib->data_addrs.begin());
                parent->keys[idx] = sib->keys.front(); // 更新对应的父索引冗余键值
            } else {
                auto* curr = static_cast<NonLeafNode*>(node);
                auto* sib = static_cast<NonLeafNode*>(right_sib);
                // 非叶子右侧旋转下沉借调
                curr->keys.push_back(parent->keys[idx]);
                curr->children.push_back(sib->children.front());
                curr->children.back()->parent = curr;
                parent->keys[idx] = sib->keys.front();
                sib->keys.erase(sib->keys.begin());
                sib->children.erase(sib->children.begin());
            }
            return true; // 成功借调
        }
    }
    return false; // 邻居由于饱合限制无法借出，被迫返回失败以滑入物理大合并阶段
}

// 物理吞噬大合并两个兄弟节点页面
void BPlusTree::mergeSiblings(BPlusNode* node, NonLeafNode* parent, int idx) {
    if (idx > 0) {
        // 优先考虑：将当前下溢节点彻底合入到其相对应的左兄弟之中
        BPlusNode* left_sib = parent->children[idx - 1];
        if (node->type == NodeType::LEAF) {
            auto* curr = static_cast<LeafNode*>(node);
            auto* sib = static_cast<LeafNode*>(left_sib);
            // 物理拼接两片连续 vector 的存储空间
            sib->keys.insert(sib->keys.end(), curr->keys.begin(), curr->keys.end());
            sib->data_addrs.insert(sib->data_addrs.end(), curr->data_addrs.begin(), curr->data_addrs.end());
            // 物理修补水平双向数据页跨页跳转的链表骨架
            sib->next = curr->next;
            if (curr->next) curr->next->prev = sib;
            delete curr; // 彻底物理释放当前空闲节点页面
        } else {
            auto* curr = static_cast<NonLeafNode*>(node);
            auto* sib = static_cast<NonLeafNode*>(left_sib);
            // 非叶子大合并：需要将父索引节点对应的隔断路由键无缝拉扯下来一同参与物理拼装
            sib->keys.push_back(parent->keys[idx - 1]);
            sib->keys.insert(sib->keys.end(), curr->keys.begin(), curr->keys.end());
            sib->children.insert(sib->children.end(), curr->children.begin(), curr->children.end());
            for (BPlusNode* child : curr->children) child->parent = sib; // 重置归属纽带
            delete curr;
        }
        // 从父路由项中擦除已经失效的分割键及子树悬挂指针
        parent->keys.erase(parent->keys.begin() + idx - 1);
        parent->children.erase(parent->children.begin() + idx);
    } else {
        // 如果没有左邻居，说明当前节点是长子，则强行吞噬右侧邻居
        BPlusNode* right_sib = parent->children[idx + 1];
        if (node->type == NodeType::LEAF) {
            auto* curr = static_cast<LeafNode*>(node);
            auto* sib = static_cast<LeafNode*>(right_sib);
            curr->keys.insert(curr->keys.end(), sib->keys.begin(), sib->keys.end());
            curr->data_addrs.insert(curr->data_addrs.end(), sib->data_addrs.begin(), sib->data_addrs.end());
            curr->next = sib->next;
            if (sib->next) sib->next->prev = curr;
            delete sib;
        } else {
            auto* curr = static_cast<NonLeafNode*>(node);
            auto* sib = static_cast<NonLeafNode*>(right_sib);
            curr->keys.push_back(parent->keys[idx]);
            curr->keys.insert(curr->keys.end(), sib->keys.begin(), sib->keys.end());
            curr->children.insert(curr->children.end(), sib->children.begin(), sib->children.end());
            for (BPlusNode* child : sib->children) child->parent = curr;
            delete sib;
        }
        parent->keys.erase(parent->keys.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);
    }
}

// 沿水平底座进行线性遍历
void BPlusTree::traverse() {
    LeafNode* curr = head_;
    std::cout << "[TRAVERSE LOG] Scan chain along leaf linklist:\n               START -> ";
    while (curr) {
        for (const auto& key : curr->keys) {
            std::cout << key << " -> ";
        }
        curr = curr->next; // 磁盘连续块预读流转
    }
    std::cout << "END_NULL\n";
}

// 返回当前树层高
int BPlusTree::getHeight() const {
    return calculateHeight(root_);
}

int BPlusTree::calculateHeight(BPlusNode* node) const {
    if (!node) return 0;
    int height = 1;
    BPlusNode* curr = node;
    // 沿着最左侧子树一路向下探测直到触达底层叶子节点，由于 B+ 树绝对平衡，最左侧层高即为整树层高
    while (curr->type == NodeType::NON_LEAF) {
        curr = static_cast<NonLeafNode*>(curr)->children.front();
        height++;
    }
    return height;
}

// 广度优先控制台层级可视化图形化输出
void BPlusTree::printTree() const {
    if (!root_) {
        std::cout << "[VISUAL REPORT] Tree is fully vacant.\n";
        return;
    }
    std::queue<BPlusNode*> q; // 利用底层队列进行层级剥离
    q.push(root_);
    int level = 0;
    std::cout << "\n========================[ DISK INDEX TREE MORPHOLOGY ]========================\n";
    while (!q.empty()) {
        int sz = q.size();
        std::cout << "  [Layer " << level << "] : ";
        for (int i = 0; i < sz; ++i) {
            BPlusNode* curr = q.front();
            q.pop();
            std::cout << "(";
            if (curr->type == NodeType::LEAF) {
                auto* leaf = static_cast<LeafNode*>(curr);
                for (size_t j = 0; j < leaf->keys.size(); ++j) {
                    std::cout << leaf->keys[j] << (j == leaf->keys.size() - 1 ? "" : ",");
                }
            } else {
                auto* non_leaf = static_cast<NonLeafNode*>(curr);
                for (size_t j = 0; j < non_leaf->keys.size(); ++j) {
                    std::cout << non_leaf->keys[j] << (j == non_leaf->keys.size() - 1 ? "" : ",");
                }
                for (BPlusNode* child : non_leaf->children) {
                    q.push(child); // 子树页面灌入队列
                }
            }
            std::cout << ")  ";
        }
        std::cout << "\n";
        level++;
    }
    std::cout << "==============================================================================\n";
}

// 全树结构健全性与强边界规则合法性静态自验
bool BPlusTree::validate() const {
    if (!root_) return true;
    KeyType min_k, max_k;
    return validateNode(root_, min_k, max_k);
}

bool BPlusTree::validateNode(BPlusNode* node, KeyType& min_key, KeyType& max_key) const {
    if (node->type == NodeType::LEAF) {
        auto* leaf = static_cast<LeafNode*>(node);
        if (leaf->keys.empty()) return false;
        // 数据记录页项不能低于约束下限值
        if (node != root_ && static_cast<int>(leaf->keys.size()) < (order_ + 1) / 2) return false;
        // 校验单页内部主键是否维持绝对的严格升序排列
        if (!std::is_sorted(leaf->keys.begin(), leaf->keys.end())) return false;
        min_key = leaf->keys.front();
        max_key = leaf->keys.back();
        return true;
    }

    auto* non_leaf = static_cast<NonLeafNode*>(node);
    if (non_leaf->keys.empty()) return false;
    // 校验非叶子路由节点的下限界限
    if (node != root_ && static_cast<int>(non_leaf->keys.size()) < order_ / 2) return false;
    if (non_leaf->children.size() != non_leaf->keys.size() + 1) return false;

    KeyType child_min, child_max;
    for (size_t i = 0; i < non_leaf->children.size(); ++i) {
        if (!validateNode(non_leaf->children[i], child_min, child_max)) return false;
        // 冗余分界界限规则强悍校验
        if (i > 0 && child_min < non_leaf->keys[i - 1]) return false;
        if (i < non_leaf->keys.size() && child_max > non_leaf->keys[i]) return false;
        if (i == 0) min_key = child_min;
        if (i == non_leaf->children.size() - 1) max_key = child_max;
    }
    return true;
}