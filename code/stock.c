/*
 * stock.c —— 出入库业务模块（本系统的核心逻辑）
 *
 * 关键业务规则（题目要求原文）：
 *   入库：同一编号、生产厂商、批次及单价的产品只做数量累加，不添加记录。
 *   出库：修改库存并存入文件，注意输入的合法性。
 *   警示：当货品数量低于指定值，给出补货提示。
 */
#include "goods.h"

/*
 * 入库：录入 → 四字段判重 → 累加或新增 → 存盘 → 记日志
 * 返回 1 成功，0 取消/失败
 */
int stock_in(Goods **head)
{
    Goods  tmp;
    Goods *hit;
    int    before;

    if (!io_input_goods(&tmp)) return 0;

    if (tmp.qty <= 0) {
        printf("\n  ! 入库数量必须大于 0，本次入库取消。\n");
        return 0;
    }

    hit = list_find_match(*head, tmp.id, tmp.factory, tmp.batch, tmp.price);
    if (hit) {
        /* 命中判重规则：只累加数量，不新增记录 */
        before   = hit->qty;
        hit->qty += tmp.qty;
        printf("\n  √ 该货品已存在（编号/厂家/批次/单价完全一致），执行数量累加：\n");
        printf("    %s %s | %s | 批次 %s | %d → %d\n",
               hit->id, hit->name, hit->factory, hit->batch, before, hit->qty);
        file_log("入库(累加) %s %s 批次%s 数量 +%d (%d→%d) 单价%.2f",
                 hit->id, hit->name, hit->batch, tmp.qty, before, hit->qty, hit->price);
    } else {
        Goods *node = list_create_node(tmp.id, tmp.name, tmp.factory,
                                       tmp.batch, tmp.qty, tmp.price);
        if (!node) {
            printf("\n  ! 内存分配失败，入库失败。\n");
            return 0;
        }
        list_append(head, node);
        printf("\n  √ 新货品入库成功：%s %s | %s | 批次 %s | 数量 %d | 单价 %.2f\n",
               node->id, node->name, node->factory, node->batch,
               node->qty, node->price);
        file_log("入库(新增) %s %s 厂家%s 批次%s 数量%d 单价%.2f",
                 node->id, node->name, node->factory, node->batch,
                 node->qty, node->price);
    }

    g_stat.inCount++;
    if (file_save(*head, DATA_FILE) < 0)
        printf("  ! 警告：数据文件写入失败（可能被占用）。\n");
    return 1;
}

/*
 * 出库：按编号定位 → 合法性校验 → 扣减 → 存盘 → 记日志
 * 校验点：编号必须存在；出库数量 > 0；出库数量 ≤ 当前库存
 */
int stock_out(Goods *head)
{
    char   id[ID_LEN];
    Goods *hit;
    int    num;

    printf("\n----- 货品出库 -----\n");
    if (!io_read_line("请输入货品编号: ", id, ID_LEN, 0)) return 0;

    hit = list_find_by_id(head, id);
    if (!hit) {
        printf("\n  ! 未找到编号为 %s 的货品，出库失败。\n", id);
        return 0;
    }

    printf("\n  当前记录：%s %s | 厂家 %s | 批次 %s | 库存 %d | 单价 %.2f\n",
           hit->id, hit->name, hit->factory, hit->batch, hit->qty, hit->price);

    if (hit->qty == 0) {
        printf("\n  ! 该货品当前库存为 0，无法出库。\n");
        return 0;
    }

    /* 上限直接给当前库存，从输入层就堵住"出库超过库存" */
    if (!io_read_int("请输入出库数量: ", 1, hit->qty, &num)) return 0;

    hit->qty -= num;
    printf("\n  √ 出库成功：%s %s 出库 %d，剩余库存 %d\n",
           hit->id, hit->name, num, hit->qty);

    if (hit->qty < g_stat.lowLimit)
        printf("  【提示】该货品库存已低于警戒值 %d，建议及时补货。\n", g_stat.lowLimit);

    file_log("出库 %s %s 批次%s 数量 -%d 剩余%d",
             hit->id, hit->name, hit->batch, num, hit->qty);

    g_stat.outCount++;
    if (file_save(head, DATA_FILE) < 0)
        printf("  ! 警告：数据文件写入失败（可能被占用）。\n");
    return 1;
}

/* 查询：按编号精确查 / 按名称模糊查 */
void stock_query(Goods *head)
{
    int choice;

    printf("\n----- 查询货品 -----\n");
    printf("  1. 按编号查询\n");
    printf("  2. 按名称模糊查询\n");
    printf("  0. 返回\n");
    if (!io_read_int("请选择: ", 0, 2, &choice) || choice == 0) return;

    if (choice == 1) {
        char   id[ID_LEN];
        Goods *hit;

        if (!io_read_line("请输入货品编号: ", id, ID_LEN, 0)) return;
        hit = list_find_by_id(head, id);
        if (!hit) {
            printf("\n  ! 未找到编号为 %s 的货品。\n", id);
            return;
        }
        printf("\n");
        io_print_table_header();
        io_print_one(hit);
    } else {
        char   kw[NAME_LEN];
        Goods *hit;
        int    n = 0;

        if (!io_read_line("请输入名称关键字: ", kw, NAME_LEN, 0)) return;
        printf("\n");
        io_print_table_header();
        hit = list_find_by_name(head, kw, NULL);
        while (hit) {
            io_print_one(hit);
            n++;
            hit = list_find_by_name(head, kw, hit);
        }
        if (n == 0) printf("  (没有名称包含「%s」的货品)\n", kw);
        else        printf("  共匹配到 %d 条记录。\n", n);
    }
}

/* 低库存警示：列出所有低于阈值的货品 */
void stock_warn(Goods *head)
{
    Goods *p = head;
    int    n = 0;

    printf("\n----- 低库存警示（阈值：%d）-----\n", g_stat.lowLimit);
    printf("%-10s %-18s %-14s %8s %8s\n", "编号", "名称", "生产厂家", "库存", "警戒值");
    printf("-----------------------------------------------------------------\n");
    while (p) {
        if (p->qty < g_stat.lowLimit) {
            printf("%-10s %-18s %-14s %8d %8d\n",
                   p->id, p->name, p->factory, p->qty, g_stat.lowLimit);
            n++;
        }
        p = p->next;
    }
    printf("-----------------------------------------------------------------\n");
    if (n == 0) printf("  当前所有货品库存充足，无需补货。\n");
    else        printf("  共 %d 种货品需要补货。\n", n);
}
