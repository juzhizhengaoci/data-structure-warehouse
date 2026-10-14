/*
 * ============================================================
 *  数据结构课程设计 —— 仓库管理系统 (main.c)
 * ------------------------------------------------------------
 *  存储结构：单链表（链式存储）
 *  数据持久化：data.txt（| 分隔文本）
 *  流水日志：log.txt
 *  模块划分：goods.h / list.c / io.c / file.c / stock.c / sort.c / stat.c
 *  编译：build.bat  或  VS Code 按 Ctrl+Shift+B
 * ============================================================
 */
#include "goods.h"

/* 全局运行统计 */
SysStat g_stat = { 0, 0, LOW_STOCK_DEFAULT };

/* ---------- 内部辅助函数 ---------- */
static void menu_show(Goods *head);
static void menu_delete(Goods **head);
static void menu_set_limit(void);
static void menu_gen_data(Goods **head);
static int  confirm(const char *prompt);

/* ---------- 主函数 ---------- */
int main(void)
{
    Goods *head = NULL;
    int    n, choice;

    printf("=====================================================\n");
    printf("        仓库管理系统（数据结构课程设计）\n");
    printf("        存储结构：单链表    语言：C\n");
    printf("=====================================================\n\n");

    n = file_load(&head, DATA_FILE);
    if (n < 0)
        printf("[提示] 未找到数据文件 %s，系统将以空仓库启动。\n", DATA_FILE);
    else
        printf("[提示] 已从 %s 载入 %d 条货品记录。\n", DATA_FILE, n);

    for (;;) {
        menu_show(head);
        if (!io_read_int("请选择操作 (0-13): ", 0, 13, &choice)) break;

        switch (choice) {
        case 1:
            printf("\n----- 全部货品清单 -----\n");
            io_print_all(head);
            break;

        case 2:
            stock_in(&head);
            break;

        case 3:
            stock_out(head);
            break;

        case 4:
            stock_query(head);
            break;

        case 5:
            menu_delete(&head);
            break;

        case 6:
            sort_menu(head);
            break;

        case 7:
            stock_warn(head);
            break;

        case 8:
            stat_summary(head);
            break;

        case 9:
            stat_filter(head);
            break;

        case 10:
            stat_export(head);
            break;

        case 11: {
            int loaded = file_load(&head, DATA_FILE);
            if (loaded < 0)
                printf("\n  ! 数据文件 %s 不存在。\n", DATA_FILE);
            else
                printf("\n  √ 已重新从文件导入 %d 条记录（当前共 %d 条）。\n",
                       loaded, list_count(head));
            break;
        }

        case 12:
            menu_set_limit();
            break;

        case 13:
            menu_gen_data(&head);
            break;

        case 0:
            if (file_save(head, DATA_FILE) < 0)
                printf("\n  ! 警告：退出时保存失败。\n");
            else
                printf("\n  √ 数据已保存到 %s。\n", DATA_FILE);
            printf("\n  感谢使用，再见！\n");
            list_free(&head);
            return 0;

        default:
            printf("\n  ! 无此选项。\n");
            break;
        }
        io_pause();
    }

    list_free(&head);
    return 0;
}

/* ---------- 显示主菜单 ---------- */
static void menu_show(Goods *head)
{
#ifdef _WIN32
    system("cls");
#endif
    printf("=====================================================\n");
    printf("                  仓 库 管 理 系 统\n");
    printf("=====================================================\n");
    printf("   1. 显示全部货品            2. 入库（新增/累加）\n");
    printf("   3. 出库                     4. 查询货品\n");
    printf("   5. 删除货品                 6. 排序（5 种算法+对比）\n");
    printf("   7. 低库存警示               8. 库存统计报表\n");
    printf("   9. 多条件筛选              10. 导出库存盘点表\n");
    printf("  11. 从文件重新导入          12. 设置库存警戒值\n");
    printf("  13. 生成测试数据（性能测试） 0. 保存并退出\n");
    printf("=====================================================\n");
    printf("  当前：%d 种货品 | 库存总量 %ld | 总金额 %.2f 元 | 警戒值 %d\n",
           list_count(head), list_total_qty(head), list_total_amount(head),
           g_stat.lowLimit);
    printf("-----------------------------------------------------\n");
}

/* ---------- 删除货品（带二次确认） ---------- */
static void menu_delete(Goods **head)
{
    char   id[ID_LEN];
    char   bak_name[NAME_LEN], bak_batch[BATCH_LEN];
    int    bak_qty;
    Goods *hit;

    printf("\n----- 删除货品 -----\n");
    if (!*head) {
        printf("  (仓库为空，无可删除记录)\n");
        return;
    }
    if (!io_read_line("请输入要删除的货品编号: ", id, ID_LEN, 0)) return;

    hit = list_find_by_id(*head, id);
    if (!hit) {
        printf("\n  ! 未找到编号为 %s 的货品。\n", id);
        return;
    }

    printf("\n  待删除：%s %s | 厂家 %s | 批次 %s | 库存 %d | 单价 %.2f\n",
           hit->id, hit->name, hit->factory, hit->batch, hit->qty, hit->price);

    if (!confirm("  确认删除该记录？(y/n): ")) {
        printf("\n  已取消删除。\n");
        return;
    }

    /* 节点即将被 free，先把日志需要的字段备份下来 */
    snprintf(bak_name,  sizeof(bak_name),  "%s", hit->name);
    snprintf(bak_batch, sizeof(bak_batch), "%s", hit->batch);
    bak_qty = hit->qty;

    if (list_delete_by_id(head, id)) {
        printf("\n  √ 删除成功。\n");
        file_log("删除货品 %s %s 批次%s 原库存%d", id, bak_name, bak_batch, bak_qty);
        if (file_save(*head, DATA_FILE) < 0)
            printf("  ! 警告：数据文件写入失败。\n");
    } else {
        printf("\n  ! 删除失败。\n");
    }
}

/* ---------- 设置低库存警戒值 ---------- */
static void menu_set_limit(void)
{
    int v;

    printf("\n----- 设置库存警戒值（当前 %d）-----\n", g_stat.lowLimit);
    if (!io_read_int("请输入新的警戒值 (0-100000): ", 0, 100000, &v)) return;
    g_stat.lowLimit = v;
    printf("\n  √ 警戒值已更新为 %d。\n", v);
    file_log("修改库存警戒值 %d", v);
}

/*
 * 生成测试数据：用于性能测试（排序算法对比、大数据量演示）
 * 数据随机，编号形如 T0001，与手工录入的数据不会冲突
 */
static void menu_gen_data(Goods **head)
{
    static const char *FACS[10] = {
        "得力", "博世", "史丹利", "3M", "霍尼韦尔", "牧田", "三丰", "世达", "钢盾", "田岛"
    };
    static const char *PREF[12] = {
        "卷尺", "螺丝刀", "扳手", "电钻", "锤子", "锯子",
        "钳子", "卡尺", "手套", "护目镜", "尖嘴钳", "美工刀"
    };

    int  n, i, added = 0;
    char id[ID_LEN], name[NAME_LEN], fac[FACTORY_LEN], batch[BATCH_LEN];

    printf("\n----- 生成测试数据 -----\n");
    printf("  用途：排序算法性能对比、大数据量演示（编号 T0001 起，可重复生成）\n");
    if (!io_read_int("请输入生成条数 (1-5000): ", 1, 5000, &n)) return;

    srand((unsigned)time(NULL));
    for (i = 0; i < n; i++) {
        Goods *node;

        snprintf(id,    sizeof(id),    "T%04d", i + 1);
        snprintf(name,  sizeof(name),  "%s%02d", PREF[rand() % 12], rand() % 100);
        snprintf(fac,   sizeof(fac),   "%s", FACS[rand() % 10]);
        snprintf(batch, sizeof(batch), "2025%02d%02d", rand() % 12 + 1, rand() % 28 + 1);

        node = list_create_node(id, name, fac, batch,
                                rand() % 500,
                                (rand() % 50000) / 100.0);
        if (!node) break;
        list_append(head, node);
        added++;
    }

    printf("\n  √ 已生成 %d 条测试数据，当前共计 %d 条。\n", added, list_count(*head));
    file_log("生成测试数据 %d 条", added);

    if (file_save(*head, DATA_FILE) < 0)
        printf("  ! 警告：数据文件写入失败。\n");
    else
        printf("  数据已保存（可用菜单 6 → 算法 6 做五种排序的性能对比）。\n");
}

/* ---------- 通用确认 ---------- */
static int confirm(const char *prompt)
{
    char buf[16];

    if (!io_read_line(prompt, buf, sizeof(buf), 0)) return 0;
    return (buf[0] == 'y' || buf[0] == 'Y');
}
