/*
 * synced_printf.h
 *
 *  Created on: 2016/05/29
 *      Author: yosuke
 */

#ifndef SYNCED_PRINTF_H_
#define SYNCED_PRINTF_H_

#include <cstdio>
#include <mutex>

/**
 * SYNCED_PRINTFマクロの内部実装で使用されている、mutexです.
 */
extern std::mutex g_synced_printf_mutex;

/**
 * 排他制御された、printf()関数です.
 */
#define SYNCED_PRINTF(...) { \
  g_synced_printf_mutex.lock();      \
  std::printf(__VA_ARGS__);  \
  g_synced_printf_mutex.unlock(); }

#endif /* SYNCED_PRINTF_H_ */
