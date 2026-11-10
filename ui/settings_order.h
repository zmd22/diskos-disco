/* Hide irrelevant theme rows, but retain dependency-blocked controls. */
static int settings_hidden(const setting_t *s){
    const char *k=s->cfg_key?s->cfg_key:"";
    const char *reason=display_disabled_reason(s);
    if(reason && strcmp(k,"imm_scrub_action") && strcmp(k,CTL_CFG_ROT))return 1;
    if(s->apply==apply_grp_discoqs && !(th_disco()||th_null()))return 1;
    if(s->apply==apply_grp_zune)return 1;
    if(s->apply==apply_grp_musicprogress && th_null())return 1;
    if(!strcmp(k,"outdoor")&&(th_null()||th_zune()))return 1;
    if(s->apply==apply_grp_discocolours&&th_classic())return 1;   /* Graphite: Appearance held only Outdoor */
    if(s->apply==apply_theme_navigation && !(th_disco_family()||th_null()||th_zune()))return 1;
    if(!strcmp(k,"disco_times") && !(th_disco_family()))return 1;
    if((!strcmp(k,"clock_weather")||!strcmp(k,"clock_batt_pct")) && !(th_disco_family()||th_null()))return 1;
    if((s->apply==apply_disco_menu||s->apply==apply_grp_discolayout) && !(th_disco_family()||th_null()))return 1;
    if(s->apply==apply_grp_musicscreen && th_zune())return 1;
    if((s->apply==apply_grp_musictext||s->apply==apply_grp_musicinfo||s->apply==apply_grp_musicprogress) && !(th_disco_family()||th_null()))return 1;
    if(s->apply==apply_grp_clockinfo && !(th_disco_family()||th_null()))return 1;
    if(s->apply==apply_grp_screenstandby && !(th_zune()||theme_preset()==THEME_PRESET_RING||theme_preset()==THEME_PRESET_STONE||theme_custom_active()))return 1;
    return 0;
}
/* Shared submenu template. See docs/SETTINGS_TEMPLATE.md.
 * Sort rows, never stored option values, preset IDs or user-arranged shortcuts. */
static int setting_is_onoff(const setting_t *s){
    return s->type==ST_TOGGLE || ((s->type==ST_CYCLER || s->type==ST_CHOICE) &&
        s->nopts==2 && !strcmp(s->opts[0],"Off") && !strcmp(s->opts[1],"On"));
}
static int setting_order_band(const setting_t *s){
    /* Explicit Playback exceptions precede/follow the default alphabetic bands. */
    if(!strcmp(s->group,"Playback")){
        if(s->cfg_key && !strcmp(s->cfg_key,"work_mode"))return 0;
        if(s->apply==apply_eq_custom)return 1;
        if(s->cfg_key && !strcmp(s->cfg_key,"songsphere"))return 4;
    }
    const char *k=s->cfg_key?s->cfg_key:"";
    if(!strcmp(k,"brightness"))return -30;
    if(!strcmp(k,"theme_preset"))return -29;
    if(s->apply==apply_theme_settings)return -28;
    if(!strcmp(k,"theme_variant"))return -27;
    if(th_classic()&&!strcmp(k,"outdoor"))return -26;   /* Graphite: right under Light / dark */
    if(s->apply==apply_wifi||s->apply==apply_battery||s->apply==apply_open_datetime||s->apply==apply_volkeys_group||s->apply==apply_rescan||s->apply==system_update_sd_open||s->apply==apply_howto||s->apply==apply_ma_server||s->apply==apply_bluetooth_connection)return -20;
    if(s->apply==apply_bluetooth_settings||!strcmp(k,"idle_off_idx"))return -19;
    if(!strcmp(k,"language")||!strcmp(k,"font_size")||!strcmp(k,"album_view")||!strcmp(k,"imm_style")||!strcmp(k,"sleep_idx"))return -20;
    if(!strcmp(s->group,"Volume Keys"))return !strcmp(k,"key_single")?-20:!strcmp(k,"key_double")?-19:-18;
    return setting_is_onoff(s)?3:2;
}
static int setting_compare(const setting_t *a,const setting_t *b){
    int band=setting_order_band(a)-setting_order_band(b);
    return band?band:strcasecmp(display_label(a),display_label(b));
}
static int settings_collect(const char *group,int indices[N_SETTINGS]){
    int count=0;
    for(int i=0;i<N_SETTINGS;i++){
        const setting_t *s=&TABLE[i];
        const char *page=settings_page(s);
        if(!page || strcmp(page,group) || display_variant_hidden(s))continue;
        if(settings_hidden(s))continue;

        int at=count;
        while(at>0 && setting_compare(s,&TABLE[indices[at-1]])<0){
            indices[at]=indices[at-1];--at;
        }
        indices[at]=i;++count;
    }
    return count;
}
