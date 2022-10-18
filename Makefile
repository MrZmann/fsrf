HOME_DIR = /home/centos/src/project_data
# todo: git submodule https://github.com/aws/aws-fpga
AWS_FPGA = $(HOME_DIR)/aws-fpga
# HDK_DIR = $(AWS_FPGA)/hdk/
SDK_DIR = $(AWS_FPGA)/sdk

# VPATH = src:include:$(HDK_DIR)/common/software/src:$(HDK_DIR)/common/software/include

INCLUDES = -I$(SDK_DIR)/userspace/include -I.
# INCLUDES += -I $(HDK_DIR)/common/software/include

CC = g++
CFLAGS = -O3 -std=c++11 -fpermissive -DCONFIG_LOGLEVEL=4 -g -Wall $(INCLUDES)

LDLIBS = -lfpga_mgmt -lrt -lpthread

SRC = ${SDK_DIR}/userspace/utils/sh_dpi_tasks.c

read:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp fsrf.cpp apps/md5.cpp -DINVREAD -o my_md5
write:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp fsrf.cpp apps/md5.cpp -DINVWRITE -o my_md5
mmap:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp fsrf.cpp apps/md5.cpp -DFSRFMMAP -o my_md5
reg:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp tests/fpga/reg_test.cpp -o reg_test

dma:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp tests/fpga/dma_test.cpp -o dma_test


test:
	@make -s reg
	@make -s dma
	@sudo ./reg_test > /dev/null
	@sudo ./dma_test > /dev/null
	@rm -f reg dma
