/*
 * file.c —— 数据持久化模块
 * data.txt   : 货品主数据，格式 id|name|factory|batch|qty|price
 * log.txt    : 出入库流水日志（带时间戳）
 */
#include "goods.h"

/*
 * 从文件加载货品到链表尾部
 * 返回成功加载的记录数；文件不存在返回 -1
 */
int file_load(Goods **head, const char *path)
{
    FILE *fp;
    char  line[LINE_LEN];
    int   n = 0;

    fp = fopen(path, "r");
    if (!fp) return -1;                 /* 首次运行无文件属正常 */

    while (fgets(line, sizeof(line), fp)) {
        char *tok;
        char *f[6];
        int   i;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;   /* 跳过空行/注释 */

        tok = strtok(line, "|");
        for (i = 0; i < 6 && tok; i++) {
            f[i] = tok;
            tok  = strtok(NULL, "|");
        }
        if (i < 6) {
            printf("  ! 跳过格式错误的行：%s\n", line);
            continue;
        }

        Goods *node = list_create_node(f[0], f[1], f[2], f[3],
                                       (int)strtol(f[4], NULL, 10),
                                       strtod(f[5], NULL));
        if (!node) {
            printf("  ! 内存不足，加载中断。\n");
            break;
        }
        list_append(head, node);
        n++;
    }
    fclose(fp);
    return n;
}

/* 把整条链表写回文件，返回写入条数；失败返回 -1 */
int file_save(Goods *head, const char *path)
{
    FILE *fp;
    int   n = 0;

    fp = fopen(path, "w");
    if (!fp) return -1;

    fprintf(fp, "# 数据结构课程设计 · 仓库管理系统 数据文件\n");
    fprintf(fp, "# 格式：编号|名称|生产厂家|批次|数量|单价\n");
    while (head) {
        fprintf(fp, "%s|%s|%s|%s|%d|%.2f\n",
                head->id, head->name, head->factory, head->batch,
                head->qty, head->price);
        head = head->next;
        n++;
    }
    fclose(fp);
    return n;
}

/* 追加一条带时间戳的流水日志 */
void file_log(const char *fmt, ...)
{
    FILE   *fp;
    time_t  t;
    struct tm *lt;
    char    ts[32];
    va_list ap;

    fp = fopen(LOG_FILE, "a");
    if (!fp) return;

    t  = time(NULL);
    lt = localtime(&t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", lt);

    fprintf(fp, "[%s] ", ts);
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fprintf(fp, "\n");
    fclose(fp);
}
