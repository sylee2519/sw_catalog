#include <stdio.h>
#include <mpi.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define FILE_CNT 190000

int main(int argc, char *argv[])
{
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    int rank, worldSize;

    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    unsigned char temp_fp[SHA_DIGEST_LENGTH];

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("%d\t%s\t", rank, hostname);

    uint64_t file_size = strtoull(argv[1], NULL, 10);
    int file_cnt = atoi(argv[2]);
    char *outputdir = argv[3];

    char working_dir[1024];
    if (getcwd(working_dir, sizeof(working_dir)) == NULL)
    {
        perror("getcwd() error");
        exit(1);
    }

    if (rank == 0)
    {
        char output_path[1024];
        snprintf(output_path, sizeof(output_path), "%s/%s", working_dir, outputdir);
        struct stat st;
        if (stat(output_path, &st) == -1)
        {
            if (mkdir(output_path, 0700) == -1)
            {
                perror("mkdir() error");
                exit(1);
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD); 

    char *buffer = (char *)malloc(file_size);
    if (buffer == NULL)
    {
        perror("malloc failed");
        exit(1);
    }

    double start_time = MPI_Wtime();

    int FileCntPerWorker = file_cnt / worldSize;
    for (int i = 0; i < FileCntPerWorker; i++)
    {
        int idx = i * worldSize + rank;

        memset(buffer, 'A', file_size);

        char output_path[1024];
        snprintf(output_path, sizeof(output_path), "%s/%s/%d.txt", working_dir, outputdir, idx);
        int fd = open(output_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1)
        {
            printf("open error %d\n", errno);
            exit(-1);
        }

        SHA1((unsigned char *)buffer, file_size, temp_fp);

        int bytes_written = write(fd, buffer, file_size);
        if (bytes_written == -1)
        {
            perror("Error writing to file");
            close(fd);
            exit(1);
        }
        close(fd);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0)
    {
        double end_time = MPI_Wtime();
        double duration = end_time - start_time;
        printf("execution time\t%lf\n", duration);

        FILE *fp = fopen("exec_time.eval", "a");
        if (fp == NULL)
        {
            perror("file open error");
            exit(1);
        }
        fprintf(fp, "%s\t%d\t%lu\t%d\t%lf\n", outputdir, worldSize, file_size, file_cnt, duration);
        fclose(fp);
    }
    free(buffer);
    MPI_Finalize();

    return 0;
}

