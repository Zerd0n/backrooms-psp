#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>

static FILE *log_file;
void log_open(void) {
    remove("backrooms.previous.log");rename("backrooms.log","backrooms.previous.log");
    log_file=fopen("backrooms.log","w");log_write("Backrooms PSP / rewrite 2.0 starting");
}
void log_write(const char *format,...) {
    char message[512],stamp[40];time_t now=time(NULL);struct tm *t=localtime(&now);
    if(t)strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",t);
    else snprintf(stamp,sizeof(stamp),"unknown-time");
    va_list args;va_start(args,format);vsnprintf(message,sizeof(message),format,args);va_end(args);
    printf("[%s] %s\n",stamp,message);
    if(log_file){fprintf(log_file,"[%s] %s\n",stamp,message);fflush(log_file);}
}
void log_close(void) { if(log_file)fclose(log_file);log_file=NULL; }
static bool load_file(Settings *s,const char *path) {
    FILE *f=fopen(path,"r");if(!f)return false;
    Settings parsed;settings_defaults(&parsed);
    char line[160];bool valid=true;int fields=0;
    while(fgets(line,sizeof(line),f)) {
        if(!strchr(line,'\n') && !feof(f)){valid=false;break;}
        char key[40],value[80],extra;
        if(line[0]=='#' || line[0]=='\n' || line[0]=='\r')continue;
        if(sscanf(line," %39[^=]=%79s %c",key,value,&extra)!=2){valid=false;break;}
        char *end;errno=0;float v=strtof(value,&end);
        if(errno || *end || !isfinite(v)){valid=false;break;}
        if(strcmp(key,"sensitivity")==0 && v>=0.5f && v<=2)parsed.sensitivity=v;
        else if(strcmp(key,"brightness")==0 && v>=0.6f && v<=1.4f)parsed.brightness=v;
        else if(strcmp(key,"volume")==0 && v>=0 && v<=100 && floorf(v)==v)parsed.volume=(int)v;
        else if(strcmp(key,"head_bob")==0 && (v==0 || v==1))parsed.head_bob=(int)v;
        else if(strcmp(key,"show_fps")==0 && (v==0 || v==1))parsed.show_fps=(int)v;
        else if(strcmp(key,"checkpoint")==0 && (v==0 || v==1))parsed.checkpoint=(int)v;
        else {valid=false;break;}
        ++fields;
    }
    if(ferror(f))valid=false;
    fclose(f);
    if(valid && fields>0){*s=parsed;return true;}
    log_write("Invalid settings file: %s; attempting recovery",path);return false;
}
void settings_load(Settings *s) {
    settings_defaults(s);
    if(load_file(s,"settings.ini"))return;
    if(load_file(s,"settings.ini.bak"))log_write("Settings recovered from backup");
    else log_write("Using default settings");
}
bool settings_save(const Settings *s) {
    FILE *f=fopen("settings.ini.tmp","w");
    if(!f){log_write("Cannot save settings: %s",strerror(errno));return false;}
    int written=fprintf(f,"# Backrooms PSP settings v2\nsensitivity=%.2f\nbrightness=%.2f\nvolume=%d\nhead_bob=%d\nshow_fps=%d\ncheckpoint=%d\n",
        s->sensitivity,s->brightness,s->volume,s->head_bob,s->show_fps,s->checkpoint);
    bool ok=written>0;
    if(fflush(f)!=0)ok=false;
    if(fclose(f)!=0)ok=false;
    if(!ok){remove("settings.ini.tmp");log_write("Settings write failed");return false;}
    Settings previous;
    if(load_file(&previous,"settings.ini")) {
        remove("settings.ini.bak");
        if(rename("settings.ini","settings.ini.bak")!=0){remove("settings.ini.tmp");return false;}
    } else remove("settings.ini");
    if(rename("settings.ini.tmp","settings.ini")!=0){log_write("Settings replace failed; backup retained");return false;}
    log_write("Settings saved; checkpoint=%d",s->checkpoint);return true;
}
