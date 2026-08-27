#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "c-api.h"

typedef struct { float *samples; int32_t sample_rate; int32_t num_samples; } Wave;
static const SherpaOnnxOfflineRecognizer *g_rec;

#define BUF_MAX (60*16000)
static float g_acc[BUF_MAX];
static int g_acc_len=0;
static int g_voice_state=0;      // 1=语音中
static long g_silent_cnt=0;      // 累积静音帧数
static int g_need_result=0;      // 有提交结果待取

// base64 解码
static size_t b64_decode(const char *in, unsigned char *out, size_t max) {
    static int8_t T[256]; static int init=0;
    if(!init){ for(int i=0;i<256;i++)T[i]=-1;
        const char *d="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for(int i=0;i<64;i++) T[(unsigned char)d[i]]=i; init=1; }
    size_t o=0; int v=0, vbits=0;
    for(;*in;in++){
        if(*in=='\n'||*in=='\r')continue;
        int8_t c=T[(unsigned char)*in]; if(c<0)break;
        v=(v<<6)|c; vbits+=6;
        if(vbits>=8){ if(o<max)out[o++]=(v>>(vbits-8))&0xff; vbits-=8; }
    }
    return o;
}

static Wave wav_from_mem(const unsigned char *d, size_t len);

// 检测有声
static int detect_voice(const float *s, int n) {
    if(n<=0) return 0;
    int hits=0; for(int i=0;i<n;i++){ float a=s[i]<0?-s[i]:s[i]; if(a>0.02f) hits++; }
    return (hits*10 > n*3);
}

// 累积缓冲识别整句
static int submit_accumulate(char *out_rtext) {
    if(g_acc_len < 800) { g_acc_len=0; return 0; }
    if(g_rec){
        SherpaOnnxOfflineStream *s=SherpaOnnxCreateOfflineStream(g_rec);
        SherpaOnnxAcceptWaveformOffline(s,16000,g_acc,g_acc_len);
        SherpaOnnxDecodeOfflineStream(g_rec,s);
        const SherpaOnnxOfflineRecognizerResult *r=SherpaOnnxGetOfflineStreamResult(s);
        snprintf(out_rtext,511,"%s",r->text);
        fprintf(stderr,"SUBMIT %d frames => [%s]\n",g_acc_len,out_rtext);
        SherpaOnnxDestroyOfflineStream(s);
        g_acc_len=0; g_voice_state=0; g_silent_cnt=0;
        return 1;
    }
    g_acc_len=0; return 0;
}

static void handle(int cfd){
    char hdr[32768]; int hn=0;
    while (hn < sizeof(hdr)-1) {
        int r = read(cfd, hdr+hn, 1); if (r<=0) break;
        hn++; if(hn>=4 && memcmp(hdr+hn-4,"\r\n\r\n",4)==0) break;
    }
    if(hn<=0){close(cfd);return;}
    hdr[hn]=0;
    long body_len=0; char *cl=strstr(hdr,"Content-Length:"); if(cl) body_len=atol(cl+15);
    char rtext[512]=""; char resp[900];
    if(body_len>0 && body_len<4*1024*1024){
        char *body=malloc(body_len+1); int have=0;
        while(have<body_len){ int r=read(cfd,body+have,body_len-have); if(r<=0)break; have+=r; }
        body[have]=0;
        char *p=strstr(body,"\"audio\":");
        if(p){ char *b64=p+9; while(*b64==' '||*b64=='"') b64++; char *end=strchr(b64,'"');
            if(end){ *end=0; long bl=strlen(b64); unsigned char *raw=malloc(bl+4); size_t rawlen=b64_decode(b64,raw,bl);
                Wave w=wav_from_mem(raw,rawlen);
                if(w.samples && w.num_samples>0){
                    fprintf(stderr,"req raw=%zu sr=%d n=%d recon...\n",rawlen,w.sample_rate,w.num_samples);
                    // 重采样到16k
                    int32_t sr=w.sample_rate; float *s16=w.samples; int32_t nn=w.num_samples;
                    if(sr!=16000 && sr>0){ int32_t on=(int32_t)((int64_t)nn*16000/sr); float *tmp=malloc(sizeof(float)*on);
                        for(int32_t i=0;i<on;i++){ double x=(double)i*nn/on; int32_t i0=(int32_t)x; if(i0>=nn-1)tmp[i]=w.samples[nn-1]; else{double f=x-i0; tmp[i]=(float)(w.samples[i0]*(1-f)+w.samples[i0+1]*f);} }
                        s16=tmp; nn=on; sr=16000;
                    }
                    SherpaOnnxOfflineStream *s=SherpaOnnxCreateOfflineStream(g_rec);
                    SherpaOnnxAcceptWaveformOffline(s,sr,s16,nn);
                    SherpaOnnxDecodeOfflineStream(g_rec,s);
                    const SherpaOnnxOfflineRecognizerResult *r=SherpaOnnxGetOfflineStreamResult(s);
                    snprintf(rtext,sizeof(rtext),"%s",r->text);
                    fprintf(stderr,"RECOG n=%d => [%s]\n",nn,rtext);
                    SherpaOnnxDestroyOfflineStream(s);
                    if(s16!=w.samples) free(s16);
                }
                free(w.samples); free(raw);
                // 简单 JSON 转义
                for(char *q=rtext;*q;q++) if(*q=='"'||*q=='\\') *q=' ';
            }
        }
        free(body);
    }
    snprintf(resp,sizeof(resp),"{\"ok\":true,\"text\":\"%s\"}",rtext);
    char out[1200];
    snprintf(out,sizeof(out),"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",strlen(resp),resp);
    write(cfd,out,strlen(out));
    close(cfd);
}

static Wave wav_from_mem(const unsigned char *d, size_t len) {
    Wave w={0};
    if(len < 44) return w;
    if(memcmp(d,"RIFF",4)!=0 || memcmp(d+8,"WAVE",4)!=0) return w;
    uint32_t sr; memcpy(&sr,d+24,4);
    uint16_t ch; memcpy(&ch,d+22,2);
    size_t db = len-44; if(db<2) return w;
    int32_t n=(int32_t)(db/2); int32_t mono=(ch==2)?n/2:n; if(mono<=0)return w;
    const int16_t *pcm=(const int16_t*)(d+44);
    w.samples=(float*)malloc(sizeof(float)*mono);
    for(int32_t k=0;k<mono;k++) w.samples[k]=pcm[k*ch]/32768.0f;
    w.sample_rate=(int32_t)sr; w.num_samples=mono;
    return w;
}

// 线程处理函数：单段即时识别
static void *thread_handler(void *arg){
    int cfd=(int)(intptr_t)arg;
    handle(cfd);
    return NULL;
}

int main(int argc,char**argv){
    const char *mp=getenv("SENSEVOICE_MODEL"), *tk=getenv("SENSEVOICE_TOKENS");
    if(!mp||!tk){fprintf(stderr,"setenv\n");return 1;}
    SherpaOnnxOfflineModelConfig mc; memset(&mc,0,sizeof(mc));
    mc.sense_voice=(SherpaOnnxOfflineSenseVoiceModelConfig){.model=mp,.language="zh",.use_itn=1};
    mc.tokens=tk; mc.num_threads=4; mc.provider="cpu";
    SherpaOnnxFeatureConfig fc; memset(&fc,0,sizeof(fc)); fc.sample_rate=16000; fc.feature_dim=80;
    SherpaOnnxOfflineRecognizerConfig rc; memset(&rc,0,sizeof(rc));
    rc.feat_config=fc; rc.model_config=mc; rc.decoding_method="greedy_search";
    g_rec=SherpaOnnxCreateOfflineRecognizer(&rc);
    if(!g_rec){fprintf(stderr,"init fail\n");return 1;}
    int sfd=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons(18082); a.sin_addr.s_addr=htonl(INADDR_ANY);
    bind(sfd,(struct sockaddr*)&a,sizeof(a)); listen(sfd,8);
    fprintf(stderr,"ASR HTTP multithread :18082\n");
    for(;;){
        int c=accept(sfd,NULL,NULL);
        if(c<0) continue;
        pthread_t tid;
        if(pthread_create(&tid,NULL,thread_handler,(void*)(intptr_t)c)!=0){ close(c); }
        pthread_detach(tid);
    }
}
