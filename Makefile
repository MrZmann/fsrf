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

md5:
	@sudo fpga-load-local-image -D -S 0 -I agfi-0a9192afc18f97549
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp fsrf.cpp apps/md5.cpp -o md5.out
aes:
	@sudo fpga-load-local-image -D -S 0 -I agfi-0057779ad2eb6dae4
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp fsrf.cpp apps/aes.cpp -o aes.out

reg:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp tests/fpga/reg_test.cpp -o reg_test

dma:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp tests/fpga/dma_test.cpp -o dma_test


test:
	@make -s reg
	@make -s dma
	@sudo ./reg_test > /dev/null
	@sudo ./dma_test > /dev/null
	@rm -f reg_test dma_test

