#include "../src/psp/platform.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static void write_bad(const char *text) {
    FILE *f=fopen("settings.ini","w");assert(f);fputs(text,f);assert(fclose(f)==0);
}
int main(void) {
    Settings a,b;settings_defaults(&a);settings_load(&b);assert(b.volume==70);
    a.volume=40;a.checkpoint=1;assert(settings_save(&a));settings_load(&b);
    assert(b.volume==40 && b.checkpoint==1);
    a.volume=80;assert(settings_save(&a));
    write_bad("volume=nan\n");settings_load(&b);assert(b.volume==40);
    assert(settings_save(&b));settings_load(&a);assert(a.volume==40);
    const char *bad[]={"volume=-1\n","volume=101\n","volume=1.5\n","checkpoint=2\n","brightness=inf\n","sensitivity=999\n","volume=50 garbage\n","unknown=10\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        write_bad(bad[i]);settings_load(&a);assert(a.volume==40);
    }
    remove("settings.ini");settings_load(&a);assert(a.volume==40);
    remove("settings.ini.bak");settings_load(&a);assert(a.volume==70);
    puts("Settings round trip, corrupt-file fallback and interrupted replace recovery: OK");return 0;
}
