/*
 * ============================================================
 *  webserver.c —— Web 版入口：用 C 语言实现的本地 HTTP 服务
 * ------------------------------------------------------------
 *  架构：浏览器(HTML/JS)  ←HTTP/JSON→  本文件(C)  ←→  data.txt
 *
 *  为什么后端也用 C：课程设计硬性要求"C 语言实现"，因此这里
 *  用 Winsock 手写最小 HTTP 服务，不引入任何第三方框架。
 *
 *  复用模块：list.c（链式存储）/ sort.c（排序）/ 数据结构见 goods.h
 *  数据互通：data.txt 以 GBK 存储（与控制台版一致），
 *            本服务在读写时用 Windows API 做 GBK ↔ UTF-8 转码。
 *
 *  编译：build_web.bat   或   gcc ... -lws2_32
 *  运行：双击 server_web.bat，然后浏览器打开 http://localhost:8080
 * ============================================================
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <process.h>      /* _beginthreadex：每个连接一个工作线程 */
#include <ctype.h>
#include "goods.h"

/* MinGW 下需在链接时加 -lws2_32（MSVC 可用下行 pragma） */
/* #pragma comment(lib, "ws2_32.lib") */

#define DEFAULT_PORT 8123
#define REQ_BUF      65536
#define RES_BUF      524288
#define HTML_MAX     262144

/* goods.h 中声明了 extern，这里给出唯一定义（Web 版不含 main.c） */
SysStat g_stat = { 0, 0, LOW_STOCK_DEFAULT };

static Goods *g_head = NULL;

/*
 * 并发模型：每连接一个工作线程。
 * 单线程串行时，任何一个客户端卡住（不发完整请求 / 不读响应）都会让
 * 整个服务停摆；改成多线程后，坏连接只影响它自己那条线程。
 * 业务处理段用临界区加锁串行执行，保证共享链表操作安全。
 */
static CRITICAL_SECTION g_lock;
static int              g_lock_ready = 0;

/* ============================================================
 *  一、编码转换（GBK ⇄ UTF-8）
 * ============================================================ */

static void gbk2utf8(const char *in, char *out, int outSize)
{
    WCHAR wbuf[2048];
    int   n;

    if (!in || !out || outSize <= 0) return;
    n = MultiByteToWideChar(936, 0, in, -1, wbuf, 2048);
    if (n <= 0) { snprintf(out, outSize, "%s", in); return; }   /* 兜底：原样拷贝 */
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out, outSize, NULL, NULL);
}

static void utf82gbk(const char *in, char *out, int outSize)
{
    WCHAR wbuf[2048];
    int   n;

    if (!in || !out || outSize <= 0) return;
    n = MultiByteToWideChar(CP_UTF8, 0, in, -1, wbuf, 2048);
    if (n <= 0) { snprintf(out, outSize, "%s", in); return; }
    WideCharToMultiByte(936, 0, wbuf, -1, out, outSize, NULL, NULL);
}

/* 向控制台输出中文（cmd 是 GBK，需转换后打印） */
static void print_gbk(const char *utf8)
{
    char g[1024];
    utf82gbk(utf8, g, sizeof(g));
    printf("%s\n", g);
}

/*
 * 按 GBK 编码向文件输出。
 * 所有参数一律按 UTF-8 传入，由本函数统一转码 —— 调用方不要自行预转码，
 * 否则会出现"二次转换"，把中文烧成乱码。
 */
static void fprintf_gbk(FILE *fp, const char *fmt, ...)
{
    char    utf8buf[4096];
    char    gbuf[8192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(utf8buf, sizeof(utf8buf), fmt, ap);
    va_end(ap);

    utf82gbk(utf8buf, gbuf, sizeof(gbuf));
    fputs(gbuf, fp);
}

/* ============================================================
 *  二、数据持久化（带编码转换，与控制台版共用 data.txt）
 * ============================================================ */

static int web_load(void)
{
    FILE *fp;
    char  line[LINE_LEN];
    int   n = 0;

    fp = fopen(DATA_FILE, "r");
    if (!fp) return -1;

    list_free(&g_head);

    while (fgets(line, sizeof(line), fp)) {
        char *tok, *f[6];
        char  u8name[512], u8fac[512], u8batch[512];
        int   i;
        Goods *node;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        tok = strtok(line, "|");
        for (i = 0; i < 6 && tok; i++) { f[i] = tok; tok = strtok(NULL, "|"); }
        if (i < 6) continue;

        gbk2utf8(f[1], u8name,  sizeof(u8name));
        gbk2utf8(f[2], u8fac,   sizeof(u8fac));
        gbk2utf8(f[3], u8batch, sizeof(u8batch));

        node = list_create_node(f[0], u8name, u8fac, u8batch,
                                (int)strtol(f[4], NULL, 10),
                                strtod(f[5], NULL));
        if (node) { list_append(&g_head, node); n++; }
    }
    fclose(fp);
    return n;
}

static int web_save(void)
{
    FILE  *fp;
    Goods *p = g_head;
    int    n = 0;

    fp = fopen(DATA_FILE, "w");
    if (!fp) return -1;

    fprintf_gbk(fp, "# 数据结构课程设计 · 仓库管理系统 数据文件\n");
    fprintf_gbk(fp, "# 格式：编号|名称|生产厂家|批次|数量|单价\n");
    while (p) {
        fprintf_gbk(fp, "%s|%s|%s|%s|%d|%.2f\n",
                    p->id, p->name, p->factory, p->batch, p->qty, p->price);
        n++;
        p = p->next;
    }
    fclose(fp);
    return n;
}

static void web_log(const char *fmt, ...)
{
    FILE   *fp;
    time_t  t;
    char    ts[32], msg[1024];
    va_list ap;

    fp = fopen(LOG_FILE, "a");
    if (!fp) return;
    t = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    fprintf_gbk(fp, "[%s] (Web) %s\n", ts, msg);
    fclose(fp);
}

/* ============================================================
 *  三、HTTP 基础工具
 * ============================================================ */

static int send_all(SOCKET s, const char *buf, int len)
{
    int sent = 0;

    while (sent < len) {
        int r = send(s, buf + sent, len - sent, 0);
        if (r <= 0) return -1;
        sent += r;
    }
    return sent;
}

static void send_http(SOCKET cli, int code, const char *ctype,
                      const char *body, int len)
{
    char head[512];
    int  hl;
    const char *stat = (code == 200) ? "200 OK"
                     : (code == 404) ? "404 Not Found"
                     : "400 Bad Request";

    if (len < 0) len = body ? (int)strlen(body) : 0;
    if (!ctype) ctype = "text/plain; charset=utf-8";

    hl = snprintf(head, sizeof(head),
                  "HTTP/1.1 %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %d\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n",
                  stat, ctype, len);
    if (send_all(cli, head, hl) < 0) return;          /* 对端已走，直接放弃本连接 */
    if (body && len > 0) send_all(cli, body, len);
}

/* URL 百分号解码（就地可重入） */
static void url_decode(const char *src, char *dst, int dstSize)
{
    int i = 0, j = 0;

    if (!src || !dst) return;
    while (src[i] && j < dstSize - 1) {
        if (src[i] == '%' &&
            isxdigit((unsigned char)src[i + 1]) &&
            isxdigit((unsigned char)src[i + 2])) {
            char hex[3];
            hex[0] = src[i + 1];
            hex[1] = src[i + 2];
            hex[2] = '\0';
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

/* 从 body 中取字段值（a=b&c=d 形式），取到后解码为 UTF-8 原文 */
static int form_get(const char *body, const char *key, char *out, int outSize)
{
    char        pat[64];
    const char *p;
    int         i = 0;

    if (!body || !key || !out) return 0;
    snprintf(pat, sizeof(pat), "%s=", key);

    p = strstr(body, pat);
    if (!p) { out[0] = '\0'; return 0; }
    if (p != body && *(p - 1) != '&') { out[0] = '\0'; return 0; }  /* 必须字段开头 */

    p += strlen(pat);
    while (*p && *p != '&' && i < outSize - 1) out[i++] = *p++;
    out[i] = '\0';

    url_decode(out, out, outSize);      /* 就地解码，输出不长于输入 */
    return 1;
}

static int form_get_int(const char *body, const char *key, int *out)
{
    char buf[64], *end;
    long v;

    if (!form_get(body, key, buf, sizeof(buf)) || buf[0] == '\0') return 0;
    v = strtol(buf, &end, 10);
    if (*end != '\0') return 0;
    *out = (int)v;
    return 1;
}

static int form_get_double(const char *body, const char *key, double *out)
{
    char buf[64], *end;
    double v;

    if (!form_get(body, key, buf, sizeof(buf)) || buf[0] == '\0') return 0;
    v = strtod(buf, &end);
    if (*end != '\0') return 0;
    *out = v;
    return 1;
}

/* JSON 字符串转义 */
static void json_esc(const char *in, char *out, int outSize)
{
    int j = 0;

    if (!in) { out[0] = '\0'; return; }
    while (*in && j < outSize - 8) {
        unsigned char c = (unsigned char)*in++;
        if (c == '"' || c == '\\')      { out[j++] = '\\'; out[j++] = (char)c; }
        else if (c == '\n')             { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\r')             { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c == '\t')             { out[j++] = '\\'; out[j++] = 't'; }
        else if (c < 0x20)              { /* 丢弃控制字符 */ }
        else                            { out[j++] = (char)c; }
    }
    out[j] = '\0';
}

static void reply_ok(char *res, int resSize, const char *msg)
{
    char e[512];
    json_esc(msg, e, sizeof(e));
    snprintf(res, resSize, "{\"ok\":true,\"msg\":\"%s\"}", e);
}

static void reply_err(char *res, int resSize, const char *msg)
{
    char e[512];
    json_esc(msg, e, sizeof(e));
    snprintf(res, resSize, "{\"ok\":false,\"msg\":\"%s\"}", e);
}

/* ============================================================
 *  四、业务 API
 * ============================================================ */

/* 货品列表 + 汇总统计（前端一次请求拿全） */
static void api_list(char *res, int resSize)
{
    Goods *p = g_head;
    int    off = 0, kinds = 0, lowN = 0;
    long   tq = 0;
    double ta = 0.0;

    off += snprintf(res + off, resSize - off, "{\"ok\":true,\"goods\":[");
    while (p) {
        char eid[256], ename[1024], efac[1024], ebat[256];

        json_esc(p->id,      eid,   sizeof(eid));
        json_esc(p->name,    ename, sizeof(ename));
        json_esc(p->factory, efac,  sizeof(efac));
        json_esc(p->batch,   ebat,  sizeof(ebat));

        off += snprintf(res + off, resSize - off,
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"factory\":\"%s\",\"batch\":\"%s\","
            "\"qty\":%d,\"price\":%.2f,\"amount\":%.2f,\"low\":%s}",
            (p == g_head) ? "" : ",", eid, ename, efac, ebat,
            p->qty, p->price, p->qty * p->price,
            (p->qty < g_stat.lowLimit) ? "true" : "false");

        if (p->qty < g_stat.lowLimit) lowN++;
        tq += p->qty;
        ta += p->qty * p->price;
        kinds++;
        p = p->next;
    }
    off += snprintf(res + off, resSize - off,
        "],\"stats\":{\"kinds\":%d,\"totalQty\":%ld,\"totalAmount\":%.2f,"
        "\"lowCount\":%d,\"lowLimit\":%d,\"inCount\":%d,\"outCount\":%d}}",
        kinds, tq, ta, lowN, g_stat.lowLimit, g_stat.inCount, g_stat.outCount);
}

/* 入库：四字段判重，命中则数量累加 */
static void api_in(const char *body, char *res, int resSize)
{
    char   id[64], name[256], fac[256], batch[256], msg[512];
    int    qty = 0;
    double price = 0.0;
    Goods *hit;

    if (!form_get(body, "id", id, sizeof(id)) || id[0] == '\0') {
        reply_err(res, resSize, "货品编号不能为空");
        return;
    }
    if (!form_get(body, "name", name, sizeof(name)) || name[0] == '\0') {
        reply_err(res, resSize, "货品名称不能为空");
        return;
    }
    if (!form_get(body, "factory", fac, sizeof(fac)))   snprintf(fac, sizeof(fac), "%s", "未填写");
    if (!form_get(body, "batch", batch, sizeof(batch))) snprintf(batch, sizeof(batch), "%s", "-");

    if (!form_get_int(body, "qty", &qty) || qty <= 0) {
        reply_err(res, resSize, "入库数量必须是大于 0 的整数");
        return;
    }
    if (!form_get_double(body, "price", &price) || price < 0) {
        reply_err(res, resSize, "单价必须是大于等于 0 的数值");
        return;
    }

    hit = list_find_match(g_head, id, fac, batch, price);
    if (hit) {
        snprintf(msg, sizeof(msg), "编号/厂家/批次/单价完全一致，数量累加：%d → %d",
                 hit->qty, hit->qty + qty);
        hit->qty += qty;
        {
            char gl[1024];
            snprintf(gl, sizeof(gl), "入库(累加) %s %s 批次%s +%d", id, name, batch, qty);
            web_log("%s", gl);
        }
    } else {
        Goods *node = list_create_node(id, name, fac, batch, qty, price);
        if (!node) { reply_err(res, resSize, "内存分配失败"); return; }
        list_append(&g_head, node);
        snprintf(msg, sizeof(msg), "新货品入库成功：%s %s，数量 %d，单价 %.2f",
                 id, name, qty, price);
        web_log("入库(新增) %s %s 厂家%s 批次%s 数量%d 单价%.2f",
                id, name, fac, batch, qty, price);
    }

    g_stat.inCount++;
    web_save();
    reply_ok(res, resSize, msg);
}

/* 出库：编号存在 + 数量>0 + 不超过库存 */
static void api_out(const char *body, char *res, int resSize)
{
    char   id[64], msg[512];
    int    qty = 0;
    Goods *hit;

    if (!form_get(body, "id", id, sizeof(id)) || id[0] == '\0') {
        reply_err(res, resSize, "货品编号不能为空");
        return;
    }
    hit = list_find_by_id(g_head, id);
    if (!hit) { reply_err(res, resSize, "未找到该编号的货品"); return; }

    if (!form_get_int(body, "qty", &qty) || qty <= 0) {
        reply_err(res, resSize, "出库数量必须是大于 0 的整数");
        return;
    }
    if (qty > hit->qty) {
        snprintf(msg, sizeof(msg), "出库数量超过当前库存（库存 %d），操作已拒绝", hit->qty);
        reply_err(res, resSize, msg);
        return;
    }

    hit->qty -= qty;
    g_stat.outCount++;
    web_save();
    web_log("出库 %s %s 批次%s 数量 -%d 剩余%d", hit->id, hit->name, hit->batch, qty, hit->qty);
    snprintf(msg, sizeof(msg), "出库成功：%s %s 出库 %d，剩余库存 %d",
             hit->id, hit->name, qty, hit->qty);
    reply_ok(res, resSize, msg);
}

/* 删除货品 */
static void api_del(const char *body, char *res, int resSize)
{
    char   id[64], msg[512];
    char   bak_name[256];
    Goods *hit;

    if (!form_get(body, "id", id, sizeof(id)) || id[0] == '\0') {
        reply_err(res, resSize, "货品编号不能为空");
        return;
    }
    hit = list_find_by_id(g_head, id);
    if (!hit) { reply_err(res, resSize, "未找到该编号的货品"); return; }

    snprintf(bak_name, sizeof(bak_name), "%s", hit->name);
    if (list_delete_by_id(&g_head, id)) {
        web_save();
        web_log("删除货品 %s %s", id, bak_name);
        snprintf(msg, sizeof(msg), "已删除货品：%s %s", id, bak_name);
        reply_ok(res, resSize, msg);
    } else {
        reply_err(res, resSize, "删除失败");
    }
}

/* 深拷贝链表（用于两种排序算法的公平对比：起始数据一致） */
static Goods *clone_list(Goods *head)
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

/* 排序：五种算法 + 全面对比（algo: 1冒泡 2选择 3插入 4希尔 5快排 6五算法对比） */
static void api_sort(const char *body, char *res, int resSize)
{
    static const char *NAME[5] = {
        "冒泡排序", "选择排序", "直接插入排序", "希尔排序", "快速排序"
    };

    int    field = 1, asc = 1, algo = 1, i, off = 0;
    long   cmp[5] = {0}, mov[5] = {0};
    char   msg[1024];
    const char *fname;

    form_get_int(body, "field", &field);
    form_get_int(body, "asc",   &asc);
    form_get_int(body, "algo",  &algo);
    if (field != 1 && field != 2) field = 1;
    if (algo < 1 || algo > 6) algo = 1;
    fname = (field == 1) ? "单价" : "库存数量";

    if (algo == 6) {
        /* 五份独立副本：起始数据完全一致，统计才公平 */
        Goods *c[5];
        int    n = list_count(g_head);

        for (i = 0; i < 5; i++) c[i] = clone_list(g_head);

        sort_bubble(c[0], field, asc, &cmp[0], &mov[0]);
        sort_select(c[1], field, asc, &cmp[1], &mov[1]);
        sort_insert(c[2], field, asc, &cmp[2], &mov[2]);
        sort_shell (c[3], field, asc, &cmp[3], &mov[3]);
        sort_quick (c[4], field, asc, &cmp[4], &mov[4]);

        for (i = 0; i < 5; i++) list_free(&c[i]);

        /* 原链表用快速排序落地并保存 */
        sort_quick(g_head, field, asc, NULL, NULL);
        web_save();

        snprintf(msg, sizeof(msg), "五种算法对比完成（N=%d，按%s%s）",
                 n, fname, asc ? "升序" : "降序");
        web_log("排序对比 %s N=%d | 冒泡 %ld/%ld 选择 %ld/%ld 插入 %ld/%ld 希尔 %ld/%ld 快排 %ld/%ld",
                fname, n, cmp[0], mov[0], cmp[1], mov[1], cmp[2], mov[2],
                cmp[3], mov[3], cmp[4], mov[4]);

        off += snprintf(res + off, resSize - off,
                        "{\"ok\":true,\"algo\":6,\"n\":%d,\"msg\":\"%s\",\"results\":[",
                        n, msg);
        for (i = 0; i < 5; i++) {
            off += snprintf(res + off, resSize - off,
                            "%s{\"name\":\"%s\",\"cmp\":%ld,\"mov\":%ld}",
                            i ? "," : "", NAME[i], cmp[i], mov[i]);
        }
        snprintf(res + off, resSize - off, "]}");
        return;
    }

    /* 单算法排序 */
    switch (algo) {
    case 1:  sort_bubble(g_head, field, asc, &cmp[0], &mov[0]); break;
    case 2:  sort_select(g_head, field, asc, &cmp[1], &mov[1]); break;
    case 3:  sort_insert(g_head, field, asc, &cmp[2], &mov[2]); break;
    case 4:  sort_shell (g_head, field, asc, &cmp[3], &mov[3]); break;
    default: sort_quick (g_head, field, asc, &cmp[4], &mov[4]); break;
    }
    web_save();

    {
        int idx = algo - 1;
        snprintf(msg, sizeof(msg), "%s完成（按%s，%s）：比较 %ld 次，移动 %ld 次",
                 NAME[idx], fname, asc ? "升序" : "降序", cmp[idx], mov[idx]);
        web_log("排序 %s", msg);
        snprintf(res, resSize,
                 "{\"ok\":true,\"algo\":%d,\"n\":%d,\"msg\":\"%s\","
                 "\"results\":[{\"name\":\"%s\",\"cmp\":%ld,\"mov\":%ld}]}",
                 algo, list_count(g_head), msg, NAME[idx], cmp[idx], mov[idx]);
    }
}

/* 生成测试数据：性能测试用（编号 T0001 起） */
static void api_gen(const char *body, char *res, int resSize)
{
    static const char *FACS[10] = {
        "得力", "博世", "史丹利", "3M", "霍尼韦尔", "牧田", "三丰", "世达", "钢盾", "田岛"
    };
    static const char *PREF[12] = {
        "卷尺", "螺丝刀", "扳手", "电钻", "锤子", "锯子",
        "钳子", "卡尺", "手套", "护目镜", "尖嘴钳", "美工刀"
    };

    int  n = 0, i, added = 0;
    char id[ID_LEN], name[NAME_LEN], fac[FACTORY_LEN], batch[BATCH_LEN], msg[256];

    if (!form_get_int(body, "n", &n) || n < 1 || n > 5000) {
        reply_err(res, resSize, "生成条数须在 1-5000 之间");
        return;
    }

    srand((unsigned)time(NULL));
    for (i = 0; i < n; i++) {
        Goods *node;

        snprintf(id,    sizeof(id),    "T%04d", i + 1);
        snprintf(name,  sizeof(name),  "%s%02d", PREF[rand() % 12], rand() % 100);
        snprintf(fac,   sizeof(fac),   "%s", FACS[rand() % 10]);
        snprintf(batch, sizeof(batch), "2025%02d%02d", rand() % 12 + 1, rand() % 28 + 1);

        node = list_create_node(id, name, fac, batch,
                                rand() % 500, (rand() % 50000) / 100.0);
        if (!node) break;
        list_append(&g_head, node);
        added++;
    }

    web_save();
    snprintf(msg, sizeof(msg), "已生成 %d 条测试数据（当前共 %d 条）",
             added, list_count(g_head));
    web_log("生成测试数据 %d 条", added);
    reply_ok(res, resSize, msg);
}

/* 设置低库存警戒值 */
static void api_limit(const char *body, char *res, int resSize)
{
    int  v = 0;
    char msg[256];

    if (!form_get_int(body, "limit", &v) || v < 0) {
        reply_err(res, resSize, "警戒值必须是非负整数");
        return;
    }
    g_stat.lowLimit = v;
    snprintf(msg, sizeof(msg), "库存警戒值已更新为 %d", v);
    reply_ok(res, resSize, msg);
}

/* 导出盘点表（与控制台版一致的 GBK 文本） */
static void api_export(char *res, int resSize)
{
    FILE  *fp;
    Goods *p = g_head;
    int    n = 0;
    char   msg[512];
    char   ts[32];
    time_t t;

    if (!g_head) { reply_err(res, resSize, "暂无数据，无需导出"); return; }

    fp = fopen(REPORT_FILE, "w");
    if (!fp) { reply_err(res, resSize, "无法创建盘点表文件"); return; }

    t = time(NULL);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));

    fprintf_gbk(fp, "仓库管理系统 · 库存盘点表（Web 导出）\n");
    fprintf_gbk(fp, "导出时间：%s\n\n", ts);
    fprintf_gbk(fp, "%-12s %-20s %-16s %-14s %8s %10s %12s\n",
                "编号", "名称", "生产厂家", "批次", "数量", "单价", "金额");
    while (p) {
        fprintf_gbk(fp, "%-12s %-20s %-16s %-14s %8d %10.2f %12.2f\n",
                    p->id, p->name, p->factory, p->batch,
                    p->qty, p->price, p->qty * p->price);
        n++;
        p = p->next;
    }
    fprintf_gbk(fp, "\n货品种类数：%d    库存总数量：%ld    库存总金额：%.2f 元\n",
                n, list_total_qty(g_head), list_total_amount(g_head));
    fclose(fp);

    snprintf(msg, sizeof(msg), "盘点表已导出到 %s（共 %d 条记录）", REPORT_FILE, n);
    web_log("导出盘点表 %s 记录数%d", REPORT_FILE, n);
    reply_ok(res, resSize, msg);
}

/* ============================================================
 *  五、请求分发
 * ============================================================ */

static int load_file(const char *path, char *buf, int size)
{
    FILE *fp;
    int   n;

    fp = fopen(path, "rb");
    if (!fp) return -1;
    n = (int)fread(buf, 1, size - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return n;
}

static void get_exe_dir(char *dir, int size)
{
    char  path[MAX_PATH];
    char *p;

    GetModuleFileNameA(NULL, path, MAX_PATH);
    p = strrchr(path, '\\');
    if (p) *p = '\0';
    snprintf(dir, size, "%s", path);
}

static void handle_client(SOCKET cli)
{
    static char req[REQ_BUF];
    static char res[RES_BUF];
    static char html[HTML_MAX];

    int   total = 0, n, contentLen = 0;
    char  method[16] = {0}, path[512] = {0};
    char *body = NULL, *p;

    /* ---- 读取请求头 ---- */
    while (total < REQ_BUF - 1) {
        n = recv(cli, req + total, REQ_BUF - 1 - total, 0);
        if (n <= 0) break;
        total += n;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (total <= 0) return;

    if (sscanf(req, "%15s %511s", method, path) != 2) return;

    /* ---- Content-Length 与 body ---- */
    p = strstr(req, "Content-Length:");
    if (!p) p = strstr(req, "content-length:");
    if (p) contentLen = atoi(p + 15);

    p = strstr(req, "\r\n\r\n");
    body = p ? p + 4 : req + total;

    if (contentLen > 0) {
        int have = (int)(body - req) < total ? total - (int)(body - req) : 0;
        while (have < contentLen && total < REQ_BUF - 1) {
            n = recv(cli, req + total, REQ_BUF - 1 - total, 0);
            if (n <= 0) break;
            total += n;
            have  += n;
            req[total] = '\0';
        }
        body = strstr(req, "\r\n\r\n");
        body = body ? body + 4 : req + total;
        if (contentLen < REQ_BUF - 1) body[contentLen] = '\0';
    } else {
        body = "";
    }

    /* ---- 路由 ---- */
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        char dir[MAX_PATH], file[MAX_PATH + 64];
        get_exe_dir(dir, sizeof(dir));
        snprintf(file, sizeof(file), "%s\\webui.html", dir);
        if (load_file(file, html, sizeof(html)) < 0)
            send_http(cli, 404, "text/plain; charset=utf-8",
                      "webui.html not found (should be next to server.exe)", -1);
        else
            send_http(cli, 200, "text/html; charset=utf-8", html, -1);
        return;
    }

    if (strcmp(path, "/api/list") == 0) {
        api_list(res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/in") == 0 && strcmp(method, "POST") == 0) {
        api_in(body, res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/out") == 0 && strcmp(method, "POST") == 0) {
        api_out(body, res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/del") == 0 && strcmp(method, "POST") == 0) {
        api_del(body, res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/sort") == 0 && strcmp(method, "POST") == 0) {
        api_sort(body, res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/limit") == 0 && strcmp(method, "POST") == 0) {
        api_limit(body, res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/export") == 0) {
        api_export(res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/gen") == 0 && strcmp(method, "POST") == 0) {
        api_gen(body, res, sizeof(res));
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }
    if (strcmp(path, "/api/reload") == 0) {
        int k = web_load();
        if (k < 0) reply_err(res, sizeof(res), "数据文件不存在");
        else       { snprintf(res, sizeof(res),
                              "{\"ok\":true,\"msg\":\"已重新载入 %d 条记录\"}", k); }
        send_http(cli, 200, "application/json; charset=utf-8", res, -1);
        return;
    }

    if (strcmp(path, "/favicon.ico") == 0) {
        send_http(cli, 404, "text/plain; charset=utf-8", "", 0);
        return;
    }

    send_http(cli, 404, "text/plain; charset=utf-8", "not found", -1);
}

/* ============================================================
 *  六、连接工作线程
 * ============================================================ */

static unsigned __stdcall client_proc(void *arg)
{
    SOCKET cli = (SOCKET)(UINT_PTR)arg;
    DWORD  tv  = 3000;      /* 收发超时 3 秒，坏连接最多占用 3 秒 */

    setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    /* 业务处理涉及共享链表，加锁串行执行；I/O 有超时兜底，不会死等 */
    if (g_lock_ready) EnterCriticalSection(&g_lock);
    handle_client(cli);
    if (g_lock_ready) LeaveCriticalSection(&g_lock);

    /*
     * 优雅关闭：先 shutdown(SD_SEND) 发送 FIN，让内核把发送缓冲区里的
     * 响应数据完整送达，再 closesocket。
     * 直接 closesocket 时，若响应体较大（超过发送缓冲区，例如上千条
     * 货品的 JSON），未发送的数据会被丢弃，客户端只能等到超时。
     */
    shutdown(cli, SD_SEND);
    closesocket(cli);
    return 0;
}

/* ============================================================
 *  七、主函数
 * ============================================================ */

int main(int argc, char *argv[])
{
    WSADATA            wsa;
    SOCKET             srv;
    struct sockaddr_in addr;
    int                port = DEFAULT_PORT;
    int                loaded;

    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) port = DEFAULT_PORT;
    }

    printf("=====================================================\n");
    printf("   Depot Management System - Web Server (written in C)\n");
    printf("=====================================================\n");

    loaded = web_load();
    if (loaded < 0) print_gbk("[提示] 未找到 data.txt，将以空仓库启动。");
    else            printf("[OK] loaded %d record(s) from %s\n", loaded, DATA_FILE);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[ERROR] WSAStartup failed\n");
        return 1;
    }

    srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) {
        printf("[ERROR] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    {
        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");   /* 只监听本机，不触发防火墙 */

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[ERROR] bind() failed on port %d : %d\n", port, WSAGetLastError());
        printf("        The port may be in use. Try:  server.exe 8081\n");
        closesocket(srv);
        WSACleanup();
        return 1;
    }

    if (listen(srv, 16) == SOCKET_ERROR) {
        printf("[ERROR] listen() failed: %d\n", WSAGetLastError());
        closesocket(srv);
        WSACleanup();
        return 1;
    }

    printf("\n");
    print_gbk("  服务已启动，请在浏览器中打开：");
    printf("      http://localhost:%d\n\n", port);
    print_gbk("  按 Ctrl + C 停止服务。");
    printf("-----------------------------------------------------\n");

    InitializeCriticalSection(&g_lock);
    g_lock_ready = 1;

    for (;;) {
        SOCKET cli = accept(srv, NULL, NULL);
        if (cli == INVALID_SOCKET) continue;

        /* 每条连接交给独立线程处理，坏连接不会拖垮整个服务 */
        if (_beginthreadex(NULL, 0, client_proc, (void *)(UINT_PTR)cli, 0, NULL) == 0)
            closesocket(cli);        /* 线程创建失败，直接关掉这条连接 */
    }

    closesocket(srv);
    WSACleanup();
    list_free(&g_head);
    return 0;
}
