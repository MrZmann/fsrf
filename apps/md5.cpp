#include <chrono>
#include "aos.hpp"
#include "utils.hpp"

using namespace std::chrono;

struct config
{
	uint32_t abcd[4];
	uint64_t src_addr;
	uint64_t rd_credits;
	uint64_t num_words;
};

int main(int argc, char *argv[])
{
	config configs[4];
	utils util;

	int argi = 1;
	uint64_t length = 25;
	if (argi < argc)
		length = atol(argv[argi]);
	assert(length <= 34);
	++argi;

	uint64_t num_apps;
	bool populate;
	util.parse_std_args(argc, argv, argi, num_apps, populate);

	configs[0].rd_credits = 8;
	configs[0].num_words = 1 << length;

	high_resolution_clock::time_point start, end[4];
	duration<double> diff;
	double seconds;

	aos_client *aos[4];
	util.setup_aos_client(aos);

	int fd[4];
	const char *fnames[4] = {"/mnt/nvme0/file0.bin", "/mnt/nvme0/file1.bin",
							 "/mnt/nvme0/file2.bin", "/mnt/nvme0/file3.bin"};
	for (uint64_t app = 0; app < num_apps; ++app)
	{
		aos[app]->aos_file_open(fnames[app], fd[app]);
	}

	for (uint64_t app = 0; app < num_apps; ++app)
	{
		void *addr = nullptr;
		int flags = populate ? MAP_POPULATE : 0;

		start = high_resolution_clock::now();
		length = configs[0].num_words * 64;
		aos[app]->aos_mmap(addr, length, PROT_READ, flags, fd[app], 0);
		configs[app].src_addr = (uint64_t)addr;
		end[0] = high_resolution_clock::now();

		diff = end[0] - start;
		seconds = diff.count();
		printf("App %lu mmaped file %d at 0x%lX in %g\n", app,
			   fd[app], configs[app].src_addr, seconds);
	}

	// start runs
	start = high_resolution_clock::now();
	for (uint64_t app = 0; app < num_apps; ++app)
	{
		// important, src_addr -> 4k aligned
		aos[app]->aos_cntrlreg_write(0x10, configs[app].src_addr);
		aos[app]->aos_cntrlreg_write(0x18, configs[0].rd_credits);

		// important -> number of 64 byte words to access
		aos[app]->aos_cntrlreg_write(0x20, configs[0].num_words);
	}

	// end runs
	// important -> polls on completion register
	util.finish_runs(aos, end, 0x28, true, configs[0].num_words);
	// important -> answer lives in cntrlreg_read 0x0, 0x8

	// print stats
	uint64_t app_bytes = configs[0].num_words * 64;
	util.print_stats("md5", app_bytes, start, end);

	length = configs[0].num_words * 64;
	for (uint64_t app = 0; app < num_apps; ++app)
	{
		aos[app]->aos_munmap((void *)configs[app].src_addr, length);
		aos[app]->aos_file_close(fd[app]);
	}

	return 0;
}
