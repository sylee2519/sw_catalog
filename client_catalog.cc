#include <dlfcn.h>
#include <iostream>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <chrono>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <mutex>
#include <lustre/lustreapi.h>

#define MAX_FILE_LEN 512
#define NUM_ELEMENTS 500000
#define SHARED_MEMORY_KEY 1234000
#define MSG_SIZE 255
#define MPIPROCS 3
#define NUMPERNODE 3

#define ROOT_PATH ("s5104a21")
#define PATH_MAX 4096

using namespace std;

typedef struct Element{
    char msg[MSG_SIZE];
    bool filled;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Element;

struct Element *msg_queue;

int shmid;
static int Rank, worldSize;
static int head_idx = 0;
string ROOT;
string DUMMY;
bool initial_call = true;

char *env_home;
int process_id;
char current_working_directory[PATH_MAX];

void init_environ_var();
void init_msg_queue();

int (*origin_close)(int fd);
int (*origin_open)(const char *file_path, int flags, ...);

// get the full path in case of relative path
void get_full_path(const char *path, char *full_path){ 
    if (path[0] == '/'){
        strncpy(full_path, path, PATH_MAX);
    }
    else{
        strncpy(full_path, current_working_directory, PATH_MAX);
        strncat(full_path, "/", PATH_MAX - strlen(full_path) - 1);
        strncat(full_path, path, PATH_MAX - strlen(full_path) - 1);
    }
}

// custom open
int open(const char *pathname, int flags, ...){

    origin_open = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");

    // shared memory and environment setting
    if (initial_call){
        initial_call = false;
        init_environ_var();
        init_msg_queue();
        if (getcwd(current_working_directory, PATH_MAX) == NULL){
            perror("getcwd");
            return -1;
        }
    }

    char full_path[PATH_MAX];
    get_full_path(pathname, full_path);

    // not intercepted form my program (don't know why)
    if (string(full_path).find(ROOT_PATH) == string::npos){
        if (flags & O_CREAT){
            va_list args;
            va_start(args, flags);
            mode_t mode = va_arg(args, mode_t);
            va_end(args);
            return origin_open(pathname, flags, mode);
        }
        else{
            return origin_open(pathname, flags);
        }
    }
    int fd;

    // from my application
    if ((flags & O_CREAT) || (flags & O_RDWR) || (flags & O_WRONLY)){
        va_list args;
        va_start(args, flags);
        mode_t mode = va_arg(args, mode_t);
        va_end(args);
        fd = origin_open(full_path, flags, mode);

        // write message to the server
        if (fd >= 0){
            Element *element = &msg_queue[head_idx];
            snprintf(element->msg, MSG_SIZE, "OPEN\t%s\t%d\t%d", full_path, fd, flags);
            element->filled = true;
            head_idx = (head_idx + NUMPERNODE) % NUM_ELEMENTS;
        }
    }
    // proceed with the original open
    else{
        fd = origin_open(full_path, flags);
    }
    return fd;
}

int close(int fd){
    char file_path[MAX_FILE_LEN] = {0};
    string _file_path;
    int shmid;

    origin_close = (int (*)(int))dlsym(RTLD_NEXT, "close");

    string descriptor_path = "/proc/self/fd/" + to_string(fd);
    readlink(descriptor_path.c_str(), file_path, MAX_FILE_LEN);
    _file_path = file_path;

    if (_file_path.find(ROOT_PATH) == string::npos)
        return (*origin_close)(fd);

    Element *element = &msg_queue[head_idx];
    snprintf(element->msg, MSG_SIZE, "CLOSE\t%d", fd);
    element->filled = true;
    head_idx = (head_idx + NUMPERNODE) % NUM_ELEMENTS;
    printf("close sent\n");
    return (*origin_close)(fd);
}

void init_environ_var(){
    char *env_rank = getenv("OMPI_COMM_WORLD_RANK");
    char *env_size = getenv("OMPI_COMM_WORLD_SIZE");
    process_id = getpid();

    if (!env_rank || !env_size){
        puts("Getting environment variable failed.");
        exit(0);
    }

    char *hostname = getenv("HOSTNAME");
    if (hostname != NULL){
        puts("client");
        puts(hostname);
        puts(env_rank);
        puts(env_size);
        puts(to_string(process_id).c_str());
    }
    Rank = atoi(env_rank);
    worldSize = atoi(env_size);
    head_idx = Rank % NUMPERNODE;
}

void init_msg_queue(){
    int node_idx = Rank / MPIPROCS;

    // processes from the same node share the array buffer
    key_t key = ftok(".", SHARED_MEMORY_KEY + node_idx);
    shmid = shmget(key, NUM_ELEMENTS * sizeof(struct Element), 0666);
    if (shmid == -1){
        perror("shmget");
        exit(1);
    }

    msg_queue = (struct Element *)shmat(shmid, NULL, 0);
    if (msg_queue == (void *)-1){
        perror("shmat");
        exit(1);
    }

    int name_len;
    char processor_name[256];

    puts("shared memory was successfully attached on");
    process_id = getpid();
    char pid_msg[MSG_SIZE];
    // send the pid to the server for server program shutdown
    snprintf(pid_msg, MSG_SIZE, "PID\t%d", process_id);
    Element *element = &msg_queue[head_idx];
    strncpy(element->msg, pid_msg, MSG_SIZE);
    element->filled = true;
    head_idx = (head_idx + NUMPERNODE) % NUM_ELEMENTS;
}
