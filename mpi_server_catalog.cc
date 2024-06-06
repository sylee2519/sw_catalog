#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <lustre/lustreapi.h>
#include <signal.h>
#include <mutex>
#include <mpi.h>
#include <map>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <fcntl.h>

#define MSG_SIZE 255
#define SHARED_MEMORY_KEY 1234000
#define NUM_ELEMENTS 500000
#define NUMPERNODE 3


using namespace std;

struct Element *msg_queue;
FILE * catalog_fp;
int shmid;
int tail_idx = 0;

typedef struct Element {
    char msg[MSG_SIZE];
    bool filled;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
}Element;

struct OpenFileInfo {
    string path;
    int flags;
    struct stat64 sb;
};


static int processed_cnt = 0;
static int cnt = 0;
int Rank, worldSize;
bool real_dead = false;

map<int, OpenFileInfo> open_files;
vector<string> catalog_entries;
vector<pid_t> client_pids;

void msg_queue_init();
int layout_analysis(const char *file_path, struct stat64 *sb);
void sig_kill_handler(int signal);
void check_alive();
void write_catalog();
void record_fstat(const char *file_path, const struct stat64 *sb);

int main() {

    printf("%lu\n", NUM_ELEMENTS * sizeof(struct Element));
    int provided;
    MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &provided);

    signal(SIGINT, sig_kill_handler);
    signal(SIGKILL, sig_kill_handler);
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
    MPI_Comm_rank(MPI_COMM_WORLD, &Rank);

    char * string_env_rank = getenv("OMPI_COMM_WORLD_RANK");
    int env_rank = atoi(string_env_rank);
    printf("rank: %d env_rank: %d processID: %d\n", Rank, env_rank,  getpid());

    int node_idx = Rank / NUMPERNODE;
    char hostname[256];
        gethostname(hostname, sizeof(hostname));
    printf("%d\t%s\t", Rank, hostname);


    if (Rank % NUMPERNODE != 0)
    {
        MPI_Finalize();
        return 0;
    }

    msg_queue_init();

    thread check_thread(check_alive);

    string catalog_file = "catalog" + to_string(node_idx) + ".txt";
    puts(catalog_file.c_str());

    catalog_fp = fopen(catalog_file.c_str(), "w");
    if (catalog_fp == NULL)
    {
        perror("Open catalog");
        exit(1);
    }


    char file_path [MSG_SIZE];

    string _file_path;


    while(1){

        if (msg_queue[tail_idx].filled == true){

            strncpy(file_path, msg_queue[tail_idx].msg, MSG_SIZE);
            printf("receieved %s\n", file_path);
            if (strncmp(file_path, "PID\t", 4) == 0) {
                pid_t pid;
                sscanf(file_path, "PID\t%d", &pid);
                client_pids.push_back(pid);
            }
            else if (strncmp(file_path, "OPEN\t", 5) == 0) {
                char path[PATH_MAX];
                int fd, flags;
                sscanf(file_path, "OPEN\t%s\t%d\t%d", path, &fd, &flags);
                struct stat64 sb;
                fstat64(fd, &sb);
                open_files[fd] = {string(path), flags, sb};
            }

            else if (strncmp(file_path, "CLOSE\t", 6) == 0){
                  int fd;
                sscanf(file_path, "CLOSE\t%d", &fd);
                if (open_files.find(fd) != open_files.end()){
                    OpenFileInfo info = open_files[fd];
                    open_files.erase(fd);
                    record_fstat(info.path.c_str(), &info.sb);
                   if(info.flags & O_CREAT){
                    if (layout_analysis(info.path.c_str(), &info.sb) == 0) {
     
                        catalog_entries.push_back(info.path);
                        if (catalog_entries.size() >= 20) {
                            write_catalog();
                        }
                    }
                    }

                }
            }
            msg_queue[tail_idx].filled = false;
            processed_cnt++;
            tail_idx = (tail_idx + 1) % NUM_ELEMENTS;


        }
        if(real_dead == true){
            break;
        }



    }

       MPI_Finalize();
       
            fclose(catalog_fp);
                if (shmdt(msg_queue) == -1) {
                    perror("shmdt");
                    exit(1);
                }

                if (shmctl(shmid, IPC_RMID, NULL) == -1) {
                    perror("shmctl");
                    exit(1);
                }

                printf("processed cnt: %d\n", processed_cnt);
                puts("Server Process Completed");
                exit(0);

    return 0;
}


void msg_queue_init()
{
    int node_idx = Rank / NUMPERNODE;
    key_t key = ftok(".", SHARED_MEMORY_KEY+node_idx);
    
    shmid = shmget(key, NUM_ELEMENTS * sizeof(struct Element), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    printf("id: %d\n", shmid);

    msg_queue = (struct Element *)shmat(shmid, NULL, 0);
    if (msg_queue == (void *)-1) {
        perror("shmat");
        exit(1);
    }

    for (int i = 0; i < NUM_ELEMENTS; ++i) {
        msg_queue[i].filled = false;
    pthread_mutex_init(&msg_queue[i].mutex, NULL);
    pthread_cond_init(&msg_queue[i].cond, NULL);
     }

    printf("shared memory has created well\n");

    char * hostname = getenv("HOSTNAME");
    printf("server: %s\n", hostname);

}

int catalog_append(const char *file_path, int idx, uint64_t start, uint64_t end, uint64_t interval, uint64_t size) {
    static int cnt = 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%d %s %d %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64, cnt, file_path, idx, start, end, interval, size);
    {
 
        catalog_entries.push_back(string(buf));
    }
    printf("%s\n", buf);
    cnt += 1;
    return 0;
}



int layout_analysis(const char *file_path, struct stat64 *sb){

    struct llapi_layout * layout;

    int rc[3];
    uint64_t count, ost,  start, end, size, file_size, interval ;

    layout = llapi_layout_get_by_path(file_path, 0);
    if (layout == NULL){
        puts(file_path);
        perror("layout error");;
        return 1;
    }


    file_size = sb->st_size;


    rc[0] = llapi_layout_comp_use(layout, 1);
    if (rc[0]){
        printf("error: layout component iteration failed\n");
        return 1 ;
    }
    while(1){
        rc[0] = llapi_layout_stripe_count_get(layout, &count);
        rc[1] = llapi_layout_stripe_size_get(layout, &size);
        rc[2] = llapi_layout_comp_extent_get(layout, &start, &end);
        if (rc[0] || rc[1] || rc[2])
        {
            printf("error: cannot get stripe information\n");
            continue;
        }

        interval = count * size;
        end = (end < file_size)? end : file_size;
        for (int i = 0; i < count; i ++)
        {

            
            rc[0] = llapi_layout_ost_index_get(layout, i, &ost);
            if (rc[0]){
                goto here_exit;
            }

            catalog_append(file_path, ost, start + i * size, end, interval, size);
        }

        rc[0] = llapi_layout_comp_use(layout, 3);
        if (rc[0] == 0)
            continue;
        if (rc[0] < 0)
            printf("error: layout component iteration failed\n");
        break;
    }
    here_exit:
        return  0;

}

void sig_kill_handler(int signal)
{
    fclose(catalog_fp);

    if (shmdt(msg_queue) == -1) {
        perror("shmdt");
        exit(1);
    }

    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl");
        exit(1);
    }


    printf("processed cnt: %d\n", processed_cnt);
    puts("Server Process Completed");
    exit(0);
}
void check_alive() {

    while(true){
        if(processed_cnt > 0){
            break;
        }
    }
    printf("escape\n");
    while (true) {
        this_thread::sleep_for(chrono::seconds(5));


        if (open_files.empty()) {
            bool all_dead = true;
            for (pid_t pid : client_pids) {
                if (kill(pid, 0) != -1 || errno != ESRCH) {
                    all_dead = false;
                    break;
                }
            }
            if (all_dead) {
                write_catalog();
                real_dead = true;
            }
        }
    }

}

void write_catalog() {
    for (const string &entry : catalog_entries) {
        fprintf(catalog_fp, "%s\n", entry.c_str());
    }
    fflush(catalog_fp);
    catalog_entries.clear();
}

void record_fstat(const char *file_path, const struct stat64 *sb) {
    char buf[512];
    snprintf(buf, sizeof(buf), "fstat: %s %ld %ld %ld %ld %ld %ld %ld %ld %ld", file_path, (long)sb->st_dev, (long)sb->st_ino, (long)sb->st_mode, (long)sb->st_nlink, (long)sb->st_uid, (long)sb->st_gid, (long)sb->st_rdev, (long)sb->st_size, (long)sb->st_blksize, (long)sb->st_blocks);
    catalog_entries.push_back(string(buf));
}

