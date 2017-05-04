#include <stdio.h>

char *msql_tmpnam(baseName)
        char *baseName;
{
    	static char buf[100];
    	static int nr = 0;
 
    	nr++;
 
    	if (nr == 9999999)
		nr = 1;
 
	sprintf(buf, "/tmp/%s.%07d.%07d", baseName ? baseName:"msql", nr,
		getpid());
	return (char *)strdup(buf);
}

