#define _GNU_SOURCE 1
#include "mount_graph.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct node {unsigned id,parent,major,minor;char path[256],fs[32],options[256];};
static int unsupported(void){errno=ENOTSUP;return -1;}
static int load_nodes(const char *path,struct node nodes[64],unsigned *count){
 *count=0;int pin=open(path,O_PATH|O_CLOEXEC|O_NOFOLLOW);if(pin<0)return -1;
 struct stat st;if(fstat(pin,&st) || !S_ISREG(st.st_mode)){close(pin);return unsupported();}
 char fdpath[64];snprintf(fdpath,sizeof(fdpath),"/proc/self/fd/%d",pin);
 int fd=open(fdpath,O_RDONLY|O_CLOEXEC);int saved=errno;close(pin);errno=saved;if(fd<0)return -1;
 FILE *file=fdopen(fd,"r");if(!file){close(fd);return -1;}char line[8192];int bad=0;
 while(fgets(line,sizeof(line),file)){
  if(*count==64 || !strchr(line,'\n')){bad=1;break;}
  struct node *n=&nodes[*count];char root[256];int used=0;
  if(sscanf(line,"%u %u %u:%u %255s %255s %255s%n",&n->id,&n->parent,&n->major,&n->minor,root,n->path,n->options,&used)!=7 || strchr(root,'\\') || strchr(n->path,'\\')){bad=1;break;}
  char *sep=strstr(line+used," - ");if(!sep || sscanf(sep+3,"%31s",n->fs)!=1){bad=1;break;}
  *sep=0;if(strstr(line+used,"shared:") || strstr(line+used,"master:") || strstr(line+used,"propagate_from:")){bad=1;break;}
  for(unsigned j=0;j<*count;j++)if(nodes[j].id==n->id || !strcmp(nodes[j].path,n->path))bad=1;
  if(bad)break;
  (*count)++;
 }
 if(ferror(file))bad=1;
 fclose(file);if(bad)return unsupported();return 0;
}
static struct node *find(struct node *nodes,unsigned count,const char *path){
 for(unsigned i=0;i<count;i++)if(!strcmp(nodes[i].path,path))return &nodes[i];
 return NULL;
}
static int fs_is(const struct node *n,const char *fs){return n && !strcmp(n->fs,fs);}
static int overlay(const struct node *n){return fs_is(n,"overlayfs") || fs_is(n,"overlay");}
static int readonly(const struct node *n){
 char options[260];if(!n)return 0;snprintf(options,sizeof(options),",%s,",n->options);return strstr(options,",ro,")!=NULL;
}
static void action(struct mer_mount_plan *plan,int kind,unsigned id,const char *src,const char *dst){
 struct mer_mount_action *a=&plan->actions[plan->count++];a->kind=kind;a->id=id;snprintf(a->source,sizeof(a->source),"%s",src);snprintf(a->destination,sizeof(a->destination),"%s",dst?dst:"");
}
int mer_original_mount_plan(const char *mountinfo,struct mer_mount_plan *plan,FILE *report){
 memset(plan,0,sizeof(*plan));struct node nodes[64];unsigned count;if(load_nodes(mountinfo,nodes,&count))return -1;
 struct node *root=find(nodes,count,"/"),*old=NULL;const char *lower_path;
 if(overlay(root)){plan->phase=MER_GRAPH_PREPIVOT;old=root;lower_path="/rom";}
 else if(fs_is(root,"tmpfs") && readonly(root) && overlay(find(nodes,count,"/mnt"))){plan->phase=MER_GRAPH_POSTPIVOT;old=find(nodes,count,"/mnt");lower_path="/mnt/rom";}
 else return unsupported();
 struct node *lower=find(nodes,count,lower_path),*tmp=find(nodes,count,"/tmp"),*upper=find(nodes,count,"/tmp/root"),*jffs=find(nodes,count,"/tmp/userconfig");
 struct node *proc=find(nodes,count,"/proc"),*sys=find(nodes,count,"/sys"),*dev=find(nodes,count,"/dev"),*pts=find(nodes,count,"/dev/pts"),*debug=find(nodes,count,"/sys/kernel/debug");
 if(!fs_is(lower,"squashfs") || lower->major!=31 || lower->minor!=2 || !readonly(lower) || lower->parent!=old->id || !fs_is(tmp,"tmpfs") || tmp->major!=0 || !fs_is(upper,"tmpfs") || upper->major!=0 || upper->minor==tmp->minor || upper->parent!=tmp->id || !fs_is(jffs,"jffs2") || jffs->major!=31 || jffs->minor!=7 || jffs->parent!=tmp->id || !fs_is(proc,"proc") || !fs_is(sys,"sysfs") || !fs_is(dev,"tmpfs") || !fs_is(pts,"devpts") || !fs_is(debug,"debugfs"))return unsupported();
 unsigned top=root->id;
 if(tmp->parent!=top || proc->parent!=top || sys->parent!=top || dev->parent!=top || pts->parent!=dev->id || debug->parent!=sys->id || (plan->phase==MER_GRAPH_POSTPIVOT && old->parent!=top))return unsupported();
 struct node *bundle=find(nodes,count,MER_RAM_ROOT);
 if(plan->phase==MER_GRAPH_PREPIVOT && bundle && (!fs_is(bundle,"tmpfs") || !readonly(bundle) || bundle->parent!=tmp->id))return unsupported();
 if(plan->phase==MER_GRAPH_POSTPIVOT && (root->major!=0 || root->minor==tmp->minor || root->minor==upper->minor || bundle))return unsupported();
 unsigned expected=plan->phase==MER_GRAPH_PREPIVOT?(bundle?11u:10u):11u;
 if(count!=expected)return unsupported();
 action(plan,MER_MOVE_CHILD,lower->id,"/mnt/rom","/held/lower");
 action(plan,MER_MOVE_CHILD,upper->id,"/tmp/root","/held/upper");
 action(plan,MER_MOVE_CHILD,jffs->id,"/tmp/userconfig","/held/userconfig");
 action(plan,MER_ORDINARY_UMOUNT,old->id,"/mnt",NULL);
 action(plan,MER_ORDINARY_UMOUNT,lower->id,"/held/lower",NULL);
 action(plan,MER_ORDINARY_UMOUNT,upper->id,"/held/upper",NULL);
 plan->approved=1;
 if(report){
  fprintf(report,"GRAPH_RECOGNIZED phase=%s mounts=%u prepared_bundle=%s; EXECUTION_DISABLED\n",plan->phase==MER_GRAPH_PREPIVOT?"PREPIVOT":"POSTPIVOT",count,bundle?"present":"not present/already root");
  fputs("Prerequisite pivot sequence: move proc; pivot prepared RAM mount without redundant bind; move sys/dev/tmp including children.\n",report);
  for(unsigned i=0;i<plan->count;i++)fprintf(report,"%u %s mount_id=%u %s%s%s\n",i+1,plan->actions[i].kind==MER_MOVE_CHILD?"MOVE":"ORDINARY_UMOUNT",plan->actions[i].id,plan->actions[i].source,plan->actions[i].destination[0]?" -> ":"",plan->actions[i].destination);
  fputs("JFFS2 stays mounted at /held/userconfig; no JFFS2 unmount/remount/flush requested. Existing filesystem activity is not a zero-total-flash-I/O guarantee. /tmp/proc/sys/dev stay mounted.\n",report);
 }
 return 0;
}
int mer_check_mount_action(const char *mountinfo,const struct mer_mount_action *a){
 struct node nodes[64];unsigned count;if(load_nodes(mountinfo,nodes,&count))return -1;
 struct node *source=find(nodes,count,a->source);if(!source || source->id!=a->id)return unsupported();
 if(a->kind==MER_MOVE_CHILD){if(find(nodes,count,a->destination))return unsupported();}
 else if(a->kind==MER_ORDINARY_UMOUNT){for(unsigned i=0;i<count;i++)if(nodes[i].parent==source->id)return unsupported();}
 else return unsupported();
 return 0;
}
