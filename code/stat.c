/*
 * stat.c —— 统计与报表模块
 * 1) stat_summary : 库存总览统计
 * 2) stat_filter  : 多条件筛选（价格区间 / 生产厂家 / 低库存）
 * 3) stat_export  : 导出库存盘点表到文件
 */
#include "goods.h"

/* 库存总览统计 */
void stat_summary(Goods *head)
{
    Goods *p;
    Goods *max_qty = NULL, *min_qty = NULL, *max_amt = NULL;
    int    n = list_count(head);
    int    lowN = 0;
    long   tq;
    double ta;

    printf("\n=============== 库存统计报表 ===============\n");
    if (n == 0) {
        printf("  (暂无货品数据)\n");
        return;
    }

    tq = list_total_qty(head);
    ta = list_total_amount(head);

    p = head;
    while (p) {
        if (!max_qty || p->qty > max_qty->qty) max_qty = p;
        if (!min_qty || p->qty < min_qty->qty) min_qty = p;
        if (!max_amt || p->qty * p->price > max_amt->qty * max_amt->price) max_amt = p;
        if (p->qty < g_stat.lowLimit) lowN++;
        p = p->next;
    }

    printf("  货品种类数        : %d 种\n", n);
    printf("  库存总数量        : %ld\n", tq);
    printf("  库存总金额        : %.2f 元\n", ta);
    printf("  平均单品库存金额  : %.2f 元\n", ta / n);
    printf("  累计入库操作      : %d 次\n", g_stat.inCount);
    printf("  累计出库操作      : %d 次\n", g_stat.outCount);
    printf("  低库存货品种类    : %d 种（阈值 %d）\n", lowN, g_stat.lowLimit);
    printf("  -------------------------------------------\n");
    printf("  库存最多：%s %s —— %d 件\n", max_qty->id, max_qty->name, max_qty->qty);
    printf("  库存最少：%s %s —— %d 件\n", min_qty->id, min_qty->name, min_qty->qty);
    printf("  金额最高：%s %s —— %.2f 元\n",
           max_amt->id, max_amt->name, max_amt->qty * max_amt->price);
    printf("=============================================\n");
}

/* 多条件筛选 */
void stat_filter(Goods *head)
{
    int choice;

    printf("\n----- 多条件筛选 -----\n");
    printf("  1. 按价格区间筛选\n");
    printf("  2. 按生产厂家筛选\n");
    printf("  3. 筛选低于警戒值的货品\n");
    printf("  0. 返回\n");
    if (!io_read_int("请选择: ", 0, 3, &choice) || choice == 0) return;

    if (choice == 1) {
        double lo, hi;
        Goods *p = head;
        int    n = 0;

        if (!io_read_double("价格下限(元): ", 0.0, &lo)) return;
        if (!io_read_double("价格上限(元): ", lo,  &hi)) return;

        printf("\n  价格区间 [%.2f, %.2f] 的货品：\n", lo, hi);
        io_print_table_header();
        while (p) {
            if (p->price >= lo - EPS && p->price <= hi + EPS) {
                io_print_one(p);
                n++;
            }
            p = p->next;
        }
        printf("  共 %d 条。\n", n);
    } else if (choice == 2) {
        char   fac[FACTORY_LEN];
        Goods *p = head;
        int    n = 0;

        if (!io_read_line("请输入生产厂家关键字: ", fac, FACTORY_LEN, 0)) return;

        printf("\n  厂家包含「%s」的货品：\n", fac);
        io_print_table_header();
        while (p) {
            if (strstr(p->factory, fac) != NULL) {
                io_print_one(p);
                n++;
            }
            p = p->next;
        }
        printf("  共 %d 条。\n", n);
    } else {
        stock_warn(head);
    }
}

/* 导出库存盘点表 */
void stat_export(Goods *head)
{
    FILE  *fp;
    Goods *p = head;
    int    n = 0;
    time_t t;
    char   ts[32];

    if (!head) {
        printf("\n  (暂无货品数据，无需导出)\n");
        return;
    }

    fp = fopen(REPORT_FILE, "w");
    if (!fp) {
        printf("\n  ! 无法创建报表文件 %s\n", REPORT_FILE);
        return;
    }

    t = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));

    fprintf(fp, "仓库管理系统 · 库存盘点表\n");
    fprintf(fp, "生成时间：%s\n\n", ts);
    fprintf(fp, "%-10s %-18s %-14s %-12s %8s %10s %12s\n",
            "编号", "名称", "生产厂家", "批次", "数量", "单价", "金额");
    fprintf(fp, "------------------------------------------------------------------------------------\n");
    while (p) {
        fprintf(fp, "%-10s %-18s %-14s %-12s %8d %10.2f %12.2f\n",
                p->id, p->name, p->factory, p->batch,
                p->qty, p->price, p->qty * p->price);
        n++;
        p = p->next;
    }
    fprintf(fp, "------------------------------------------------------------------------------------\n");
    fprintf(fp, "货品种类数：%d    库存总数量：%ld    库存总金额：%.2f 元\n",
            n, list_total_qty(head), list_total_amount(head));
    fclose(fp);

    printf("\n  √ 盘点表已导出：%s（共 %d 条记录）\n", REPORT_FILE, n);
    file_log("导出盘点表 %s 记录数%d 总金额%.2f",
             REPORT_FILE, n, list_total_amount(head));
}
