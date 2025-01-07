#include "bgpmgmtd/bgpmgmtd.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    FILE *fp = fopen("/tmp/frr-bgpmgmtd.log", "w");
    if (!fp) {
        perror("fopen");
        return 1;
    }
    fprintf(fp, "bgpmgmtd, start write your own daemon here!\n");
    fclose(fp);
    return 0;
}