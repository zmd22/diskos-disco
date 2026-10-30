/* Presentation only: TABLE retains saved keys, defaults and handlers. */
typedef struct {const char *key;void (*apply)(int);const char *page,*label;} display_entry_t;
static const display_entry_t DISPLAY_ENTRIES[]={
    {"imm_scrub",NULL,"Immersive","Circular control"},
    {"imm_scrub_action",NULL,"Immersive","Control action"},
    {"brightness",NULL,"Display","Brightness"},
    {"theme_preset",NULL,"Display","Theme"},
    {"theme_variant",NULL,"Display","Light / dark"},
    {"outdoor",NULL,"Appearance","Outdoor mode"},
    {"null_palette",NULL,"Appearance","Colour scheme"},
    {"disco_sheen",NULL,"Appearance","Rim sheen"},
    {"disco_eq",NULL,"Appearance","Equalizer colours"},
    {"disco_vol",NULL,"Appearance","Volume colours"},
    {"null_drawer_state_colours",NULL,"Appearance","Menu colours"},
    {"simple_background",NULL,"Music screen","Background"},
    {"null_music_background",NULL,"Music screen","Background"},
    {"disco_controls",NULL,"Music screen","Controls"},
    {"null_controls",NULL,"Music screen","Controls"},
    {"np_style",NULL,"Music screen","Now Playing style"},
    {"np_font",NULL,"Music text","Track font"},
    {"null_music_fonts",NULL,"Music text","Track font"},
    {"disco_title_al",NULL,"Music text","Text alignment"},
    {"null_text_align",NULL,"Music text","Text alignment"},
    {"disco_clock",NULL,"Music info","Show clock"},
    {"null_clock",NULL,"Music info","Show clock"},
    {NULL,apply_grp_clockinfo,"Music info","Clock and info"},
    {"clock_batt_pct",NULL,"Clock and info","Battery percentage"},
    {"clock_weather",NULL,"Clock and info","Weather"},
    {"disco_times",NULL,"Music info","Track times"},
    {"null_music_album",NULL,"Music info","Show album"},
    {"disco_trackno",NULL,"Music info","Track number"},
    {"null_trackno",NULL,"Music info","Track number"},
    {"simple_play_state",NULL,"Music info","Playback state"},
    {"simple_play_mode",NULL,"Music info","Playback mode"},
    {"disco_progress",NULL,"Music progress","Progress colour"},
    {"disco_prog_shape",NULL,"Music progress","Progress shape"},
    {"imm_style",NULL,"Immersive","Immersive style"},
    {"imm_lyrics",NULL,"Immersive","Immersive lyrics"},
    {"saver_idx",NULL,"Standby","Screensaver delay"},
    {"screenoff_idx",NULL,"Standby","Screen-off delay"},
    {"saver_style",NULL,"Standby","Screensaver style"},
    {"disco_qs_style",NULL,"Quick settings","Layout"},
    {"null_qs_layout",NULL,"Quick settings","Layout"},
    {"disco_hints",NULL,"Help","Interaction hints"},
    {"disco_side_pick",NULL,"Navigation","Navigation side"},
    {"null_side",NULL,"Navigation","Navigation side"},
    {"null_autohide",NULL,"Navigation","Menu timeout"},
    {"disco_title_hold",NULL,"Gestures","Title hold"},
    {"null_title_hold",NULL,"Gestures","Title hold"},
    {"disco_nav_double",NULL,"Gestures","Menu double tap"},
    {"null_nav_double",NULL,"Gestures","Menu double tap"},
    {"disco_nav_hold",NULL,"Gestures","Menu hold"},
    {"null_nav_hold",NULL,"Gestures","Menu hold"},
    {"swipe_thresh",NULL,"Gestures","Back-swipe distance"},
    {"font_size",NULL,"Text & layout","Text size"},
    {CTL_CFG_ROT,NULL,"Text & layout","Screen rotation"},
    {"time_24h",NULL,"Text & layout","24-hour time"},
    {"disco_scroll",NULL,"Text & layout","List motion"},
    {"null_scroll",NULL,"Text & layout","List motion"},
    {"anim",NULL,"Text & layout","Screen transitions"},
    {"track_numbers",NULL,"Text & layout","Album track numbers"},
    {"album_view",NULL,"Text & layout","Album view"},
    {"online_art",NULL,"Online services","Online album art"},
    {"online_lyrics",NULL,"Online services","Online lyrics"},
    {"artcache",NULL,"Online services","Artwork cache"},
    {"weather_on",NULL,"Online services","Weather"},
    {NULL,apply_grp_zune,"Music screen","Zune"},
    {NULL,apply_grp_musictext,"Music screen","Text"},
    {NULL,apply_grp_musicinfo,"Music screen","Information"},
    {NULL,apply_grp_musicprogress,"Music screen","Progress"},
    {NULL,apply_grp_immersive,"Music screen","Immersive"},
    {NULL,apply_grp_themecolours,"Display","Appearance"},
    {NULL,apply_disco_options,"Display","Music screen"},
    {NULL,apply_grp_screenstandby,"Display","Standby"},
    {NULL,apply_null_options,"Display","Navigation"},
    {NULL,apply_grp_textlayout,"Display","Text & layout"},
    {NULL,apply_open_colorpick,"Appearance","Accent colour"},
    {NULL,apply_disco_menu,"Navigation","Menu items"},
    {NULL,apply_null_menu,NULL,"Menu items"},
    {NULL,apply_grp_discoqs,"Navigation","Quick settings"},
    {NULL,apply_null_qs,NULL,"Quick settings"},
    {NULL,apply_shortcuts,"Navigation","Shortcuts"},
    {NULL,apply_grp_discolayout,"Navigation","Gestures"},
    {NULL,apply_hints_reset,"Help","Show hints again"},
    {NULL,apply_disco_qs_items,"Quick settings","Items"},
    {NULL,apply_grp_onlineextras,"Text & layout","Online services"},
    {NULL,apply_grp_musicscreen,NULL,"Music screen"},
    {NULL,apply_grp_discocolours,NULL,"Colours"},
};

static const display_entry_t *display_entry(const setting_t *s){
    for(size_t i=0;i<sizeof DISPLAY_ENTRIES/sizeof DISPLAY_ENTRIES[0];i++){
        const display_entry_t *d=&DISPLAY_ENTRIES[i];
        if(d->key ? s->cfg_key && !strcmp(d->key,s->cfg_key) : s->type==ST_ACTION && s->apply==d->apply)return d;
    }
    return NULL;
}
static int display_variant_hidden(const setting_t *s){
    if(th_braun2()&&s->cfg_key&&(!strcmp(s->cfg_key,"np_style")||!strcmp(s->cfg_key,"disco_qs_style")))return 1;
    if(th_braun2()&&s->apply==apply_open_colorpick)return 1;   /* Braun 2.0: a fixed orange accent, so no picker */
    if(s->cfg_key&&th_zune()&&!strcmp(s->cfg_key,"saver_style"))return 1;
    if(s->apply==apply_grp_zune&&!th_zune())return 1;
    if(s->cfg_key&&th_zune()&&!strcmp(s->cfg_key,"np_style"))return 1;
    if(s->cfg_key&&!th_zune()&&!strncmp(s->cfg_key,"zune_",5))return 1;
    if(s->cfg_key&&!th_zune()&&(!strcmp(s->cfg_key,"zune_nav_hold")||!strncmp(s->cfg_key,"zlock_",6)))return 1;
    static const struct{const char *common,*null_key;} pairs[]={
        {"simple_background","null_music_background"},{"disco_controls","null_controls"},
        {"np_font","null_music_fonts"},{"disco_title_al","null_text_align"},
        {"disco_clock","null_clock"},{"disco_trackno","null_trackno"},
        {"disco_qs_style","null_qs_layout"},{"disco_side_pick","null_side"},
        {"disco_title_hold","null_title_hold"},{"disco_nav_double","null_nav_double"},
        {"disco_nav_hold","null_nav_hold"},{"disco_scroll","null_scroll"}
    };
    if(!s->cfg_key)return 0;
    for(size_t i=0;i<sizeof pairs/sizeof pairs[0];i++)if(!strcmp(s->cfg_key,th_null()?pairs[i].common:pairs[i].null_key))return 1;
    return 0;
}
static const char *display_original_label(const setting_t *s){const display_entry_t *d=display_entry(s);return d?d->label:s->label;}
static const char *display_disabled_reason(const setting_t *s){
    const char *k=s->cfg_key?s->cfg_key:"";
    if(!strcmp(k,"zune_nav_hold")&&!th_zune())return "This action belongs to the Zune main menu.";
    if(!strcmp(k,"imm_scrub_action")&&!simple_cfg_get_int("imm_scrub",0))return "Enable Circular control first.";
    if(s->apply==apply_open_colorpick && (th_simple()||th_null()||th_braun2()))return accent_disabled_reason();
    if(!strcmp(k,"disco_qs_style") && th_zune())return "Zune uses six circular quick actions. Choose and reorder them in Items.";
    if(!strcmp(k,"theme_variant") && th_null())return "Null uses a fixed dark appearance. Choose a colour scheme instead.";
    if(!strcmp(k,"null_side"))return "Null keeps its navigation on the right.";
    if((!strcmp(k,"zlock_src") || !strcmp(k,"zlock_tint")) && !th_zune())return "This option belongs to the Zune lock screen.";
    if(!strcmp(k,"null_palette") && !th_null())return "Colour schemes are specific to Null. Use light/dark and accent settings for this theme.";
    if(!strncmp(k,"null_",5) && !th_null())return "This option is available in Null.";
    if(!strcmp(k,"simple_background") && !th_simple())return "This theme uses its own artwork background.";
    if(!strncmp(k,"simple_play_",12) && !th_simple())return "These indicators belong to the Squash music screen.";
    if(!strcmp(k,"saver_style") && (th_braun()||th_disco_family()||th_null()))return "This theme has its own screensaver design.";
    if((!strcmp(k,"np_style") || !strcmp(k,"disc_colour")) && (th_disco_family()||th_null()))return "This theme uses its own Music and startup design.";
    if(th_simple()){
        if(!strcmp(k,"disco_controls"))return "Squash uses gesture controls on its Music screen.";
        if(!strcmp(k,"disco_sheen"))return "Squash uses its own border styling.";
        if(!strcmp(k,"disco_side_pick"))return "Squash keeps its navigation on the right.";
        if(!strcmp(k,"disco_qs_style"))return "Squash uses its own fixed Quick settings layout.";
    }
    if(!th_disco_family() && !strncmp(k,"disco_",6) && strcmp(k,"disco_hints"))return "This option controls the Disco and Squash layout.";
    if(!th_disco_family() && !th_null() && (s->apply==apply_disco_menu || !strcmp(k,"np_font")))return "This option belongs to the Disco, Squash and Null music/navigation layouts.";
    if(!th_disco_family() && !th_null() && (s->apply==apply_hints_reset || !strcmp(k,"disco_hints")))return "Interaction hints are available in Disco, Squash and Null.";
    return NULL;
}
static int display_blocked(const setting_t *s){const char *reason=display_disabled_reason(s);if(reason){ui_toast(reason);return 1;}return 0;}
static const char *display_group_alias(const char *g){
    static const struct{const char *old,*page;} aliases[]={
        {"Network","Connectivity"},{"Power","Battery & power"},
        {"Theme Colours","Appearance"},{"Text & Layout","Text & layout"},{"Online & Extras","Online services"},
        {"Disco Options","Music screen"},{"Null Options","Navigation"},{"Music Screen","Music screen"},
        {"Disco Colours","Appearance"},{"Disco Layout","Gestures"},{"Quick Settings","Quick settings"}
    };
    for(size_t i=0;i<sizeof aliases/sizeof aliases[0];i++)if(!strcmp(g,aliases[i].old))return aliases[i].page;
    return g;
}

/* Canonical ownership: presentation moves only; keys and apply handlers stay intact. */
static const char *settings_page_title(const char *page){
    if(!strcmp(page,"Theme navigation"))return "Navigation";
    if(strcmp(page,"Theme settings"))return page;
    if(th_disco())return "Disco settings";
    if(th_simple())return "Squash settings";
    if(th_null())return "Null settings";
    if(th_zune())return "Zune settings";
    if(th_braun2())return "Braun 2.0 settings";
    if(th_braun())return "Braun settings";
    if(theme_preset()==THEME_PRESET_RING)return "Ring settings";
    return theme_custom_active()?"Custom settings":"Stone settings";
}
static const char *settings_page(const setting_t *s){
    const char *k=s->cfg_key?s->cfg_key:"";
    const display_entry_t *d=display_entry(s);
    const char *page=d?d->page:s->group;
    if(!strcmp(k,"screenoff_idx")||!strcmp(k,"saver_idx")||!strcmp(k,"clock_batt_pct"))return "Battery & power";
    if(!strcmp(k,"sleep_idx")||!strcmp(k,"sleep_action"))return "Sleep timer";
    if(s->apply==apply_restart)return "Battery & power";
    if(!strcmp(k,"time_24h"))return "Date & Time";
    if(!strcmp(k,"language"))return "System";
    if(!strcmp(k,"bt_codec"))return "Bluetooth";
    if(!strcmp(k,"disco_scroll")||!strcmp(k,"null_scroll")||!strcmp(k,"zune_nav_hold"))return "Theme navigation";
    if(s->apply==apply_grp_about)return "Help";
    if(s->apply==apply_grp_onlineextras)return "Connectivity";
    if(s->apply==apply_grp_immersive)return "Display";
    if(s->apply==apply_grp_screenstandby||s->apply==apply_grp_discocolours)return "Theme settings";
    if(s->apply==apply_grp_musicscreen)return "Theme settings";
    if(s->apply==apply_grp_themecolours)return NULL; /* old wrapper; Theme is now the picker */
    if(s->apply==apply_disco_options||s->apply==apply_null_options)return NULL;
    if(s->apply==apply_grp_discoqs)return "Theme settings";
    if(s->apply==apply_grp_discolayout||s->apply==apply_grp_zune||s->apply==apply_disco_menu)return "Theme navigation";
    if(s->apply==apply_disco_qs_items)return !strcmp(s->group,"Text & Layout")?"Navigation":NULL;
    if(s->apply==apply_shortcuts)return "Navigation";
    if(!strcmp(k,"swipe_thresh"))return "Navigation";
    if(!strcmp(k,"album_view")||!strcmp(k,"track_numbers"))return "Library appearance";
    if(s->apply==apply_shared_navigation)return "Display";
    if(page&&!strcmp(page,"Navigation"))return "Theme navigation";
    if(page&&!strcmp(page,"Appearance"))return "Appearance";
    if(page&&!strcmp(page,"Zune"))return "Theme navigation";
    return page;
}
static const char *display_label(const setting_t *s){
    if(s->apply==apply_theme_settings)return settings_page_title("Theme settings");
    if(s->apply==apply_grp_screenstandby)return "Screensaver";
    if(s->apply==apply_grp_discocolours)return "Appearance";
    if(s->apply==apply_grp_discoqs)return "Quick settings layout";
    if(s->apply==apply_grp_discolayout)return "Gestures";
    if(s->apply==apply_disco_qs_items)return "Quick settings items";
    if(s->apply==apply_grp_onlineextras)return "Online services";
    if(s->apply==apply_grp_textlayout)return "Text & layout";
    if(s->apply==apply_grp_library)return "Library management";
    if(s->apply==apply_grp_musictext)return "Text";
    if(s->cfg_key&&!strcmp(s->cfg_key,"sleep_idx"))return "Duration";
    if(s->cfg_key&&!strcmp(s->cfg_key,"imm_style"))return "Style";
    return display_original_label(s);
}
