/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef LASTFM_H
#define LASTFM_H
#include "ipc.h"   /* track_state_t */

/* Auth flow states (returned by lastfm_auth_state) */
enum { LFM_AUTH_IDLE=0, LFM_AUTH_TOKEN, LFM_AUTH_WAIT, LFM_AUTH_OK, LFM_AUTH_ERR };

/* Lifecycle - all of these run on the LVGL/main thread only. */
void        lastfm_init(void);                     /* load config + queue; once at startup */
void        lastfm_poll(void);                     /* each loop tick: consume worker results, drive auth + scrobble queue */
void        lastfm_watch(const track_state_t *st); /* each loop tick: play-state -> now-playing + scrobble eligibility */

/* Status */
int         lastfm_enabled(void);
int         lastfm_connected(void);                /* has a session key */
int         lastfm_has_creds(void);                /* api_key + secret provisioned */
const char *lastfm_username(void);                 /* "" if not connected */
int         lastfm_queue_count(void);              /* pending (offline) scrobbles */

/* Settings actions */
void        lastfm_set_enabled(int on);
void        lastfm_set_credentials(const char *api_key, const char *secret); /* from setup server; clears session */
void        lastfm_logout(void);                   /* clear session key (stays provisioned) */

/* QR account-authorization flow (driven by the Last.fm settings screen) */
void        lastfm_auth_begin(void);               /* getToken -> WAIT (poll getSession) */
int         lastfm_auth_state(void);               /* LFM_AUTH_* */
const char *lastfm_auth_url(void);                 /* last.fm approve URL for the QR (or "") */

/* Per-user credential provisioning: a transient LAN web server (paste your api_key/secret) */
int         lastfm_setup_start(void);              /* start server; 0 ok, -1 (no Wi-Fi/bind) */
void        lastfm_setup_stop(void);
const char *lastfm_setup_url(void);                /* http://<ip>:8080/<token> for the QR (or "") */
int         lastfm_setup_received(void);           /* 1 once creds were posted + applied */

#endif
