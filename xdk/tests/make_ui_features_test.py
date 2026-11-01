"""Actual UI/dictionary checks and previews, including 4:3 and translated help."""
from pathlib import Path
import re,sys
r=Path(__file__).resolve().parents[2];app=(r/'src/app.c').read_text()
def array(name):
 return re.search(r'static const [^;]+\b'+name+r'\[[^]]+\]\s*=\s*(\{.*?\n\s*\});',app,re.S)[1]
fixture=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../src/settings_layout.h"
#include "../src/ui/ui.c"
void ui_present(){}
/* ROWS */
static void bounds(){
 int n=0;const ui_draw_command *commands=ui_draw_commands(&n);assert(commands && n>0 && n<UI_DRAW_CAPACITY);
 for(int i=0;i<n;i++)if(commands[i].kind==UI_GLYPH){
  const ui_draw_command &c=commands[i];
  assert(c.x>=0 && c.y>=0 && c.x+c.w<=cv.w && c.y+c.h<=cv.h);
 }
}
static void save(const char *name){
 ui_draw_software();FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",cv.w,cv.h);
 for(int i=0;i<cv.w*cv.h;i++){unsigned char rgb[3]={(unsigned char)(cv.px[i]>>16),(unsigned char)(cv.px[i]>>8),(unsigned char)cv.px[i]};fwrite(rgb,1,3,f);}fclose(f);
}
static std::vector<uint64_t> value_glyphs(){
 int n;const ui_draw_command *p=ui_draw_commands(&n);std::vector<uint64_t> out;
 for(int i=0;i<n;i++)if(p[i].kind==UI_GLYPH && p[i].x>=776 && p[i].y>=270 && p[i].y<298){
  out.push_back((uintptr_t)p[i].pixels);out.push_back((uint64_t)p[i].x);out.push_back((uint64_t)p[i].y);
 }
 return out;
}
int main(){
 ui_set_language(1);
    for(int i=0;i<SET_COUNT;i++){
      assert(strcmp(ui_translate(helps[i]),helps[i]));
      if(strcmp(labels[i],"FPS"))assert(strcmp(ui_translate(labels[i]),labels[i]));
    }
 for(unsigned i=0;i<sizeof(ui_translations)/sizeof(ui_translations[0]);i++){
  if(i)assert(strcmp(ui_translations[i-1].en,ui_translations[i].en)<0);
  if(!*ui_translations[i].es || strcmp(ui_translate(ui_translations[i].en),ui_translations[i].es))printf("Translation lookup failed: %s\n",ui_translations[i].en);
  assert(*ui_translations[i].es && !strcmp(ui_translate(ui_translations[i].en),ui_translations[i].es));
 }
 ui_set_language(CONFIG_LANGUAGE_PT_BR);
 for(unsigned i=0;i<sizeof(ui_translations)/sizeof(ui_translations[0]);i++)
  assert(*ui_translations[i].pt_br && !strcmp(ui_translate(ui_translations[i].en),ui_translations[i].pt_br));
 assert(!strcmp(ui_translate("Language"),"Idioma") && !strcmp(ui_translate("Save"),"Salvar"));
 ui_set_language(99);assert(!strcmp(ui_translate("Settings"),"Settings"));
 ui_set_language(0);const char *original="Settings";assert(ui_translate(original)==original);
 ui_set_language(1);assert(!strcmp(ui_translate("Starting %s"),"Iniciando %s"));
 char title[80];snprintf(title,sizeof(title),ui_translate("Starting %s"),"Settings");assert(!strcmp(title,"Iniciando Settings"));
 ui_set_page(UI_SETTINGS);ui_set_language(0);int root=ui_location_enter("Settings");ui_location_enter("Developer");
 ui_set_language(1);char path[256];ui_location_path(path,sizeof(path),NULL,NULL);assert(!strcmp(path,"Ajustes  >  Avanzado"));ui_location_restore(root);
 ui_set_page(UI_LIBRARY);root=ui_location_enter_raw("Settings");ui_location_path(path,sizeof(path),NULL,NULL);assert(!strcmp(path,"Biblioteca  >  Settings"));ui_location_restore(root);
 ui_set_language(CONFIG_LANGUAGE_PT_BR);ui_set_page(UI_LIBRARY);root=ui_location_enter_raw("Right");
 ui_location_path(path,sizeof(path),NULL,NULL);assert(!strcmp(path,"Biblioteca  >  Right"));ui_location_restore(root);
 ui_draw_enable(true);assert(ui_init(1280,720));
 // Entering Stream from Home seeds the correct root before any frame is drawn.
 ui_set_page(UI_HOME);ui_set_settings_context(UI_STREAM,-1);root=ui_location_enter("Stream");
 ui_location_path(path,sizeof(path),NULL,NULL);assert(!strcmp(path,ui_translate("Stream")));
 assert(active_page==UI_STREAM);ui_location_restore(root);ui_set_settings_context(-1,-1);
 ui_setting profile_actions[]={{"Name","Right",NULL},{"Apply","Next connection",NULL},{"Rename","Edit",NULL},{"Delete","Confirm...",NULL}};
 assert(option_kind("Delete profile?",profile_actions,1,true)==UI_OPTION_ACTION);
 assert(option_kind("Rename profile",profile_actions,0,true)==UI_OPTION_ACTION);
 ui_setting hostrows[]={{"Play / Connect","Ready",NULL},{"Host","Right",NULL}};
 ui_draw_settings_page("Host details",hostrows,2,0);std::vector<uint64_t> actual=value_glyphs();
 ui_draw_begin(&cv);font_set_sink(&cv,ui_glyph);text_fit(FONT_REGULAR,UI_TEXT_PRIMARY,776,293,410,C_TEXT_DIM,"Right");
 assert(!actual.empty() && actual==value_glyphs());
 hostrows[1].label="Name";ui_draw_settings_page("Profile actions",hostrows,2,0);actual=value_glyphs();
 ui_draw_begin(&cv);font_set_sink(&cv,ui_glyph);text_fit(FONT_REGULAR,UI_TEXT_PRIMARY,776,293,410,C_TEXT_DIM,"Right");
 assert(!actual.empty() && actual==value_glyphs()); // exact host-name glyphs, never "Derecho"
 const int sizes[][2]={{848,480},{1280,720},{1920,1080},{640,480},{720,576}};
 for(unsigned k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++)for(int language=0;language<CONFIG_LANGUAGE_COUNT;language++){
  ui_set_language(language);assert(ui_init(sizes[k][0],sizes[k][1]));assert(W>=1279.9f && H>=719.9f);
  for(int id=0;id<SET_COUNT;id++){
   ui_setting rows[7];for(int i=0;i<7;i++)rows[i]={labels[id],"Confirm...",helps[id]};
   ui_draw_settings_page("Settings",rows,7,6);bounds();
   // Help is TV-sized and clipped above controls, including long translations.
   int commands_n;const ui_draw_command *help=ui_draw_commands(&commands_n);
   for(int j=0;j<commands_n;j++)if(help[j].kind==UI_GLYPH && help[j].clip[3]==SI(H-82))assert(help[j].y<SI(H-82));
   ui_set_settings_context(UI_SETTINGS,SETTINGS_SYSTEM);ui_draw_settings_page("System and maintenance",rows,7,6);bounds();ui_set_settings_context(-1,-1);
   help=ui_draw_commands(&commands_n);
   for(int j=0;j<commands_n;j++)if(help[j].kind==UI_GLYPH && help[j].clip[3]==SI(H-82))assert(help[j].y<SI(H-82));

  }
  ui_setting categories[6];for(int c=0;c<6;c++)categories[c]={settings_categories[c],"Open",settings_pages[MENU_PCS+c].help};
  for(int selected=0;selected<6;selected++){ui_draw_settings_page("Settings",categories,6,selected);bounds();}
  // Actual ordered tree rows, contexts and translated help, rather than synthetic old groups.
  for(int page=0;page<MENU_COUNT;page++){
   ui_setting menu[SET_COUNT];int kinds[SET_COUNT],n=0;
   for(int i=0;i<SETTINGS_ENTRY_COUNT;i++)if(settings_entries[i].page==page){
    int id=settings_entries[i].setting,child=settings_entries[i].child;
    if(id==SET_RENDERER)continue;
    if(id>=0){menu[n]={labels[id],"Open",helps[id]};kinds[n++]=settings_kind(id);}
    else{menu[n]={settings_pages[child].title,"Open",settings_pages[child].help};kinds[n++]=UI_OPTION_OPEN;}
   }
   if(!n)continue;
   int tab=page==MENU_STREAM || page==MENU_STREAM_ADJUST || page==MENU_AUDIO?UI_STREAM:UI_SETTINGS;
   ui_set_settings_context(tab,settings_pages[page].category);
   for(int selected=0;selected<n;selected++){
    ui_draw_settings_options(settings_pages[page].title,menu,kinds,n,selected);bounds();
    assert(active_page==tab && settings_context_category==settings_pages[page].category);
    int help_n;const ui_draw_command *help=ui_draw_commands(&help_n);
    for(int j=0;j<help_n;j++)if(help[j].kind==UI_GLYPH && help[j].clip[3]==SI(H-82))assert(help[j].y<SI(H-82));
   }
   ui_set_settings_context(-1,-1);
  }
  int aw,ah;ui_art_size(&aw,&ah);std::vector<uint32_t> art(aw*ah,0xFF365647);
  ui_set_motion(false);
  ui_app library[17];for(int i=0;i<17;i++)library[i]={"Settings",art.data(),false,false,(unsigned)i};
  for(int selected=0;selected<17;selected++){
   ui_draw_library(NULL,library,17,selected,"All Apps");bounds();
   int count;const ui_draw_command *commands=ui_draw_commands(&count);int covers=0;bool selected_cover=false;
   for(int i=0;i<count;i++)if(commands[i].kind==UI_IMAGE && commands[i].pixels==art.data()){
    covers++;const ui_draw_command &c=commands[i];
    assert(c.clip[0]>=SI(56) && c.clip[2]<=cv.w-SI(64) && c.y>=S(198) && c.y+c.h<=S(H-170));
    if(c.generation==(unsigned)selected)assert(c.x>=S(64)-1 && c.x+c.w<=cv.w-S(64)+1);
    assert(c.iw==aw && c.ih==ah); // display scaling never changes cover buffer dimensions
    if(c.generation==(unsigned)selected)selected_cover=true;
   }
   assert(selected_cover && covers<=12 && covers>=1);
  }
  ui_draw_splash("Preparing video decoder...",0);bounds();
  int n;const ui_draw_command *p=ui_draw_commands(&n);std::vector<uint32_t> colors;
  for(int i=0;i<n;i++)colors.push_back(p[i].color);
  ui_draw_splash("Preparing video decoder...",160);p=ui_draw_commands(&n);bool changed=false;
  for(int i=0;i<n && i<(int)colors.size();i++)if(colors[i]!=p[i].color)changed=true;assert(changed);
  ui_draw_busy("Checking host","Checking the host's status...",200);bounds();
  ui_draw_message(ICON_ERROR,"No network","The console has no IP address. Connect a network cable or set up Wi-Fi in the dashboard's network settings, then start Moonlight again.");bounds();
  const char *tag=language==CONFIG_LANGUAGE_PT_BR?"pt-br":"es";
  if(language && (k==0 || k==3)){char file[80];snprintf(file,sizeof(file),"ui-%s-%dx%d-dialog.ppm",tag,cv.w,cv.h);save(file);}
  ui_setting rows[]={{"Stick","Right","Choose the stick to calibrate. Menu navigation is unchanged."},{"Minimum","30%","Increase until the resting stick reads zero. 0-30%."},{"Maximum","50%","Input reaches full output at this travel. 50-100%."},{"Raw input","X -24000 | Y 18000","Live values from the first connected controller."},{"Calibrated input","X -32768 | Y 32767","Axial calibration preview. Applied when streaming."},{"Reset calibration","Reset","Restore both sticks to 0-100%."}};
  int calibration_kinds[]={UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_CYCLE,UI_OPTION_INFO,UI_OPTION_INFO,UI_OPTION_ACTION};
  ui_draw_settings_options("Stick calibration",rows,calibration_kinds,6,5);bounds();
  if(language && k==0){char file[80];snprintf(file,sizeof(file),"ui-%s-480-calibration.ppm",tag);save(file);}
  ui_setting power[]={{"Cancel","Back","Return without changing console power."},{"Confirm","Shut down","Save settings and perform the selected power action."}};
  int power_kinds[]={UI_OPTION_ACTION,UI_OPTION_ACTION};ui_draw_settings_options("Shut down console",power,power_kinds,2,0);bounds();
  if(language && k==3){char file[80];snprintf(file,sizeof(file),"ui-%s-4x3-power.ppm",tag);save(file);}
 }
 assert(ui_init(1280,720));ui_host host={"Settings","10.0.0.2",HOST_PAIRED,NULL,NULL};
 ui_menu_item items[]={{"Settings","",ICON_PLAY,true},{"Library","Games & apps",ICON_PLAY,true},{"Stream","Profiles & audio",ICON_MONITOR,true},{"Recent Session","Your play history",ICON_PLAY,true},{"Settings","Hosts & advanced",ICON_GEAR,true}};
 ui_dashboard dashboard={"Standard","Good",NULL,0};ui_draw_dashboard(&host,items,5,0,"10.0.0.3",&dashboard);assert(main_cache_valid);
 ui_set_language(0);assert(!main_cache_valid && !ui_scene_restore(&cv));
 assert(ui_init(854,480));
 for(int background=0;background<3;background++)for(int accent=0;accent<4;accent++){
  int previous_bg=theme_background_id,previous_accent=theme_accent_id;
  ui_set_appearance(background,accent);
  if(previous_bg!=background || previous_accent!=accent)assert(!main_cache_valid);
  ui_draw_dashboard(&host,items,5,0,"10.0.0.3",&dashboard);assert(main_cache_valid);bounds();
  ui_set_appearance(background,accent);assert(main_cache_valid); // unchanged preference retains scene
  if(accent)for(int pixel=0;pixel<128*128;pixel++){
   assert((theme_icon[pixel]&0xFF000000u)==(moonx360_icon[pixel]&0xFF000000u));
   assert((theme_icon[pixel]&0xFFFFFFu)==(ui_theme_accent()&0xFFFFFFu));
  }
  ui_draw_software();std::vector<uint32_t> pixels(cv.px,cv.px+cv.w*cv.h);
  ui_drop_main_cache();ui_draw_dashboard(&host,items,5,0,"10.0.0.3",&dashboard);ui_draw_software();
  assert(!memcmp(pixels.data(),cv.px,pixels.size()*4));
 }
 ui_set_appearance(-1,999);assert(!theme_background_id && !theme_accent_id && ui_theme_accent()==0xFF72C840);
 puts("PASS: twelve appearance combinations, retained/fresh scene equality, no-op cache retention, icon alpha/color and invalid-theme fallback.");

 puts("PASS: every Spanish/PT-BR lookup, ordered UTF-8 keys, raw names, formatted names, breadcrumb/cache invalidation, two-column categories/help, all 17 library selections, preserved cover buffers and visible controls in all 3 languages at 480/720/1080 and 4:3; host previews only.");
}
'''
rows='static const char *labels[]='+array('labels')+';\nstatic const char *helps[]='+array('helps')+';\n'
Path(sys.argv[1]).write_text(fixture.replace('/* ROWS */',rows),encoding='utf-8')
