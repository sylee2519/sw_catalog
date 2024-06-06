#!/bin/sh
#PBS -V
#PBS -N catalog
#PBS -q perf2
#PBS -A etc
#PBS -l select=1:ncpus=68:mpiprocs=4:ompthreads=1 
#PBS -l walltime=24:00:00
#PBS -m abe
#PBS -M sylee2519@naver.com
#PBS -W sandbox=PRIVATE

cd $PBS_O_WORKDIR
module purge
module load craype-x86-skylake gcc/7.2.0 openmpi/3.1.0
module load forge/18.1.2
module load cmake/3.17.4

mpirun -np 1 ./mpi_server_catalog 1>stdoutc 2>stderrc &
sleep 5
mpirun -np 3 -x LD_PRELOAD=./client_catalog.so ./write_mpi_program 1024 60 out 1>stdout 2>stderr

