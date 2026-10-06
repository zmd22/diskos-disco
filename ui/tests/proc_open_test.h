/* Test boundary adapter: inner syscall PID -> the same live process in outer proc.
 * Read the real comm/stat; never synthesize a successful identity observation. */
#include <dirent.h>
#include <stdarg.h>
static int namespace_open(const char *path, int flags, ...){
    mode_t mode=0;
    if(flags & O_CREAT){ va_list ap; va_start(ap,flags); mode=va_arg(ap,int); va_end(ap); }
    long pid; int used=0;
    if(sscanf(path,"/proc/%ld/%n",&pid,&used)==1 && used>0){
        char self[256], target[256];
        ssize_t sl=readlink("/proc/self/ns/pid",self,sizeof self-1);
        if(sl<0) return -1;
        self[sl]=0;
        DIR *dir=opendir("/proc"); if(!dir) return -1;
        struct dirent *entry; int result=-1;
        while((entry=readdir(dir))){
            if(entry->d_name[0]<'0' || entry->d_name[0]>'9') continue;
            char ns[512]; snprintf(ns,sizeof ns,"/proc/%s/ns/pid",entry->d_name);
            ssize_t tl=readlink(ns,target,sizeof target-1);
            if(tl<0) continue;
            target[tl]=0; if(strcmp(self,target)) continue;
            char status[512]; snprintf(status,sizeof status,"/proc/%s/status",entry->d_name);
            FILE *file=fopen(status,"r"); if(!file) continue;
            char line[512]; long inner=-1;
            while(fgets(line,sizeof line,file)) if(!strncmp(line,"NSpid:",6)){
                char *q=line+6,*end;
                while(1){ long next=strtol(q,&end,10); if(end==q) break; inner=next; q=end; }
                break;
            }
            fclose(file);
            if(inner!=pid) continue;
            char mapped[640]; snprintf(mapped,sizeof mapped,"/proc/%s/%s",entry->d_name,path+used);
            result=open(mapped,flags,mode); break;
        }
        closedir(dir); return result;
    }
    return open(path,flags,mode);
}
#define open namespace_open
