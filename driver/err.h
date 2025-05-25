#ifndef ERR_H
#define ERR_H

#include <linux/printk.h>

// Устанавливаем префикс для journalctl -t RIPC
#undef pr_fmt
#define pr_fmt(fmt) "RIPC: %s:%d: " fmt, __FUNCTION__, __LINE__
#ifdef _DEBUG
#define INF(fmt, ...) pr_info(fmt "\n", ##__VA_ARGS__)
#else
#define INF(smt, ...){}
#endif

#define ERR(fmt, ...) pr_err(fmt "\n", ##__VA_ARGS__)

#endif // !ERR_H