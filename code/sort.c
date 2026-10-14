/*
 * sort.c —— 排序模块（五种算法 + 性能对比）
 *
 * 题目要求"根据学号等进行排序，多种排序采用不同的算法"，
 * 本模块实现五种内部排序，并统计「比较次数 / 移动次数」供报告做算法分析：
 *
 *   冒泡排序  O(N^2)，带提前退出，最好 O(N)
 *   选择排序  O(N^2)，比较次数恒定 N(N-1)/2，移动次数少
 *   直接插入  O(N^2)，带"交换式"实现，接近有序时表现好
 *   希尔排序  O(N^1.3) 左右，间隔递减（gap = N/2, N/4, ..., 1）
 *   快速排序  O(N log N) 平均，链表分区法，本例取首元素为基准
 *
 * 实现约定：所有算法都直接作用于链表，通过交换节点数据域完成排序，
 * 不额外申请数组，空间复杂度均为 O(1)。
 */
#include "goods.h"

/* ---------- 内部辅助 ---------- */

static double key_of(Goods *g, int field)
{
    return (field == 1) ? g->price : (double)g->qty;
}

static int is_before(Goods *a, Goods *b, int field, int asc)
{
    double va = key_of(a, field);
    double vb = key_of(b, field);
    return asc ? (va < vb) : (va > vb);
}

/* 深拷贝整条链表：让多种算法在完全相同的乱序数据上起跑，对比才公平 */
static Goods *list_clone(Goods *head)
{
    Goods *nh = NULL;

    while (head) {
        Goods *n = list_create_node(head->id, head->name, head->factory,
                                    head->batch, head->qty, head->price);
        if (!n) break;
        list_append(&nh, n);
        head = head->next;
    }
    return nh;
}

/* ---------- 1. 冒泡排序 ---------- */
void sort_bubble(Goods *head, int field, int asc, long *cmp, long *mov)
{
    Goods *p, *q;
    int    swapped;

    if (cmp) *cmp = 0;
    if (mov) *mov = 0;
    if (!head) return;

    for (p = head; p; p = p->next) {
        swapped = 0;
        for (q = head; q->next; q = q->next) {
            if (cmp) (*cmp)++;
            if (!is_before(q, q->next, field, asc)) {
                list_swap_fields(q, q->next);
                if (mov) (*mov) += 3;
                swapped = 1;
            }
        }
        if (!swapped) break;        /* 本趟无交换 => 已有序，提前结束 */
    }
}

/* ---------- 2. 简单选择排序 ---------- */
void sort_select(Goods *head, int field, int asc, long *cmp, long *mov)
{
    Goods *p, *q, *best;

    if (cmp) *cmp = 0;
    if (mov) *mov = 0;
    if (!head) return;

    for (p = head; p; p = p->next) {
        best = p;
        for (q = p->next; q; q = q->next) {
            if (cmp) (*cmp)++;
            if (is_before(q, best, field, asc)) best = q;
        }
        if (best != p) {
            list_swap_fields(p, best);
            if (mov) (*mov) += 3;
        }
    }
}

/* ---------- 3. 直接插入排序（交换式实现，避免链表断链重接） ---------- */
void sort_insert(Goods *head, int field, int asc, long *cmp, long *mov)
{
    Goods *p, *q, *prev;

    if (cmp) *cmp = 0;
    if (mov) *mov = 0;
    if (!head || !head->next) return;

    /* 从第 2 个节点起，每个元素向前交换到它在有序区中的位置 */
    for (p = head->next; p; p = p->next) {
        q = p;
        while (q != head) {
            prev = head;
            while (prev->next != q) prev = prev->next;   /* 找前驱 */
            if (cmp) (*cmp)++;
            if (is_before(q, prev, field, asc)) {
                list_swap_fields(prev, q);
                if (mov) (*mov) += 3;
                q = prev;
            } else {
                break;                                   /* 已落位 */
            }
        }
    }
}

/* ---------- 4. 希尔排序（间隔递减：N/2, N/4, ..., 1） ---------- */
void sort_shell(Goods *head, int field, int asc, long *cmp, long *mov)
{
    int    n, gap, i, swapped;
    Goods *a, *b;

    if (cmp) *cmp = 0;
    if (mov) *mov = 0;
    if (!head) return;

    n = list_count(head);
    if (n < 2) return;

    for (gap = n / 2; gap > 0; gap /= 2) {
        do {
            swapped = 0;
            a = head;
            b = head;
            for (i = 0; i < gap && b; i++) b = b->next;   /* b = a + gap */

            while (b) {
                if (cmp) (*cmp)++;
                if (!is_before(a, b, field, asc)) {       /* 间隔为 gap 的逆序对 */
                    list_swap_fields(a, b);
                    if (mov) (*mov) += 3;
                    swapped = 1;
                }
                a = a->next;
                b = b->next;
            }
        } while (swapped);                                 /* 该间隔下不再交换为止 */
    }
}

/* ---------- 5. 快速排序（链表分区法） ---------- */
static void quick_rec(Goods *lo, Goods *hi, int field, int asc, long *cmp, long *mov)
{
    Goods *store, *p;

    /* 区间 [lo, hi) 长度不足 2 时结束 */
    if (lo == NULL || lo == hi || lo->next == hi) return;

    /* 以 lo 的值为基准，把比基准小的节点依次换到前面 */
    store = lo;
    for (p = lo->next; p != hi; p = p->next) {
        if (cmp) (*cmp)++;
        if (is_before(p, lo, field, asc)) {
            store = store->next;
            if (store != p) {
                list_swap_fields(store, p);
                if (mov) (*mov) += 3;
            }
        }
    }
    /* 基准归位 */
    if (store != lo) {
        list_swap_fields(store, lo);
        if (mov) (*mov) += 3;
    }

    quick_rec(lo, store, field, asc, cmp, mov);            /* 左半区 */
    quick_rec(store->next, hi, field, asc, cmp, mov);      /* 右半区 */
}

void sort_quick(Goods *head, int field, int asc, long *cmp, long *mov)
{
    if (cmp) *cmp = 0;
    if (mov) *mov = 0;
    quick_rec(head, NULL, field, asc, cmp, mov);
}

/* ---------- 排序菜单 ---------- */
void sort_menu(Goods *head)
{
    static const char *ALGO_NAME[5] = {
        "冒泡排序", "选择排序", "直接插入排序", "希尔排序", "快速排序"
    };

    int   field, dir, algo, asc, i;
    long  cmp[5] = {0}, mov[5] = {0};
    const char *fname;

    if (!head) {
        printf("\n  (暂无货品数据，无法排序)\n");
        return;
    }

    printf("\n----- 货品排序 -----\n");
    printf("  1. 按单价排序\n");
    printf("  2. 按库存数量排序\n");
    printf("  0. 返回\n");
    if (!io_read_int("请选择排序字段: ", 0, 2, &field) || field == 0) return;

    printf("\n  1. 升序    2. 降序\n");
    if (!io_read_int("请选择排序方向: ", 1, 2, &dir)) return;
    asc = (dir == 1);

    printf("\n  1. 冒泡排序\n");
    printf("  2. 选择排序\n");
    printf("  3. 直接插入排序\n");
    printf("  4. 希尔排序\n");
    printf("  5. 快速排序\n");
    printf("  6. 五种算法全面对比（性能实测）\n");
    if (!io_read_int("请选择排序算法: ", 1, 6, &algo)) return;

    fname = (field == 1) ? "单价" : "库存数量";

    /* ---------- 六种算法全面对比 ---------- */
    if (algo == 6) {
        Goods *c[5];
        int    n = list_count(head);

        for (i = 0; i < 5; i++) c[i] = list_clone(head);

        sort_bubble(c[0], field, asc, &cmp[0], &mov[0]);
        sort_select(c[1], field, asc, &cmp[1], &mov[1]);
        sort_insert(c[2], field, asc, &cmp[2], &mov[2]);
        sort_shell (c[3], field, asc, &cmp[3], &mov[3]);
        sort_quick (c[4], field, asc, &cmp[4], &mov[4]);

        for (i = 0; i < 5; i++) list_free(&c[i]);

        printf("\n=============== 五种排序算法性能对比 ===============\n");
        printf("  排序字段：%s    方向：%s    记录数 N = %d\n\n",
               fname, asc ? "升序" : "降序", n);
        printf("  %-18s %14s %14s %18s\n", "算法", "比较次数", "移动次数", "平均时间复杂度");
        printf("  ------------------------------------------------------------------------\n");
        printf("  %-18s %14ld %14ld %18s\n", ALGO_NAME[0], cmp[0], mov[0], "O(N^2) 最好O(N)");
        printf("  %-18s %14ld %14ld %18s\n", ALGO_NAME[1], cmp[1], mov[1], "O(N^2)");
        printf("  %-18s %14ld %14ld %18s\n", ALGO_NAME[2], cmp[2], mov[2], "O(N^2) 最好O(N)");
        printf("  %-18s %14ld %14ld %18s\n", ALGO_NAME[3], cmp[3], mov[3], "约 O(N^1.3)");
        printf("  %-18s %14ld %14ld %18s\n", ALGO_NAME[4], cmp[4], mov[4], "O(N log N) 平均");
        printf("  ------------------------------------------------------------------------\n");
        printf("  说明：五种算法在同一份乱序数据上各跑一遍，均只交换节点数据域，\n");
        printf("        空间复杂度都是 O(1)。数据量越大，快排 / 希尔 的优势越明显\n");
        printf("        （可用菜单 13 生成上千条测试数据后再来对比）。\n");

        file_log("排序对比 字段%s %s N=%d | 冒泡 %ld/%ld | 选择 %ld/%ld | 插入 %ld/%ld | 希尔 %ld/%ld | 快排 %ld/%ld",
                 fname, asc ? "升序" : "降序", n,
                 cmp[0], mov[0], cmp[1], mov[1], cmp[2], mov[2],
                 cmp[3], mov[3], cmp[4], mov[4]);

        /* 原链表按快速排序落地 */
        sort_quick(head, field, asc, NULL, NULL);
        printf("\n  已按%s排序并保存（本次采用快速排序作为落地算法）。\n", fname);
        io_print_all(head);
        if (file_save(head, DATA_FILE) < 0)
            printf("  ! 警告：数据文件写入失败。\n");
        return;
    }

    /* ---------- 单算法排序 ---------- */
    {
        int idx = algo - 1;

        switch (algo) {
        case 1: sort_bubble(head, field, asc, &cmp[idx], &mov[idx]); break;
        case 2: sort_select(head, field, asc, &cmp[idx], &mov[idx]); break;
        case 3: sort_insert(head, field, asc, &cmp[idx], &mov[idx]); break;
        case 4: sort_shell (head, field, asc, &cmp[idx], &mov[idx]); break;
        default: sort_quick (head, field, asc, &cmp[idx], &mov[idx]); break;
        }

        printf("\n  √ %s完成：比较 %ld 次，移动 %ld 次。\n",
               ALGO_NAME[idx], cmp[idx], mov[idx]);
        file_log("排序 %s 字段%s %s 比较%ld 移动%ld",
                 ALGO_NAME[idx], fname, asc ? "升序" : "降序", cmp[idx], mov[idx]);
    }

    printf("\n  排序结果如下：\n");
    io_print_all(head);

    if (file_save(head, DATA_FILE) < 0)
        printf("  ! 警告：数据文件写入失败（可能被占用）。\n");
}
