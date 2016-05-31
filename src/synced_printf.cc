/*
 * synced_printf.cc
 *
 *  Created on: 2016/05/29
 *      Author: yosuke
 */

#include "synced_printf.h"

std::mutex g_synced_printf_mutex;
