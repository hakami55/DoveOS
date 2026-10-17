#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "doveos.h"
#include <gio/gio.h>
#include <glib-unix.h>
#include "qr/qrcodegen.h"
#include "web_assets.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#define MAX_FILE ((guint64)4294967295ULL)
#define DEFAULT_FOLDER "/media/usb0/DrMoSa/Downloads/Local Transfer"
typedef struct {
 GMutex lock;GSocketService *service;GHashTable *clients,*pending;int rootfd,port;gint running,active,copying;
 gchar *folder,*token,*last;GtkWidget *window,*qr_box,*url,*address,*folder_entry,*status,*start,*choose,*add;
 GMainLoop *loop;gboolean headless,closing;guint monitor;
} App;
static App app={.rootfd=-1,.port=8787};
static gboolean random_hex(char *out,int bytes) {
 unsigned char data[32];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0)return FALSE;
 size_t used=0;while(used<(size_t)bytes){ssize_t n=read(fd,data+used,bytes-used);if(n<=0){close(fd);return FALSE;}used+=n;}close(fd);
 for(int i=0;i<bytes;i++)sprintf(out+i*2,"%02x",data[i]);out[bytes*2]=0;return TRUE;
}
static void latest(const char *message) {g_mutex_lock(&app.lock);g_free(app.last);app.last=g_strdup(message);g_mutex_unlock(&app.lock);}
static gboolean send_all(int fd,const void *data,size_t length) {
 const char *p=data;while(length){ssize_t n=send(fd,p,length,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0)return FALSE;p+=n;length-=n;}return TRUE;
}
static gboolean headers(int fd,int code,const char *reason,const char *type,guint64 size,const char *extra) {
 char *h=g_strdup_printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %" G_GUINT64_FORMAT "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'\r\n%s\r\n",code,reason,type,size,extra?extra:"");
 gboolean ok=send_all(fd,h,strlen(h));g_free(h);return ok;
}
static void text_reply(int fd,int code,const char *reason,const char *message) {headers(fd,code,reason,"text/plain; charset=utf-8",strlen(message),NULL);send_all(fd,message,strlen(message));}
static gboolean path_valid(const char *path) {
 if(!path||strlen(path)>3000||!g_utf8_validate(path,-1,NULL)||path[0]=='/')return FALSE;
 if(!*path)return TRUE;char **parts=g_strsplit(path,"/",-1);gboolean valid=TRUE;
 for(int i=0;parts[i];i++)if(!*parts[i]||!strcmp(parts[i],".")||!strcmp(parts[i],"..")||g_str_has_prefix(parts[i],".dove-transfer-")||strchr(parts[i],'\\')||strchr(parts[i],'\n')||strchr(parts[i],'\r')){valid=FALSE;break;}
 g_strfreev(parts);return valid;
}
/* Walk descriptor-relative directories without following links out of the share. */
static int open_path(int root,const char *path,gboolean directory) {
 if(!path_valid(path)){errno=EINVAL;return -1;}if(!*path)return directory?dup(root):-1;
 char **parts=g_strsplit(path,"/",-1);int fd=dup(root);
 for(int i=0;parts[i]&&fd>=0;i++){int next=openat(fd,parts[i],O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|((parts[i+1]||directory)?O_DIRECTORY:0));close(fd);fd=next;}
 g_strfreev(parts);return fd;
}
static char *parameter(const char *query,const char *key) {
 if(!query)return g_strdup("");char **parts=g_strsplit(query,"&",-1);char *result=NULL;
 for(int i=0;parts[i];i++){char *eq=strchr(parts[i],'=');if(!eq)continue;*eq=0;if(!strcmp(parts[i],key)){result=g_uri_unescape_string(eq+1,NULL);break;}}
 g_strfreev(parts);return result?result:g_strdup("");
}
static void json_string(GString *json,const char *s) {
 g_string_append_c(json,'"');for(const unsigned char *p=(const unsigned char*)s;*p;p++) {
  if(*p=='"'||*p=='\\'){g_string_append_c(json,'\\');g_string_append_c(json,*p);}else if(*p<32)g_string_append_printf(json,"\\u%04x",*p);else g_string_append_c(json,*p);
 }g_string_append_c(json,'"');
}
static gboolean is_pending(int parent,const char *name);
typedef struct {char *name;gboolean directory;guint64 size;} Item;
static void item_free(gpointer data){Item *i=data;g_free(i->name);g_free(i);}
static gint item_sort(gconstpointer a,gconstpointer b){Item *x=*(Item*const*)a,*y=*(Item*const*)b;return x->directory!=y->directory?(x->directory?-1:1):g_utf8_collate(x->name,y->name);}
static void list_files(int fd,int root,const char *path) {
 int dirfd=open_path(root,path,TRUE);if(dirfd<0){text_reply(fd,404,"Not Found","Folder not available.");return;}
 DIR *dir=fdopendir(dirfd);if(!dir){close(dirfd);text_reply(fd,500,"Error","Could not read folder.");return;}
 GPtrArray *items=g_ptr_array_new_with_free_func(item_free);struct dirent *entry;gboolean truncated=FALSE;
 while((entry=readdir(dir))) {
  if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,"..")||g_str_has_prefix(entry->d_name,".dove-transfer-")||!g_utf8_validate(entry->d_name,-1,NULL))continue;
  if(is_pending(dirfd,entry->d_name))continue;struct stat st;if(fstatat(dirfd,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)||(!S_ISDIR(st.st_mode)&&!S_ISREG(st.st_mode)))continue;
  if(items->len>=2000){truncated=TRUE;break;}Item *i=g_new0(Item,1);i->name=g_strdup(entry->d_name);i->directory=S_ISDIR(st.st_mode);i->size=st.st_size;g_ptr_array_add(items,i);
 }closedir(dir);g_ptr_array_sort(items,item_sort);GString *json=g_string_new("{\"files\":[");
 for(guint n=0;n<items->len;n++){Item *i=g_ptr_array_index(items,n);if(n)g_string_append_c(json,',');g_string_append(json,"{\"name\":");json_string(json,i->name);g_string_append_printf(json,",\"directory\":%s,\"size\":%" G_GUINT64_FORMAT "}",i->directory?"true":"false",i->size);}
 g_string_append_printf(json,"],\"truncated\":%s}",truncated?"true":"false");headers(fd,200,"OK","application/json; charset=utf-8",json->len,NULL);send_all(fd,json->str,json->len);g_string_free(json,TRUE);g_ptr_array_unref(items);
}
static void download(int fd,int root,const char *path,const char *range,gboolean head) {
 char *parent_path=g_path_get_dirname(path),*filename=g_path_get_basename(path);int parent=open_path(root,!strcmp(parent_path,".")?"":parent_path,TRUE);gboolean pending=parent>=0&&is_pending(parent,filename);if(parent>=0)close(parent);g_free(parent_path);g_free(filename);if(pending){text_reply(fd,409,"Conflict","This file is still being saved. Try again in a moment.");return;}
 int file=open_path(root,path,FALSE);struct stat st;if(file<0||fstat(file,&st)||!S_ISREG(st.st_mode)){if(file>=0)close(file);text_reply(fd,404,"Not Found","File not available.");return;}
 guint64 first=0,last=st.st_size?((guint64)st.st_size-1):0,length=st.st_size;gboolean partial=FALSE;
 if(range&&*range){
  char *end=NULL;if(!g_str_has_prefix(range,"bytes=")||strchr(range,',')){goto invalid_range;}
  errno=0;first=g_ascii_strtoull(range+6,&end,10);if(errno||end==range+6||*end!='-'||first>=(guint64)st.st_size)goto invalid_range;
  if(end[1]){char *finish=NULL;errno=0;last=g_ascii_strtoull(end+1,&finish,10);if(errno||*finish||last<first)goto invalid_range;last=MIN(last,(guint64)st.st_size-1);}
  length=last-first+1;partial=TRUE;
 }
 char *base=g_path_get_basename(path),*encoded=g_uri_escape_string(base,NULL,FALSE);GString *extra=g_string_new("Accept-Ranges: bytes\r\n");g_string_append_printf(extra,"Content-Disposition: attachment; filename=\"download\"; filename*=UTF-8''%s\r\n",encoded);g_free(base);g_free(encoded);
 if(partial)g_string_append_printf(extra,"Content-Range: bytes %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "/%" G_GUINT64_FORMAT "\r\n",first,last,(guint64)st.st_size);
 gboolean ok=headers(fd,partial?206:200,partial?"Partial Content":"OK","application/octet-stream",length,extra->str);g_string_free(extra,TRUE);
 if(!head&&ok&&lseek(file,(off_t)first,SEEK_SET)>=0){char buffer[65536];while(length&&g_atomic_int_get(&app.running)){ssize_t n=read(file,buffer,MIN(length,sizeof buffer));if(n<0&&errno==EINTR)continue;if(n<=0||!send_all(fd,buffer,n))break;length-=n;}}
 close(file);return;
invalid_range:;
 char *extra_range=g_strdup_printf("Content-Range: bytes */%" G_GUINT64_FORMAT "\r\n",(guint64)st.st_size);headers(fd,416,"Range Not Satisfiable","text/plain",0,extra_range);g_free(extra_range);close(file);
}
static int temporary(int dir,char name[80]) {char random[25];if(!random_hex(random,12))return -1;g_snprintf(name,80,".dove-transfer-%s.part",random);return openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);}
static gboolean write_all(int fd,const char *p,size_t count) {while(count){ssize_t n=write(fd,p,count);if(n<0&&errno==EINTR)continue;if(n<=0)return FALSE;p+=n;count-=n;}return TRUE;}
static char *pending_key(int parent,const char *name){struct stat st;if(fstat(parent,&st))return NULL;return g_strdup_printf("%llu:%llu:%s",(unsigned long long)st.st_dev,(unsigned long long)st.st_ino,name);}
static gboolean is_pending(int parent,const char *name){char *key=pending_key(parent,name);g_mutex_lock(&app.lock);gboolean value=key&&g_hash_table_contains(app.pending,key);g_mutex_unlock(&app.lock);g_free(key);return value;}
/* FAT has no hard links. Its fallback reserves the destination with O_EXCL,
 * streams the completed temporary file, and hides that name from web clients
 * until it is flushed. Existing destination files are never replaced. */
static gboolean commit_file(int parent,const char *temp,const char *name,gboolean network) {
 if(!linkat(parent,temp,parent,name,0))return TRUE;
 if(errno!=EPERM&&errno!=EOPNOTSUPP&&errno!=ENOSYS)return FALSE;
 char *key=pending_key(parent,name);if(!key)return FALSE;g_mutex_lock(&app.lock);
 if(g_hash_table_contains(app.pending,key)){g_mutex_unlock(&app.lock);g_free(key);return FALSE;}
 g_hash_table_add(app.pending,g_strdup(key));g_mutex_unlock(&app.lock);
 int input=openat(parent,temp,O_RDONLY|O_NOFOLLOW|O_CLOEXEC),output=-1;gboolean ok=input>=0;
 if(ok){output=openat(parent,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0644);ok=output>=0;}
 char buffer[65536];while(ok){if(network&&!g_atomic_int_get(&app.running)){ok=FALSE;break;}ssize_t n=read(input,buffer,sizeof buffer);if(n<0&&errno==EINTR)continue;if(!n)break;if(n<0||!write_all(output,buffer,n))ok=FALSE;}
 if(input>=0)close(input);if(output>=0){if(ok&&fsync(output))ok=FALSE;if(!ok){struct stat actual,owned;if(!fstat(output,&owned)&&!fstatat(parent,name,&actual,AT_SYMLINK_NOFOLLOW)&&actual.st_dev==owned.st_dev&&actual.st_ino==owned.st_ino)unlinkat(parent,name,0);}close(output);}
 g_mutex_lock(&app.lock);g_hash_table_remove(app.pending,key);g_mutex_unlock(&app.lock);g_free(key);return ok;
}
static void upload(int fd,FILE *input,int root,const char *dir,const char *name,guint64 size,gboolean expect) {
 if(!path_valid(dir)||!path_valid(name)||!*name||strchr(name,'/')||strlen(name)>255){text_reply(fd,400,"Bad Request","Choose a valid filename.");return;}
 if(size>MAX_FILE){text_reply(fd,413,"Too Large","This file is too large for the dock's USB filesystem (maximum 4 GB minus one byte).");return;}
 int parent=open_path(root,dir,TRUE);if(parent<0){text_reply(fd,404,"Not Found","Folder not available.");return;}
 struct stat st;if(!fstatat(parent,name,&st,AT_SYMLINK_NOFOLLOW)){close(parent);text_reply(fd,409,"Conflict","A file with this name already exists. Rename your file and try again.");return;}
 struct statvfs space;if(!fstatvfs(parent,&space)&&size>((guint64)space.f_bavail*space.f_frsize)){close(parent);text_reply(fd,507,"Insufficient Storage","Not enough free space on the dock.");return;}
 char temp[80];int file=temporary(parent,temp);if(file<0){close(parent);text_reply(fd,500,"Error","Could not create the file.");return;}
 if(expect&&!send_all(fd,"HTTP/1.1 100 Continue\r\n\r\n",25)){close(file);unlinkat(parent,temp,0);close(parent);return;}
 char buffer[65536];guint64 remaining=size;gboolean good=TRUE;
 while(remaining&&g_atomic_int_get(&app.running)){size_t n=fread(buffer,1,MIN(remaining,sizeof buffer),input);if(!n||!write_all(file,buffer,n)){good=FALSE;break;}remaining-=n;}
 if(remaining)good=FALSE;if(good&&fsync(file))good=FALSE;close(file);
 if(good&&!commit_file(parent,temp,name,TRUE))good=FALSE;unlinkat(parent,temp,0);if(good)fsync(parent);close(parent);
 if(good){char *message=g_strdup_printf("Received: %s",name);latest(message);g_free(message);text_reply(fd,201,"Created","File saved on the dock.");}
 else text_reply(fd,500,"Error","Transfer interrupted or the file could not be saved. Existing files were kept.");
}
static gboolean serve(GThreadedSocketService *service,GSocketConnection *connection,GObject *source,gpointer unused) {
 (void)service;(void)source;(void)unused;GSocket *socket=g_socket_connection_get_socket(connection);int fd=g_socket_get_fd(socket);g_socket_set_blocking(socket,TRUE);int flags=fcntl(fd,F_GETFL,0);if(flags>=0)fcntl(fd,F_SETFL,flags&~O_NONBLOCK);
 struct timeval timeout={30,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 int root=-1;char *token=NULL;g_mutex_lock(&app.lock);
 if(g_atomic_int_get(&app.running)){root=dup(app.rootfd);if(root>=0){token=g_strdup(app.token);g_hash_table_add(app.clients,g_object_ref(socket));g_atomic_int_inc(&app.active);}}g_mutex_unlock(&app.lock);
 if(root<0)return FALSE;FILE *input=fdopen(dup(fd),"rb");if(!input)goto done;
 char line[8192],method[12],target[4096],version[20];size_t total=0;char *range=NULL,*origin=NULL,*host=NULL,*length_text=NULL;gboolean expect=FALSE,bad=FALSE,transfer_encoding=FALSE;
 if(!fgets(line,sizeof line,input)||sscanf(line,"%11s %4095s %19s",method,target,version)!=3||(!strcmp(version,"HTTP/1.1")?FALSE:strcmp(version,"HTTP/1.0")!=0)){text_reply(fd,400,"Bad Request","Invalid request.");goto finish;}
 total=strlen(line);for(;;){if(!fgets(line,sizeof line,input)){bad=TRUE;break;}total+=strlen(line);if(total>16384||!strchr(line,'\n')){bad=TRUE;break;}if(!strcmp(line,"\r\n")||!strcmp(line,"\n"))break;char *colon=strchr(line,':');if(!colon){bad=TRUE;break;}*colon=0;char *value=g_strstrip(colon+1);
  if(!g_ascii_strcasecmp(line,"Content-Length")){if(length_text){bad=TRUE;break;}length_text=g_strdup(value);}else if(!g_ascii_strcasecmp(line,"Transfer-Encoding"))transfer_encoding=TRUE;
  else if(!g_ascii_strcasecmp(line,"Range")){g_free(range);range=g_strdup(value);}else if(!g_ascii_strcasecmp(line,"Host")){g_free(host);host=g_strdup(value);}else if(!g_ascii_strcasecmp(line,"Origin")){g_free(origin);origin=g_strdup(value);}else if(!g_ascii_strcasecmp(line,"Expect"))expect=!g_ascii_strcasecmp(value,"100-continue");
 }
 if(bad||transfer_encoding){text_reply(fd,400,"Bad Request","Invalid request headers.");goto finish;}
 char *prefix=g_strdup_printf("/%s/",token);gboolean allowed=g_str_has_prefix(target,prefix);size_t prefix_len=strlen(prefix);g_free(prefix);if(!allowed){text_reply(fd,403,"Forbidden","Open the link or QR code shown on the dock.");goto finish;}
 char *route=target+prefix_len,*query=strchr(route,'?');if(query)*query++=0;
 gboolean get=!strcmp(method,"GET"),head=!strcmp(method,"HEAD"),put=!strcmp(method,"PUT");
 if(!get&&!head&&!put){text_reply(fd,405,"Method Not Allowed","Use the Local Transfer page.");goto finish;}
 if(put){char *expected=host?g_strdup_printf("http://%s",host):NULL;gboolean valid=!origin||(expected&&!strcmp(origin,expected));g_free(expected);if(!valid){text_reply(fd,403,"Forbidden","Upload must come from this Local Transfer page.");goto finish;}}
 if((get||head)&&(!*route||!strcmp(route,"style.css")||!strcmp(route,"app.js"))){const char *content=!*route?web_html:!strcmp(route,"style.css")?web_css:web_js;const char *type=!*route?"text/html; charset=utf-8":!strcmp(route,"style.css")?"text/css; charset=utf-8":"application/javascript; charset=utf-8";headers(fd,200,"OK",type,strlen(content),NULL);if(!head)send_all(fd,content,strlen(content));}
 else if(get&&!strcmp(route,"api/list")){char *path=parameter(query,"path");list_files(fd,root,path);g_free(path);}
 else if((get||head)&&!strcmp(route,"api/download")){char *path=parameter(query,"path");download(fd,root,path,range,head);g_free(path);}
 else if(put&&!strcmp(route,"api/upload")){char *end=NULL;errno=0;guint64 length=length_text?g_ascii_strtoull(length_text,&end,10):0;if(!length_text||!*length_text||length_text[0]=='-'||errno||!end||*end){text_reply(fd,411,"Length Required","File length is required.");goto finish;}char *dir=parameter(query,"dir"),*name=parameter(query,"name");upload(fd,input,root,dir,name,length,expect);g_free(dir);g_free(name);}
 else text_reply(fd,404,"Not Found","Not found.");
finish:;
 g_free(range);g_free(origin);g_free(host);g_free(length_text);fclose(input);
done:
 close(root);g_free(token);g_mutex_lock(&app.lock);g_hash_table_remove(app.clients,socket);g_mutex_unlock(&app.lock);g_socket_close(socket,NULL);g_atomic_int_add(&app.active,-1);return FALSE;
}
static gboolean folder_set(const char *path,GError **error) {
 char *resolved=realpath(path,NULL);int fd=resolved?open(resolved,O_RDONLY|O_DIRECTORY|O_CLOEXEC):-1;
 if(fd<0){g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot open folder: %s",g_strerror(errno));free(resolved);return FALSE;}
 g_mutex_lock(&app.lock);if(app.rootfd>=0)close(app.rootfd);app.rootfd=fd;g_free(app.folder);app.folder=g_strdup(resolved);g_mutex_unlock(&app.lock);free(resolved);return TRUE;
}
static gboolean start_server(GError **error) {
 if(g_atomic_int_get(&app.active)){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"The previous transfer is stopping. Try again in a moment.");return FALSE;}
 char token[33];if(!random_hex(token,16)){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Could not create a transfer link.");return FALSE;}
 GSocketService *service=G_SOCKET_SERVICE(g_threaded_socket_service_new(3));g_socket_listener_set_backlog(G_SOCKET_LISTENER(service),8);
 if(!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(service),app.port,NULL,error)){g_object_unref(service);return FALSE;}
 g_mutex_lock(&app.lock);g_free(app.token);app.token=g_strdup(token);app.service=service;g_atomic_int_set(&app.running,1);g_mutex_unlock(&app.lock);
 g_signal_connect(service,"run",G_CALLBACK(serve),NULL);g_socket_service_start(service);latest("Ready to transfer files.");return TRUE;
}
static void stop_server(void) {
 GPtrArray *clients=g_ptr_array_new_with_free_func(g_object_unref);g_mutex_lock(&app.lock);g_atomic_int_set(&app.running,0);GSocketService *service=app.service;app.service=NULL;
 GHashTableIter iter;gpointer key;g_hash_table_iter_init(&iter,app.clients);while(g_hash_table_iter_next(&iter,&key,NULL))g_ptr_array_add(clients,g_object_ref(key));g_mutex_unlock(&app.lock);
 if(service){g_socket_service_stop(service);g_socket_listener_close(G_SOCKET_LISTENER(service));g_object_unref(service);}
 for(guint i=0;i<clients->len;i++)g_socket_shutdown(g_ptr_array_index(clients,i),TRUE,TRUE,NULL);g_ptr_array_unref(clients);latest("Transfer stopped. The old link is no longer active.");
}
static char *network_addresses(void) {
 struct ifaddrs *interfaces=NULL;GString *list=g_string_new("");if(getifaddrs(&interfaces)==0){
  for(int pass=0;pass<2;pass++)for(struct ifaddrs *it=interfaces;it;it=it->ifa_next){if(!it->ifa_addr||it->ifa_addr->sa_family!=AF_INET||!(it->ifa_flags&IFF_UP)||(it->ifa_flags&IFF_LOOPBACK))continue;gboolean wifi=g_str_has_prefix(it->ifa_name,"wl");if(wifi!=(pass==0))continue;char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&((struct sockaddr_in*)it->ifa_addr)->sin_addr,ip,sizeof ip);g_string_append_printf(list,"%s|%s\n",it->ifa_name,ip);}
  freeifaddrs(interfaces);
 }return g_string_free(list,FALSE);
}
static void show_qr(const char *url) {
 uint8_t temp[qrcodegen_BUFFER_LEN_MAX],qr[qrcodegen_BUFFER_LEN_MAX];if(!qrcodegen_encodeText(url,temp,qr,qrcodegen_Ecc_MEDIUM,1,12,qrcodegen_Mask_AUTO,TRUE))return;
 int modules=qrcodegen_getSize(qr),scale=MAX(3,220/(modules+8)),size=(modules+8)*scale;GdkPixbuf *pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,size,size);gdk_pixbuf_fill(pixels,0xffffffff);
 guchar *data=gdk_pixbuf_get_pixels(pixels);int stride=gdk_pixbuf_get_rowstride(pixels);
 for(int y=0;y<modules;y++)for(int x=0;x<modules;x++)if(qrcodegen_getModule(qr,x,y))for(int yy=0;yy<scale;yy++)for(int xx=0;xx<scale;xx++){unsigned char *p=data+(y+4)*scale*stride+yy*stride+((x+4)*scale+xx)*3;p[0]=p[1]=p[2]=0;}
 GtkWidget *old=gtk_bin_get_child(GTK_BIN(app.qr_box));if(old)gtk_widget_destroy(old);GtkWidget *image=dove_toolbar_icon(pixels);gtk_container_add(GTK_CONTAINER(app.qr_box),image);gtk_widget_show(image);g_object_unref(pixels);
}
static char *selected_ip(void){char *text=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app.address));char *space=text?strrchr(text,' '):NULL;char *ip=space?g_strdup(space+1):NULL;g_free(text);return ip;}
static void update_link(GtkWidget *widget,gpointer unused) {
 (void)widget;(void)unused;char *ip=selected_ip();
 char *url=(ip&&g_atomic_int_get(&app.running))?g_strdup_printf("http://%s:%d/%s/",ip,app.port,app.token):g_strdup("");
 if(strcmp(gtk_entry_get_text(GTK_ENTRY(app.url)),url)){gtk_entry_set_text(GTK_ENTRY(app.url),url);if(*url)show_qr(url);else{GtkWidget *old=gtk_bin_get_child(GTK_BIN(app.qr_box));if(old)gtk_widget_destroy(old);GtkWidget *label=dove_label(g_atomic_int_get(&app.running)?"Connect Wi-Fi or Ethernet\nto show the QR code.":"Transfer is stopped.");gtk_container_add(GTK_CONTAINER(app.qr_box),label);gtk_widget_show(label);}}
 g_free(ip);g_free(url);
}
static gboolean refresh(gpointer unused) {
 (void)unused;static char *previous=NULL;char *addresses=network_addresses();
 if(g_strcmp0(addresses,previous)){char *selected=selected_ip();gtk_list_store_clear(GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(app.address))));int active=0,index=0;char **rows=g_strsplit(addresses,"\n",-1);
  for(int i=0;rows[i]&&*rows[i];i++){char *sep=strchr(rows[i],'|');if(!sep)continue;*sep=0;char *label=g_strdup_printf("%s - %s",g_str_has_prefix(rows[i],"wl")?"Wi-Fi":"Ethernet",sep+1);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app.address),label);if(!g_strcmp0(selected,sep+1))active=index;index++;g_free(label);}
  gtk_combo_box_set_active(GTK_COMBO_BOX(app.address),active);
  g_free(selected);g_strfreev(rows);g_free(previous);previous=g_strdup(addresses);
 }g_free(addresses);update_link(NULL,NULL);
 gboolean running=g_atomic_int_get(&app.running);gtk_button_set_label(GTK_BUTTON(app.start),running?"Stop transfer":"Start transfer");gtk_widget_set_sensitive(app.choose,!running&&!g_atomic_int_get(&app.active)&&!g_atomic_int_get(&app.copying));gtk_widget_set_sensitive(app.add,!g_atomic_int_get(&app.copying));
 g_mutex_lock(&app.lock);char *state=g_strdup_printf("%s%s",app.last?app.last:"",g_atomic_int_get(&app.active)?"  |  Transferring...":"");g_mutex_unlock(&app.lock);gtk_label_set_text(GTK_LABEL(app.status),state);g_free(state);return TRUE;
}
static void toggle(GtkWidget *widget,gpointer unused){(void)widget;(void)unused;if(g_atomic_int_get(&app.running))stop_server();else{GError *error=NULL;if(!start_server(&error)){dove_message(GTK_WINDOW(app.window),error->message);g_error_free(error);}}refresh(NULL);}
static void copy_link(GtkWidget *widget,gpointer unused){(void)widget;(void)unused;gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),gtk_entry_get_text(GTK_ENTRY(app.url)),-1);}
static void choose(GtkWidget *widget,gpointer unused) {
 (void)widget;(void)unused;GtkWidget *dialog=dove_folderdialog(GTK_WINDOW(app.window),"Choose shared folder");gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog),app.folder);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){char *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GError *error=NULL;if(folder_set(path,&error)){gtk_entry_set_text(GTK_ENTRY(app.folder_entry),app.folder);}else{dove_message(GTK_WINDOW(app.window),error->message);g_error_free(error);}g_free(path);}gtk_widget_destroy(dialog);
}
static void open_folder(GtkWidget *widget,gpointer unused){(void)widget;(void)unused;char *args[]={"/bin/sh","/media/usb0/DoveOS-v2/Explorer/launch.sh",app.folder,NULL};GError *error=NULL;if(!g_spawn_async(NULL,args,NULL,0,NULL,NULL,NULL,&error)){dove_message(GTK_WINDOW(app.window),error->message);g_error_free(error);}}
typedef struct {GSList *paths;int folder;} Copies;
static void copies_free(gpointer data){Copies *c=data;g_slist_free_full(c->paths,g_free);close(c->folder);g_free(c);}
static void copy_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;(void)cancel;Copies *copies=data;guint good=0,bad=0;
 for(GSList *it=copies->paths;it;it=it->next){char *name=g_path_get_basename(it->data);int input=open(it->data,O_RDONLY|O_CLOEXEC);struct stat st;char temp[80]={0};int output=-1;gboolean ok=input>=0&&!fstat(input,&st)&&S_ISREG(st.st_mode)&&path_valid(name);
  if(ok){output=temporary(copies->folder,temp);ok=output>=0;}char buffer[65536];while(ok){ssize_t n=read(input,buffer,sizeof buffer);if(n<0&&errno==EINTR)continue;if(n==0)break;if(n<0||!write_all(output,buffer,n))ok=FALSE;}
  if(input>=0)close(input);if(output>=0){if(ok&&fsync(output))ok=FALSE;close(output);}if(ok&&!commit_file(copies->folder,temp,name,FALSE))ok=FALSE;if(*temp)unlinkat(copies->folder,temp,0);if(ok)good++;else bad++;g_free(name);
 }g_task_return_pointer(task,g_strdup_printf("%u file(s) added to the shared folder.%s",good,bad?" Some files could not be added or already exist.":""),g_free);
}
static void copy_done(GObject *source,GAsyncResult *result,gpointer unused){(void)source;(void)unused;char *message=g_task_propagate_pointer(G_TASK(result),NULL);latest(message);g_free(message);g_atomic_int_set(&app.copying,0);if(!app.closing)refresh(NULL);}
static void add_files(GtkWidget *widget,gpointer unused) {
 (void)widget;(void)unused;GtkWidget *dialog=dove_openfiledialog(GTK_WINDOW(app.window),"Add files for another device to download");gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog),TRUE);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT){Copies *copies=g_new0(Copies,1);copies->paths=gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));copies->folder=dup(app.rootfd);g_atomic_int_set(&app.copying,1);latest("Adding files to the shared folder...");GTask *task=g_task_new(NULL,NULL,copy_done,NULL);g_task_set_task_data(task,copies,copies_free);g_task_run_in_thread(task,copy_worker);g_object_unref(task);}gtk_widget_destroy(dialog);refresh(NULL);
}
static gboolean close_ready(gpointer unused){(void)unused;if(g_atomic_int_get(&app.active)||g_atomic_int_get(&app.copying))return TRUE;if(app.headless)g_main_loop_quit(app.loop);else gtk_main_quit();return FALSE;}
static gboolean close_app(GtkWidget *widget,GdkEvent *event,gpointer unused){(void)widget;(void)event;(void)unused;if(!app.closing){app.closing=TRUE;stop_server();gtk_widget_hide(app.window);g_timeout_add(100,close_ready,NULL);}return TRUE;}
static gboolean signal_stop(gpointer unused){(void)unused;stop_server();g_timeout_add(100,close_ready,NULL);return G_SOURCE_REMOVE;}
static void ui(void) {
 app.window=dove_form("Local Transfer",650,390);gtk_window_set_wmclass(GTK_WINDOW(app.window),"doveos-transfer","DoveTransfer");gtk_window_set_position(GTK_WINDOW(app.window),GTK_WIN_POS_CENTER);gtk_window_set_resizable(GTK_WINDOW(app.window),FALSE);
 GtkWidget *outer=dove_panel(FALSE,9);gtk_container_set_border_width(GTK_CONTAINER(outer),12);gtk_container_add(GTK_CONTAINER(app.window),outer);dove_add(outer,dove_label("Send and receive files with a browser on your PC, iPhone or Android."));
 GtkWidget *row=dove_panel(TRUE,14);gtk_box_pack_start(GTK_BOX(outer),row,TRUE,TRUE,0);app.qr_box=gtk_frame_new(NULL);gtk_frame_set_shadow_type(GTK_FRAME(app.qr_box),GTK_SHADOW_IN);gtk_widget_set_size_request(app.qr_box,228,228);gtk_box_pack_start(GTK_BOX(row),app.qr_box,FALSE,FALSE,0);
 GtkWidget *right=dove_panel(FALSE,7);gtk_box_pack_start(GTK_BOX(row),right,TRUE,TRUE,0);dove_add(right,dove_label("1. Connect both devices to the same Wi-Fi or router.\n2. Scan the QR code or open the link in a browser.\n3. Send files, or choose Download."));
 app.address=gtk_combo_box_text_new();dove_add(right,app.address);g_signal_connect(app.address,"changed",G_CALLBACK(update_link),NULL);
 app.url=dove_textbox("");gtk_editable_set_editable(GTK_EDITABLE(app.url),FALSE);dove_add(right,app.url);GtkWidget *buttons=dove_panel(TRUE,6);dove_add(right,buttons);GtkWidget *copy=dove_button("Copy link");dove_add(buttons,copy);g_signal_connect(copy,"clicked",G_CALLBACK(copy_link),NULL);app.start=dove_button("Stop transfer");dove_add(buttons,app.start);g_signal_connect(app.start,"clicked",G_CALLBACK(toggle),NULL);
 dove_add(right,dove_label("Only the shared folder below is available.\nClosing this app stops the transfer link."));
 GtkWidget *folder=dove_panel(TRUE,6);gtk_box_pack_start(GTK_BOX(outer),folder,FALSE,FALSE,0);dove_add(folder,dove_label("Shared folder:"));app.folder_entry=dove_textbox(app.folder);gtk_editable_set_editable(GTK_EDITABLE(app.folder_entry),FALSE);gtk_box_pack_start(GTK_BOX(folder),app.folder_entry,TRUE,TRUE,0);app.choose=dove_button("Choose...");dove_add(folder,app.choose);g_signal_connect(app.choose,"clicked",G_CALLBACK(choose),NULL);
 GtkWidget *actions=dove_panel(TRUE,6);dove_add(outer,actions);app.add=dove_button("Add files...");dove_add(actions,app.add);g_signal_connect(app.add,"clicked",G_CALLBACK(add_files),NULL);GtkWidget *open=dove_button("Open folder");dove_add(actions,open);g_signal_connect(open,"clicked",G_CALLBACK(open_folder),NULL);app.status=dove_label("Starting...");gtk_box_pack_start(GTK_BOX(actions),app.status,TRUE,TRUE,0);
 g_signal_connect(app.window,"delete-event",G_CALLBACK(close_app),NULL);gtk_widget_show_all(app.window);refresh(NULL);app.monitor=g_timeout_add_seconds(2,refresh,NULL);
}
int main(int argc,char **argv) {
 signal(SIGPIPE,SIG_IGN);g_mutex_init(&app.lock);app.clients=g_hash_table_new_full(g_direct_hash,g_direct_equal,g_object_unref,NULL);app.pending=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 app.headless=argc>=2&&!strcmp(argv[1],"--serve");const char *folder=app.headless&&argc>=3?argv[2]:DEFAULT_FOLDER;if(app.headless&&argc>=4)app.port=atoi(argv[3]);
 int lock=open(app.headless?"/tmp/doveos-transfer-cli.lock":"/tmp/doveos-local-transfer.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 0;
 if(!app.headless){gtk_init(&argc,&argv);g_mkdir_with_parents(folder,0755);}GError *error=NULL;if(!folder_set(folder,&error)){fprintf(stderr,"%s\n",error->message);g_error_free(error);return 1;}
 gboolean started=start_server(&error);if(app.headless){if(!started){fprintf(stderr,"%s\n",error->message);g_error_free(error);return 1;}printf("http://127.0.0.1:%d/%s/\n",app.port,app.token);fflush(stdout);app.loop=g_main_loop_new(NULL,FALSE);g_unix_signal_add(SIGTERM,signal_stop,NULL);g_unix_signal_add(SIGINT,signal_stop,NULL);g_main_loop_run(app.loop);g_main_loop_unref(app.loop);}else{ui();if(!started){dove_message(GTK_WINDOW(app.window),error->message);g_error_free(error);}gtk_main();g_source_remove(app.monitor);gtk_widget_destroy(app.window);}
 if(app.rootfd>=0)close(app.rootfd);g_hash_table_unref(app.clients);g_hash_table_unref(app.pending);g_free(app.folder);g_free(app.token);g_free(app.last);close(lock);return 0;
}
