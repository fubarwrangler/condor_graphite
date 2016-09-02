#include <stdio.h>
#include <libcgroup.h>

int main(int argc, char *argv[])
{
    int ret;

	if((ret = cgroup_init()) != 0) {
		fprintf(stderr, "Error initalizing libcgroup: %s\n", cgroup_strerror(ret));
		return 1;
	}

	return 0;
}
