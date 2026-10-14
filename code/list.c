/*
 * list.c —— 链式存储基础操作模块
 * 包含：节点创建、尾插、查找、删除、统计、字段交换、释放
 */
#include "goods.h"

/* 创建节点：字段做安全截断拷贝，next 置空 */
Goods *list_create_node(const char *id, const char *name, const char *factory,
                        const char *batch, int qty, double price)
{
    Goods *p = (Goods *)malloc(sizeof(Goods));
    if (!p) return NULL;

    snprintf(p->id,      ID_LEN,      "%s", id);
    snprintf(p->name,    NAME_LEN,    "%s", name);
    snprintf(p->factory, FACTORY_LEN, "%s", factory);
    snprintf(p->batch,   BATCH_LEN,   "%s", batch);
    p->qty   = qty;
    p->price = price;
    p->next  = NULL;
    return p;
}

/* 尾插法：保持与文件中的记录顺序一致 */
void list_append(Goods **head, Goods *node)
{
    Goods *cur;

    if (!head || !node) return;
    node->next = NULL;

    if (*head == NULL) {
        *head = node;
        return;
    }
    cur = *head;
    while (cur->next) cur = cur->next;
    cur->next = node;
}

/* 按编号查找，返回首个匹配节点 */
Goods *list_find_by_id(Goods *head, const char *id)
{
    while (head) {
        if (strcmp(head->id, id) == 0) return head;
        head = head->next;
    }
    return NULL;
}

/* 按【编号+厂家+批次+单价】四字段精确匹配，用于入库判重 */
Goods *list_find_match(Goods *head, const char *id, const char *factory,
                       const char *batch, double price)
{
    while (head) {
        if (strcmp(head->id, id) == 0 &&
            strcmp(head->factory, factory) == 0 &&
            strcmp(head->batch, batch) == 0 &&
            (head->price - price < EPS) && (price - head->price < EPS)) {
            return head;
        }
        head = head->next;
    }
    return NULL;
}

/* 按名称模糊查找：从 from 开始找下一个命中项（from 为 NULL 表示从头开始） */
Goods *list_find_by_name(Goods *head, const char *keyword, Goods *from)
{
    Goods *cur = from ? from->next : head;

    while (cur) {
        if (strstr(cur->name, keyword) != NULL) return cur;
        cur = cur->next;
    }
    return NULL;
}

/* 按编号删除，需修改头指针故传二级指针 */
int list_delete_by_id(Goods **head, const char *id)
{
    Goods *cur, *prev;

    if (!head || !*head) return 0;

    prev = NULL;
    cur  = *head;
    while (cur) {
        if (strcmp(cur->id, id) == 0) {
            if (prev) prev->next = cur->next;
            else      *head = cur->next;
            free(cur);
            return 1;
        }
        prev = cur;
        cur  = cur->next;
    }
    return 0;
}

/* 节点总数（货品种类数） */
int list_count(Goods *head)
{
    int n = 0;
    while (head) { n++; head = head->next; }
    return n;
}

/* 库存总数量 */
long list_total_qty(Goods *head)
{
    long sum = 0;
    while (head) { sum += head->qty; head = head->next; }
    return sum;
}

/* 库存总金额 = Σ(数量 × 单价) */
double list_total_amount(Goods *head)
{
    double sum = 0.0;
    while (head) { sum += head->qty * head->price; head = head->next; }
    return sum;
}

/* 交换两个节点的数据域（不含 next），用于链表排序 */
void list_swap_fields(Goods *a, Goods *b)
{
    char   t_id[ID_LEN], t_name[NAME_LEN], t_factory[FACTORY_LEN], t_batch[BATCH_LEN];
    int    t_qty;
    double t_price;

    if (a == b) return;

    strcpy(t_id,      a->id);      strcpy(a->id,      b->id);      strcpy(b->id,      t_id);
    strcpy(t_name,    a->name);    strcpy(a->name,    b->name);    strcpy(b->name,    t_name);
    strcpy(t_factory, a->factory); strcpy(a->factory, b->factory); strcpy(b->factory, t_factory);
    strcpy(t_batch,   a->batch);   strcpy(a->batch,   b->batch);   strcpy(b->batch,   t_batch);
    t_qty      = a->qty;      a->qty      = b->qty;      b->qty      = t_qty;
    t_price    = a->price;    a->price    = b->price;    b->price    = t_price;
}

/* 释放整条链表 */
void list_free(Goods **head)
{
    Goods *cur, *next;

    if (!head) return;
    cur = *head;
    while (cur) {
        next = cur->next;
        free(cur);
        cur = next;
    }
    *head = NULL;
}
