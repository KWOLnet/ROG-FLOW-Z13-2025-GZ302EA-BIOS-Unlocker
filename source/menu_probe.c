/* GZ302EA BIOS .314: temporary HII browser and native AOD save repair.
 * SetVariable is used ONLY for the native volatile NvLockMailbox command.
 * AOD settings are still validated/written by the original RouteConfig.
 * No BIOS flash, direct AOD-variable write, SMU write or reset is performed.
 * Native firmware form callbacks can save settings when the user submits them.
 * Protocol layouts follow the UEFI specification / TianoCore MdePkg headers.
 */
#include "patches.h"

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;
typedef unsigned long long UN;
typedef U64 STATUS;
typedef U16 C16;
typedef void *HANDLE;
typedef struct { U32 a; U16 b,c; U8 d[8]; } GUID;
typedef struct { U64 signature; U32 revision,size,crc,reserved; } TABLE_HEADER;
typedef struct { TABLE_HEADER hdr; void *fn[44]; } BOOT_SERVICES;
typedef struct { void *reset,*read; HANDLE wait; } INPUT;
typedef struct { void *fn[9]; void *mode; } OUTPUT;
typedef struct { GUID guid; void *table; } CONFIG_ENTRY;
typedef struct {
    TABLE_HEADER hdr; C16 *vendor; U32 revision;
    HANDLE in_handle; INPUT *in; HANDLE out_handle; OUTPUT *out;
    HANDLE error_handle; OUTPUT *error; void *runtime;
    BOOT_SERVICES *boot; UN config_count; CONFIG_ENTRY *config;
} SYSTEM_TABLE;
typedef struct { U16 scan,unicode; } KEY;
typedef struct { void *fn[11]; } HII_DATABASE;
typedef struct { void *send_form,*callback; } FORM_BROWSER;

_Static_assert(sizeof(GUID)==16, "GUID layout");
_Static_assert(sizeof(TABLE_HEADER)==24, "table header layout");
_Static_assert(__builtin_offsetof(SYSTEM_TABLE,boot)==96, "system table boot services");
_Static_assert(__builtin_offsetof(SYSTEM_TABLE,config)==112, "system table config pointer");
_Static_assert(__builtin_offsetof(BOOT_SERVICES,fn[37])==320, "LocateProtocol ABI");

#define ERROR_BIT 0x8000000000000000ULL
#define EFI_ERROR(x) (((x)&ERROR_BIT)!=0)
#define BUFFER_TOO_SMALL (ERROR_BIT|5)
#define BAD_DATA (ERROR_BIT|33)
#define NOT_FOUND (ERROR_BIT|14)
#define UNSUPPORTED (ERROR_BIT|3)
#define MAX_LIST (8ULL*1024*1024)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))

static const GUID aod_guid={0xe39ac631,0x48ed,0x445e,{0x90,0x42,0x55,0xb3,0x4f,0x40,0x58,0x44}};
static const GUID hii_guid={0xef9fc172,0xa1b2,0x4693,{0xb3,0x27,0x6d,0x32,0xfc,0x41,0x60,0x42}};
static const GUID browser_guid={0xb9d4c360,0xbcfb,0x4f9b,{0x92,0x98,0x53,0xc1,0x36,0x98,0x22,0x58}};
static const GUID smbios3_guid={0xf2fd1544,0x9794,0x4a2c,{0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};
static const GUID smbios2_guid={0xeb9d2d31,0x2d88,0x11d3,{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
static const GUID smu_guid={0x264272b3,0x81d1,0x453a,{0x85,0x03,0x89,0x89,0xf4,0x38,0x2e,0x68}};
static const GUID config_guid={0x330d4706,0xf2a0,0x4e4f,{0xa3,0x69,0xb6,0x6f,0xa8,0xd5,0x43,0x85}};
static const GUID aod_var_guid={0x5ed15dc0,0xedef,0x4161,{0x91,0x51,0x60,0x14,0xc4,0xcc,0x63,0x0c}};
static const GUID routing_guid={0x587e72d7,0xcc50,0x4f79,{0x82,0x09,0xca,0x29,0x1f,0xc1,0xa1,0x0f}};
static const GUID setup_guid={0xec87d643,0xeba4,0x4bb5,{0xa1,0xe5,0x3f,0x3e,0x36,0xb2,0x0d,0xa9}};
static const GUID lock_guid={0x504af431,0x3025,0x4d32,{0x9f,0xbf,0xe9,0xf3,0x18,0x55,0x55,0x1d}};
static const GUID cbs_form_guid={0xb04535e3,0x3004,0x4946,{0x9e,0xb7,0x14,0x94,0x28,0x98,0x30,0x53}};
static const GUID cbs_var_guid={0x3a997502,0x647a,0x4c82,{0x99,0x8e,0x52,0xef,0x94,0x86,0xa2,0x47}};
typedef struct {
    const C16 *name,*last_name;const GUID *form_guid,*var_guid;
    UN size,prefix;int require_last,is_cbs;
} STORE;
static const STORE aod_store={L"AodSetupStxh",L"AodSetupStxhLast",&aod_guid,&aod_var_guid,801,44,1,0};
static const STORE cbs_store={L"AmdSetup",0,&cbs_form_guid,&cbs_var_guid,1522,4,0,1};
static const STORE *active_store=&aod_store;
static SYSTEM_TABLE *st;
static BOOT_SERVICES *bs;
static U16 last_scan,last_unicode;
typedef struct { STATUS status;UN size;U32 attributes;U8 data[1522]; } SNAPSHOT;
typedef STATUS (*ROUTE_CONFIG)(void*,const C16*,C16**);
static SNAPSHOT startup_main,startup_last,before_main,after_main,after_last,restored_main,route_main;
static void **watched_access;
static ROUTE_CONFIG saved_route;
static UN route_count;
static STATUS route_status,watch_status;
static SNAPSHOT route_before,requested_state,verified_last;
static SNAPSHOT startup_cbs;
static STATUS first_save_status,repair_plan_status,unlock_status,retry_status,relock_status,verify_status;
static int repair_attempted,need_relock;
static UN native_save_calls;

static U32 u32(const U8 *p) { return (U32)p[0]|((U32)p[1]<<8)|((U32)p[2]<<16)|((U32)p[3]<<24); }
static U64 u64(const U8 *p) { return (U64)u32(p)|((U64)u32(p+4)<<32); }
static int equal(const void *aa,const void *bb,UN n) {
    const U8 *a=aa,*b=bb; UN i;
    for(i=0;i<n;i++) if(a[i]!=b[i]) return 0;
    return 1;
}
static void copy(void *dd,const void *ss,UN n) {
    U8 *d=dd; const U8 *s=ss; UN i;
    for(i=0;i<n;i++) d[i]=s[i];
}
static void out(const C16 *s) {
    ((STATUS(*)(OUTPUT*,const C16*))st->out->fn[1])(st->out,s);
}
static void hex64(U64 v) {
    C16 s[19]; UN i; s[0]='0';s[1]='x';
    for(i=0;i<16;i++) s[i+2]=L"0123456789ABCDEF"[(v>>((15-i)*4))&15];
    s[18]=0;out(s);
}
static void ascii(const char *s) {
    C16 b[128];UN i=0;
    while(s[i]&&i<127) {b[i]=(U8)s[i];i++;}b[i]=0;out(b);
}
static void status_line(const C16 *label,STATUS s) {out(label);hex64(s);out(L"\r\n");}
static void decimal(U32 n) {
    C16 s[11];UN pos=10;s[pos]=0;
    do{s[--pos]=(C16)('0'+n%10);n/=10;}while(n);
    out(s+pos);
}
static void *alloc(UN size) {
    void *p=0; STATUS s=((STATUS(*)(U32,UN,void**))bs->fn[5])(4,size,&p);
    return EFI_ERROR(s)?0:p;
}
static void release(void *p) {if(p)((STATUS(*)(void*))bs->fn[6])(p);}
static STATUS locate(const GUID *g,void **p) {
    *p=0;return ((STATUS(*)(const GUID*,void*,void**))bs->fn[37])(g,0,p);
}
static U16 key(void) {
    KEY k;UN index;STATUS s;
    for(;;) {
        k.scan=0;k.unicode=0;
        s=((STATUS(*)(INPUT*,KEY*))st->in->read)(st->in,&k);
        if(!EFI_ERROR(s)) {
            last_scan=k.scan;last_unicode=k.unicode;
            if(k.scan==23)return 27;
            if(k.unicode>=0xff10&&k.unicode<=0xff19)return k.unicode-0xff10+'0';
            return k.unicode;
        }
        s=((STATUS(*)(UN,HANDLE*,UN*))bs->fn[9])(1,&st->in->wait,&index);
        if(EFI_ERROR(s)) return 27;
    }
}
static int contains(const char *s,const char *needle) {
    UN i,j;
    for(i=0;s[i];i++) {for(j=0;needle[j]&&s[i+j]==needle[j];j++) {} if(!needle[j])return 1;}
    return 0;
}
static int ends314(const char *s) {
    UN n=0;while(s[n])n++;
    return n>=3&&s[n-3]=='3'&&s[n-2]=='1'&&s[n-1]=='4'&&(n==3||s[n-4]=='.');
}
static const char *smb_string(const U8 *p,const U8 *end,U8 index) {
    UN i=1;const U8 *s=p+p[1];
    if(!index)return "";
    while(s<end&&*s) {
        const U8 *start=s;
        while(s<end&&*s)s++;
        if(s==end)return "";
        if(i==index)return (const char*)start;
        i++;s++;
    }
    return "";
}
static int check_model(void) {
    UN i;const U8 *table=0,*end,*p;U32 size=0;int model=0,version=0;
    for(i=0;i<st->config_count;i++) {
        const U8 *e=st->config[i].table;
        if(equal(&st->config[i].guid,&smbios3_guid,16)&&equal(e,"_SM3_",5)&&e[6]>=24) {
            size=u32(e+12);table=(const U8*)(UN)u64(e+16);break;
        }
        if(equal(&st->config[i].guid,&smbios2_guid,16)&&equal(e,"_SM_",4)&&e[5]>=31) {
            size=(U32)e[22]|((U32)e[23]<<8);table=(const U8*)(UN)u32(e+24);
        }
    }
    if(!table||size<6||size>16*1024*1024)return 0;
    end=table+size;p=table;
    while(p+4<=end&&p[1]>=4&&p+p[1]+2<=end) {
        const U8 *next=p+p[1];
        while(next+1<end&&(next[0]||next[1]))next++;
        if(next+1>=end)break;
        if(p[0]==0&&p[1]>5) {
            const char *v=smb_string(p,next+2,p[5]);
            out(L"BIOS: ");ascii(v);out(L"\r\n");
            if(ends314(v))version=1;
        }
        if(p[0]==1&&p[1]>5) {
            const char *v=smb_string(p,next+2,p[5]);
            out(L"Product: ");ascii(v);out(L"\r\n");
            if(contains(v,"GZ302EA"))model=1;
        }
        if(p[0]==127)break;
        p=next+2;
    }
    return model&&version;
}

/* Returns seven edits on success. Failure is transactional: no bytes changed. */
#ifdef HOST_TEST
__declspec(dllexport)
#endif
int patch_forms(U8 *b,UN n) {
    UN i,j,p,pos[COUNT(patches)],scope=0;U32 length;
    if(!b||n<28||n>MAX_LIST)return -1;
    length=u32(b);
    if((length>>24)!=2||(length&0xffffff)!=n||b[4]!=14||
       (b[5]&127)<23||!equal(b+6,&aod_guid,16))return -2;
    for(i=0;i<COUNT(patches);i++)pos[i]=(UN)-1;
    for(p=4;p<n;) {
        U8 op=b[p],len;
        if(p+2>n)return -3;
        len=b[p+1]&127;
        if(len<2||p+len>n)return -3;
        for(i=0;i<COUNT(patches);i++) {
            if(p+patches[i].size<=n&&equal(b+p,patches[i].old_data,patches[i].size)) {
                if(pos[i]!=(UN)-1)return -4;
                pos[i]=p;
            }
        }
        if(op==0x29) {if(!scope)return -5;scope--;}
        if(b[p+1]&128)scope++;
        p+=len;
    }
    if(scope)return -5;
    for(i=0;i<COUNT(patches);i++) {
        if(pos[i]==(UN)-1)return -6;
        for(j=0;j<i;j++)
            if(pos[i]<pos[j]+patches[j].size&&pos[j]<pos[i]+patches[i].size)return -7;
    }
    for(i=0;i<COUNT(patches);i++)copy(b+pos[i],patches[i].new_data,patches[i].size);
    return (int)COUNT(patches);
}

static STATUS export_list(HII_DATABASE *db,HANDLE h,U8 **data,UN *size) {
    STATUS s;*size=0;*data=0;
    s=((STATUS(*)(HII_DATABASE*,HANDLE,UN*,void*))db->fn[4])(db,h,size,0);
    if(s!=BUFFER_TOO_SMALL||*size<24||*size>MAX_LIST)return BAD_DATA;
    *data=alloc(*size);if(!*data)return ERROR_BIT|9;
    s=((STATUS(*)(HII_DATABASE*,HANDLE,UN*,void*))db->fn[4])(db,h,size,*data);
    if(EFI_ERROR(s)){release(*data);*data=0;}return s;
}
static STATUS find_aod(HII_DATABASE *db,HANDLE *target,U8 **list,UN *list_size,UN *form_offset,UN *form_size) {
    HANDLE *handles=0;UN bytes=0,i;STATUS s;
    s=((STATUS(*)(HII_DATABASE*,U8,GUID*,UN*,HANDLE*))db->fn[3])(db,2,0,&bytes,0);
    if(s!=BUFFER_TOO_SMALL||!bytes||bytes>65536||bytes%sizeof(HANDLE))return NOT_FOUND;
    handles=alloc(bytes);if(!handles)return ERROR_BIT|9;
    s=((STATUS(*)(HII_DATABASE*,U8,GUID*,UN*,HANDLE*))db->fn[3])(db,2,0,&bytes,handles);
    if(EFI_ERROR(s)){release(handles);return s;}
    for(i=0;i<bytes/sizeof(HANDLE);i++) {
        U8 *b=0;UN n=0,p;
        if(EFI_ERROR(export_list(db,handles[i],&b,&n)))continue;
        if(u32(b+16)!=n){release(b);continue;}
        for(p=20;p+4<=n;) {
            U32 h=u32(b+p),len=h&0xffffff;
            if(len<4||p+len>n)break;
            if((h>>24)==2&&len>=28&&b[p+4]==14&&equal(b+p+6,active_store->form_guid,16)) {
                *target=handles[i];*list=b;*list_size=n;*form_offset=p;*form_size=len;
                release(handles);return 0;
            }
            p+=len;
        }
        release(b);
    }
    release(handles);return NOT_FOUND;
}
static U8 *update_list(const U8 *original,const U8 *forms,UN size) {
    U8 *b=alloc(24+size);UN n=24+size;
    if(!b)return 0;
    copy(b,original,16);b[16]=(U8)n;b[17]=(U8)(n>>8);b[18]=(U8)(n>>16);b[19]=(U8)(n>>24);
    copy(b+20,forms,size);b[20+size]=4;b[21+size]=0;b[22+size]=0;b[23+size]=0xdf;
    return b;
}
static STATUS prepare_forms(HII_DATABASE *db,HANDLE *target,U8 **changed,U8 **restore) {
    U8 *list=0,*forms=0;UN list_size=0,fo=0,fs=0;STATUS s;
    *changed=0;*restore=0;*target=0;
    s=find_aod(db,target,&list,&list_size,&fo,&fs);
    if(EFI_ERROR(s))return s;
    forms=alloc(fs);
    if(!forms){s=ERROR_BIT|9;goto done;}
    copy(forms,list+fo,fs);
    if(!active_store->is_cbs&&patch_forms(forms,fs)!=7){s=BAD_DATA;goto done;}
    *restore=update_list(list,list+fo,fs);
    *changed=update_list(list,forms,fs);
    if(!*restore||!*changed){s=ERROR_BIT|9;goto done;}
    s=0;
done:
    release(forms);release(list);
    if(EFI_ERROR(s)){release(*changed);release(*restore);*changed=0;*restore=0;}
    return s;
}
static int caps(void) {
    void **smu=0;U32 args[6]={0,0,0,0,0,0};STATUS s=locate(&smu_guid,(void**)&smu);
    if(EFI_ERROR(s)||!smu){status_line(L"SMU protocol unavailable: ",s);return -1;}
    s=((STATUS(*)(void*,UN,U32,U32*,U32*))smu[3])(smu,0,94,args,args);
    if(EFI_ERROR(s)){status_line(L"SMU capability query failed: ",s);return -1;}
    out(L"SMU capabilities: ");hex64(args[0]);out(L"\r\n");
    out((args[0]&1)?L"  CPU manual voltage: advertised\r\n":L"  CPU manual voltage: NOT advertised\r\n");
    out((args[0]&16)?L"  iGPU manual voltage: advertised\r\n":L"  iGPU manual voltage: NOT advertised\r\n");
    out((args[0]&2)?L"  PBO: advertised\r\n":L"  PBO: NOT advertised\r\n");
    out((args[0]&4)?L"  Curve menu capability: advertised\r\n":L"  Curve menu capability: NOT advertised\r\n");
    return (int)args[0];
}
/* Native save diagnostics and bounded retry. The native request is unchanged. */
static void read_snapshot(const C16 *name,SNAPSHOT *snap) {
    void **runtime=st->runtime?(void**)((U8*)st->runtime+24):0;
    snap->size=active_store->size;snap->attributes=0;snap->status=UNSUPPORTED;
    if(!name||!runtime||!runtime[6])return;
    snap->status=((STATUS(*)(const C16*,const GUID*,U32*,UN*,void*))runtime[6])(
        name,active_store->var_guid,&snap->attributes,&snap->size,snap->data);
    if(!EFI_ERROR(snap->status)&&(snap->size!=active_store->size||
       (!active_store->is_cbs&&(snap->data[0]!=0x21||snap->data[1]!=3))))snap->status=BAD_DATA;
}
static int settings_equal(const SNAPSHOT *a,const SNAPSHOT *b) {
    if(EFI_ERROR(a->status)||EFI_ERROR(b->status))return -1;
    if(active_store->is_cbs)return equal(a->data,b->data,active_store->size);
    return a->data[0x95]==b->data[0x95]&&equal(a->data+0xb8,b->data+0xb8,8)&&
           equal(a->data+0xc0,b->data+0xc0,192);
}
static void dump_snapshot(const C16 *label,const SNAPSHOT *snap) {
    UN i;U32 fingerprint=2166136261U;
    out(label);
    if(EFI_ERROR(snap->status)){out(L" read status=");hex64(snap->status);out(L"\r\n");return;}
    if(active_store->is_cbs) {
        out(L" attrs=");hex64(snap->attributes);
        out((snap->attributes&1)?L" NV=yes ThermalControl=":L" NV=no ThermalControl=");
        decimal(snap->data[0x152]);out(L" TjMax=");decimal(u32(snap->data+0x153));out(L" C\r\n");
        for(i=active_store->prefix;i<active_store->size;i++){fingerprint^=snap->data[i];fingerprint*=16777619U;}
        out(L"  CBS settings fingerprint=");hex64(fingerprint);out(L"\r\n");return;
    }
    out(L" attrs=");hex64(snap->attributes);
    out((snap->attributes&1)?L" NV=yes PBO=":L" NV=no PBO=");decimal(snap->data[0x95]);
    out(L" CPU-mode=");decimal(snap->data[0xbc]);
    out(snap->data[0xbd]?L" CPU=-":L" CPU=+");
    decimal((U32)snap->data[0xbe]|((U32)snap->data[0xbf]<<8));
    out(L" GFX-enable=");decimal(snap->data[0xb8]);
    out(snap->data[0xb9]?L" GFX=-":L" GFX=+");
    decimal((U32)snap->data[0xba]|((U32)snap->data[0xbb]<<8));out(L"\r\n");
    for(i=44;i<801;i++){fingerprint^=snap->data[i];fingerprint*=16777619U;}
    out(L"  AOD settings fingerprint=");hex64(fingerprint);
    out(L" CPU-freq=");decimal(u32(snap->data+0x2c));
    out(L" CPU-mV=");decimal((U32)snap->data[0x30]|((U32)snap->data[0x31]<<8));
    out(L" GFX-freq=");decimal((U32)snap->data[0x34]|((U32)snap->data[0x35]<<8));
    out(L" GFX-mV=");decimal((U32)snap->data[0x36]|((U32)snap->data[0x37]<<8));out(L"\r\n");
}
static STATUS native_lock(U8 locked) {
    void **runtime=st->runtime?(void**)((U8*)st->runtime+24):0;
    // Exact native AmiSetupNVLockDxe .314 command: ANV$, state, reset-password=0.
    U8 command[6]={'A','N','V','$',locked,0};
    if(!runtime||!runtime[8])return UNSUPPORTED;
    return ((STATUS(*)(const C16*,const GUID*,U32,UN,const void*))runtime[8])(
        L"NvLockMailbox",&lock_guid,2,6,command);
}
static void ensure_relocked(void) {
    if(need_relock) {
        relock_status=native_lock(1);
        if(!EFI_ERROR(relock_status))need_relock=0;
    }
}
static STATUS prepare_save_retry(const C16 *configuration) {
    void **runtime=st->runtime?(void**)((U8*)st->runtime+24):0;
    void **routing=0;U8 setup[148];UN n=sizeof(setup),block=active_store->size;U32 attrs=0;C16 *progress=0;STATUS s;
    if(!runtime||!runtime[6]||!runtime[8])return UNSUPPORTED;
    // Require the exact .314 native protection feature, not a generic unlock.
    s=((STATUS(*)(const C16*,const GUID*,U32*,UN*,void*))runtime[6])(
        L"Setup",&setup_guid,&attrs,&n,setup);
    if(EFI_ERROR(s))return s;
    if(n!=148||setup[0x50]!=1)return UNSUPPORTED;
    if(EFI_ERROR(route_before.status)||EFI_ERROR(route_main.status)||
       route_before.attributes!=7||route_main.attributes!=7||
       !equal(route_before.data,route_main.data,active_store->size))return BAD_DATA;
    s=locate(&routing_guid,(void**)&routing);
    if(EFI_ERROR(s)||!routing||!routing[4])return EFI_ERROR(s)?s:UNSUPPORTED;
    copy(requested_state.data,route_before.data,active_store->size);
    s=((STATUS(*)(void*,const C16*,U8*,UN*,C16**))routing[4])(
        routing,configuration,requested_state.data,&block,&progress);
    if(EFI_ERROR(s))return s;
    // Permit the entire native AOD settings area; preserve store identity and
    // runtime capability/topology metadata in the first 44 bytes.
    if(block>active_store->size||!equal(requested_state.data,route_before.data,active_store->prefix))return BAD_DATA;
    if(equal(requested_state.data,route_before.data,active_store->size))return UNSUPPORTED;
    requested_state.status=0;requested_state.size=active_store->size;requested_state.attributes=7;
    return 0;
}
static STATUS watched_route(void *this,const C16 *configuration,C16 **progress) {
    STATUS s;C16 *refresh_progress=0,*refresh_result=0;
    route_count++;native_save_calls=0;repair_attempted=0;
    first_save_status=repair_plan_status=unlock_status=retry_status=relock_status=verify_status=NOT_FOUND;
    read_snapshot(active_store->name,&route_before);
    native_save_calls++;
    s=saved_route(this,configuration,progress);
    first_save_status=s;
    read_snapshot(active_store->name,&route_main);
    if(s==(ERROR_BIT|15)||s==(ERROR_BIT|8)||s==(ERROR_BIT|26)) {
        repair_plan_status=prepare_save_retry(configuration);
        if(!EFI_ERROR(repair_plan_status)) {
            // Refresh native storage before retrying a potentially partial
            // BlockConfig, so unrelated bytes cannot come from a stale cache.
            if(!watched_access||!watched_access[0])repair_plan_status=UNSUPPORTED;
            else repair_plan_status=((STATUS(*)(void*,const C16*,C16**,C16**))watched_access[0])(
                this,0,&refresh_progress,&refresh_result);
            release(refresh_result);
        }
        if(!EFI_ERROR(repair_plan_status)) {
            repair_attempted=1;need_relock=1;
            unlock_status=native_lock(0);
            if(!EFI_ERROR(unlock_status)) {
                native_save_calls++;
                s=saved_route(this,configuration,progress);retry_status=s;
            }
            // Always restore the native locked state, including unlock/retry
            // errors. Nothing leaves the settings write window open on purpose.
            ensure_relocked();
            if(need_relock)s=ERROR_BIT|21;
            read_snapshot(active_store->name,&route_main);
            read_snapshot(active_store->last_name,&verified_last);
            if(!EFI_ERROR(s)) {
                verify_status=(!EFI_ERROR(route_main.status)&&route_main.attributes==7&&
                    equal(route_main.data,requested_state.data,active_store->size)&&
                    (!active_store->require_last||(!EFI_ERROR(verified_last.status)&&verified_last.attributes==7&&
                    equal(verified_last.data,requested_state.data,active_store->size))))?0:BAD_DATA;
                if(EFI_ERROR(verify_status))s=verify_status;
            }
        }
    }
    route_status=s;
    return s;
}
static STATUS start_watch(HII_DATABASE *db,HANDLE hii_handle) {
    HANDLE driver=0;void **access=0;STATUS s;
    route_count=0;route_status=NOT_FOUND;watched_access=0;saved_route=0;
    if(!db->fn[10])return UNSUPPORTED;
    s=((STATUS(*)(HII_DATABASE*,HANDLE,HANDLE*))db->fn[10])(db,hii_handle,&driver);
    if(EFI_ERROR(s)||!driver)return EFI_ERROR(s)?s:NOT_FOUND;
    s=((STATUS(*)(HANDLE,const GUID*,void**))bs->fn[16])(driver,&config_guid,(void**)&access);
    if(EFI_ERROR(s)||!access||!access[1])return EFI_ERROR(s)?s:NOT_FOUND;
    watched_access=access;saved_route=(ROUTE_CONFIG)access[1];
    access[1]=(void*)watched_route;
    return 0;
}
static void stop_watch(void) {
    ensure_relocked();
    if(watched_access&&watched_access[1]==(void*)watched_route)watched_access[1]=(void*)saved_route;
    watched_access=0;saved_route=0;
}
static void save_report(void) {
    status_line(L"Save observer installation: ",watch_status);
    out(L"Native RouteConfig calls: ");decimal((U32)route_count);out(L"\r\n");
    if(route_count){status_line(L"Final RouteConfig result: ",route_status);dump_snapshot(L"Inside native submission:",&route_main);}
    if(route_count) {
        status_line(L"First native save status: ",first_save_status);
        out(L"Native save invocations: ");decimal((U32)native_save_calls);out(L"\r\n");
        if(EFI_ERROR(first_save_status))status_line(L"Save repair preparation: ",repair_plan_status);
        if(repair_attempted) {
            dump_snapshot(active_store->is_cbs?L"Requested CBS settings:":L"Requested AOD settings:",&requested_state);
            status_line(L"Temporary native write context: ",unlock_status);
            status_line(L"Native save retry: ",retry_status);
            status_line(L"Native write protection re-lock: ",relock_status);
            status_line(active_store->is_cbs?L"CBS MAIN readback verification: ":L"MAIN + LAST readback verification: ",verify_status);
        }
    }
    dump_snapshot(L"Before browser:",&before_main);
    dump_snapshot(L"After browser MAIN:",&after_main);
    if(active_store->require_last)dump_snapshot(L"After browser LAST:",&after_last);
    dump_snapshot(L"After forms restore MAIN:",&restored_main);
    if(settings_equal(&before_main,&after_main)==1)out(active_store->is_cbs?L"CBS readback unchanged across browser.\r\n":L"PBO/Curve readback unchanged across browser.\r\n");
    if(settings_equal(&before_main,&after_main)==0)out(active_store->is_cbs?L"CBS readback changed across browser.\r\n":L"PBO/Curve readback changed across browser.\r\n");
    if(settings_equal(&after_main,&restored_main)==0)out(L"Configuration readback changed during forms restore.\r\n");
    if(active_store->is_cbs)out(L"ThermalControl: 0=Auto 1=Manual. Auto uses the platform default, not 0 C.\r\n");
    else out(L"CPU-mode: 0=disabled 1=all-core 2=per-core. PBO: 2=Advanced 255=Auto.\r\n");
    out(L"Readback is not proof of SMU acceptance or persistence after reboot.\r\n");
}

#ifdef HOST_TEST
__declspec(dllexport)
#endif
STATUS efi_main(HANDLE image,SYSTEM_TABLE *system) {
    HII_DATABASE *db=0;FORM_BROWSER *browser=0;HANDLE target=0;
    U8 *changed=0,*restore=0;UN action;STATUS s;U16 choice,first_form;
    (void)image;st=system;bs=st->boot;active_store=&aod_store;
    out(L"\r\nGZ302EA .314 V5 - AOD/CBS SAVE REPAIR + THERMAL MENU - EXPERIMENTAL\r\n");
    if(!check_model()) {
        out(L"Target mismatch or missing SMBIOS. No HII changes made. Press a key.\r\n");key();return UNSUPPORTED;
    }
    // UEFI loaders can arm a five-minute watchdog. Disable that volatile timer
    // so reading the firmware menus cannot trigger an unexpected timeout reset.
    if(bs->fn[29])((STATUS(*)(UN,U64,UN,C16*))bs->fn[29])(0,0,0,0);
    out(L"No BIOS flash. Temporary HII edits are restored when the browser returns.\r\n");
    out(L"Native Save/Apply can persist settings. Only save values you intend to use.\r\n");
    out(L"F10: native AOD/CBS save; on locked-store rejection, bounded native-context retry.\r\n");
    out(L"Native validation is retained. No voltage, Curve or TjMax values are preset.\r\n");
    out(L"Curve controls retain the PBO Advanced dependency. No tuning values are preset.\r\n");
    caps();
    read_snapshot(L"AodSetupStxh",&startup_main);read_snapshot(L"AodSetupStxhLast",&startup_last);
    dump_snapshot(L"STARTUP MAIN:",&startup_main);dump_snapshot(L"STARTUP LAST:",&startup_last);
    active_store=&cbs_store;read_snapshot(active_store->name,&startup_cbs);dump_snapshot(L"STARTUP CBS:",&startup_cbs);
    active_store=&aod_store;
    s=locate(&hii_guid,(void**)&db);if(EFI_ERROR(s))goto fail;
    s=locate(&browser_guid,(void**)&browser);if(EFI_ERROR(s))goto fail;
    s=prepare_forms(db,&target,&changed,&restore);if(EFI_ERROR(s))goto fail;
    release(changed);release(restore);changed=0;restore=0;
    out(L"Matched all 7 expected .314 menu anchors.\r\n");
    for(;;) {
        int update_attempted=0;
        out(L"\r\n1 / O = original AMD menus   2 / U = temporary unlocked menus\r\n");
        out(L"T = CBS Thermal Control / TjMax   B = full AMD CBS menu\r\n");
        out(L"Q / Esc = exit   C = capabilities   R = read saved PBO/Curve\r\n");
        for(;;) {
            choice=key();
            if(choice=='o'||choice=='O')choice='1';
            if(choice=='u'||choice=='U')choice='2';
            if(choice=='1'||choice=='2'||choice==27||choice=='q'||choice=='Q'||choice=='c'||choice=='C'||choice=='r'||choice=='R'||choice=='t'||choice=='T'||choice=='b'||choice=='B')break;
            out(L"Ignored key: Unicode=");hex64(last_unicode);
            out(L" Scan=");hex64(last_scan);
            out(L". Press letter U to open unlocked menus.\r\n");
        }
        if(choice==27||choice=='q'||choice=='Q')break;
        if(choice=='c'||choice=='C'){caps();continue;}
        if(choice=='r'||choice=='R') {
            read_snapshot(active_store->name,&after_main);read_snapshot(active_store->last_name,&after_last);
            dump_snapshot(L"CURRENT MAIN:",&after_main);if(active_store->require_last)dump_snapshot(L"CURRENT LAST:",&after_last);continue;
        }
        if(choice=='t'||choice=='T'||choice=='b'||choice=='B') {
            active_store=&cbs_store;first_form=(choice=='t'||choice=='T')?0x7006:0x7000;
            out(L"Selection accepted: native AMD CBS menus\r\n");
        } else {
            active_store=&aod_store;first_form=0x98;
            out(choice=='2'?L"Selection accepted: temporary unlocked menus\r\n":L"Selection accepted: original AMD menus\r\n");
        }
        out(active_store->is_cbs?L"Refreshing live CBS handle and forms...\r\n":L"Refreshing live AOD handle and forms...\r\n");
        s=prepare_forms(db,&target,&changed,&restore);
        if(EFI_ERROR(s))goto fail;
        action=0;
        route_count=0;route_status=NOT_FOUND;watch_status=NOT_FOUND;
        read_snapshot(active_store->name,&before_main);
        if(choice=='2') {
            // An error can follow partial database changes. Always attempt restore.
            update_attempted=1;
            s=((STATUS(*)(HII_DATABASE*,HANDLE,const void*))db->fn[2])(db,target,changed);
            if(EFI_ERROR(s))status_line(L"Temporary HII update failed: ",s);
        } else s=0;
        if(!EFI_ERROR(s)) {
            watch_status=start_watch(db,target);
            out(L"Calling firmware browser...\r\n");
            s=((STATUS(*)(FORM_BROWSER*,HANDLE*,UN,const GUID*,U16,void*,UN*))browser->send_form)(
                browser,&target,1,active_store->form_guid,first_form,0,&action);
            stop_watch();
            status_line(L"Browser return: ",s);
            out(L"Browser action request: ");hex64(action);out(L"\r\n");
        }
        read_snapshot(active_store->name,&after_main);read_snapshot(active_store->last_name,&after_last);
        if(need_relock) {
            read_snapshot(active_store->name,&restored_main);save_report();
            out(L"Protection re-lock failed. No further menu operations. Restart the computer now.\r\n");
            out(L"Press a key to exit; this utility does not reset automatically.\r\n");key();s=ERROR_BIT|21;goto cleanup;
        }
        if(update_attempted) {
            s=((STATUS(*)(HII_DATABASE*,HANDLE,const void*))db->fn[2])(db,target,restore);
            status_line(L"Original forms restore: ",s);
            if(EFI_ERROR(s)) {
                read_snapshot(active_store->name,&restored_main);save_report();
                out(L"Restore failed. Reboot before using other firmware menus. Press a key.\r\n");
                key();goto cleanup;
            }
        }
        read_snapshot(active_store->name,&restored_main);save_report();
        if(need_relock) {
            out(L"Write protection could not be re-locked. Restart now; do not continue firmware operations.\r\n");
            out(L"Press a key to exit; this utility does not reset automatically.\r\n");key();s=ERROR_BIT|21;goto cleanup;
        }
        release(changed);release(restore);changed=0;restore=0;
        if(action==1) {
            out(L"Firmware requested a restart. Restart before opening menus again.\r\n");
            out(L"This utility will not restart automatically. Press a key to exit.\r\n");key();goto cleanup;
        }
        out(L"Press a key to return to the main menu; status codes stay above.\r\n");key();
    }
    s=0;goto cleanup;
fail:
    status_line(L"Stopped without opening menus: ",s);out(L"Press a key to exit.\r\n");key();
cleanup:
    stop_watch();
    release(restore);release(changed);return s;
}
