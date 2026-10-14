/*
 * ============================================================
 *  bench.c —— 稳定性 / 性能压测客户端（C 语言 + Winsock）
 * ------------------------------------------------------------
 *  用途：对本地 Web 服务（server.exe）持续施加混合请求负载，
 *        连续运行指定时长（默认 3600 秒 = 1 小时），统计：
 *          · 请求总数、成功率、吞吐量（req/s）
 *          · 平均 / 最大响应时间、响应时间分布直方图
 *          · 服务端进程内存变化（检测内存泄漏）
 *
 *  用法：bench.exe [秒数] [端口]      默认 3600 秒 / 8080 端口
 *  结果：屏幕输出 + bench_report.txt
 *
 *  编译：build_bench.bat
 *        gcc -O2 -Wall -std=c99 bench.c -o bench.exe -lws2_32 -lpsapi
 * ============================================================
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_SECONDS 3600
#define DEFAULT_PORT    8123
#define RESP_MAX        262144     /* 单次响应最多读 256KB */
#define BUCKET_N        7

static const char *BUCKET_NAME[BUCKET_N] = {
    "< 10 ms", "10-20 ms", "20-50 ms", "50-100 ms", "100-300 ms", "300-1000 ms", ">= 1000 ms"
};
static const long  BUCKET_EDGE[BUCKET_N] = { 10, 20, 50, 100, 300, 1000, -1 };

/* ---------------- 统计量 ---------------- */
static long  g_total = 0;          /* 总请求数            */
static long  g_ok = 0;             /* 成功响应            */
static long  g_transport_fail = 0; /* 传输失败（连不上）  */
static long  g_business_fail = 0;  /* 业务失败（ok:false）*/
static double g_sum_ms = 0.0;      /* 响应时间总和        */
static double g_max_ms = 0.0;      /* 最大响应时间        */
static long  g_bucket[BUCKET_N] = {0};

static double g_mem_first = -1.0, g_mem_last = -1.0, g_mem_peak = -1.0;

/* ---------------- 工具函数 ---------------- */

static void fmt_elapsed(long sec, char *buf, int size)
{
    snprintf(buf, size, "%02ld:%02ld:%02ld", sec / 3600, (sec % 3600) / 60, sec % 60);
}

static int bucket_of(double ms)
{
    int i;

    for (i = 0; i < BUCKET_N - 1; i++) {
        if (ms < (double)BUCKET_EDGE[i]) return i;
    }
    return BUCKET_N - 1;
}

/* 找进程 PID（按可执行文件名） */
static DWORD find_pid(const char *exeName)
{
    HANDLE         snap;
    PROCESSENTRY32 pe;
    DWORD          pid = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exeName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

/* 读进程工作集（MB），失败返回 -1 */
static double process_mem_mb(DWORD pid)
{
    HANDLE                  h;
    PROCESS_MEMORY_COUNTERS pmc;
    double                  mb = -1.0;

    if (!pid) return -1.0;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return -1.0;
    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
        mb = (double)pmc.WorkingSetSize / 1048576.0;
    CloseHandle(h);
    return mb;
}

/*
 * 发一次 HTTP 请求（短连接）
 * 返回 0 = 收到响应；-1 = 传输失败
 */
static int http_request(int port, const char *method, const char *path,
                        const char *body, char *resp, int respSize, double *ms)
{
    SOCKET             s;
    struct sockaddr_in addr;
    char               req[4096];
    int                n, total = 0, hlen;
    ULONGLONG          t0, t1;
    DWORD              tv = 5000;

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;

    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    t0 = GetTickCount64();
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }

    if (body && body[0]) {
        hlen = snprintf(req, sizeof(req),
                        "%s %s HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Content-Type: application/x-www-form-urlencoded\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n\r\n%s",
                        method, path, (int)strlen(body), body);
    } else {
        hlen = snprintf(req, sizeof(req),
                        "%s %s HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Connection: close\r\n\r\n",
                        method, path);
    }

    if (send(s, req, hlen, 0) <= 0) { closesocket(s); return -1; }

    while (total < respSize - 1) {
        n = recv(s, resp + total, respSize - 1 - total, 0);
        if (n <= 0) break;
        total += n;
    }
    resp[total] = '\0';

    t1 = GetTickCount64();
    closesocket(s);

    if (ms) *ms = (double)(t1 - t0);
    return (total > 0) ? 0 : -1;
}

/* 判断业务是否成功：响应里找 "ok":true */
static int business_ok(const char *resp)
{
    if (!resp) return 0;
    return (strstr(resp, "\"ok\":true") != NULL) ? 1 : 0;
}

/* 生成一条入库/出库的表单体 */
static void make_form(char *buf, int size, int kind, long seq)
{
    static const char *PREF[8] = { "卷尺", "扳手", "锯子", "钳子", "锤子", "卡尺", "手套", "钻头" };

    if (kind == 0) {
        /* 入库：编号落在 T8000-T8999，反复累加，数据规模可控 */
        snprintf(buf, size,
                 "id=T8%03d&name=%s%02d&factory=bench&batch=2025%02d%02d&qty=%d&price=%.2f",
                 (int)(seq % 1000), PREF[seq % 8], (int)(seq % 90),
                 (int)(seq % 12) + 1, (int)(seq % 28) + 1,
                 (int)(seq % 9) + 1, 10.0 + (double)(seq % 900) / 10.0);
    } else {
        /* 出库：挑一条已存在的记录，数量 1~2；不存在时服务端会返回业务失败，属预期 */
        snprintf(buf, size, "id=T%04d&qty=%d", (int)(seq % 500) + 1, (int)(seq % 2) + 1);
    }
}

/* ---------------- 主程序 ---------------- */

int main(int argc, char *argv[])
{
    WSADATA wsa;
    int     seconds = DEFAULT_SECONDS;
    int     port    = DEFAULT_PORT;
    int     i;
    long    seq = 1;
    ULONGLONG t_start, t_now;
    long    last_report_sec = -1;
    DWORD   srv_pid;
    char    progress[64];
    char   *resp = (char *)malloc(RESP_MAX);

    if (!resp) { printf("[ERROR] out of memory\n"); return 1; }

    if (argc > 1) { seconds = atoi(argv[1]); if (seconds <= 0) seconds = DEFAULT_SECONDS; }
    if (argc > 2) { port    = atoi(argv[2]); if (port <= 0 || port > 65535) port = DEFAULT_PORT; }

    printf("============================================================\n");
    printf("  仓库管理系统 · 稳定性压测客户端（C + Winsock）\n");
    printf("============================================================\n");
    printf("  目标服务   : http://localhost:%d\n", port);
    printf("  运行时长   : %d 秒 (%.2f 小时)\n", seconds, seconds / 3600.0);
    printf("  请求配比   : list 45%% / in 20%% / out 20%% / sort 10%% / export 5%%\n");
    printf("------------------------------------------------------------\n");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[ERROR] WSAStartup failed\n");
        free(resp);
        return 1;
    }

    /* ---- 连通性检查 ---- */
    if (http_request(port, "GET", "/api/list", NULL, resp, RESP_MAX, NULL) != 0) {
        printf("[ERROR] 无法连接 http://localhost:%d\n", port);
        printf("        请先启动 server.exe，再运行本压测程序。\n");
        WSACleanup();
        free(resp);
        return 1;
    }
    printf("[OK] 服务可达，开始压测 ...\n\n");

    /* ---- 预置 500 条基础数据，方便出库类请求命中 ---- */
    {
        double ms = 0;
        char   form[256];
        snprintf(form, sizeof(form), "n=500");
        if (http_request(port, "POST", "/api/gen", form, resp, RESP_MAX, &ms) == 0)
            printf("[prepare] /api/gen 生成基础测试数据完成 (%.0f ms)\n\n", ms);
    }

    srv_pid    = find_pid("server.exe");
    g_mem_first = process_mem_mb(srv_pid);
    if (g_mem_first > 0)
        printf("[monitor] 已定位 server.exe (PID %lu)，起始内存 %.1f MB\n\n",
               (unsigned long)srv_pid, g_mem_first);

    t_start = GetTickCount64();

    for (;;) {
        double ms = 0.0;
        int    r, dice;
        ULONGLONG tl0, tl1;              /* 诊断：循环体耗时 */

        tl0 = GetTickCount64();
        t_now = tl0;
        if ((long)((t_now - t_start) / 1000) >= seconds) break;

        dice = rand() % 100;
        if (dice < 45) {
            r = http_request(port, "GET",  "/api/list", NULL, resp, RESP_MAX, &ms);
        } else if (dice < 65) {
            char form[512];
            make_form(form, sizeof(form), 0, seq++);
            r = http_request(port, "POST", "/api/in", form, resp, RESP_MAX, &ms);
        } else if (dice < 85) {
            char form[256];
            make_form(form, sizeof(form), 1, seq++);
            r = http_request(port, "POST", "/api/out", form, resp, RESP_MAX, &ms);
        } else if (dice < 95) {
            char form[128];
            snprintf(form, sizeof(form), "field=%d&asc=%d&algo=%d",
                     (int)(seq % 2) + 1, (int)(seq % 2), (int)(seq % 5) + 1);
            seq++;
            r = http_request(port, "POST", "/api/sort", form, resp, RESP_MAX, &ms);
        } else {
            r = http_request(port, "GET",  "/api/export", NULL, resp, RESP_MAX, &ms);
        }

        g_total++;
        if (r != 0) {
            g_transport_fail++;
        } else {
            g_ok++;
            if (!business_ok(resp)) g_business_fail++;
            g_sum_ms += ms;
            if (ms > g_max_ms) g_max_ms = ms;
            g_bucket[bucket_of(ms)]++;
        }

        /* ---- 诊断：输出前 8 次循环的耗时构成 ---- */
        tl1 = GetTickCount64();
        if (g_total <= 8) {
            printf("[dbg] loop#%ld net=%.1fms body=%llums sofar\n",
                   g_total, ms, (unsigned long long)(tl1 - tl0));
            fflush(stdout);
        }

        /* ---- 每分钟输出一次进度 ---- */
        {
            long el = (long)((t_now - t_start) / 1000);
            if (el / 60 != last_report_sec) {
                double mem = process_mem_mb(srv_pid);
                last_report_sec = el / 60;
                fmt_elapsed(el, progress, sizeof(progress));
                if (mem > g_mem_peak) g_mem_peak = mem;
                if (mem > 0) g_mem_last = mem;
                printf("[%s] 请求 %-8ld 成功 %-8ld 失败 %-5ld 平均 %.1f ms 最大 %.0f ms 内存 %.1f MB\n",
                       progress, g_total, g_ok, g_transport_fail,
                       g_ok ? g_sum_ms / g_ok : 0.0, g_max_ms, mem > 0 ? mem : -1.0);
                fflush(stdout);
            }
        }

        /* ---- 诊断：进度报告段耗时 ---- */
        if (g_total <= 8) {
            printf("[dbg] loop#%ld after-progress=%llums TOTAL-loop=%llums\n",
                   g_total,
                   (unsigned long long)(GetTickCount64() - tl1),
                   (unsigned long long)(GetTickCount64() - tl0));
            fflush(stdout);
        }
    }

    /* ---------------- 汇总 ---------------- */
    {
        double elapsed = (double)(GetTickCount64() - t_start) / 1000.0;
        double avg = g_ok ? g_sum_ms / g_ok : 0.0;
        double mem_end = process_mem_mb(srv_pid);
        FILE  *fp = fopen("bench_report.txt", "w");

        if (mem_end > g_mem_peak) g_mem_peak = mem_end;
        if (mem_end > 0) g_mem_last = mem_end;

        printf("\n============================================================\n");
        printf("                    压 测 汇 总\n");
        printf("============================================================\n");
        printf("  运行时长        : %.0f 秒 (%.2f 小时)\n", elapsed, elapsed / 3600.0);
        printf("  总请求数        : %ld\n", g_total);
        printf("  成功响应        : %ld  (%.3f%%)\n", g_ok,
               g_total ? g_ok * 100.0 / g_total : 0.0);
        printf("  传输失败        : %ld\n", g_transport_fail);
        printf("  业务失败(ok:false): %ld  (出库未命中属正常)\n", g_business_fail);
        printf("  吞吐量          : %.1f req/s\n", elapsed > 0 ? g_total / elapsed : 0.0);
        printf("  平均响应时间    : %.2f ms\n", avg);
        printf("  最大响应时间    : %.0f ms\n", g_max_ms);
        printf("  ------------------------------------------------\n");
        printf("  响应时间分布：\n");
        for (i = 0; i < BUCKET_N; i++) {
            printf("    %-12s : %8ld  (%.2f%%)\n", BUCKET_NAME[i], g_bucket[i],
                   g_ok ? g_bucket[i] * 100.0 / g_ok : 0.0);
        }
        printf("  ------------------------------------------------\n");
        if (g_mem_first > 0)
            printf("  服务端内存      : 起始 %.1f MB -> 结束 %.1f MB (峰值 %.1f MB)\n",
                   g_mem_first, mem_end > 0 ? mem_end : -1.0, g_mem_peak);
        else
            printf("  服务端内存      : 未采集到（server.exe 未在运行？）\n");
        printf("============================================================\n");

        if (fp) {
            time_t now = time(NULL);
            char   ts[64];
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

            fprintf(fp, "仓库管理系统 · 稳定性压测报告\n");
            fprintf(fp, "生成时间：%s\n", ts);
            fprintf(fp, "目标服务：http://localhost:%d\n\n", port);
            fprintf(fp, "运行时长(秒)      ：%.0f\n", elapsed);
            fprintf(fp, "总请求数          ：%ld\n", g_total);
            fprintf(fp, "成功响应          ：%ld (%.3f%%)\n", g_ok,
                    g_total ? g_ok * 100.0 / g_total : 0.0);
            fprintf(fp, "传输失败          ：%ld\n", g_transport_fail);
            fprintf(fp, "业务失败(ok:false)：%ld\n", g_business_fail);
            fprintf(fp, "吞吐量(req/s)     ：%.1f\n", elapsed > 0 ? g_total / elapsed : 0.0);
            fprintf(fp, "平均响应时间(ms)  ：%.2f\n", avg);
            fprintf(fp, "最大响应时间(ms)  ：%.0f\n", g_max_ms);
            fprintf(fp, "服务端内存起始(MB)：%.1f\n", g_mem_first);
            fprintf(fp, "服务端内存结束(MB)：%.1f\n", mem_end);
            fprintf(fp, "服务端内存峰值(MB)：%.1f\n\n", g_mem_peak);
            fprintf(fp, "响应时间分布：\n");
            for (i = 0; i < BUCKET_N; i++)
                fprintf(fp, "  %-12s : %8ld (%.2f%%)\n", BUCKET_NAME[i], g_bucket[i],
                        g_ok ? g_bucket[i] * 100.0 / g_ok : 0.0);
            fclose(fp);
            printf("\n  报告已写入 bench_report.txt\n");
        }
    }

    WSACleanup();
    free(resp);
    return 0;
}
