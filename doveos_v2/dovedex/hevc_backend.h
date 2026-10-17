/* Experimental Wave410 HEVC Main8 backend for the RTD119x dock.
 * Vendor ABI validated by hevc_lab/hevc_sequence.c. No flash writes.
 * VOUT retains a linear buffer until its release flag becomes 3.
 * Use a 64-slot display fence ring independently of decoder buffer indexes:
 * Wave4 rapidly reuses the lowest free buffer; reusing its display fence too
 * soon can lose an acknowledgement and permanently reduce pipeline depth. */
typedef struct { uint32_t size,phys,base,virt; } HevcBuffer;
_Static_assert(sizeof(HevcBuffer)==16,"Wave4 buffer ABI");
static struct {
 void *lib,*handle;
 HevcBuffer bitstream;
 int initialized,allocated,configured,count,stride,slice,extra,limit,rate;
 int held[64];unsigned fence_cursor;
 unsigned submitted,failed;
 double first,last,wait_seconds,decode_seconds,write_seconds,render_seconds,recycle_seconds,max_gap,queue_us,next_present;
 unsigned gaps50,input_gaps,last_number;
 unsigned steady_frames,steady_gaps50;
 double steady_first,steady_last,steady_max_gap;
 unsigned (*vdi_get_realchip_memory_size)(unsigned);
 int (*VPU_Init)(unsigned);
 int (*VPU_InitWithBitcode)(unsigned,const uint16_t*,unsigned);
 int (*VPU_DeInit)(unsigned);
 int (*VPU_GetOpenInstanceNum)(unsigned);
 int (*vdi_allocate_dma_memory)(unsigned,HevcBuffer*);
 void (*vdi_free_dma_memory)(unsigned,HevcBuffer*);
 int (*vdi_write_memory)(unsigned,unsigned,unsigned char*,int,int);
 int (*VPU_DecOpen)(void**,void*);
 int (*VPU_DecClose)(void*);
 int (*VPU_DecSetRdPtr)(void*,unsigned,int);
 int (*VPU_DecUpdateBitstreamBuffer)(void*,int);
 int (*VPU_DecGetInitialInfo)(void*,void*);
 int (*VPU_DecRegisterFrameBuffer)(void*,void*,int,int,int,int);
 int (*VPU_DecStartOneFrame)(void*,void*);
 int (*VPU_WaitInterrupt)(unsigned,int);
 void (*VPU_ClearInterrupt)(unsigned);
 int (*VPU_DecGetOutputInfo)(void*,void*);
 int (*VPU_DecGetFrameBuffer)(void*,int,void*);
 int (*VPU_DecClrDispFlag)(void*,int);
} hevc;
typedef struct {int index;uint32_t fb[17];uint64_t received;} HevcFrame;
static HevcFrame hevc_queue[16];
static unsigned hevc_read,hevc_write;
static unsigned hevc_outstanding;
static unsigned hevc_owned;

static void hevc_close(void){
 /* Stop the display before freeing decoder-owned surfaces. */
 output_close();
 if(hevc.handle){printf("HEVC_CLOSE=%d\n",hevc.VPU_DecClose(hevc.handle));hevc.handle=NULL;}
 if(hevc.allocated){hevc.vdi_free_dma_memory(1,&hevc.bitstream);hevc.allocated=0;}
 if(hevc.initialized){printf("HEVC_DEINIT=%d\n",hevc.VPU_DeInit(1));hevc.initialized=0;}
 printf("HEVC_SUMMARY submitted=%u displayed=%u errors=%u span=%.3f fps=%.2f\n",
 hevc.submitted,displayed,hevc.failed,hevc.last-hevc.first,
 displayed>1&&hevc.last>hevc.first?(displayed-1)/(hevc.last-hevc.first):0.0);
 if(hevc.submitted)printf("HEVC_TIMING decode_ms=%.3f display_wait_ms=%.3f\n",hevc.decode_seconds*1000/hevc.submitted,hevc.wait_seconds*1000/hevc.submitted);
 if(hevc.submitted)printf("HEVC_PROFILE write_ms=%.3f render_ms=%.3f recycle_ms=%.3f input_queue_ms=%.3f frame_gaps=%u max_gap_ms=%.3f gaps50=%u\n",hevc.write_seconds*1000/hevc.submitted,hevc.render_seconds*1000/hevc.submitted,hevc.recycle_seconds*1000/hevc.submitted,hevc.queue_us/hevc.submitted/1000,hevc.input_gaps,hevc.max_gap*1000,hevc.gaps50);
 if(hevc.steady_frames>1)printf("HEVC_STEADY frames=%u span=%.3f fps=%.2f max_gap_ms=%.3f gaps50=%u\n",hevc.steady_frames,hevc.steady_last-hevc.steady_first,(hevc.steady_frames-1)/(hevc.steady_last-hevc.steady_first),hevc.steady_max_gap*1000,hevc.steady_gaps50);
}
static int hevc_open(void){
 hevc.extra=5;hevc.limit=6;hevc.rate=60;
 const char *extra=getenv("DRMOSA_HEVC_EXTRA"),*limit=getenv("DRMOSA_HEVC_LIMIT");
 if(extra&&atoi(extra)>=3&&atoi(extra)<=5)hevc.extra=atoi(extra);
 if(limit&&atoi(limit)>=3&&atoi(limit)<=6)hevc.limit=atoi(limit);
 printf("HEVC_PIPELINE extra=%d limit=%d\n",hevc.extra,hevc.limit);
 if(!dlopen("/usr/lib/libRTKOMX_OSAL.so",RTLD_LAZY|RTLD_GLOBAL|RTLD_DEEPBIND))return -1;
 hevc.lib=dlopen("/usr/lib/libvpu.so",RTLD_NOW|RTLD_GLOBAL|RTLD_DEEPBIND);
 if(!hevc.lib){printf("HEVC_LIBRARY_ERROR=%s\n",dlerror());return -1;}
#define HEVC_SYM(name) do{hevc.name=dlsym(hevc.lib,#name);if(!hevc.name){puts("HEVC_MISSING=" #name);return -1;}}while(0)
 HEVC_SYM(vdi_get_realchip_memory_size);HEVC_SYM(VPU_Init);HEVC_SYM(VPU_InitWithBitcode);
 HEVC_SYM(VPU_DeInit);HEVC_SYM(VPU_GetOpenInstanceNum);HEVC_SYM(vdi_allocate_dma_memory);HEVC_SYM(vdi_free_dma_memory);
 HEVC_SYM(vdi_write_memory);HEVC_SYM(VPU_DecOpen);HEVC_SYM(VPU_DecClose);HEVC_SYM(VPU_DecSetRdPtr);
 HEVC_SYM(VPU_DecUpdateBitstreamBuffer);HEVC_SYM(VPU_DecGetInitialInfo);HEVC_SYM(VPU_DecRegisterFrameBuffer);
 HEVC_SYM(VPU_DecStartOneFrame);HEVC_SYM(VPU_WaitInterrupt);HEVC_SYM(VPU_ClearInterrupt);
 HEVC_SYM(VPU_DecGetOutputInfo);HEVC_SYM(VPU_DecGetFrameBuffer);HEVC_SYM(VPU_DecClrDispFlag);
#undef HEVC_SYM
 if(hevc.vdi_get_realchip_memory_size(1)<=0x10000000u){puts("HEVC_MEMORY_UNSUPPORTED=1");return -1;}
 int rc=hevc.VPU_Init(1);
 if(rc==18){
  FILE *f=fopen("/system/vendor/modules/wave410.out","rb");if(!f)return -1;
  fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
  if(n<=0||n>524288||(n&1)){fclose(f);return -1;}
  uint16_t *code=calloc(1,((size_t)n+15)&~15u);if(!code){fclose(f);return -1;}
  if(fread(code,1,(size_t)n,f)!=(size_t)n){fclose(f);free(code);return -1;}fclose(f);
  rc=hevc.VPU_InitWithBitcode(1,code,(unsigned)n/2);free(code);
 }
 printf("HEVC_WAVE4_INIT=%d\n",rc);if(rc!=0&&rc!=12)return -1;
 hevc.initialized=1;
 if(hevc.VPU_GetOpenInstanceNum(1)!=0){puts("HEVC_CORE_BUSY=1");return -1;}
 hevc.bitstream.size=4*1024*1024;
 if(hevc.vdi_allocate_dma_memory(1,&hevc.bitstream))return -1;
 hevc.allocated=1;
 uint32_t p[22]={0};p[0]=12;p[1]=hevc.bitstream.phys;p[2]=hevc.bitstream.size;
 p[8]=1;p[9]=1;p[10]=1;p[14]=31;p[15]=31;p[16]=2;p[17]=1;
 rc=hevc.VPU_DecOpen(&hevc.handle,p);printf("HEVC_OPEN=%d\n",rc);
 if(rc){hevc.handle=NULL;return -1;}
 if(output_open())return -1;
 puts("HEVC_WAVE4_HARDWARE_READY=1");return 0;
}
static int hevc_recycle(void){
 for(int i=0;i<64;i++)if(hevc.held[i]&&vo_status(i)==3){
  double start=seconds();
  /* Clear ownership before making this surface reusable by the decoder. */
  __atomic_fetch_and(&hevc_owned,~(1u<<(hevc.held[i]-1)),__ATOMIC_RELEASE);
  if(hevc.VPU_DecClrDispFlag(hevc.handle,hevc.held[i]-1))return -1;
  hevc.recycle_seconds+=seconds()-start;
  hevc.held[i]=0;
  __atomic_sub_fetch(&hevc_outstanding,1,__ATOMIC_RELEASE);
 }
 return 0;
}
static void hevc_pump(void){
 if(hevc_recycle()){hw_failed=1;ended=1;return;}
 unsigned r=__atomic_load_n(&hevc_read,__ATOMIC_RELAXED);
 if(r==__atomic_load_n(&hevc_write,__ATOMIC_ACQUIRE))return;
 if(seconds()<hevc.next_present)return;
 /* Keep at most two decoded frames ready. Late network bursts must not
    stall compressed input until Moonlight discards an entire GOP. */
 unsigned w=__atomic_load_n(&hevc_write,__ATOMIC_ACQUIRE);
 while(w-r>2){
  int index=hevc_queue[r%16].index;
  for(int i=0;i<64;i++)if(hevc.held[i]==index+1){hw_failed=1;ended=1;return;}
  __atomic_fetch_and(&hevc_owned,~(1u<<index),__ATOMIC_RELEASE);
  if(hevc.VPU_DecClrDispFlag(hevc.handle,index)){hw_failed=1;ended=1;return;}
  __atomic_sub_fetch(&hevc_outstanding,1,__ATOMIC_RELEASE);
  r++;render_dropped++;
  __atomic_store_n(&hevc_read,r,__ATOMIC_RELEASE);
 }
 HevcFrame frame=hevc_queue[r%16];int slot=-1;
 for(unsigned n=0;n<64;n++){
  unsigned candidate=(hevc.fence_cursor+n)%64;
  if(!hevc.held[candidate]){slot=(int)candidate;hevc.fence_cursor=candidate+1;break;}
 }
 if(slot<0)return;
 hw_vo_buffer image={.physical=frame.fb[0],.bytes=frame.fb[11]*frame.fb[12]*3/2,
   .width=hw_width,.height=hw_height,.stride=frame.fb[11],.aligned_height=frame.fb[12]};
 double start=seconds();
 if(!output_queue(&image,slot)){hw_failed=1;ended=1;return;}
 hevc.render_seconds+=seconds()-start;
 hevc.held[slot]=frame.index+1;
 __atomic_store_n(&hevc_read,r+1,__ATOMIC_RELEASE);
 double now=seconds();if(!displayed)hevc.first=now;
 else {double gap=now-hevc.last;if(gap>hevc.max_gap)hevc.max_gap=gap;if(gap>.050)hevc.gaps50++;}
 hevc.last=now;displayed++;
 /* Report sustained motion separately from the first three seconds of
    virtual-display creation and test-app startup. */
 if(now-hevc.first>=3){
  if(!hevc.steady_frames)hevc.steady_first=now;
  else{double gap=now-hevc.steady_last;if(gap>hevc.steady_max_gap)hevc.steady_max_gap=gap;if(gap>.050)hevc.steady_gaps50++;}
  hevc.steady_last=now;hevc.steady_frames++;
 }
 double local_delay=now*1000000.0-frame.received;
 if(local_delay>=0&&local_delay<10000000){latency_total+=local_delay;if(local_delay>latency_max)latency_max=local_delay;}
 double interval=1.0/__atomic_load_n(&hevc.rate,__ATOMIC_ACQUIRE);
 /* Keep a fixed cadence. Resetting to now + interval on every slightly
    late poll adds the polling delay to every frame and loses 60 Hz pacing.
    Skip missed deadlines after a stall instead of racing old frames. */
 if(!hevc.next_present)hevc.next_present=now+interval;
 else {
  hevc.next_present+=interval;
  if(hevc.next_present<=now)
   hevc.next_present+=((unsigned)((now-hevc.next_present)/interval)+1)*interval;
 }
 if(displayed==1||displayed%60==0)printf("HEVC_DISPLAYED=%u\n",displayed);
}
static int hevc_decode(PDECODE_UNIT unit){
 if(!unit)return DR_OK;
 if(hw_failed)return DR_NEED_IDR;
 if(hevc.submitted&&(unsigned)unit->frameNumber>hevc.last_number+1)hevc.input_gaps+=(unsigned)unit->frameNumber-hevc.last_number-1;
 hevc.last_number=(unsigned)unit->frameNumber;
 uint64_t now_us=LiGetMicroseconds();if(now_us>=unit->enqueueTimeUs)hevc.queue_us+=now_us-unit->enqueueTimeUs;
 double wait_start=seconds(),ready_limit=wait_start+2.0;
 while(__atomic_load_n(&hevc_outstanding,__ATOMIC_ACQUIRE)>=(unsigned)hevc.limit){
  if(hw_failed||ended||seconds()>ready_limit)goto fatal;
  delay_ms(1);
 }
 hevc.wait_seconds+=seconds()-wait_start;
 static unsigned char *bytes;static size_t capacity;
 size_t n=0;for(PLENTRY p=unit->bufferList;p;p=p->next)n+=p->length;
 if(!n||n>2*1024*1024)return DR_NEED_IDR;
 size_t padded=(n+511)&~511u;
 if(padded>capacity){void *b=realloc(bytes,padded);if(!b)return DR_NEED_IDR;bytes=b;capacity=padded;}
 size_t off=0;for(PLENTRY p=unit->bufferList;p;p=p->next){memcpy(bytes+off,p->data,p->length);off+=p->length;}
 memset(bytes+n,0,padded-n);
 /* PIC_END: reuse this input buffer only after the previous frame completed.
    Native endian 31 avoids the vendor's in-place 16-byte byte swapping. */
 double write_start=seconds();
 if(hevc.VPU_DecSetRdPtr(hevc.handle,hevc.bitstream.phys,1))goto fatal;
 if(hevc.vdi_write_memory(1,hevc.bitstream.phys,bytes,(int)padded,31)!=(int)padded)goto fatal;
 if(hevc.VPU_DecUpdateBitstreamBuffer(hevc.handle,(int)n))goto fatal;
 hevc.write_seconds+=seconds()-write_start;
 hevc.submitted++;
 if(!hevc.configured){
  uint32_t info[256]={0};int rc=hevc.VPU_DecGetInitialInfo(hevc.handle,info);
  printf("HEVC_SEQUENCE rc=%d coded=%ux%u crop=%u,%u,%u,%u depth=%u/%u buffers=%u\n",
    rc,info[0],info[1],info[4],info[5],info[6],info[7],info[47],info[48],info[12]);
  if(rc||info[0]!=(unsigned)hw_width||info[1]<(unsigned)hw_height||info[1]>(unsigned)hw_height+63||
     info[4]||info[5]||info[6]!=(unsigned)hw_width||info[7]!=(unsigned)hw_height||
     info[47]!=8||info[48]!=8||info[12]==0||info[12]>5)goto fatal;
  hevc.count=(int)info[12]+hevc.extra;if(hevc.count>8)hevc.count=8;
  if(hevc.limit>hevc.count-2)hevc.limit=hevc.count-2;
  hevc.stride=(hw_width+31)&~31;hevc.slice=(info[1]+31)&~31;
  rc=hevc.VPU_DecRegisterFrameBuffer(hevc.handle,NULL,hevc.count,hevc.stride,hevc.slice,10);
  printf("HEVC_BUFFERS rc=%d count=%d stride=%d slice=%d\n",rc,hevc.count,hevc.stride,hevc.slice);
  if(rc)goto fatal;
  hevc.configured=1;
 }
 uint32_t params[64]={0},result[256]={0};
 double decode_start=seconds();
 int rc=hevc.VPU_DecStartOneFrame(hevc.handle,params);if(rc){printf("HEVC_START_ERROR=%d\n",rc);goto fatal;}
 int irq=hevc.VPU_WaitInterrupt(1,2000);if(irq>=0)hevc.VPU_ClearInterrupt(1);
 rc=hevc.VPU_DecGetOutputInfo(hevc.handle,result);
 hevc.decode_seconds+=seconds()-decode_start;
 if(hevc.submitted<4||hevc.submitted%60==0)
  printf("HEVC_RESULT n=%u indexes=%d/%d/%d/%d success=%u irq=%d\n",hevc.submitted,(int32_t)result[0],(int32_t)result[1],(int32_t)result[2],(int32_t)result[3],result[11],irq);
 if(rc||irq<0||result[11]!=1){
  printf("HEVC_DECODE_ERROR rc=%d irq=%d success=%u reason=%08x\n",rc,irq,result[11],result[135]);
  hevc.failed++;if(hevc.failed>=5||irq<0)goto fatal;return DR_NEED_IDR;
 }
 int index=(int32_t)result[0];if(index<0)return DR_OK;
 if(index>=hevc.count)goto fatal;
 if(__atomic_fetch_or(&hevc_owned,1u<<index,__ATOMIC_ACQ_REL)&(1u<<index)){
  printf("HEVC_BUFFER_OWNERSHIP_ERROR=%d\n",index);goto fatal;
 }
 uint32_t fb[17]={0};rc=hevc.VPU_DecGetFrameBuffer(hevc.handle,index+hevc.count,fb);
 if(rc||fb[10]!=0||!fb[0]||fb[1]!=fb[0]+fb[11]*fb[12]||fb[11]!=(unsigned)hevc.stride)goto fatal;
 unsigned w=__atomic_load_n(&hevc_write,__ATOMIC_RELAXED);
 if(w-__atomic_load_n(&hevc_read,__ATOMIC_ACQUIRE)>=16)goto fatal;
 hevc_queue[w%16].index=index;hevc_queue[w%16].received=unit->enqueueTimeUs;memcpy(hevc_queue[w%16].fb,fb,sizeof fb);
 __atomic_add_fetch(&hevc_outstanding,1,__ATOMIC_RELEASE);
 __atomic_store_n(&hevc_write,w+1,__ATOMIC_RELEASE);
 return DR_OK;
fatal:
 puts("HEVC_BACKEND_STOPPED=1");hw_failed=1;ended=1;return DR_NEED_IDR;
}
