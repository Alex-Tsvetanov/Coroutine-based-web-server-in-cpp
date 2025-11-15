#pragma once

#include "awaitable.h"
#include "common.h"
#include "http_parser.h"
#include "scheduler.h"
#include "task.h"

Task<> handle_connection(int client_fd);
