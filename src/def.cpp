//
// Created by pkuyo on 25-2-27.
//
#include "def.h"
#include <sys/mman.h>

#include "spdlog/spdlog.h"

ListenerConfig::ListenerConfig() = default;

ProcContext::~ProcContext() {
    if (!is_valid)
        return;
    if (shm_fd != -1) {
        if (munmap(load_balancer, mem_length) == -1) {
               spdlog::error("Failed to unmap shared memory");
        }
        if (close(shm_fd)) {
            spdlog::error("close(shm_fd) failed");
        }
    }
    spdlog::debug("ProcContext destroyed");
}
pid_t fork_with_cleanup() {
    return fork();
}