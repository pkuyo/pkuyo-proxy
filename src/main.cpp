

#include <fstream>
#include <csignal>
#include <bits/signum-generic.h>
#include <sys/stat.h>
#include <wait.h>
#include "logger.h"
#include "config_loader.h"
#include "init_listener.h"



const char* PID_FILE = "/var/run/pkuyo_proxy.pid";

//设置子进程退出
bool needExit = false;

//设置重新加载
bool need_reload = true;

void create_daemon() {

#ifndef FRONT

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "Fork failed!");
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }

    umask(0);

    if (setsid() < 0) {
        fprintf(stderr,"Failed to create a new session!");
        exit(1);
    }

#endif

    if (chdir("/") < 0) {
        fprintf(stderr,"Failed to change directory!");
        exit(1);
    }

    std::ofstream pidFile(PID_FILE);
    if (!pidFile) {
        fprintf(stderr, "Failed to create PID file!\n");
        exit(1);
    }
    pidFile << getpgid(0) << std::endl;
    pidFile.close();

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);


}

void send_signal_to_daemon(const char* signal_type) {
    std::ifstream pidFile(PID_FILE);
    if (!pidFile) {
        fprintf(stderr, "Failed to open PID file!\n");
        exit(1);
    }

    pid_t pid;
    pidFile >> pid;
    pidFile.close();

    if (strcmp(signal_type, "reload") == 0) {
        if (kill(-pid, SIGHUP) == 0) {
            printf("Sent SIGHUP signal to daemon (PID: %d)\n",pid);
        } else {
            fprintf(stderr, "Failed to send SIGHUP signal!\n");
        }
    } else if (strcmp(signal_type, "stop") == 0) {
        if (kill(-pid, SIGTERM) == 0) {
            printf("Sent SIGTERM signal to daemon (PID: %d)\n",pid);
        } else {
            fprintf(stderr,"Failed to send SIGTERM signal!\n");
        }
    } else {
        fprintf(stderr, "Unknown signal type: %s\n", signal_type);
        exit(1);
    }

    exit(0);
}



void cleanup() {
    remove(PID_FILE);
    spdlog::info("Shutting down gracefully");
}

void global_signal_handler(int signum) {
    if (signum == SIGHUP) {
        need_reload = true;
    }
    else if (signum == SIGTERM) {
        cleanup();
        exit(0);
    }
    else if (signum == SIGCHLD) {
        while ((waitpid(-1, nullptr, WNOHANG)) > 0);
    }
}

void reload_config() {

    spdlog::info("reload config");



    ProxyConfig config;
    if (load_config("/conf/pkuyo_proxy.json",config))
        exit(1);

    fork_listeners(config);




}


void init() {

    create_daemon();

    if (setup_logger())
        exit(1);

    signal(SIGHUP, global_signal_handler);
    signal(SIGTERM, global_signal_handler);
    signal(SIGCHLD, global_signal_handler);

    while (true) {
        if (need_reload) {
            reload_config();
            need_reload = false;
        }
        pause();
    }
}



int main(int argc, char* argv[]) {

    if (argc < 2) {
        fprintf(stderr,"Invalid number of arguments\n");
        exit(1);
    }
    if (strcmp(argv[1], "init") == 0) {
        if (access(PID_FILE, F_OK) != -1) {
            fprintf(stderr,"already started proxy");
            exit(1);
        }
        init();
    }
    else {
        send_signal_to_daemon(argv[1]);
    }
    return 0;
}