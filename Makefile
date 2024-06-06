CC = g++
MPICXX=mpicxx
CFLAGS = -std=c++17 -fpermissive -Wall -Wextra -g
LD_CFLAGS = -fPIC -shared
CLIB = -lrt -lm -llustreapi -ldl -lssl -lcrypto 

working_dir = /scratch/s5104a21/mpi_danzer/new/catalog

binary_files = mpi_server_catalog write_mpi_program client_catalog.so

all: $(binary_files)

client_catalog.so: client_catalog.cc
	$(CC) $(CFLAGS) $(LD_CFLAGS) -o $@ $< $(CLIB)
#	cp $@ $(working_dir) 

mpi_server_catalog: mpi_server_catalog.cc
	$(MPICXX) $(CFLAGS) -fpermissive -o $@ $< $(CLIB)
#	cp $@ $(working_dir) 


write_mpi_program: write_mpi_program.cc
	$(MPICXX) $(CFLAGS) -fpermissive -o $@ $< $(CLIB)
#	cp $@ $(working_dir) 
clean:
	rm -f $(binary_files) 
