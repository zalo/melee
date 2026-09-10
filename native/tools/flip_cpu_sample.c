// Sample user instruction pointers in all current process threads using perf_event.
#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
struct stream { int fd,tid; void *map; uint64_t tail; } streams[256];
static void drain(struct stream *s,size_t page) {
 struct perf_event_mmap_page *m=s->map;
 uint64_t head=__atomic_load_n(&m->data_head,__ATOMIC_ACQUIRE);
 size_t size=16*page;
 while(s->tail<head) {
  struct perf_event_header h;
  unsigned char *data=(unsigned char*)s->map+page;
  for(size_t i=0;i<sizeof(h);i++) ((unsigned char*)&h)[i]=data[(s->tail+i)%size];
  if(h.size<sizeof(h)||h.size>size) { fprintf(stderr,"corrupt ring\n"); exit(2); }
  if(h.type==PERF_RECORD_SAMPLE) {
   uint64_t ip; for(size_t i=0;i<8;i++) ((unsigned char*)&ip)[i]=data[(s->tail+8+i)%size];
   uint64_t regs[6] = {0};
   if(h.size >= 8+8+sizeof(regs)) for(size_t i=0;i<sizeof(regs);i++) ((unsigned char*)regs)[i]=data[(s->tail+16+i)%size];
   printf("%d %llx %llx %llx %llx %llx %llx\n",s->tid,(unsigned long long)ip,(unsigned long long)regs[1],(unsigned long long)regs[2],(unsigned long long)regs[3],(unsigned long long)regs[4],(unsigned long long)regs[5]);
  } else if(h.type==PERF_RECORD_LOST) fprintf(stderr,"lost samples tid %d\n",s->tid);
  s->tail+=h.size;
 }
 __atomic_store_n(&m->data_tail,s->tail,__ATOMIC_RELEASE);
}
int main(int argc,char **argv) {
 if(argc!=3) { fprintf(stderr,"usage: %s PID SECONDS\n",argv[0]);return 2; }
 char path[128];snprintf(path,sizeof(path),"/proc/%d/task",atoi(argv[1]));
 DIR *dir=opendir(path);if(!dir) {perror("task");return 1;}
 size_t page=sysconf(_SC_PAGESIZE); int n=0;struct dirent *ent;
 while((ent=readdir(dir))&&n<256) {
  int tid=atoi(ent->d_name);if(!tid)continue;
  // x30 adds a return-address snapshot for attributing leaf memcpy/memcmp
  // samples to their caller. It is not a complete or guaranteed stack unwind.
  struct perf_event_attr a={.type=PERF_TYPE_SOFTWARE,.size=sizeof(a),.config=PERF_COUNT_SW_CPU_CLOCK,.sample_period=1000000,.sample_type=PERF_SAMPLE_IP|PERF_SAMPLE_REGS_USER,.sample_regs_user=(1ULL<<0)|(1ULL<<2)|(1ULL<<20)|(1ULL<<21)|(1ULL<<30),.disabled=1,.exclude_kernel=1,.exclude_hv=1,.wakeup_events=1};
  int fd=syscall(__NR_perf_event_open,&a,tid,-1,-1,0);
  if(fd<0){perror("perf_event_open");return 1;}
  void *map=mmap(NULL,17*page,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
  if(map==MAP_FAILED){perror("mmap");return 1;}
  streams[n++]=(struct stream){fd,tid,map,0};
  ioctl(fd,PERF_EVENT_IOC_ENABLE,0);
 }
 closedir(dir);
 struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);
 do {usleep(10000);for(int i=0;i<n;i++)drain(&streams[i],page);clock_gettime(CLOCK_MONOTONIC,&now);}while(now.tv_sec-start.tv_sec<atoi(argv[2]));
 for(int i=0;i<n;i++){ioctl(streams[i].fd,PERF_EVENT_IOC_DISABLE,0);drain(&streams[i],page);munmap(streams[i].map,17*page);close(streams[i].fd);}return 0;
}
