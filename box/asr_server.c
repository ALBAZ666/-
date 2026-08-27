#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "c-api.h"

typedef struct { float *samples; int32_t sample_rate; int32_t num_samples; } Wave;
static const SherpaOnnxOfflineRecognizer *g_rec;

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

// 从 base64 解码的 WAV（PCM16）解析
static Wave wav_from_mem(const unsigned char *d, size_t len) {
    Wave w={0};
    if(len<44) return w;
    // 找 data chunk
    size_t i=12;
    while(i+8<=len){
        unsigned br,size;
        memcpy(&br,d+i,4); memcpy(&size,d+i+4,4);
        if(memcmp(d+i,"fmt ",4)==0){
            unsigned fmt; memcpy(&fmt,d+i+8,2);
            // 若 fmt 不是 PCM（format tag=1）可能需要其他; 假设 1
        } else if(memcmp(d+i,"data",4)==0){
            // 头部: 44 固定对 PCM。取 data from i+8, size bytes
            int16_t *pcm=(int16_t*)(d+i+8);
            int32_t n=size/2;
            // sample_rate 在 24 偏移, channels 在 22
            int32_t sr; memcpy(&sr,d+24,4);
            unsigned ch; memcpy(&ch,d+22,2);
            int32_t mono = (ch==2)? n/2 : n;
            w.samples=(float*)malloc(sizeof(float)*mono);
            for(int32_t k=0;k<mono;k++) w.samples[k]=pcm[k*ch]/32768.0f;
            w.sample_rate=sr; w.num_samples=mono;
            break;
        }
        i+=8+size;
    }
    return w;
}

static void handle(int cfd){
    char head[4096]; int hn=read(cfd,head,sizeof(head)-1); if(hn<=0){close(cfd);return;} head[hn]=0;
    char *hb=strstr(head,"\r\n\r\n"); if(!hb){close(cfd);return;}
    int hlen=(int)(hb-head)+4;
    // Content-Length
    int body_len=0; char *cl=strstr(head,"Content-Length:"); if(cl) body_len=atoi(cl+15);
    // 读 header 内已有的 body 部分 + 剩余
    int have = hn-hlen; 
    char *body=malloc(body_len+1);
    memcpy(body, head+hlen, have);
    int got=have;
    while(got<body_len){ int r=read(cfd,body+got,body_len-got); if(r<=0)break; got+=r; }
    body[got]=0;
    // {"audio":"BASE64"}
    char *p=strstr(body,"\"audio\":\""); if(!p){ body_len=0; }
    char *b64 = p? p+9 : NULL;
    char *end = b64? strchr(b64,'"') : NULL;
    unsigned char *raw=malloc(body_len);
    size_t rawlen=0;
    if(b64&&end){ *end=0; rawlen=b64_decode(b64,raw,body_len); }
    Wave w=wav_from_mem(raw, rawlen);
    const char *resp="{\"ok\":false,\"text\":\"\"}";
    char rtext[512]="";
    if(w.samples){
        SherpaOnnxOfflineStream *s=SherpaOnnxCreateOfflineStream(g_rec);
        SherpaOnnxAcceptWaveformOffline(s,w.sample_rate,w.samples,w.num_samples);
        SherpaOnnxDecodeOfflineStream(g_rec,s);
        const SherpaOnnxOfflineRecognizerResult *r=SherpaOnnxGetOfflineStreamResult(s);
        snprintf(rtext,sizeof(rtext),"%s",r->text);
        // 简单 JSON 转义
        for(char *q=rtext;*q;q++) if(*q=='"'||*q=='\\') *q=' ';
        SherpaOnnxDestroyOfflineStream(s);
        free(w.samples);
    }
    char out[2048];
    snprintf(out,sizeof(out),"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n{\"ok\":true,\"text\":\"%s\"}", strlen(rtext)+11, rtext);
    write(cfd,out,strlen(out));
    free(body); free(raw); close(cfd);
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
    bind(sfd,(struct sockaddr*)&a,sizeof(a)); listen(sfd,5);
    fprintf(stderr,"ASR HTTP :18082\n");
    for(;;){ int c=accept(sfd,NULL,NULL); if(c>=0) handle(c); }
}
