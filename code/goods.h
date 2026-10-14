#ifndef GOODS_H
#define GOODS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

/* ================= 常量定义 ================= */
#define ID_LEN        32
#define NAME_LEN      100
#define FACTORY_LEN   100
#define BATCH_LEN     32
#define LINE_LEN      256

#define DATA_FILE     "data.txt"          /* 货品数据文件 */
#define LOG_FILE      "log.txt"           /* 出入库流水日志 */
#define REPORT_FILE   "stock_report.txt"  /* 库存盘点表导出 */

#define LOW_STOCK_DEFAULT  20             /* 默认低库存阈值 */
#define EPS  1e-6                         /* 浮点比较精度 */

/* ================= 数据结构（链式存储） ================= */
typedef struct Goods {
    char   id[ID_LEN];            /* 货品编号 */
    char   name[NAME_LEN];        /* 货品名称 */
    char   factory[FACTORY_LEN];  /* 生产厂家 */
    char   batch[BATCH_LEN];      /* 批次 */
    int    qty;                   /* 库存数量 */
    double price;                 /* 单价 */
    struct Goods *next;           /* 链指针 */
} Goods;

/* 运行期统计信息 */
typedef struct {
    int inCount;    /* 入库操作次数 */
    int outCount;   /* 出库操作次数 */
    int lowLimit;   /* 低库存警戒阈值 */
} SysStat;

extern SysStat g_stat;

/* ================= list.c：链式存储基础操作 ================= */
Goods *list_create_node(const char *id, const char *name, const char *factory,
                        const char *batch, int qty, double price);
void   list_append(Goods **head, Goods *node);
Goods *list_find_by_id(Goods *head, const char *id);
Goods *list_find_match(Goods *head, const char *id, const char *factory,
                       const char *batch, double price);
Goods *list_find_by_name(Goods *head, const char *keyword, Goods *from);
int    list_delete_by_id(Goods **head, const char *id);
int    list_count(Goods *head);
long   list_total_qty(Goods *head);
double list_total_amount(Goods *head);
void   list_swap_fields(Goods *a, Goods *b);
void   list_free(Goods **head);

/* ================= io.c：输入校验与输出 ================= */
void io_clear_buffer(void);
int  io_read_line(const char *prompt, char *buf, int size, int allowEmpty);
int  io_read_int(const char *prompt, int min, int max, int *out);
int  io_read_double(const char *prompt, double min, double *out);
void io_pause(void);
void io_print_table_header(void);
void io_print_one(Goods *g);
void io_print_all(Goods *head);
int  io_input_goods(Goods *g);           /* 交互录入一条货品，返回 1 成功 */

/* ================= file.c：持久化 ================= */
int  file_load(Goods **head, const char *path);
int  file_save(Goods *head, const char *path);
void file_log(const char *fmt, ...);

/* ================= stock.c：出入库业务 ================= */
int  stock_in(Goods **head);                    /* 入库（含同四字段累加） */
int  stock_out(Goods *head);                    /* 出库（合法性校验） */
void stock_query(Goods *head);                  /* 按编号 / 名称查询 */
void stock_warn(Goods *head);                   /* 低库存警示 */

/* ================= sort.c：排序与算法对比 ================= */
/* field: 1=按单价  2=按库存数量 ; asc: 1=升序 0=降序 */
void sort_bubble(Goods *head, int field, int asc, long *cmp, long *mov);
void sort_select(Goods *head, int field, int asc, long *cmp, long *mov);
void sort_insert(Goods *head, int field, int asc, long *cmp, long *mov);
void sort_shell (Goods *head, int field, int asc, long *cmp, long *mov);
void sort_quick (Goods *head, int field, int asc, long *cmp, long *mov);
void sort_menu(Goods *head);

/* ================= stat.c：统计与报表 ================= */
void stat_summary(Goods *head);
void stat_filter(Goods *head);
void stat_export(Goods *head);

#endif /* GOODS_H */
