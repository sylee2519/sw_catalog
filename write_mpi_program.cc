#include <stdio.h> 
#include <mpi.h> 
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <syscall.h> 
#include <unistd.h>
#include <fcntl.h>
#include <openssl/sha.h> 
#include "uthash.h" 


#define FILE_CNT 190000

using namespace std;

int main(int argc, char *argv[])
{
	int provided; 
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided); 
	int rank, worldSize; 

	MPI_Comm_size(MPI_COMM_WORLD, &worldSize); 
	MPI_Comm_rank(MPI_COMM_WORLD, &rank); 

	unsigned char  temp_fp[SHA_DIGEST_LENGTH];
	

	char hostname[256];
    	gethostname(hostname, sizeof(hostname));
	cout << rank << '\t' << hostname << '\t'; 

	uint64_t file_size = atoi(argv[1]); 
	int file_cnt = atoi(argv[2]); 
	int mode = atoi(argv[3]); 

	char working_dir[1024]; 
	if (getcwd(working_dir, sizeof(working_dir)) == NULL)
	{
		perror("getcwd() error"); 
		exit(1); 
	};

	char * buffer = (char *)malloc(file_size); 
	if (buffer == NULL)
	{
		perror("malloc failed\n"); 
		exit(1); 
	}
	auto start = chrono::high_resolution_clock::now(); 


	int FileCntPerWorker = file_cnt / worldSize; 
	for(int i = 0; i < FileCntPerWorker; i++)
	{
		int idx = i * worldSize + rank;
		
		memset(buffer, 'A', file_size); 

		string file_path = working_dir + string("/output") + to_string(mode) + "/" +  to_string(idx) + ".txt"; 
 //       printf("file_path : %s\n", file_path.c_str());		
		int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644); 
		if (fd == -1) 
		{
			printf("open error %d\n", errno) ; 
			exit(-1); 
		}
		
		// Computing PHase 
		SHA1(buffer, file_size, temp_fp); 


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
		auto end = chrono::high_resolution_clock::now(); 
		chrono::duration<double> duration = end - start; 
		cout << "execution time\t" << duration.count() << endl; 

		FILE * fp = fopen("exec_time.eval", "a"); 
		if (fp == NULL)
		{
			perror("file open error"); 
			exit(1); 
		}
		fprintf(fp, "%d\t%d\t%d\t%d\t%lf\n", mode, worldSize, file_size, file_cnt, duration.count()); 
		fclose(fp); 
	}
	MPI_Finalize(); 

}
