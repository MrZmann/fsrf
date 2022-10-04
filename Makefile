HOME_DIR = /home/centos/src/project_data
AWS_FPGA = $(HOME_DIR)/aws-fpga
HDK_DIR = $(AWS_FPGA)/hdk/
SDK_DIR = $(AWS_FPGA)/sdk/

VPATH = src:include:$(HDK_DIR)/common/software/src:$(HDK_DIR)/common/software/include

INCLUDES = -I$(SDK_DIR)/userspace/include
INCLUDES += -I $(HDK_DIR)/common/software/include

CC = g++
CFLAGS = -O3 -std=c++11 -fpermissive -DCONFIG_LOGLEVEL=4 -g -Wall $(INCLUDES)

LDLIBS = -lfpga_mgmt -lrt -lpthread

SRC = ${SDK_DIR}/userspace/utils/sh_dpi_tasks.c

reg:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp reg_test.cpp -o reg_test

dma:
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(SRC) fpga.cpp dma_test.cpp -o dma_test
