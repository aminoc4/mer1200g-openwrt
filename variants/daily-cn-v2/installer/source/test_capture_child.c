#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>
int main(int argc,char **argv){
 if(argc!=3)return 2;
 if(!strcmp(argv[1],"delay")){struct timespec wait={1,400000000};nanosleep(&wait,NULL);}
 if(!strcmp(argv[1],"overflow")){for(unsigned i=0;i<5000;i++)putchar('x');return 0;}
 FILE *f=fopen(argv[2],"rb");if(!f)return 3;int c;while((c=fgetc(f))!=EOF)putchar(c);fclose(f);
 return !strcmp(argv[1],"error")?7:0;
}
