#include "Bplus_Tree.h"
#include <algorithm>
#include<iostream>

// 初始化 B+ 树的阶数约束
void BPlusTree::init(int order) {
    // 核心约束：B+ 树的阶数 M 至少为 3，否则无法实现合法的分裂与借调逻辑
    if (order < 3) {
        throw std::invalid_argument("B+ Tree order must be greater than or equal to 3.");
    }
    order_ = order; // 设置自定义阶数
    root_ = nullptr; // 根节点清空
    head_ = nullptr; // 双向链表头清空
}

// 销毁整棵树，释放所有物理页面节点
void BPlusTree::destroy() {
    if (root_) {
        recursiveDestroy(root_); // 递归销毁整个倒置树形结构
        root_ = nullptr;
    }
    head_ = nullptr;
}

// 私有深度优先递归析构函数
void BPlusTree::recursiveDestroy(BPlusNode* node) {
    if (node->type == NodeType::NON_LEAF) {
        // 如果是非叶子节点，强制转换为 NonLeafNode 指针并遍历释放所有子节点
        auto* non_leaf = static_cast<NonLeafNode*>(node);
        for (BPlusNode* child : non_leaf->children) {
            recursiveDestroy(child); // 递归向下清理
        }
    }
    delete node; // 销毁当前节点，释放内存
}

// 磁盘索引路由寻道：根据 key 自上而下寻找其应归属的叶子节点
LeafNode* BPlusTree::findLeaf(KeyType key) {
    if (!root_) return nullptr; // 空树返回空
    BPlusNode* curr = root_;
    // 模拟轻量级非叶子节点索引文件的寻道过程
    while (curr->type == NodeType::NON_LEAF) {
        auto* non_leaf = static_cast<NonLeafNode*>(curr);
        // 使用 std::upper_bound 在有序数组中通过二分查找准确定位首个大于 key 的位置
        auto it = std::upper_bound(non_leaf->keys.begin(), non_leaf->keys.end(), key);
        // 计算目标子树在 children 数组中的索引偏移量
        int idx = std::distance(non_leaf->keys.begin(), it);
        curr = non_leaf->children[idx]; // 深入下一层
    }
    return static_cast<LeafNode*>(curr); // 返回最终定位的物理叶子数据页
}

// 点对点等值查询接口
DataAddr BPlusTree::search(KeyType key) {
    LeafNode* leaf = findLeaf(key); // 首先定位到数据所在的叶子节点
    if (!leaf) return -1; // 树为空则未命中

    // 在叶子节点内部执行二分查找，检索是否存在匹配的主键
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        // 键值匹配成功，计算其索引位置
        int idx = std::distance(leaf->keys.begin(), it);
        return leaf->data_addrs[idx]; // 返回模拟的磁盘记录物理地址
    }
    return -1; // 索引未命中，返回非法的 -1 地址
}

// 区间范围查询接口，充分利用叶子节点双向链表的磁盘连续预读特性
std::vector<DataAddr> BPlusTree::rangeSearch(KeyType l, KeyType r) {
    std::vector<DataAddr> results;
    if (l > r) return results; // 非法边界直接返回空

    LeafNode* curr = findLeaf(l); // O(log N) 寻道定位到区间的左边界叶子节点
    if (!curr) return results;

    bool keep_scanning = true; // 循环扫描控制开关
    while (curr && keep_scanning) {
        // 遍历当前叶子节点内部的所有有序键值对
        for (size_t i = 0; i < curr->keys.size(); ++i) {
            if (curr->keys[i] >= l && curr->keys[i] <= r) {
                results.push_back(curr->data_addrs[i]); // 搜集符合闭区间要求的物理数据
            }
            if (curr->keys[i] > r) {
                keep_scanning = false; // 超出右边界，立刻截断扫描流程
                break;
            }
        }
        curr = curr->next; // 沿着叶子节点的 next 指针直接进行物理页间跳转，无须再回溯父节点
    }
    return results;
}

// 按主键有序插入接口
bool BPlusTree::insert(KeyType key, DataAddr addr) {
    if (!root_) {
        // 边界：处理空树状态，首次插入直接作为根叶子
        auto* new_root = new LeafNode();
        new_root->keys.push_back(key);
        new_root->data_addrs.push_back(addr);
        root_ = new_root;
        head_ = new_root; // 初始化双向链表的头部
        return true;
    }

    LeafNode* leaf = findLeaf(key); // 寻寻道定位叶子页
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        return false; // 去重规则约束：主键 key 唯一，重复直接拒绝并报错
    }

    insertIntoLeaf(leaf, key, addr); // 将元素有序安全插入叶子中
    
    // 检查是否破坏了 B+ 树的阶数上界约束（节点数据项不能超过 M-1 这一标准，此处临时允许达到 M 进行后续分裂）
    if (static_cast<int>(leaf->keys.size()) >= order_) {
        splitLeaf(leaf); // 触发叶子节点分裂调整
    }
    return true;
}

// 有序插入叶子节点的辅助实现
void BPlusTree::insertIntoLeaf(LeafNode* leaf, KeyType key, DataAddr addr) {
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    int idx = std::distance(leaf->keys.begin(), it);
    leaf->keys.insert(it, key); // 插入键
    leaf->data_addrs.insert(leaf->data_addrs.begin() + idx, addr); // 插入相对应的物理数据地址
}

// 叶子节点满触发的分裂逻辑
void BPlusTree::splitLeaf(LeafNode* leaf) {
    auto* new_leaf = new LeafNode(); // 创建分裂后的右侧新叶子节点
    int split_idx = order_ / 2;     // 确定中间分裂点的物理索引位置

    // 将原叶子节点的右半部分数据彻底迁移（拷贝并分配）给新叶子
    new_leaf->keys.assign(leaf->keys.begin() + split_idx, leaf->keys.end());
    new_leaf->data_addrs.assign(leaf->data_addrs.begin() + split_idx, leaf->data_addrs.end());

    // 擦除原叶子节点中已被搬移迁走的右半部分数据项
    leaf->keys.erase(leaf->keys.begin() + split_idx, leaf->keys.end());
    leaf->data_addrs.erase(leaf->data_addrs.begin() + split_idx, leaf->data_addrs.end());

    // 精密维护底层物理页的双向有序链表指针
    new_leaf->next = leaf->next;
    if (leaf->next) leaf->next->prev = new_leaf;
    leaf->next = new_leaf;
    new_leaf->prev = leaf;

    // 提取新叶子的首个主键，作为冗余路由键向上提级插入到父节点中
    KeyType split_key = new_leaf->keys.front();
    insertIntoParent(leaf, split_key, new_leaf);
}

// 递归自底向上调整父节点索引
void BPlusTree::insertIntoParent(BPlusNode* old_node, KeyType new_key, BPlusNode* new_node) {
    if (old_node == root_) {
        // 如果原来的节点就是整个索引树的根节点，说明根节点发生分裂，树高度递增
        auto* new_root = new NonLeafNode(); // 创建全新的非叶子根节点
        new_root->keys.push_back(new_key); // 填入上提的路由键
        new_root->children.push_back(old_node); // 左子树指向旧根
        new_root->children.push_back(new_node); // 右子树指向新分裂节点
        old_node->parent = new_root;
        new_node->parent = new_root;
        root_ = new_root; // 重置整棵树的根句柄
        return;
    }

    auto* parent = static_cast<NonLeafNode*>(old_node->parent);
    auto it = std::lower_bound(parent->keys.begin(), parent->keys.end(), new_key);
    int idx = std::distance(parent->keys.begin(), it);

    // 将上提的 key 及其对应的右子树指针顺次插入到父索引节点的有序队列中
    parent->keys.insert(it, new_key);
    parent->children.insert(parent->children.begin() + idx + 1, new_node);
    new_node->parent = parent; // 绑定父子纽带关系

    // 非叶子节点的关键字数量不能等于阶数 M（键上限为 M-1，分支上限为 M）
    if (static_cast<int>(parent->keys.size()) >= order_) {
        splitNonLeaf(parent); // 递归触发非叶子节点的向上分裂
    }
}

// 非叶子节点（索引路由节点）的分裂逻辑
void BPlusTree::splitNonLeaf(NonLeafNode* node) {
    auto* new_non_leaf = new NonLeafNode(); // 创建右侧新索引节点
    int split_idx = node->keys.size() / 2;   // 选取核心对称中点

    // 提取上提到上一层的 Key，注意：该 Key 在当前层级将被直接抽离（B+树非叶子键不重复保留）
    KeyType push_up_key = node->keys[split_idx];

    // 分配右半部分的键与子树分支给新创建的非叶子节点
    new_non_leaf->keys.assign(node->keys.begin() + split_idx + 1, node->keys.end());
    new_non_leaf->children.assign(node->children.begin() + split_idx + 1, node->children.end());

    // 重新修正右半部分子树的 parent 指针归属，使其全面指向右侧新生的非叶子节点
    for (BPlusNode* child : new_non_leaf->children) {
        child->parent = new_non_leaf;
    }

    // 从原索引节点中擦除已经被分割上移及划分给右侧的所有元素项
    node->keys.erase(node->keys.begin() + split_idx, node->keys.end());
    node->children.erase(node->children.begin() + split_idx + 1, node->children.end());

    // 继续向更高层级的父节点递归递归提交分裂结果
    insertIntoParent(node, push_up_key, new_non_leaf);
}

// 主键删除接口
bool BPlusTree::remove(KeyType key) {
    LeafNode* leaf = findLeaf(key); // 寻道定位到对应的叶子节点
    if (!leaf) return false;

    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end() || *it != key) {
        return false; // 键不存在，直接宣告删除失败
    }

    removeFromLeaf(leaf, key); // 启动底层的物理叶子页擦除与自平衡流
    return true;
}

// 物理擦除叶子内记录的自平衡逻辑
void BPlusTree::removeFromLeaf(LeafNode* leaf, KeyType key) {
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    int idx = std::distance(leaf->keys.begin(), it);
    leaf->keys.erase(it); // 移除键
    leaf->data_addrs.erase(leaf->data_addrs.begin() + idx); // 移除物理磁盘地址

    if (leaf == root_) {
        // 如果当前叶子就是根节点（即树高只有1），直接检查是否被完全清空
        if (leaf->keys.empty()) {
            delete root_;
            root_ = nullptr;
            head_ = nullptr; // 树退化为空树结构
        }
        return;
    }

    // 检查是否打破了 B+ 树的下界平衡约束：数据记录数不得低于 ceil(M / 2)
    int min_size = (order_ + 1) / 2;
    if (static_cast<int>(leaf->keys.size()) < min_size) {
        handleUnderflow(leaf); // 启动底层的下溢平衡处理（借调或物理合并）
    }
}

// 自底向上维护处理节点下溢（Underflow）现象
void BPlusTree::handleUnderflow(BPlusNode* node) {
    if (node == root_) {
        // 边界：若当前节点已回溯到整棵树的根节点
        if (node->type == NodeType::NON_LEAF) {
            auto* non_leaf = static_cast<NonLeafNode*>(node);
            if (non_leaf->keys.empty()) {
                // 如果非叶子根节点被完全借空，则直接将其唯一的独生子提拔为新根，树高度成功递减 1
                root_ = non_leaf->children.front();
                root_->parent = nullptr;
                delete non_leaf;
            }
        }
        return;
    }

    auto* parent = static_cast<NonLeafNode*>(node->parent);
    int idx = 0;
    // 找出当前节点在父节点分支 children 数组中的精确物理下标位置
    while (idx <= static_cast<int>(parent->keys.size()) && parent->children[idx] != node) {
        idx++;
    }

    // 核心平衡策略 1：尝试向左侧或右侧的亲兄弟节点秘密借调多余元素
    if (borrowFromSibling(node, parent, idx)) return;

    // 核心平衡策略 2：若邻居均处于饱合下限状态，则强行触发与左侧或右侧邻居的物理页面大合并
    mergeSiblings(node, parent, idx);

    // 计算当前非叶子父节点应当持有的最小键数限制值
    int min_keys = (parent == root_) ? 1 : (order_ / 2);
    int current_keys = static_cast<int>(parent->keys.size());

    // 若父节点同样陷入了下溢泥潭，则将 parent 作为当前节点，继续向上递归回溯自平衡
    if (current_keys < min_keys) {
        handleUnderflow(parent);
    }
}

// 借调邻居节点项的详细逻辑实现
bool BPlusTree::borrowFromSibling(BPlusNode* node, NonLeafNode* parent, int idx) {
    // 判定节点是否满足下限的临界值
    int min_size = (node->type == NodeType::LEAF) ? ((order_ + 1) / 2) : (order_ / 2);

    // 策略 A：尝试向亲左侧邻居借调
    if (idx > 0) {
        BPlusNode* left_sib = parent->children[idx - 1];
        int left_size = (left_sib->type == NodeType::LEAF) ? 
            static_cast<int>(static_cast<LeafNode*>(left_sib)->keys.size()) : 
            static_cast<int>(static_cast<NonLeafNode*>(left_sib)->keys.size());

        if (left_size > min_size) {
            if (node->type == NodeType::LEAF) {
                auto* curr = static_cast<LeafNode*>(node);
                auto* sib = static_cast<LeafNode*>(left_sib);
                // 将左邻居的末尾最大项直接移植到当前节点的最前端首位
                curr->keys.insert(curr->keys.begin(), sib->keys.back());
                curr->data_addrs.insert(curr->data_addrs.begin(), sib->data_addrs.back());
                sib->keys.pop_back();
                sib->data_addrs.pop_back();
                parent->keys[idx - 1] = curr->keys.front(); // 严格修正父节点级路由关键字冗余项
            } else {
                auto* curr = static_cast<NonLeafNode*>(node);
                auto* sib = static_cast<NonLeafNode*>(left_sib);
                // 非叶子节点借调：必须经过父节点实施“旋转”下移
                curr->keys.insert(curr->keys.begin(), parent->keys[idx - 1]);
                curr->children.insert(curr->children.begin(), sib->children.back());
                curr->children.front()->parent = curr; // 转换父子映射
                parent->keys[idx - 1] = sib->keys.back(); // 父节点同步
                sib->keys.pop_back();
                sib->children.pop_back();
            }
            return true; // 借调成功，中断平衡流
        }
    }

    // 策略 B：尝试向亲右侧邻居借调
    if (idx < static_cast<int>(parent->children.size()) - 1) {
        BPlusNode* right_sib = parent->children[idx + 1];
        int right_size = (right_sib->type == NodeType::LEAF) ? 
            static_cast<int>(static_cast<LeafNode*>(right_sib)->keys.size()) : 
            static_cast<int>(static_cast<NonLeafNode*>(right_sib)->keys.size());

        if (right_size > min_size) {
            if (node->type == NodeType::LEAF) {
                auto* curr = static_cast<LeafNode*>(node);
                auto* sib = static_cast<LeafNode*>(right_sib);
                // 将右邻居的首位最小项追加移植到当前节点的末尾位置
                curr->keys.push_back(sib->keys.front());
                curr->data_addrs.push_back(sib->data_addrs.front());
                sib->keys.erase(sib->keys.begin());
                sib->data_addrs.erase(sib->data_addrs.begin());
                parent->keys[idx] = sib->keys.front(); // 严格更新分界关键字
            } else {
                auto* curr = static_cast<NonLeafNode*>(node);
                auto* sib = static_cast<NonLeafNode*>(right_sib);
                // 非叶子右侧借调：父路由键下沉旋转
                curr->keys.push_back(parent->keys[idx]);
                curr->children.push_back(sib->children.front());
                curr->children.back()->parent = curr;
                parent->keys[idx] = sib->keys.front();
                sib->keys.erase(sib->keys.begin());
                sib->children.erase(sib->children.begin());
            }
            return true; // 借调成功
        }
    }
    return false; // 邻居均无富余元素，只能宣告借调失败，滑入合并深渊
}

// 物理合并两个濒临空置的兄弟叶子或索引页面节点
void BPlusTree::mergeSiblings(BPlusNode* node, NonLeafNode* parent, int idx) {
    if (idx > 0) {
        // 优先将当前节点彻底合入到其左兄弟节点之中
        BPlusNode* left_sib = parent->children[idx - 1];
        if (node->type == NodeType::LEAF) {
            auto* curr = static_cast<LeafNode*>(node);
            auto* sib = static_cast<LeafNode*>(left_sib);
            // 拼接两个物理叶子的有序 vector
            sib->keys.insert(sib->keys.end(), curr->keys.begin(), curr->keys.end());
            sib->data_addrs.insert(sib->data_addrs.end(), curr->data_addrs.begin(), curr->data_addrs.end());
            // 重新缝合叶子节点的双向跨页水平链表指针
            sib->next = curr->next;
            if (curr->next) curr->next->prev = sib;
            delete curr; // 物理回收废弃的空闲叶子页面
        } else {
            auto* curr = static_cast<NonLeafNode*>(node);
            auto* sib = static_cast<NonLeafNode*>(left_sib);
            // 非叶子合并：必须把父节点对应的分界路由主键也拉下来一同合并
            sib->keys.push_back(parent->keys[idx - 1]);
            sib->keys.insert(sib->keys.end(), curr->keys.begin(), curr->keys.end());
            sib->children.insert(sib->children.end(), curr->children.begin(), curr->children.end());
            for (BPlusNode* child : curr->children) child->parent = sib;
            delete curr;
        }
        // 从父非叶子节点中无情抹除该路由分界关键字以及已经不复存在的子树指针
        parent->keys.erase(parent->keys.begin() + idx - 1);
        parent->children.erase(parent->children.begin() + idx);
    } else {
        // 如果没有左兄弟，则将右兄弟强行合入到当前节点之中
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

// 全量水平顺序遍历扫描接口
void BPlusTree::traverse() {
    LeafNode* curr = head_;
    std::cout << "[TRAVERSE OUTPUT] Beginning linear scanning along the sequential doubly linked list:\n                 ";
    while (curr) {
        for (const auto& key : curr->keys) {
            std::cout << key << " -> ";
        }
        curr = curr->next; // 磁盘连续块预读流转
    }
    std::cout << "NULL\n";
}

// 公开获取树高的接口
int BPlusTree::getHeight() const {
    return calculateHeight(root_);
}

// 递归计算树高的私有辅助函数
int BPlusTree::calculateHeight(BPlusNode* node) const {
    if (!node) return 0;
    int height = 1;
    BPlusNode* curr = node;
    // 只用沿着最左侧子树一路向下探测直到触达底层叶子节点，即可计算出完全平衡的 B+ 树层高
    while (curr->type == NodeType::NON_LEAF) {
        curr = static_cast<NonLeafNode*>(curr)->children.front();
        height++;
    }
    return height;
}

// 层级打印输出，图形化直观呈现标准 M 阶 B+ 树的物理形态
void BPlusTree::printTree() const {
    if (!root_) {
        std::cout << "[VISUAL TREE] Empty Tree State.\n";
        return;
    }
    std::queue<BPlusNode*> q; // 利用队列进行经典的广度优先按层级检索
    q.push(root_);
    int level = 0;
    std::cout << "===========================[ VISUAL B+ TREE CONFIGURATION ]===========================\n";
    while (!q.empty()) {
        int sz = q.size();
        std::cout << "  Level " << level << " Pages : ";
        for (int i = 0; i < sz; ++i) {
            BPlusNode* curr = q.front();
            q.pop();
            std::cout << "[";
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
                    q.push(child); // 子页面入队供下一层级打印输出
                }
            }
            std::cout << "]  ";
        }
        std::cout << "\n";
        level++;
    }
    std::cout << "======================================================================================\n";
}

// 全树自平衡合法性以及阶数边界规则的严格检测接口
bool BPlusTree::validate() const {
    if (!root_) return true;
    KeyType min_k, max_k;
    return validateNode(root_, min_k, max_k);
}

// 递归检验核心逻辑
bool BPlusTree::validateNode(BPlusNode* node, KeyType& min_key, KeyType& max_key) const {
    if (node->type == NodeType::LEAF) {
        auto* leaf = static_cast<LeafNode*>(node);
        if (leaf->keys.empty()) return false;
        // 如果当前数据页不是整棵树的独苗根，其持有的元素量决不能低于 ceil(M/2) 这一标准约束
        if (node != root_ && static_cast<int>(leaf->keys.size()) < (order_ + 1) / 2) return false;
        // 校验叶子节点内部主键是否维持严格升序规则排列
        if (!std::is_sorted(leaf->keys.begin(), leaf->keys.end())) return false;
        min_key = leaf->keys.front();
        max_key = leaf->keys.back();
        return true;
    }

    auto* non_leaf = static_cast<NonLeafNode*>(node);
    if (non_leaf->keys.empty()) return false;
    // 校验非叶子节点的子分支下限界限
    if (node != root_ && static_cast<int>(non_leaf->keys.size()) < order_ / 2) return false;
    if (non_leaf->children.size() != non_leaf->keys.size() + 1) return false;

    KeyType child_min, child_max;
    for (size_t i = 0; i < non_leaf->children.size(); ++i) {
        if (!validateNode(non_leaf->children[i], child_min, child_max)) return false;
        // 冗余规则及路由界限严格校验
        if (i > 0 && child_min < non_leaf->keys[i - 1]) return false;
        if (i < non_leaf->keys.size() && child_max > non_leaf->keys[i]) return false;
        if (i == 0) min_key = child_min;
        if (i == non_leaf->children.size() - 1) max_key = child_max;
    }
    return true;
}