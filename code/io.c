/*
 * io.c —— 输入校验与输出美化模块
 * 全部输入走 fgets，避免 scanf 缓冲区残留导致的经典 bug
 */
#include "goods.h"

/* 清空输入缓冲区中剩余字符 */
void io_clear_buffer(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
}

/* 读取一行字符串；allowEmpty=0 时不允许空输入（会重复询问） */
int io_read_line(const char *prompt, char *buf, int size, int allowEmpty)
{
    for (;;) {
        printf("%s", prompt);
        if (fgets(buf, size, stdin) == NULL) return 0;

        /* 去掉行尾换行符 / 回车符 */
        buf[strcspn(buf, "\r\n")] = '\0';

        if (buf[0] == '\0' && !allowEmpty) {
            printf("  ! 输入不能为空，请重新输入。\n");
            continue;
        }
        return 1;
    }
}

/* 读取整数并做区间校验 */
int io_read_int(const char *prompt, int min, int max, int *out)
{
    char buf[LINE_LEN];
    char *end;
    long v;

    for (;;) {
        if (!io_read_line(prompt, buf, sizeof(buf), 0)) return 0;

        v = strtol(buf, &end, 10);
        if (*end != '\0') {
            printf("  ! 请输入合法数字。\n");
            continue;
        }
        if (v < min || v > max) {
            printf("  ! 数值须在 %d ~ %d 之间。\n", min, max);
            continue;
        }
        *out = (int)v;
        return 1;
    }
}

/* 读取浮点数并做下限校验 */
int io_read_double(const char *prompt, double min, double *out)
{
    char buf[LINE_LEN];
    char *end;
    double v;

    for (;;) {
        if (!io_read_line(prompt, buf, sizeof(buf), 0)) return 0;

        v = strtod(buf, &end);
        if (*end != '\0') {
            printf("  ! 请输入合法数值。\n");
            continue;
        }
        if (v < min) {
            printf("  ! 数值不能小于 %.2f。\n", min);
            continue;
        }
        *out = v;
        return 1;
    }
}

/* 暂停，等待用户回车 */
void io_pause(void)
{
    char buf[8];
    printf("\n按回车键继续...");
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        io_clear_buffer();
    }
}

/* 列表表头 */
void io_print_table_header(void)
{
    printf("%-10s %-18s %-14s %-12s %8s %10s %12s\n",
           "编号", "名称", "生产厂家", "批次", "数量", "单价", "金额");
    printf("------------------------------------------------------------------------------------\n");
}

/* 打印单条货品记录 */
void io_print_one(Goods *g)
{
    if (!g) return;
    printf("%-10s %-18s %-14s %-12s %8d %10.2f %12.2f\n",
           g->id, g->name, g->factory, g->batch,
           g->qty, g->price, g->qty * g->price);
}

/* 打印整条链表，并附带汇总行 */
void io_print_all(Goods *head)
{
    Goods *p = head;
    int    n  = 0;
    long   tq = 0;
    double ta = 0.0;

    if (!head) {
        printf("\n  (暂无货品记录)\n");
        return;
    }
    io_print_table_header();
    while (p) {
        io_print_one(p);
        tq += p->qty;
        ta += p->qty * p->price;
        n++;
        p = p->next;
    }
    printf("------------------------------------------------------------------------------------\n");
    printf("共 %d 种货品，库存总量 %ld，库存总金额 %.2f 元\n", n, tq, ta);
}

/* 交互录入一条货品信息（不含判重，判重由 stock_in 负责） */
int io_input_goods(Goods *g)
{
    if (!g) return 0;

    printf("\n----- 录入货品信息 -----\n");
    if (!io_read_line("货品编号   : ", g->id,      ID_LEN,      0)) return 0;
    if (!io_read_line("货品名称   : ", g->name,    NAME_LEN,    0)) return 0;
    if (!io_read_line("生产厂家   : ", g->factory, FACTORY_LEN, 0)) return 0;
    if (!io_read_line("批次       : ", g->batch,   BATCH_LEN,   0)) return 0;
    if (!io_read_int("入库数量   : ", 0, 1000000, &g->qty))         return 0;
    if (!io_read_double("单价(元)   : ", 0.0, &g->price))           return 0;
    g->next = NULL;
    return 1;
}
