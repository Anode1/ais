#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
static double ms(struct timespec a){struct timespec b;clock_gettime(CLOCK_MONOTONIC,&b);
  return (b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6;}
static char*slurp(const char*p,size_t*len){int fd=open(p,O_RDONLY);if(fd<0){perror(p);exit(1);}
  struct stat st;fstat(fd,&st);size_t n=st.st_size;char*b=malloc(n);size_t got=0;ssize_t r;
  while(got<n&&(r=read(fd,b+got,n-got))>0)got+=r;close(fd);*len=n;return b;}
static int countlines(const char*s,size_t n,const char*p){size_t pl=strlen(p);int h=0,ln=0;char p0=p[0];
  for(size_t i=0;i<n;i++){char b=s[i];
    if(b=='\n'){if(ln)h++;ln=0;}
    else if(!ln&&b==p0&&i+pl<=n){size_t j=1;while(j<pl&&s[i+j]==p[j])j++;if(j==pl)ln=1;}}
  if(ln)h++;return h;}
static long*readpost(const char*p,size_t*cnt){size_t n;char*d=slurp(p,&n);size_t L=0;
  for(size_t i=0;i<n;i++)if(d[i]=='\n')L++;long*o=malloc(L*sizeof(long));size_t k=0;long c=0;int any=0;
  for(size_t i=0;i<n;i++){char b=d[i];if(b=='\n'){if(any)o[k++]=c;c=0;any=0;}
    else if(b>='0'&&b<='9'){c=c*10+(b-'0');any=1;}}free(d);*cnt=k;return o;}
static int inter(long*A,size_t na,long*B,size_t nb){size_t i=0,j=0;int c=0;
  while(i<na&&j<nb){long x=A[i],y=B[j];if(x==y){c++;i++;j++;}else if(x<y)i++;else j++;}return c;}
int main(int ac,char**av){int K=7;(void)ac;
  if(!strcmp(av[1],"find")){size_t n;char*s=slurp(av[2],&n);
    for(int it=0;it<K;it++){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
      int h=countlines(s,n,av[3]);printf("find  iter%d  %5.0f ms  hits=%d\n",it,ms(t),h);}}
  else{size_t na,nb;long*A=readpost(av[2],&na),*B=readpost(av[3],&nb);
    for(int it=0;it<K;it++){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
      int c=inter(A,na,B,nb);printf("and   iter%d  %6.1f ms  common=%d  |A|=%zu |B|=%zu\n",it,ms(t),c,na,nb);}}
  return 0;}
