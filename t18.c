#include "rproc_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static double now_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec*1000.0+ts.tv_nsec/1e6;}
static unsigned long sum_frame(const ns_rproc_http_frame *fr){
    unsigned long s=0; const unsigned char *p=fr->pixels;
    for (int y=0;y<fr->height;y+=8) for(int x=0;x<fr->width*4;x+=16) s+=p[(size_t)y*fr->stride+x];
    return s;
}
int main(void){
    ns_rproc_http *r = ns_rproc_http_spawn_shm(
        "/home/user/nordstjernen/builddir/src/nordstjernen-renderer", 1280, 1024);
    ns_rproc_http_page pg;
    double t0=now_ms();
    if (ns_rproc_http_open(r,"http://127.0.0.1:8732/",800,600,400,&pg)!=0||!pg.ok) return 1;
    printf("open:           %.0f ms\n", now_ms()-t0);
    ns_rproc_http_page_clear(&pg);
    ns_rproc_http_frame fr;
    t0=now_ms();
    if (ns_rproc_http_render(r,800,600,0,0,1.0,&fr)!=0||!fr.ok) return 1;
    unsigned long first=sum_frame(&fr);
    printf("first paint:    %.0f ms  anim=%d (placeholder frame)\n", now_ms()-t0, fr.animating);
    free(fr.nav);free(fr.webgl);
    int got_image=0;
    for (int i=0;i<40 && !got_image;i++){
        usleep(50*1000);
        if (ns_rproc_http_render(r,800,600,0,0,1.0,&fr)!=0||!fr.ok) return 1;
        if (!fr.unchanged && sum_frame(&fr)!=first){
            printf("image painted:  +%.0f ms after open  anim=%d\n", now_ms()-t0, fr.animating);
            got_image=1;
        }
        free(fr.nav);free(fr.webgl);
    }
    if (!got_image) printf("IMAGE NEVER PAINTED\n");
    ns_rproc_http_close(r);
    return got_image?0:1;
}
