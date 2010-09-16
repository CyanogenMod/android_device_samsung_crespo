#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/wait.h>

const char *mkfs = "/system/bin/make_ext4fs";

int setup_fs(const char *blockdev)
{
    char buf[128];
    pid_t child;
    int status;

    sprintf(buf,"/sys/fs/ext4/%s", blockdev);
    if (access(buf, F_OK) == 0) {
        fprintf(stderr,"device %s already has a filesystem\n", blockdev);
        return 0;
    }
    sprintf(buf,"/dev/block/%s", blockdev);

    fprintf(stderr,"+++\n");

    child = fork();
    if (child < 0) {
        fprintf(stderr,"error: fork failed\n");
        return 0;
    }
    if (child == 0) {
        execl(mkfs, mkfs, buf, NULL);
        exit(-1);
    }

    while (waitpid(-1, &status, 0) != child) ;

    fprintf(stderr,"---\n");
    return 1;
}


int main(int argc, char **argv)
{
    int need_reboot = 0;

    while (argc > 1) {
        if (strlen(argv[1]) > 32) continue;
        need_reboot |= setup_fs(argv[1]);
        argv++;
        argc--;
    }

    if (need_reboot) {
        sync();
        sync();
        sync();
        fprintf(stderr,"REBOOT!\n");
        reboot(RB_AUTOBOOT);
        exit(-1);
    }
    return 0;
}
