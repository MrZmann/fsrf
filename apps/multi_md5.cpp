#include <assert.h>
#include <errno.h>
#include <iostream>
#include <sys/wait.h>

#include "fsrf.h"

#ifdef min
#undef min
#endif
using namespace std::chrono;

int main(int argc, char *argv[])
{
    pid_t pid; 

    for(int i = 0; i < 4; i++){
        pid = fork();
        if(pid == -1){
            std::cerr << "Fork failed\n";
            exit(1);
        } else if(pid == 0) {
            std::cout <<"making child\n";
            execl("/home/centos/fsrf/md5.out", "/home/centos/fsrf/md5.out",
                    "-a", std::to_string(i).c_str(),
                    "-m", "inv_read", 
                    "-v", (char*) NULL);
            std::cerr << "exec returned!\n";
            std::cout << "Oh dear, something went wrong with execl()! " << strerror(errno) << "\n";    
            exit(1);
        }
    }


    int done = 0;
    while (done != 4) {
        int status;
        wait(&status);
        int s = WEXITSTATUS(status);
        if(s != 0) std::cerr << "Nonzero exit status: " << s << "\n";
        done += 1;
    }

    return 0;
}
